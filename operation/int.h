#include "common2.h"
#include <string.h>

typedef enum {
  exec_normal,
  exec_return,
  exec_break,
  exec_continue
} exec_status;

typedef struct {
  exec_status status;
  typed_value val;
} exec_result;

static usize Type_size(Type t) {
  return tu_match_exp(
      t,
      (r_uint, u, (usize)(u + 7) / 8),
      (r_sint, s, (usize)(s + 7) / 8),
      (c_int, _, (usize)8), // Handled as 64-bit int max for raw math
      (r_ptr, _, sizeof(void *)),
      (c_ast, _, sizeof(astptr)),
      (c_type, _, sizeof(Type *)),
      (r_tag, tag, Type_size(*tag.type)),
      (r_decor, dec, Type_size(*dec.t)),
      (r_arr, arr, Type_size(*arr.type) * arr.count),
      (r_struct, st, ({
         usize sz = 0;
         let arr = mList_arr(st);
         usize len = mList_len(st);
         if (len > 0) {
           sz = arr[len - 1].offset + Type_size(arr[len - 1].init.t);
           usize align = arr[0].alignment;
           if (align > 0 && sz % align != 0) sz += align - (sz % align);
         }
         sz;
       })),
      (r_bitfield, bf, ({
         usize bits = 0;
         let arr = mList_arr(bf);
         for (usize i = 0; i < mList_len(bf); i++)
           bits += arr[i].width;
         (bits + 7) / 8;
       })),
      (default, (usize)0)
  );
}

static bool is_truthy(superscope *env, typed_full *cnd) {
  if (Type_isint(cnd->t)) {
    if (!cnd->v.c_int) return false;
    bool truthy = BInt.cmp(cnd->v.c_int, nullptr) != 0; // True if != 0
    return truthy;
  }
  if (tu_is(c_ast, cnd->t)) return cnd->v.c_ast != nullptr;
  return false;
}

static exec_result interpret_internal(superscope *env, scope *s, bool in_loop) {
  exec_result res = {exec_normal, {tu_of(c_type, {}), {0}}};

  for (usize i = 0; i < msList_len(s->instructions); i++) {
    ir inst = s->instructions[i];

    tu_match_void(
        inst,
        (ir_op, op, interpret_op(env, s, op)),
        (ir_call, call, interpret_call(env, s, call)),
        (ir_if, if_inst, {
          typed_full *cnd = scope_find(env, s, if_inst.cnd);
          if (is_truthy(env, cnd)) {
            res = interpret_internal(env, if_inst.t, in_loop);
          } else if (if_inst.f) {
            res = interpret_internal(env, if_inst.f, in_loop);
          }
        }),
        (ir_while, while_inst, {
          while (true) {
            typed_full *cnd = scope_find(env, s, while_inst.cnd);
            if (!is_truthy(env, cnd)) break;

            res = interpret_internal(env, while_inst.t, true);

            if (res.status == exec_break) {
              res.status = exec_normal;
              break;
            }
            if (res.status == exec_return) break;
            if (res.status == exec_continue) {
              res.status = exec_normal;
              continue;
            }
          }
        }),
        (ir_block, blk, { res = interpret_internal(env, blk, in_loop); }),
        (ir_ret, ret, {
          res.status = exec_return;
          typed_full *val = scope_find(env, s, ret);
          res.val = (typed_value){val->t, val->v};
        }),
        (ir_break, _, {
          assertMessage(in_loop, "break statement encountered outside of a loop");
          res.status = exec_break;
        }),
        (ir_continue, _, {
          assertMessage(in_loop, "continue statement encountered outside of a loop");
          res.status = exec_continue;
        })
    );

    if (res.status != exec_normal) return res;
  }
  return res;
}

typed_value interpret(superscope *env, scope *s) {
  return interpret_internal(env, s, false).val;
}

void interpret_op(superscope *env, scope *s, ir_op op) {
  typed_full *l = scope_find(env, s, op.l);
  typed_full *d = scope_find(env, s, op.d);
  typed_full *r = scope_find(env, s, op.r);

  assertMessage(!tu_is(r_ptr, l->t), "comptime evaluation of pointers is not allowed");
  assertMessage(!tu_is(r_ptr, r->t), "comptime evaluation of pointers is not allowed");

  // Handle AST Dot operations (fetching ast children during macro execution)
  if (op.op == opNumbers.dot && tu_is(c_ast, l->t)) {
    astptr node = l->v.c_ast;
    astptr name_node = r->v.c_ast;
    if (node && name_node) {
      d->v.c_ast = ast_dot(node, name_node);
    }
    return;
  }

  // Handle struct member access via dot operator
  if (op.op == opNumbers.dot) {
    assertMessage(Type_isint(r->t), "Struct member offset must be an integer");

    if (!l->v.value) return; // Uninitialized struct

    u64 offset_u64 = 0;
    usize r_bits = Type_int_size(r->t) == SIZE_MAX ? 64 : Type_int_size(r->t);
    usize r_bytes = (r_bits + 7) / 8;
    memcpy(&offset_u64, r->v.c_int, r_bytes > 8 ? 8 : r_bytes);

    void *src_ptr = (u8 *)l->v.value + offset_u64;

    if (Type_isint(d->t)) {
      usize d_bits = Type_int_size(d->t) == SIZE_MAX ? 64 : Type_int_size(d->t);
      bool d_sig = tu_is(r_sint, d->t) || tu_is(c_int, d->t);
      if (d->v.c_int) msList_deInit(env->alloc, d->v.c_int);
      d->v.c_int = bigint_fromBits(env->alloc, src_ptr, d_bits, d_sig);
    } else if (tu_is(c_ast, d->t)) {
      memcpy(&d->v.c_ast, src_ptr, sizeof(astptr));
    } else if (tu_is(c_type, d->t)) {
      memcpy(&d->v.c_type, src_ptr, sizeof(Type *));
    } else {
      usize sz = Type_size(d->t);
      if (sz > 0) {
        if (!d->v.value) d->v.value = acreate(env->alloc, arrof(u8, sz));
        memcpy(d->v.value, src_ptr, sz);
      }
    }
    return;
  }

  if (op.op == opNumbers.set) {
    if (Type_isint(l->t)) {
      // if (l->v.c_int) msList_deInit(env->alloc, l->v.c_int); TODO
      // if (d->v.c_int) msList_deInit(env->alloc, d->v.c_int); TODO
      usize bits = Type_int_size(r->t) == SIZE_MAX ? 64 : Type_int_size(r->t);
      bool sigmask = tu_is(r_sint, r->t) || tu_is(c_int, r->t);
      l->v.c_int = bigint_fromBits(env->alloc, r->v.c_int, bits, sigmask);
      d->v.c_int = bigint_fromBits(env->alloc, r->v.c_int, bits, sigmask);
    } else if (tu_is(c_ast, l->t)) {
      l->v.c_ast = r->v.c_ast;
      d->v.c_ast = r->v.c_ast;
    } else if (tu_is(c_type, l->t)) {
      l->v.c_type = r->v.c_type;
      d->v.c_type = r->v.c_type;
    } else {
      // Struct Assignment: raw byte duplication
      usize sz = Type_size(r->t);
      if (sz > 0 && r->v.value) {
        if (!l->v.value) l->v.value = acreate(env->alloc, arrof(u8, sz));
        if (!d->v.value) d->v.value = acreate(env->alloc, arrof(u8, sz));
        memcpy(l->v.value, r->v.value, sz);
        memcpy(d->v.value, r->v.value, sz);
      }
    }
    return;
  }

  // Free loop temporaries before overwriting them
  if (Type_isint(d->t) && d->v.c_int) {
    // msList_deInit(env->alloc, d->v.c_int);
    // d->v.c_int = nullptr;
  }

  // Math and Relational integer evaluations
  if (Type_isint(l->t) && Type_isint(r->t)) {

    // 1. Abstract interpretation: let the operation define its output type
    if (EQUAL_ANY(op.op, opNumbers.eq, opNumbers.neq, opNumbers.les, opNumbers.leq, opNumbers.mor, opNumbers.moq, opNumbers.oand, opNumbers.oor)) {
      d->t = (Type)tu_of(r_uint, (u16)8);
    } else {
      d->t = l->t;
    }

    // 2. Execution bailout: Stop here if we don't have compile-time values
    if (!l->v.c_int || !r->v.c_int) return;

    usize l_bits = Type_int_size(l->t) == SIZE_MAX ? 64 : Type_int_size(l->t);
    usize r_bits = Type_int_size(r->t) == SIZE_MAX ? 64 : Type_int_size(r->t);
    usize d_bits = Type_int_size(d->t) == SIZE_MAX ? 64 : Type_int_size(d->t);

    bool l_sig = tu_is(r_sint, l->t) || tu_is(c_int, l->t);
    bool r_sig = tu_is(r_sint, r->t) || tu_is(c_int, r->t);
    bool d_sig = tu_is(r_sint, d->t) || tu_is(c_int, d->t);

    if (EQUAL_ANY(op.op, opNumbers.eq, opNumbers.neq, opNumbers.les, opNumbers.leq, opNumbers.mor, opNumbers.moq)) {
      i8 c = BInt.cmp(l->v.c_int, r->v.c_int);
      u8 res = 0;
      if (op.op == opNumbers.eq) res = (c == 0);
      else if (op.op == opNumbers.neq) res = (c != 0);
      else if (op.op == opNumbers.les) res = (c < 0);
      else if (op.op == opNumbers.leq) res = (c <= 0);
      else if (op.op == opNumbers.mor) res = (c > 0);
      else if (op.op == opNumbers.moq) res = (c >= 0);

      d->v.c_int = bigint_fromBits(env->alloc, &res, 8, false);
    } else if (op.op == opNumbers.oand) {
      u8 res = is_truthy(env, l) && is_truthy(env, r);
      d->v.c_int = bigint_fromBits(env->alloc, &res, 8, false);
    } else if (op.op == opNumbers.oor) {
      u8 res = is_truthy(env, l) || is_truthy(env, r);
      d->v.c_int = bigint_fromBits(env->alloc, &res, 8, false);
    } else {
      // Extract native integers for pure calculation logic
      u64 l_u64 = 0, r_u64 = 0, res_u64 = 0;
      usize l_bytes = (l_bits + 7) / 8;
      usize r_bytes = (r_bits + 7) / 8;
      memcpy(&l_u64, l->v.c_int, l_bytes > 8 ? 8 : l_bytes);
      memcpy(&r_u64, r->v.c_int, r_bytes > 8 ? 8 : r_bytes);

      i64 l_i64 = (i64)l_u64;
      i64 r_i64 = (i64)r_u64;

      // Sign extend up to boundary limits
      if (l_sig && l_bits < 64 && (l_u64 & (1ULL << (l_bits - 1)))) l_i64 |= (~0ULL) << l_bits;
      if (r_sig && r_bits < 64 && (r_u64 & (1ULL << (r_bits - 1)))) r_i64 |= (~0ULL) << r_bits;

      l_u64 = (u64)l_i64;
      r_u64 = (u64)r_i64;

      if (op.op == opNumbers.plus) res_u64 = l_u64 + r_u64;
      else if (op.op == opNumbers.minus) res_u64 = l_u64 - r_u64;
      else if (op.op == opNumbers.mul) res_u64 = l_u64 * r_u64;
      else if (op.op == opNumbers.div) res_u64 = r_u64 == 0 ? 0 : (d_sig ? (u64)(l_i64 / r_i64) : l_u64 / r_u64);
      else if (op.op == opNumbers.mod) res_u64 = r_u64 == 0 ? 0 : (d_sig ? (u64)(l_i64 % r_i64) : l_u64 % r_u64);
      else if (op.op == opNumbers.oband) res_u64 = l_u64 & r_u64;
      else if (op.op == opNumbers.obor) res_u64 = l_u64 | r_u64;
      else if (op.op == opNumbers.shl) res_u64 = l_u64 << r_u64;
      else if (op.op == opNumbers.shr) res_u64 = d_sig ? (u64)(l_i64 >> r_u64) : l_u64 >> r_u64;

      d->v.c_int = bigint_fromBits(env->alloc, &res_u64, d_bits, d_sig);
    }
  }

  // Identity/Equality evaluations for AST
  else if (tu_is(c_ast, l->t) && tu_is(c_ast, r->t)) {
    u8 res = 0;
    if (op.op == opNumbers.eq) res = (l->v.c_ast == r->v.c_ast);
    else if (op.op == opNumbers.neq) res = (l->v.c_ast != r->v.c_ast);
    d->v.c_int = bigint_fromBits(env->alloc, &res, 8, false);
  }
  // Structural Equality evaluations for Type
  else if (tu_is(c_type, l->t) && tu_is(c_type, r->t)) {
    i8 c = Type_cmp(l->v.c_type[0], r->v.c_type[0]);
    u8 res = 0;
    if (op.op == opNumbers.eq) res = (c == 0);
    else if (op.op == opNumbers.neq) res = (c != 0);
    d->v.c_int = bigint_fromBits(env->alloc, &res, 8, false);
  }
}

void interpret_call(superscope *env, scope *s, ir_call call) {
  typed_full *fn_val = scope_find(env, s, call.l);

  if (!fn_val->func) return;

  let fn = *fn_val->func;
  let args_ast = function_args(fn);
  let body_ast = *function_body(fn);

  let call_scope = scope_inferior(env, env->outermost);
  defer { scope_destroy(env, call_scope); };

  for (usize i = 0; i < args_ast.len; i++) {
    typed_full *arg_val = scope_find(env, s, call.args.ptr[i]);
    u32 new_id = scope_add_value(env, call_scope, *arg_val);

    if (tu_is(astId, *args_ast.ptr[i])) {
      scope_bind(env, call_scope, new_id, tu_catch(astId, *args_ast.ptr[i]));
    } else if (tu_is(astDecl, *args_ast.ptr[i])) {
      let decl = tu_catch(astDecl, *args_ast.ptr[i]);
      if (decl.name && tu_is(astId, *decl.name)) {
        scope_bind(env, call_scope, new_id, tu_catch(astId, *decl.name));
      }
    }
  }

  lower(env, call_scope, body_ast);
  typed_value ret = interpret(env, call_scope);

  typed_full *d = scope_find(env, s, call.d);

  // if (Type_isint(d->t) && d->v.c_int) msList_deInit(env->alloc, d->v.c_int);
  d->v = ret.v;
}

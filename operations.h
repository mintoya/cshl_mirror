#pragma once
#include "ast.h"
#include "type.h"
#include "wheels/allocators/cballocator.h"
#include "wheels/fptr.h"
#include "wheels/mytypes.h"
#include "wheels/print/print_pre.h"
#include <wheels/allocators/fbaAllocator.h>
#include <wheels/bigint.h>
#include <wheels/macros.h>
#include <wheels/mylist.h>
#include <wheels/sList.h>
#include <wheels/smap.h>
typedef struct scope *scopeptr;
// 0 in any location is null
tu_def(
    (opcode, u8),
    (onone, nothing_t),
    // create an integer
    // creates
    (oint, fptr),
    // create a builtin
    // creates
    (obin, builtin_id),
    // create an []i8
    // creates
    (ostr, fptr),
    // create value of ast
    // creates
    (oast, /*  */ ast *),
    // creates
    // evaluate whatever is inside scope then return that
    (oscop, scopeptr),
    // create value of tpye l
    // l is nullable
    // creates
    (onew, /*  */ struct { u32 l; }),
    // load element named  name in l into new reg
    // creates
    (olelem, struct {u32 l ; fptr name; }),
    // load n'th argument of some function into new reg
    // creates
    (oarg, u32),
    // set item in l to const
    (oconst, /**/ struct { u32 l; }),
    // jump to d if l
    (ojif, /*  */ struct { u32 l, d; }),
    // call l with args starting at offset off and len len and store in d
    (ocall, /* */ struct { u32 l, off, len, d; }),
    // apply o to l and r and store in d
    (oop, /*   */ struct { u32 l, r, d, o; }),
    // return thing stored in l
    (oret, /*  */ struct { u32 l; }),
    // move thing in l to r
    (omov, /*   */ struct { u32 l, r; }),
    // copy thing pointed to by in l to thing pointed to by r
    (ocpy, /*   */ struct { u32 l, r; }),
    // store element named name in l into d
    (oselem, struct {u32 l , d ; fptr name; }),
);
_Static_assert(sizeof(opcode));
#define ocode_of (opcode) tu_of

typedef struct scope scope;
sliceDef(scopeptr);

struct scope {
  u32 start_reg;
  u32 next_reg;
  msList(opcode) code;
  scope *parent;
  msList(struct { typed_value value ; u32 cidx; }) values;
  msxmap(struct { u32 idx; bool isconst; }) locals;
};
typedef struct {
  allocfn alloc;
  scope *env;
  moxmap(Type, Type *) types;
  moxmap(void *, usize) pointers;
  r_func *fnctx;
} IrBuilder;

//
//
//

u32 env_addOp(IrBuilder *b, opcode o);
void env_bind(IrBuilder *b, fptr id, u32 reg);
u32 env_push(IrBuilder *b);

void irb_cbb(const callbackallocatorhandle *h, void *p) {
  let _map = (itypeof(IrBuilder, pointers))h->cbself->udata;
  moxmap_rem(_map, h->inptr);
  if (p) moxmap_set(_map, p, h->outsize);
}
IrBuilder IrBuilder_init(allocfn allocator) {
  IrBuilder res;
  res = (IrBuilder){
      .env = nullptr,
      .types = moxmap_init(allocator, Type, Type *, Type_cmp_hash),
      .pointers = moxmap_init(allocator, void *, usize),
  };
  res.alloc = cba_init(allocator, nullptr, irb_cbb, res.pointers);
  env_push(&res);

  struct {
    char *name;
    builtin_id id;
  } builtins[] = {
      {"@if", builtin_if},
      {"@while", builtin_while},
      {"@ast", builtin_ast},
      // {"@any", builtin_any},
      {"@int", builtin_int},
      {"@uint", builtin_uint},
      {"@type", builtin_type},
      {"@typeof", builtin_typeof},
      {"@break", builtin_break},
      {"@qot", builtin_qot},
      {"@uqot", builtin_uqot},
      {"return", builtin_return},
      {"fn", builtin_fn},
  };
  foreach (let it, vla(builtins))
    env_bind(&res, fp(it.name), env_addOp(&res, ocode_of(obin, it.id)));
  return res;
}
void IrBuilder_deinit(IrBuilder *b) {
  let realAllocator = cba_backing(b->alloc);
  foreach (let i, moxmap_iter(b->pointers, void *, usize))
    adestroy(realAllocator, (u8(*)[*i.val])i.key);
  cba_deinit(b->alloc);
  moxmap_deinit(b->pointers);
  moxmap_deinit(b->types);
}
allocfn IrBuilder_allocator(IrBuilder *b) { return b->alloc; }

Type *IrBuilder_getType(IrBuilder *b, Type t) {
  if_decl (let t, moxmap_get(b->types, t)) return *t;
  let allocator = IrBuilder_allocator(b);
  let pt = acreate(allocator, Type);
  moxmap_set(b->types, t, pt);
  *pt = tu_match_exp(
      t,
      (builtin, b, (Type)tu_of(builtin, b)),
      (r_uint, u, (Type)tu_of(r_uint, u)),
      (r_sint, s, (Type)tu_of(r_sint, s)),
      (r_tag, tag, (Type)tu_of(r_tag, {.name = tag.name, .type = IrBuilder_getType(b, *tag.type)})),
      (r_ptr, p, (Type)tu_of(r_ptr, IrBuilder_getType(b, *p))),
      (r_arr, arr, (Type)tu_of(r_arr, {IrBuilder_getType(b, *arr.type), arr.count})),
      (r_struct,
       items,
       (Type)tu_of(r_struct, mList_map(allocator, items, ($.init.t = *IrBuilder_getType(b, $.init.t), $)))),
      (r_bitfield,
       items,
       (Type)tu_of(r_bitfield, mList_map(allocator, items, ($.init.t = *IrBuilder_getType(b, $.init.t), $)))),
      (r_func,
       params,
       (Type)tu_of(r_func, mList_map(allocator, params, (*IrBuilder_getType(b, $))))),
      (default, t),
  );
  return *moxmap_set(b->types, *pt, pt);
}
scope *scope_parent(scope *s) { return s->parent; }

void env_bind(IrBuilder *b, fptr id, u32 reg) {
  if (msxmap_get(b->env->locals, id))
    error_at(id.ptr, "item with same name exists in scope");
  msxmap_set(b->env->locals, id, reg);
}

u32 env_lookup(IrBuilder *b, fptr id) {
  for (scope *s = b->env; s; s = scope_parent(s))
    if_decl (let reg_ptr, msxmap_get(s->locals, id))
      if (reg_ptr->idx >= s->start_reg) return reg_ptr->idx;
  error_at(id.ptr, snprint(stdAlloc, "unknown variable {slice(c8)}", id));
}

u32 env_push(IrBuilder *b) {
  scope *new_scope = avalue(
      b->alloc,
      ((scope){
          .parent = b->env,
          .locals = msxmap_init(b->alloc, msxmap_iType((itypeof(scope, locals))0)),
          .values = msList_init(b->alloc, ptrstype(new_scope->values)),
          .code = msList_init(b->alloc, opcode),
      })

  );
  if (b->env) {
    new_scope->start_reg = b->env->next_reg;
    new_scope->next_reg = b->env->next_reg;
    msList_push(b->alloc, b->env->code, ocode_of(oscop, new_scope));
    b->env->next_reg++;
  }
  b->env = new_scope;
  return new_scope->next_reg;
}

void env_pop(IrBuilder *b) { b->env = scope_parent(b->env); }
bool op_creates(opcode o) {
  return tu_match_exp(
      o,
      (oscop, _, true),
      (obin, _, true),
      (ostr, _, true),
      (oint, _, true),
      (onew, _, true),
      (oast, _, true),
      (olelem, _, true),
      (default, false),

  );
}
u32 env_addOp(IrBuilder *b, opcode o) {
  msList_push(((hxmap *)b->env->locals)->allocator, b->env->code, o);
  return tu_match_exp(
      o,
      (oscop, _, assertMessage(false, "pushing scope by itself not allowed"), (u32)0),
      (obin, _, ++b->env->next_reg),
      (ostr, _, ++b->env->next_reg),
      (oint, _, ; ++b->env->next_reg),
      (onew, _, ++b->env->next_reg),
      (oast, _, ; ++b->env->next_reg),
      (olelem, _, ++b->env->next_reg),
      (default, (u32)0),

  );
}

typed_value Type_asValue(Type *t) {
  return (typed_value){tu_of(c_type, {}), {.c_type = t}};
}
Type value_asType(typed_value t) {
  if (tu_is(c_type, t.t))
    return t.v.c_type[0];
  else assertMessage(false, "thing is not a type");
}
Type typeof_Value(typed_value t) { return t.t; }
typed_value convertInt(IrBuilder *b, fptr in) {
  let front = slice_split(in, (0, 2))[0];
  bigint result = nullptr;
  if (fptr_eq(front, "0x") || fptr_eq(front, "0b"))
    result = BInt.from.fptr(b->alloc, fptr_eq(front, "0x") ? 16 : 2, slice_split(in, (2, -1))[0]);
  else result = BInt.from.fptr(b->alloc, 10, in);
  return (typed_value){tu_of(c_int, {}), {.c_int = result}};
}
typed_value convertString(IrBuilder *b, fptr in) {
  assertMessage(in.len > 1 && in.ptr[0] == '"' && in.ptr[in.len - 1] == '"');
  let result = mList_init(b->alloc, char, in.len - 2);
  defer { mList_deinit(result); };

  usize i = 1;
  usize end = in.len - 1;
  while (i < end) {
    char c = in.ptr[i];
    if (c == '\\' && i + 1 < end) {
      char out;
      switch (in.ptr[i + 1]) {
          // clang-format off
        case 'n': out = '\n'; break;
        case 't': out = '\t'; break;
        case 'r': out = '\r'; break;
        case 'a': out = '\a'; break;
        case 'b': out = '\b'; break;
        case 'f': out = '\f'; break;
        case 'v': out = '\v'; break;
        case '0': out = '\0'; break;
        case '\\': out = '\\'; break;
        case '\'': out = '\''; break;
        case '"': out = '"'; break;
        default: assertMessage(false , "unknown escape %c%c",in.ptr[i] , in.ptr[i+1]);
          // clang-format on
      }
      mList_push(result, out);
      i += 2;
    } else {
      mList_push(result, c);
      i += 1;
    }
  }

  return (typed_value){
      *IrBuilder_getType(
          b,
          (Type)tu_of(
              r_arr,
              {
                  IrBuilder_getType(b, (Type)tu_of(r_sint, 8)),
                  mList_len(result),
              }
          )
      ),
      mList_toOwned(b->alloc, result)
  };
}

u32 emit_ir(IrBuilder *b, ast *node, u32 ctx) {
  assertMessage(node);
  return tu_match_exp(
      (*node),
      (astDecl, decl, ({
         u32 val_reg = 0;

         if (decl.value || decl.type) {
           val_reg = env_addOp(b, ocode_of(onew, {decl.type ? emit_ir(b, decl.type, 0) : 0}));
           if (decl.value)
             env_addOp(b, ocode_of(omov, {.l = env_addOp(b, ocode_of(oast, decl.value)), .r = val_reg}));
         } else val_reg = env_addOp(b, ocode_of(onew, {.l = 0}));

         if (decl.is_const) env_addOp(b, ocode_of(oconst, {val_reg}));
         if (decl.name) env_bind(b, tu_catch(astId, *decl.name), val_reg);

         val_reg;
       })),
      (astNumber, num, ({
         u8 base = 10;
         if (num.len > 0 && num.ptr[0] == '0') {
           if (num.len > 1 && num.ptr[1] == 'x') base = 16;
           else if (num.len > 1 && num.ptr[1] == 'b') base = 2;
         } else base = 10;
         env_addOp(b, ocode_of(oint, num));
       })),
      (astString, str, env_addOp(b, ocode_of(ostr, str))),
      (astId, id, env_lookup(b, id)),
      (astOp, op, ({
         u32 irc = 0;
         if (op.number_rep == opNumbers.call) {
           let left = emit_ir(b, op.left, 0);

           let offset = b->env->next_reg + 1;
           u32 len = 0;
           tu_match_void(
               *op.right,
               (astPBlock, bb, {
                 let lp = sentList_vla(bb.stmts);
                 len = countof(*lp);
                 foreach (ast *it, vlap(lp))
                   env_addOp(b, ocode_of(oast, it));
               }),
               (default, len = 1; env_addOp(b, ocode_of(oast, op.right));),
           );
           env_addOp(
               b,
               ocode_of(
                   ocall,
                   {
                       .d = irc = env_addOp(b, ocode_of(onew, {})),
                       .len = len,
                       .off = offset,
                       .l = left,
                   }
               )
           );
         } else if (op.number_rep == opNumbers.dot) {
           irc = env_addOp(
               b,
               ocode_of(
                   olelem,
                   {
                       .l = op.left ? emit_ir(b, op.left, 0) : ctx,
                       .name = tu_catch(astId, op.right[0]),
                   }
               )
           );
         } else {
           env_addOp(
               b,
               ocode_of(
                   oop,
                   {
                       .r = emit_ir(b, op.right, 0),
                       .l = emit_ir(b, op.left, 0),
                       .o = op.number_rep,
                       .d = irc = env_addOp(b, ocode_of(onew, {})),
                   }
               )
           );
         }
         irc;
       })),
      (astPBlock, pb, ({
         env_push(b);
         defer { env_pop(b); };
         if (sentList_len(pb.stmts) != 1)
           error_ast(node[0], snprint(stdAlloc, "no tuples"));
         emit_ir(b, pb.stmts[0], ctx);
       })),
      (astBBlock, bb, ({
         let last_reg = env_push(b);
         foreach (let stmnt, vlap(sentList_vla(bb.stmts)))
           emit_ir(b, stmnt, ctx);
         env_pop(b);
         last_reg;
       })),
      (astABlock, ab, env_addOp(b, ocode_of(oast, node))),
      (astEmptyStatement, _, (u32)0),

  );
}
ptrstype(itypeof(scope, values)) * env_getVal(scope *sp, u32 idx) {
  assertMessage(idx - 1 < sp->next_reg, "ascessing value that doesnt exist yet");
  if (idx > sp->start_reg) {
    let ridx = idx - sp->start_reg - 1;
    assertMessage(ridx < msList_len(sp->values));
    return &sp->values[ridx];
  } else {
    let parent = scope_parent(sp);
    assertMessage(parent);
    return env_getVal(parent, idx);
  }
}
scope *env_replace(IrBuilder *b, scope *target, u32 cidx) {
  assertMessage(target);
  assertMessage(op_creates(target->code[cidx]));
  scope *new_scope = avalue(
      b->alloc,
      ((scope){
          .parent = target,
          .locals = msxmap_init(b->alloc, msxmap_iType((itypeof(scope, locals))0)),
          .values = msList_init(b->alloc, ptrstype(new_scope->values)),
          .code = msList_init(b->alloc, opcode),
      })
  );
  u32 start = target->start_reg;
  foreach (let i, span(target->code, cidx))
    start += !!(op_creates(*i));
  new_scope->start_reg = start;
  new_scope->next_reg = start;
  target->code[cidx] = ocode_of(oscop, new_scope);
  return new_scope;
}
struct env_findOwner_result {
  u32 idx;
  scope *scope;
};
struct env_findOwner_result env_findOwner(scope *sp, u32 idx) {
  assertMessage(idx - 1 < sp->next_reg, "accessing value that doesn't exist yet");
  if (idx > sp->start_reg) {
    let ridx = idx - sp->start_reg - 1;
    assertMessage(ridx < msList_len(sp->values));
    return (typeof(env_findOwner(sp, idx))){
        sp->values[ridx].cidx,
        sp,
    };
  }
  sp = scope_parent(sp);
  assertMessage(sp);
  return env_findOwner(sp, idx);
}

void ir_av1(IrBuilder *b);
typed_value hv_handle_evaltype(IrBuilder *b, ast *target_ast, scope *target, u32 cidx) {
  let old_env = b->env;
  defer { b->env = old_env; };
  b->env = env_replace(b, target, cidx);
  u32 reg = emit_ir(b, target_ast, 0);
  ir_av1(b);

  return env_getVal(b->env, reg)->value;
}
void env_pushVal(allocfn allocator, scope *sp, typed_value v, u32 cidx) { msList_push(allocator, sp->values, {v, cidx}); }
void ir_handle_call(IrBuilder *b, ocall call, opcode *insloc) {
  let l = &env_getVal(b->env, call.l)->value;
  let d = &env_getVal(b->env, call.d)->value;
  enum general_JMP {
    ELSE_END = 0xEEEEEEEE,
    LOOP_EXIT = 0xFFFFFFFF,
    LOOP_START = 0xFFFFFFFE,
  };

  tu_match_void(
      l->t,
      (builtin, bin, {
        switch (bin) {
          case builtin_if: {
            assertMessage(call.len >= 2);
            let cond_val = &env_getVal(b->env, call.off)->value;
            let then_val = &env_getVal(b->env, call.off + 1)->value;

            assertMessage(tu_is(c_ast, cond_val->t));
            assertMessage(tu_is(c_ast, then_val->t));

            ast *cond_ast = cond_val->v.c_ast;
            ast *then_ast = then_val->v.c_ast;
            ast *else_ast = nullptr;

            if (call.len > 2) {
              let else_val = &env_getVal(b->env, call.off + 2)->value;
              assertMessage(tu_is(c_ast, else_val->t));
              else_ast = else_val->v.c_ast;
            }

            b->env = P$(
                env_findOwner(b->env, call.d),
                env_replace(b, $.scope, $.idx);
            );
            defer { b->env = scope_parent(b->env); }

            u32 cond_reg = emit_ir(b, cond_ast, 0);
            env_addOp(b, ocode_of(ojif, {.l = cond_reg, .d = ELSE_END}));
            emit_ir(b, then_ast, 0);
            if (else_ast) emit_ir(b, else_ast, 0);

            ir_av1(b);

            typed_value yield_val = (typed_value){tu_of(r_void, {})};
            *d = yield_val;

            *insloc = ocode_of(onone, {});
          } break;
          case builtin_while: {
            assertMessage(call.len == 2);
            let cond_val = &env_getVal(b->env, call.off)->value;
            let body_val = &env_getVal(b->env, call.off + 1)->value;

            assertMessage(tu_is(c_ast, cond_val->t));
            assertMessage(tu_is(c_ast, body_val->t));

            ast *cond_ast = cond_val->v.c_ast;
            ast *body_ast = body_val->v.c_ast;

            b->env = P$(
                env_findOwner(b->env, call.d),
                env_replace(b, $.scope, $.idx);
            );
            defer { b->env = scope_parent(b->env); }

            u32 cond_reg = emit_ir(b, cond_ast, 0);
            env_addOp(b, ocode_of(ojif, {.l = cond_reg, .d = LOOP_EXIT}));
            emit_ir(b, body_ast, 0);

            u32 true_reg = env_addOp(b, ocode_of(oint, fp("1")));
            env_addOp(b, ocode_of(ojif, {.l = true_reg, .d = LOOP_START}));

            ir_av1(b);
            *d = (typed_value){tu_of(r_void, {})};

            *insloc = ocode_of(onone, {});
          } break;
          case builtin_break: {
            b->env = P$(
                env_findOwner(b->env, call.d),
                env_replace(b, $.scope, $.idx);
            );
            defer { b->env = scope_parent(b->env); };

            u32 true_reg = env_addOp(b, ocode_of(oint, fp("1")));
            env_addOp(b, ocode_of(ojif, {.l = true_reg, .d = LOOP_EXIT}));

            ir_av1(b);
            *d = (typed_value){tu_of(r_void, {})};

            *insloc = ocode_of(onone, {});
          } break;
          case builtin_return: {
            assertMessage(call.len == 1);
            let ret_val = &env_getVal(b->env, call.off)->value;
            assertMessage(tu_is(c_ast, ret_val->t));
            ast *ret_ast = ret_val->v.c_ast;

            b->env = P$(
                env_findOwner(b->env, call.d),
                env_replace(b, $.scope, $.idx);
            );
            defer { b->env = scope_parent(b->env); };

            u32 expr_reg = emit_ir(b, ret_ast, 0);
            env_addOp(b, ocode_of(oret, {.l = expr_reg}));

            ir_av1(b);
            *d = (typed_value){tu_of(r_void, {})};

            *insloc = ocode_of(onone, {});
          } break;
          case builtin_int:
          case builtin_uint: {
            assertMessage(call.len == 1);
            let arg = &env_getVal(b->env, call.off)->value;

            if (tu_is(c_ast, arg->t))
              *arg = P$(
                  env_findOwner(b->env, call.off),
                  hv_handle_evaltype(b, arg->v.c_ast, $.scope, $.idx);
              );

            assertMessage(tu_is(c_int, arg->t), "bit width must be a constant integer");
            bigint bint = arg->v.c_int;
            bigint_trim(&bint);
            assertMessage(msList_len(bint) == 1, "bit width must fit in one limb");
            u32 bits = bint[0];
            assertMessage(bigint_cmp(bint, nullptr), "bit width must be positive");

            Type created_type = bin == builtin_int ? (Type)tu_of(r_sint, bits) : (Type)tu_of(r_uint, bits);
            *d = (typeof(*d)){(Type)tu_of(c_type, {}), IrBuilder_getType(b, created_type)};
            *insloc = ocode_of(onone, {});
          } break;
          case builtin_typeof: {
            assertMessage(call.len == 1);
            let arg = &env_getVal(b->env, call.off)->value;

            if (tu_is(c_ast, arg->t))
              *arg = P$(
                  env_findOwner(b->env, call.off),
                  hv_handle_evaltype(b, arg->v.c_ast, $.scope, $.idx)
              );

            d->t = (Type)tu_of(c_type, {});
            d->v.c_type = IrBuilder_getType(b, typeof_Value(*arg));
            *insloc = ocode_of(onone, {});
          } break;
          case builtin_type: {
            assertMessage(call.len == 0);
            d->t = (Type)tu_of(c_type, {});
            d->v.c_type = IrBuilder_getType(b, (Type)tu_of(c_type, {}));
            *insloc = ocode_of(onone, {});
          } break;
          case builtin_ast: {
            assertMessage(call.len == 0);
            d->t = (Type)tu_of(c_type, {});
            d->v.c_type = IrBuilder_getType(b, (Type)tu_of(c_ast, {}));
            *insloc = ocode_of(omov, {.l = call.d, .r = call.d});
          } break;
          case builtin_qot: {
            assertMessage(call.len == 1);
            let arg = &env_getVal(b->env, call.off)->value;
            assertMessage(tu_is(c_ast, arg->t));

            d->t = (Type)tu_of(c_ast, {});
            d->v.c_ast = arg->v.c_ast;
            *insloc = ocode_of(onone, {});
          } break;
          case builtin_uqot: {
            assertMessage(call.len == 1);
            let arg = &env_getVal(b->env, call.off)->value;
            assertMessage(tu_is(c_ast, arg->t));

            b->env = P$(
                env_findOwner(b->env, call.d),
                env_replace(b, $.scope, $.idx)
            );
            defer { b->env = scope_parent(b->env); };

            emit_ir(b, arg->v.c_ast, 0);

            ir_av1(b);
            typed_value yield_val = (typed_value){tu_of(r_void, {})};
            if_decl (let len, msList_len(b->env->values))
              yield_val = b->env->values[len - 1].value;
            *d = yield_val;

            *insloc = ocode_of(onone, {});
          } break;
          case builtin_fn: {
            assertMessage(call.len == 2, "fn expects 2 arguments: (params) and return type");
            let params_val = &env_getVal(b->env, call.off)->value;
            let ret_val = &env_getVal(b->env, call.off + 1)->value;

            assertMessage(tu_is(c_ast, params_val->t));
            assertMessage(tu_is(c_ast, ret_val->t));

            ast *params_ast = params_val->v.c_ast;
            ast *ret_ast = ret_val->v.c_ast;

            b->env = P$(
                env_findOwner(b->env, call.d),
                env_replace(b, $.scope, $.idx)
            );
            defer { b->env = scope_parent(b->env); };

            typedef struct {
              fptr name;
              u32 reg;
            } pending_param;
            mList(pending_param) pending_params = mList_init(b->alloc, pending_param);

            if (tu_is(astPBlock, *params_ast)) {
              let pblock = tu_catch(astPBlock, *params_ast);
              var_ lp = sentList_vla(pblock.stmts);

              foreach (ast *p_ast, vlap(lp)) {
                fptr p_name = fp("");
                ast *p_type_ast = p_ast;

                if (tu_is(astDecl, *p_ast)) {
                  let decl = tu_catch(astDecl, *p_ast);
                  if (decl.name) p_name = tu_catch(astId, *decl.name);
                  if (decl.type) p_type_ast = decl.type;
                  else assertMessage(false, "fn parameter missing type");
                } else if (!tu_is(astId, *p_ast) && !tu_is(astOp, *p_ast)) {
                  assertMessage(false, "fn parameter must be a declaration or type identifier");
                }

                mList_push(pending_params, {p_name, emit_ir(b, p_type_ast, 0)});
              }
            } else if (!tu_is(astEmptyStatement, *params_ast)) {
              fptr p_name = fp("");
              ast *p_type_ast = params_ast;

              if (tu_is(astDecl, *params_ast)) {
                let decl = tu_catch(astDecl, *params_ast);
                if (decl.name) p_name = tu_catch(astId, *decl.name);
                if (decl.type) p_type_ast = decl.type;
                else assertMessage(false, "fn parameter missing type");
              }

              mList_push(pending_params, ((pending_param){p_name, emit_ir(b, p_type_ast, 0)}));
            }

            u32 ret_reg = emit_ir(b, ret_ast, 0);
            ir_av1(b);

            func_value fval = mList_init(b->alloc, mList_iType(fval));
            for (u32 i = 0; i < mList_len(pending_params); i++) {
              mList_push(
                  fval,
                  {
                      .name = mList_arr(pending_params)[i].name,
                      .init = avalue(
                          b->alloc,
                          env_getVal(b->env, mList_arr(pending_params)[i].reg)->value
                      ),
                  }
              );
            }
            mList_deinit(pending_params);

            mList_push(
                fval, {
                          .name = fp(""),
                          .init = avalue(b->alloc, env_getVal(b->env, ret_reg)->value),
                      }
            );

            *d = (typeof(*d)){tu_of(c_func, {}), fval};
            *insloc = ocode_of(onone, {});
          } break;
          case builtin_anyType:
            assertMessage(false, "anyType shouldnt be accesable");
        }
      }),
      (c_func, cf, TODO()),
      (r_func, rf, TODO()),
      (c_type, _, TODO()),
      (r_tag, _, TODO()),
      (r_arr, _, TODO()),
      (r_ptr, _, TODO()),
      (r_bitfield, _, TODO()),
      (r_struct, _, TODO()),
      (default, assertMessage(false, "attempted to call a non-callable type"))
  );
}
void ir_av1(IrBuilder *b) {
  let code = b->env->code;
  let allocator = IrBuilder_allocator(b);
  for (let ip = code; ip < code + msList_len(code); ip++) {
  try_again:
    tu_match_void(
        *ip,
        (oint, str, env_pushVal(allocator, b->env, convertInt(b, str), ip - code)),
        (ostr, str, env_pushVal(allocator, b->env, convertString(b, str), ip - code)),
        (obin, bin, env_pushVal(allocator, b->env, (typed_value){tu_of(builtin, bin)}, ip - code)),
        (oast, ats, env_pushVal(allocator, b->env, (typed_value){tu_of(c_ast, {}), ats}, ip - code)),
        (oarg, oag, {
          assertMessage(b->fnctx, "");
          let fnc = *b->fnctx;
          assertMessage(
              oag < mList_len(fnc),
              "tried to get %u arg, from fn with %u args",
              oag,
              mList_len(fnc)
          );
          env_pushVal(allocator, b->env, (typed_value){mList_arr(fnc)[oag]}, ip - code);
        }),
        (onew, nit, {
          if_decl (let x, nit.l)
            env_pushVal(allocator, b->env, (typed_value){value_asType(env_getVal(b->env, nit.l)->value), {}}, ip - code);
          else env_pushVal(allocator, b->env, (typed_value){tu_of(builtin, builtin_anyType)}, ip - code);
        }),
        (omov, mov, {
          let l = env_getVal(b->env, mov.l);
          let r = env_getVal(b->env, mov.r);

          if (tu_is(c_ast, l->value.t) && !tu_is(c_ast, r->value.t))
            l->value = P$(
                env_findOwner(b->env, mov.l),
                hv_handle_evaltype(b, l->value.v.c_ast, $.scope, $.idx)
            );

          if (Type_cmp(l->value.t, r->value.t)) {
            assertMessage(tu_or(builtin, r->value.t, -1) == builtin_anyType, "type mismatch in omov assignment");
            assertMessage(!r->value.v.value, "cannot mutate initialized value");
            r->value = (typed_value){l->value.t, nullptr};
          }
        }),
        (ocall, cal, ir_handle_call(b, cal, ip)),
        (olelem, ell, {
          let l = &env_getVal(b->env, ell.l)->value;
          Type t;
          bool f = false;
          tu_match_void(
              l->t,
              (r_bitfield, bf, {
                foreach (let v, mList_iter(bf))
                  if (fptr_eq(v.name, ell.name))
                    f = true, t = v.init.t, ({ break; });
              }),
              (r_struct, st, {
                foreach (let v, mList_iter(st))
                  if (fptr_eq(v.name, ell.name))
                    f = true, t = v.init.t, ({ break; });
              }),
              (default, assertMessage(false, "type does not support loading element"))
          );
          if (!f) error_at(ell.name.ptr, "struct has no element with that name");
          env_pushVal(allocator, b->env, (typed_value){t}, ip - code);
        }),
        (oscop, sco, {
          let old_env = b->env;
          b->env = sco;
          ir_av1(b);
          b->env = old_env;

          typed_value yield_val = (typed_value){tu_of(r_void, {})};
          if_decl (let len, msList_len(sco->values)) yield_val.t = sco->values[len - 1].value.t;
          env_pushVal(allocator, b->env, yield_val, ip - code);
        }),
        (oselem, sel, {
          let l = &env_getVal(b->env, sel.l)->value;
          let d = &env_getVal(b->env, sel.d)->value;

          Type field_type;
          bool found = false;

          tu_match_void(
              l->t,
              (r_bitfield, bf, {
                foreach (let v, mList_iter(bf))
                  if (fptr_eq(v.name, sel.name))
                    found = true, field_type = v.init.t, ({ break; });
              }),
              (r_struct, st, {
                foreach (let v, mList_iter(st))
                  if (fptr_eq(v.name, sel.name))
                    found = true, field_type = v.init.t, ({ break; });
              }),
              (default, assertMessage(false, "oselem target l is not a struct or bitfield"))
          );

          if (!found) error_at(sel.name.ptr, "struct has no element with that name");

          if (Type_cmp(d->t, field_type)) {
            assertMessage(tu_or(builtin, d->t, -1) == builtin_anyType, "type mismatch in oselem assignment");
            assertMessage(!d->v.value, "cannot mutate type of an initialized value");
            d->t = field_type;
          }
        }),
        (ocpy, cpy, {
          let l = &env_getVal(b->env, cpy.l)->value;
          let r = &env_getVal(b->env, cpy.r)->value;

          assertMessage(tu_is(r_ptr, l->t), "ocpy source (l) must be a pointer");
          assertMessage(tu_is(r_ptr, r->t), "ocpy destination (r) must be a pointer");

          Type *l_pointee = tu_catch(r_ptr, l->t);
          Type *r_pointee = tu_catch(r_ptr, r->t);

          assertMessage(!Type_cmp(*l_pointee, *r_pointee), "ocpy pointee types do not match");
        }),
        (oop, opp, {
          let l = &env_getVal(b->env, opp.l)->value;
          let r = &env_getVal(b->env, opp.r)->value;
          let d = &env_getVal(b->env, opp.d)->value;

          if (opp.o == opNumbers.pipe) {
            assertMessage(false, "pipe operator (%u) not implemented", opp.o);
          } else if (opp.o == opNumbers.set) { // TODO: should probably just be omov & try again
            if (Type_cmp(l->t, r->t)) {
              assertMessage(tu_or(builtin, r->t, -1) == builtin_anyType, "type mismatch in assignment (op %u)", opp.o);
              r->t = l->t;
            }
            d->t = l->t;
          } else if (
              opp.o == opNumbers.eq || opp.o == opNumbers.neq || opp.o == opNumbers.les ||
              opp.o == opNumbers.mor || opp.o == opNumbers.leq || opp.o == opNumbers.moq
          ) {
            assertMessage(!Type_cmp(l->t, r->t), "type mismatch in comparison (op %u)", opp.o);
            assertMessage(tu_or(builtin, l->t, -1) != builtin_anyType, "cannot compare untyped values (op %u)", opp.o);
            d->t = (Type)tu_of(r_uint, 1);
          } else if (
              opp.o == opNumbers.mul || opp.o == opNumbers.div || opp.o == opNumbers.mod ||
              opp.o == opNumbers.plus || opp.o == opNumbers.minus || opp.o == opNumbers.oand ||
              opp.o == opNumbers.oor || opp.o == opNumbers.oband || opp.o == opNumbers.obor ||
              opp.o == opNumbers.shr || opp.o == opNumbers.shl
          ) {
            assertMessage(!Type_cmp(l->t, r->t), "type mismatch in binary operator (op %u)", opp.o);
            assertMessage(tu_or(builtin, r->t, -1) != builtin_anyType, "cannot operate on untyped values (op %u)", opp.o);
            d->t = r->t;
          } else if (opp.o == opNumbers.onot || opp.o == opNumbers.obnot) {
            assertMessage(tu_or(builtin, r->t, -1) != builtin_anyType, "cannot operate on untyped values (unary op %u)", opp.o);
            d->t = r->t;
          } else assertMessage(false, "unhandled or unknown operator %u in oop", opp.o);
        }),
        (oconst, _, ),
        (ojif, _, ),
        (oret, _, )
    );
  }
}
//{ printer

typePrinter(scope) {
  PUTS("scope [start_reg: ");
  USETYPEPRINTER(u32, in.start_reg);
  PUTS(", next_reg: ");
  USETYPEPRINTER(u32, in.next_reg);
  PUTS("]\n" P_INDENT_IN);

  u32 current_reg = in.start_reg;

  foreach (let o, vlap(msList_vla(in.code))) {
    if (op_creates(o)) {
      current_reg++;
      USETYPEPRINTER(u32, current_reg);
      PUTS(" = ");
    }

    tu_match_void(
        o,
        (onone, _, ),
        (oint, str, {
          PUTS("oint '");
          USENAMEDPRINTER("slice(c8)", str);
          PUTS("'\n");
        }),
        (obin, bin, {
          PUTS("obin ");
          USETYPEPRINTER(builtin_id, bin);
          PUTS("\n");
        }),
        (ostr, str, {
          PUTS("ostr \"");
          USENAMEDPRINTER("slice(c8)", str);
          PUTS("\"\n");
        }),
        (oast, astp, {
          PUTS("oast\n" P_INDENT_IN);
          USENAMEDPRINTER("ast", astp);
          PUTS(P_INDENT_OUT);
        }),
        (oscop, scop, {
          PUTS("oscop\n" P_INDENT_IN);
          USENAMEDPRINTER("scope", *scop);
          PUTS(P_INDENT_OUT);
        }),
        (onew, val, {
          PUTS("onew l:");
          USETYPEPRINTER(u32, val.l);
          PUTS("\n");
        }),
        (olelem, val, {
          PUTS("olelem l:");
          USETYPEPRINTER(u32, val.l);
          PUTS(" name:'");
          USENAMEDPRINTER("slice(c8)", val.name);
          PUTS("'\n");
        }),
        (oconst, val, {
          PUTS("oconst l:");
          USETYPEPRINTER(u32, val.l);
          PUTS("\n");
        }),
        (ojif, val, {
          PUTS("ojif l:");
          USETYPEPRINTER(u32, val.l);
          PUTS(" d:");
          USETYPEPRINTER(u32, val.d);
          PUTS("\n");
        }),
        (ocall, val, {
          PUTS("ocall l:");
          USETYPEPRINTER(u32, val.l);
          PUTS(" off:");
          USETYPEPRINTER(u32, val.off);
          PUTS(" len:");
          USETYPEPRINTER(u32, val.len);
          PUTS(" d:");
          USETYPEPRINTER(u32, val.d);
          PUTS("\n");
        }),
        (oop, val, {
          PUTS("oop l:");
          USETYPEPRINTER(u32, val.l);
          PUTS(" r:");
          USETYPEPRINTER(u32, val.r);
          PUTS(" d:");
          USETYPEPRINTER(u32, val.d);
          PUTS(" o:");
          USETYPEPRINTER(u32, val.o);
          PUTS("\n");
        }),
        (oret, val, {
          PUTS("oret l:");
          USETYPEPRINTER(u32, val.l);
          PUTS("\n");
        }),
        (omov, val, {
          PUTS("omov l:");
          USETYPEPRINTER(u32, val.l);
          PUTS(" r:");
          USETYPEPRINTER(u32, val.r);
          PUTS("\n");
        }),
        (ocpy, val, {
          PUTS("ocpy l:");
          USETYPEPRINTER(u32, val.l);
          PUTS(" r:");
          USETYPEPRINTER(u32, val.r);
          PUTS("\n");
        }),
        (oselem, val, {
          PUTS("oselem l:");
          USETYPEPRINTER(u32, val.l);
          PUTS(" d:");
          USETYPEPRINTER(u32, val.d);
          PUTS(" name:'");
          USENAMEDPRINTER("slice(c8)", val.name);
          PUTS("'\n");
        })
    );
  }
  PUTS("values{\n" P_INDENT_IN);
  foreach (let v, span(in.values, msList_len(in.values)))
    USETYPEPRINTER(typed_value, v->value), PUTS("\n");
  PUTS("named{\n" P_INDENT_IN);
  foreach (let kv, msxmap_iter(in.locals)) {
    USENAMEDPRINTER("slice(c8)", kv.key);
    PUTS(":");

    if (kv.val->idx > in.start_reg) {
      u32 ridx = kv.val->idx - in.start_reg - 1;
      if (ridx < msList_len(in.values))
        USETYPEPRINTER(typed_value, in.values[ridx].value);
      else PUTS("<unevaluated?>");

    } else
      PUTS("<parent_reg:"), USETYPEPRINTER(u32, kv.val->idx), PUTS(">");

    PUTS("\n");
  }
  PUTS(P_INDENT_OUT "}\n");
  PUTS(P_INDENT_OUT "}\n");

  PUTS(P_INDENT_OUT);
}
typePrinter(IrBuilder) {
  PUTS("IrBuilder {\n" P_INDENT_IN);
  USETYPEPRINTER(scope, *in.env);
  PUTS(P_INDENT_OUT "}\n");
}

//}

//
//

test_fn(type_interns) {
  let irb = IrBuilder_init(allocator);
  defer { IrBuilder_deinit(&irb); };

  let v = (Type)tu_of(
      r_struct,
      mList_init(irb.alloc, r_struct_item)
  );
  mList_push(
      tu_catch(r_struct, v),
      {
          .alignment = 8,
          .isconst = false,
          .name = fp("val"),
          .offset = 16,
          .init = (typed_value){tu_of(r_uint, 64)},
      }
  );
  mList_push(
      tu_catch(r_struct, v),
      {
          .alignment = 8,
          .isconst = false,
          .name = fp("next"),
          .offset = 0,
          .init = (typed_value){tu_of(r_ptr, &v)},
      }
  );
  mList_push(
      tu_catch(r_struct, v),
      {
          .alignment = 8,
          .isconst = false,
          .name = fp("prev"),
          .offset = 8,
          .init = (typed_value){tu_of(r_ptr, &v)},
      }
  );
  println("{Type}", v);

  let strArena = arena_new_ext(allocator, 1024);
  defer { arena_cleanup(strArena); };
  let str = snprint(strArena, "{Type}", v);
  let idstr = snprint(strArena, "{id_str}", str);
  let decoded = snprint(strArena, "{id_str:decode}", fptr_CSP(idstr.ptr));
  println("`{slice(c8)}' -> {slice(c8)} -> `{slice(c8)}'", str, idstr, decoded);

  let p1 = IrBuilder_getType(&irb, v);
  let p2 = IrBuilder_getType(&irb, *p1);
  let p3 = IrBuilder_getType(&irb, *p2);
  test_assert(p1 && p2 && p3);
  test_assert(!Type_cmp(*p1, *p2));
  test_assert(!Type_cmp(*p1, *p3));
  test_assert(p1 == p2);
  test_assert(p1 == p3);
}
test_fn(type_typeof) {
  u64 mem = 0xF00;
  u64 mem2 = ~(u64)0;
  let foo = (typed_value){
      tu_of(r_uint, 64), {.value = &mem}
  };
  let bar = (typed_value){
      tu_of(r_uint, 64), {.value = &mem2}
  };
  println("values : {typed_value} , {typed_value} ", foo, bar);
  let typeof_foo = typeof_Value(foo);
  let typeof_bar = typeof_Value(bar);
  test_assert(!Type_cmp(typeof_bar, typeof_foo));

  let typetype = typeof_Value(Type_asValue(&typeof_foo));

  test_assert(Type_cmp(typeof_Value(bar), typetype));
  println("{Type}", typetype);
  test_assert(!Type_cmp(typeof_Value(Type_asValue(&typeof_bar)), typetype));
  let typeof_typeof_bar = typeof_Value(Type_asValue(&typeof_bar));
  test_assert(!Type_cmp(typeof_Value(Type_asValue(&typeof_typeof_bar)), typetype));
  println("t\t: {}", ((fptr){sizeof(typeof_foo), (u8 *)&typeof_foo}));
  println("t(t)\t: {}", ((fptr){sizeof(typetype), (u8 *)&typetype}));
  println("t(t(t))\t: {}", ((fptr){sizeof(typeof_typeof_bar), (u8 *)&typeof_typeof_bar}));
}

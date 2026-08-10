// #define TV_EXPAND_VALUE 1
#pragma once
#include "ast.h"
#include "operation/common.h"
#include "type.h"
#include "wheels/allocators/cballocator.h"
#include "wheels/fptr.h"
#include "wheels/mytypes.h"
#include <assert.h>
#include <stdcountof.h>
#include <wheels/bigint.h>
#include <wheels/smap.h>
#define assert_type(tag, val)              \
  assertMessage(                           \
      tu_is(tag, val),                     \
      "expected " #tag " but got %s",      \
      snprint(stdAlloc, "{Type}", val).ptr \
  )

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
      {"@any", builtin_any},
      {"@int", builtin_int},
      {"@sint", builtin_sint},
      {"@uint", builtin_uint},
      {"@arr", builtin_arrof},
      {"@ptr", builtin_ptrof},
      {"@type", builtin_type},
      {"@typeof", builtin_typeof},
      {"@break", builtin_break},
      {"@qot", builtin_qot},
      {"@uqot", builtin_uqot},
      {"@struct", builtin_struct},
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

Type *IrBuilder_getType(IrBuilder *b, Type t) {
  if_decl (let t, moxmap_get(b->types, t)) return *t;
  let allocator = (b->alloc);
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

void env_bind(IrBuilder *b, fptr id, u32 reg) {
  if (moxmap_get(b->env->locals, id))
    error_at(id.ptr, "item with same name exists in scope");
  moxmap_set(b->env->locals, id, ((moxmap_vt(b->env->locals)){reg, false}));
}

struct env_findOwner_result env_findOwner(IrBuilder *b, u32 idx) {
  let sp = b->env;
  assertMessage(sp);
  assertMessage(idx - 1 < sp->next_reg, "accessing value that doesn't exist yet");

  while (sp) {
    if (idx > sp->start_reg) {
      let ridx = idx - sp->start_reg - 1;
      assertMessage(ridx < msList_len(sp->values));
      return (typeof(env_findOwner(b, idx))){
          sp->values[ridx]->cidx,
          sp,
      };
    }
    sp = (sp->parent);
  }
  assertMessage(false, "could'nt find value in env");
}
u32 env_lookup(IrBuilder *b, fptr id) {
  for (scope *s = b->env; s; s = (s->parent))
    if_decl (let reg_ptr, moxmap_get(s->locals, id))
      if (reg_ptr->idx >= s->start_reg) return reg_ptr->idx;
  error_at(id.ptr, snprint(stdAlloc, "unknown variable {slice(c8)}", id));
}
u32 env_push(IrBuilder *b) {
  scope *new_scope = avalue(
      b->alloc,
      ((scope){
          .parent = b->env,
          .locals = moxmap_init(b->alloc, fptr, moxmap_vt((itypeof(scope, locals))0), cmpfptr),
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

void env_pop(IrBuilder *b) { b->env = (b->env->parent); }
void env_destroy(IrBuilder *b, scope *s) {
  let allocator = b->alloc;
  moxmap_deinit(s->locals);
  msList_deInit(b->alloc, s->values);
  msList_deInit(b->alloc, s->code);
  adestroy(allocator, s);
}
u32 env_addOp(IrBuilder *b, opcode o) {
  msList_push(((hxmap *)b->env->locals)->allocator, b->env->code, o);
  return tu_match_exp(
      o,
      (oscop, _, assertMessage(false, "pushing scope by itself not allowed"), (u32)0),
      (default,
       (u32)(op_creates(o)
                 ? ++b->env->next_reg
                 : 0)),

  );
}

typed_value Type_asValue(Type *t) {
  return (typed_value){tu_of(c_type, {}), {.c_type = t}};
}
Type value_asType(typed_value t) {
  assert_type(c_type, t.t);
  assertMessage(t.v.c_type, "type is unknown");
  return t.v.c_type[0];
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
           val_reg = env_addOp(b, ocode_of(onew, decl.type ? emit_ir(b, decl.type, 0) : 0));
           if (decl.value)
             env_addOp(b, ocode_of(omov, {.l = emit_ir(b, decl.value, 0), .r = val_reg}));
         } else val_reg = env_addOp(b, ocode_of(onew, 0));

         if (decl.is_const) env_addOp(b, ocode_of(oconst, val_reg));
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
           fptr field_name;
           if (tu_is(astId, op.right[0])) {
             field_name = tu_catch(astId, op.right[0]);
           } else if (tu_is(astString, op.right[0])) {
             fptr raw_str = tu_catch(astString, op.right[0]);
             assertMessage(raw_str.len >= 2, "invalid string literal after dot");
             field_name = (fptr){raw_str.len - 2, raw_str.ptr + 1};
           } else assertMessage(false, "right side of dot operator must be an identifier or string");

           irc = env_addOp(b, ocode_of(olelem, {.l = op.left ? emit_ir(b, op.left, 0) : ctx, .name = field_name}));
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
         if (sentList_len(pb.stmts) != 1)
           error_ast(node[0], snprint(stdAlloc, "no tuples"));
         env_push(b);
         defer { env_pop(b); };
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
ptrstype(itypeof(scope, values)) env_getVal(scope *sp, u32 idx) {
  assertMessage(idx - 1 < sp->next_reg, "ascessing value that doesnt exist yet");
  if (idx > sp->start_reg) {
    let ridx = idx - sp->start_reg - 1;
    assertMessage(ridx < msList_len(sp->values));
    return sp->values[ridx];
  } else {
    let parent = (sp->parent);
    assertMessage(parent);
    return env_getVal(parent, idx);
  }
}
scope *env_replace(IrBuilder *b, struct env_findOwner_result r) {
  assertMessage(r.scope);
  assertMessage(op_creates(r.scope->code[r.idx]));
  scope *new_scope = avalue(
      b->alloc,
      ((scope){
          .parent = r.scope,
          .locals = moxmap_init(b->alloc, fptr, moxmap_vt((itypeof(scope, locals))0), cmpfptr),
          .values = msList_init(b->alloc, ptrstype(new_scope->values)),
          .code = msList_init(b->alloc, opcode),
      })
  );
  u32 start = r.scope->start_reg;
  foreach (let i, span(r.scope->code, r.idx))
    start += !!(op_creates(*i));
  new_scope->start_reg = start;
  new_scope->next_reg = start;
  r.scope->code[r.idx] = ocode_of(oscop, new_scope);
  return new_scope;
}

void lower_i(IrBuilder *b);
op_tv hv_handle_evaltype(IrBuilder *b, ast *target_ast, struct env_findOwner_result r) {
  let old_env = b->env;
  defer { b->env = old_env; };
  b->env = env_replace(b, r);
  u32 reg = emit_ir(b, target_ast, 0);
  lower_i(b);

  return env_getVal(b->env, reg)->value;
}
void env_pushVal(allocfn allocator, scope *sp, op_tv v, u32 cidx) {
  msList_push(allocator, sp->values, avalue(allocator, ((typeof(**sp->values)){v, cidx})));
}

typedef struct {
  bool returned, broke;
  op_tv value;
} exec_signal;

exec_signal ir_interpret_scope(IrBuilder *b);
op_tv get_value_temp(IrBuilder *b, ast *ast) {
  env_push(b);
  defer {
    env_pop(b);
    env_destroy(b, tu_catch(oscop, msList_last(b->env->code)));
    msList_len(b->env->code)--;
    b->env->next_reg--;
  };
  u32 val_reg = emit_ir(b, ast, 0);
  lower_i(b);
  ir_interpret_scope(b);
  return env_getVal(b->env, val_reg)->value;
}
ast *full_unquote(IrBuilder *b, ast *x) {
  if (!x) return nullptr;
  return tu_match_exp(
      *x,
      (astEmptyStatement, _, x),
      (astId, id, x),
      (astNumber, n, x),
      (astString, s, x),
      // clang-format off
      (astDecl, decl, avalue( b->alloc, (ast)tu_of(astDecl, {
          .name  = full_unquote(b , decl.name),
          .type  = full_unquote(b , decl.type),
          .value = full_unquote(b , decl.value),
          .is_const =  decl.is_const,
          }))),
      (astPBlock , pb , avalue(b->alloc , (ast)tu_of(astPBlock , {
        .stmts = 
            *({
                let len = sentList_len(pb.stmts);
                let rest = acreate(b->alloc , arrof(ptrof(ast),len+1));
                for(let i = 0 ; i< len ; i ++)
                  (*rest)[i] = full_unquote(b,pb.stmts[i]);
                rest;
              }),
          )
        })),
      (astBBlock , pb , avalue(b->alloc , (ast)tu_of(astBBlock , {
        .stmts = 
            *({
                let len = sentList_len(pb.stmts);
                let rest = acreate(b->alloc , arrof(ptrof(ast),len+1));
                for(let i = 0 ; i< len ; i ++)
                  (*rest)[i] = full_unquote(b,pb.stmts[i]);
                rest;
              }),
          )
        })),
      (astABlock , pb , avalue(b->alloc , (ast)tu_of(astABlock , {
        .items = 
            *({
                let len = sentList_len(pb.items);
                let rest = acreate(b->alloc , arrof(ptrof(ast),len+1));
                for(let i = 0 ; i< len ; i ++)
                  (*rest)[i] = full_unquote(b,pb.items[i]);
                rest;
              }),
          )
        })),
      (astOp , op , ({
        ast *res;
        if (op.number_rep == opNumbers.call && op.left && fptr_eq(tu_catch(astId, *op.left), "@uqot")) {
          let yield_val = tu_catchr(tv,get_value_temp(b , op.right));
          assert_type(c_ast, yield_val.t);
          res = yield_val.v.c_ast;
        } else res = avalue(b->alloc , (ast)tu_of(astOp , {
            .left = full_unquote(b , op.left),
            .right = full_unquote(b , op.right),
            .number_rep = op.number_rep
          }));
        
        res;
        }))
      // clang-format on
  );
}
void ir_handle_call(IrBuilder *b, ocall call, u32 insloc) {
  let l_op = env_getVal(b->env, call.l)->value;
  // print("call {op_tv} (", l_op);
  // foreach (let i, span(call.off, call.len))
  //   print("{op_tv},", env_getVal(b->env, i)->value); // removed &
  // println(")");

  iflet_tu (let it, gfn, l_op) {
    if (!it.body) {
      assertMessage(call.len == 1, "expected 1 argument (body) to complete function definition");
      let body_val = tu_catch(tv, env_getVal(b->env, call.off)->value);
      assert_type(c_ast, body_val.t);

      it.body = body_val.v.c_ast;
      it.spec = moxmap_init(b->alloc, r_func, struct scope *, r_func_cmp);

      env_getVal(b->env, call.d)->value = Op_tv(it);
      b->env->code[insloc] = ocode_of(onone, {});
    } else {
      let params = sentList_vla(it.type.p->stmts);
      assertMessage(call.len == countof(*params), "argument count mismatch");

      let osc = b->env;
      b->env = env_replace(b, env_findOwner(b, call.d));
      defer { b->env = osc; };

      //-parameter & return types
      typeof(u32[call.len]) param_regs = {};
      foreach (let reg, span(param_regs, countof(param_regs))) {
        let i = reg - param_regs;
        let param_ast = (*params)[i];
        *reg = emit_ir(b, param_ast, 0);
        env_addOp(b, ocode_of(omov, {.l = call.off + i, .r = *reg}));
      }

      u32 ret_type_reg = emit_ir(b, it.type.r, 0);
      lower_i(b);

      mList(Type) sig_types = mList_init(b->alloc, Type, call.len + 1);
      defer { mList_deinit(sig_types); };

      for (u32 i = 0; i < call.len; i++) {
        u32 param_reg = param_regs[i];
        mList_push(sig_types, tu_catch(tv, env_getVal(b->env, param_reg)->value).t);
      }
      let ret_type_val = tu_catch(tv, env_getVal(b->env, ret_type_reg)->value);
      Type expected_ret = value_asType(ret_type_val);
      mList_push(sig_types, expected_ret);

      //-function check

      Type fn_type = *IrBuilder_getType(b, (Type)tu_of(r_func, sig_types));
      if (Type_is_comptime(expected_ret)) {
        env_push(b);
        emit_ir(b, it.body, 0);
        lower_i(b);
        let sig = ir_interpret_scope(b);
        assertMessage(sig.returned, "compile time function didnt return ");
        op_tv yield_val = sig.value;
        env_pop(b);

        env_destroy(b, tu_catch(oscop, msList_last(b->env->code)));
        msList_len(b->env->code)--;

        if (tu_is(tv, yield_val)) {
          Type actual_ret = tu_catch(tv, yield_val).t;
          assertMessage(
              !Type_cmp(expected_ret, actual_ret) || tu_is(c_any, expected_ret),
              "return type mismatch: expected %s, got %s",
              snprint(stdAlloc, "{Type}", expected_ret).ptr,
              snprint(stdAlloc, "{Type}", actual_ret).ptr
          );
        }

        env_getVal(osc, call.d)->value = yield_val;
        osc->code[insloc] = ocode_of(onone, {});
      } else {
        if_decl (let made, moxmap_get(it.spec, sig_types)) {
          TODO("function already made");
        } else {
          op_tv yield_val = Op_tv(((typed_value){ret_type_val.t}));
          env_push(b);
          {
            moxmap_set(it.spec, sig_types, b->env);
            emit_ir(b, it.body, 0);
            lower_i(b);
          }
          env_pop(b);

          env_getVal(osc, call.d)->value = yield_val;
          osc->code[insloc] = ocode_of(onone, {});
        }
      }
    }
  } else iflet_tu (let it, tv, l_op) {
    tu_match_void(
        it.t,
        (builtin, bin, {
          switch (bin) {
            case builtin_if: {
              assertMessage(call.len >= 2);
              let cond_val = tu_catch(tv, env_getVal(b->env, call.off)->value);
              let then_val = tu_catch(tv, env_getVal(b->env, call.off + 1)->value);

              assert_type(c_ast, cond_val.t);
              assert_type(c_ast, then_val.t);

              ast *cond_ast = cond_val.v.c_ast;
              ast *then_ast = then_val.v.c_ast;
              ast *else_ast =
                  (call.len > 2)
                      ? P$(
                            &env_getVal(b->env, call.off + 2)->value,
                            (tu_catch(tv, *$)),
                            (assert_type(c_ast, $.t), $.v.c_ast)
                        )
                      : nullptr;

              let osc = b->env;
              b->env = env_replace(b, env_findOwner(b, call.d));
              defer { b->env = osc; }

              u32 cond_reg = emit_ir(b, cond_ast, 0);
              u32 jif_idx = msList_len(b->env->code);
              env_addOp(b, ocode_of(ojif, {.l = cond_reg, .d = 0}));
              emit_ir(b, then_ast, 0);

              if (else_ast) {
                u32 jmp_idx = msList_len(b->env->code);
                env_addOp(b, ocode_of(ojmp, 0));

                b->env->code[jif_idx] = ocode_of(ojif, {.l = cond_reg, .d = msList_len(b->env->code)});

                emit_ir(b, else_ast, 0);

                b->env->code[jmp_idx] = ocode_of(ojmp, msList_len(b->env->code));
              } else b->env->code[jif_idx] = ocode_of(ojif, {.l = cond_reg, .d = msList_len(b->env->code)});

              lower_i(b);

              env_getVal(osc, call.d)->value = Op_tv((r_void){});
              osc->code[insloc] = ocode_of(onone, {});
            } break;
            case builtin_while: {
              assertMessage(call.len == 2);
              let cond_val = tu_catch(tv, env_getVal(b->env, call.off)->value);
              let body_val = tu_catch(tv, env_getVal(b->env, call.off + 1)->value);

              assert_type(c_ast, cond_val.t);
              assert_type(c_ast, body_val.t);

              ast *cond_ast = cond_val.v.c_ast;
              ast *body_ast = body_val.v.c_ast;

              let osc = b->env;
              b->env = env_replace(b, env_findOwner(b, call.d));
              defer { b->env = osc; }

              u32 loop_start_idx = msList_len(b->env->code);
              u32 cond_reg = emit_ir(b, cond_ast, 0);

              u32 jif_exit_idx = msList_len(b->env->code);
              env_addOp(b, ocode_of(ojif, {.l = cond_reg, .d = 0}));

              emit_ir(b, body_ast, 0);

              env_addOp(b, ocode_of(ojmp, loop_start_idx));

              b->env->code[jif_exit_idx] = ocode_of(ojif, {.l = cond_reg, .d = msList_len(b->env->code)});

              lower_i(b);
              env_getVal(osc, call.d)->value = Op_tv((r_void){});

              osc->code[insloc] = ocode_of(onone, {});
            } break;
            case builtin_break: {
              env_getVal(b->env, call.d)->value = Op_tv((r_void){});

              b->env->code[insloc] = ocode_of(obreak, {});
            } break;
            case builtin_return: {
              assertMessage(call.len == 1);
              let ret_val = tu_catch(tv, env_getVal(b->env, call.off)->value);
              assertMessage(tu_is(c_ast, ret_val.t), "unexpected %s", snprint(stdAlloc, "{Type}", ret_val.t).ptr);
              ast *ret_ast = ret_val.v.c_ast;

              let osc = b->env;
              b->env = env_replace(b, env_findOwner(b, call.d));
              defer { b->env = osc; };

              u32 expr_reg = emit_ir(b, ret_ast, 0);
              env_addOp(b, ocode_of(oret, expr_reg));

              lower_i(b);
              env_getVal(osc, call.d)->value = Op_tv((r_void){});

              osc->code[insloc] = ocode_of(onone, {});
            } break;
            case builtin_sint:
            case builtin_uint: {
              assertMessage(call.len == 1);
              let arg_op = &env_getVal(b->env, call.off)->value;
              let arg = tu_catch(tv, *arg_op);

              if (tu_is(c_ast, arg.t)) {
                arg = tu_catchr(tv, hv_handle_evaltype(b, arg.v.c_ast, env_findOwner(b, call.off)));
                *arg_op = Op_tv(arg);
              }

              assertMessage(tu_is(c_int, arg.t), "bit width must be a constant integer");
              bigint bint = arg.v.c_int;
              bigint_trim(&bint);
              assertMessage(msList_len(bint) == 1, "bit width must fit in one limb");
              u32 bits = bint[0];
              assertMessage(bigint_cmp(bint, nullptr), "bit width must be positive");

              Type created_type = bin == builtin_sint ? (Type)tu_of(r_sint, bits) : (Type)tu_of(r_uint, bits);
              env_getVal(b->env, call.d)->value = Op_tv(IrBuilder_getType(b, created_type));
              b->env->code[insloc] = ocode_of(onone, {});
            } break;
            case builtin_arrof: {
              assertMessage(call.len == 2, "@arr expects 2 arguments: size and type");
              let size_op = &env_getVal(b->env, call.off)->value;
              let type_op = &env_getVal(b->env, call.off + 1)->value;

              let size_arg = tu_catch(tv, *size_op);
              if (tu_is(c_ast, size_arg.t)) {

                size_arg = tu_catchr(tv, hv_handle_evaltype(b, size_arg.v.c_ast, env_findOwner(b, call.off)));
                *size_op = Op_tv(size_arg);
              }
              assertMessage(tu_is(c_int, size_arg.t), "array size must evaluate to an integer");

              let type_arg = tu_catch(tv, *type_op);
              if (tu_is(c_ast, type_arg.t)) {
                type_arg = tu_catchr(tv, hv_handle_evaltype(b, type_arg.v.c_ast, env_findOwner(b, call.off + 1)));
                *type_op = Op_tv(type_arg);
              }

              bigint bint = size_arg.v.c_int;
              if (bint) bigint_trim(&bint);
              assertMessage(!bint || msList_len(bint) == 1, "array size must fit in one limb");
              u32 array_size = bint ? bint[0] : 0;

              Type elem_type = value_asType(type_arg);
              assertMessage(!Type_is_comptime(elem_type), "array of comptime is meaningless");
              Type arr_type = (Type)tu_of(r_arr, {IrBuilder_getType(b, elem_type), array_size});

              env_getVal(b->env, call.d)->value = Op_tv(IrBuilder_getType(b, arr_type));
              b->env->code[insloc] = ocode_of(onone, {});
            } break;

            case builtin_ptrof: {
              assertMessage(call.len == 1, "@ptr expects 1 argument: type");
              let type_op = &env_getVal(b->env, call.off)->value;
              let type_arg = tu_catch(tv, *type_op);

              if (tu_is(c_ast, type_arg.t)) {
                type_arg = tu_catchr(tv, hv_handle_evaltype(b, type_arg.v.c_ast, env_findOwner(b, call.off)));
                *type_op = Op_tv(type_arg);
              }

              Type elem_type = value_asType(type_arg);
              assertMessage(!Type_is_comptime(elem_type), "array of comptime has no meaning");
              Type ptr_type = (Type)tu_of(r_ptr, IrBuilder_getType(b, elem_type));

              env_getVal(b->env, call.d)->value = Op_tv(IrBuilder_getType(b, ptr_type));
              b->env->code[insloc] = ocode_of(onone, {});
            } break;
            case builtin_typeof: {
              assertMessage(call.len == 1);
              let arg_op = &env_getVal(b->env, call.off)->value;
              let arg = tu_catch(tv, *arg_op);

              if (tu_is(c_ast, arg.t)) {
                arg = tu_catchr(tv, hv_handle_evaltype(b, arg.v.c_ast, env_findOwner(b, call.off)));
                *arg_op = Op_tv(arg);
              }

              env_getVal(b->env, call.d)->value = Op_tv(IrBuilder_getType(b, typeof_Value(arg)));
              b->env->code[insloc] = ocode_of(onone, {});
            } break;
            case builtin_type: {
              assertMessage(call.len == 0);
              env_getVal(b->env, call.d)->value = Op_tv(IrBuilder_getType(b, (Type)tu_of(c_type, {})));
              b->env->code[insloc] = ocode_of(onone, {});
            } break;
            case builtin_any: {
              assertMessage(call.len == 0);
              env_getVal(b->env, call.d)->value = Op_tv(IrBuilder_getType(b, (Type)tu_of(c_any, {})));
              b->env->code[insloc] = ocode_of(onone, {});
            } break;
            case builtin_int: {
              assertMessage(call.len == 0);
              env_getVal(b->env, call.d)->value = Op_tv(IrBuilder_getType(b, (Type)tu_of(c_int, {})));
              b->env->code[insloc] = ocode_of(onone, {});
            } break;
            case builtin_ast: {
              assertMessage(call.len == 0);
              env_getVal(b->env, call.d)->value = Op_tv(IrBuilder_getType(b, (Type)tu_of(c_ast, {})));
              b->env->code[insloc] = ocode_of(onone, {});
            } break;
            case builtin_qot: {
              assertMessage(call.len == 1);
              let arg = tu_catch(tv, env_getVal(b->env, call.off)->value);
              assert_type(c_ast, arg.t);
              env_getVal(b->env, call.d)->value = Op_tv(
                  ((typed_value){
                      tu_of(c_ast, {}),
                      full_unquote(b, arg.v.c_ast),
                  })
              );
              b->env->code[insloc] = ocode_of(onone, {});
            } break;
            case builtin_uqot: {
              assertMessage(call.len == 1);
              let arg = tu_catch(tv, env_getVal(b->env, call.off)->value);
              assert_type(c_ast, arg.t);

              let osc = b->env;
              b->env = env_replace(b, env_findOwner(b, call.d));
              defer { b->env = osc; };

              emit_ir(b, arg.v.c_ast, 0);

              lower_i(b);
              op_tv yield_val = Op_tv((r_void){});
              if_decl (let len, msList_len(b->env->values))
                yield_val = b->env->values[len - 1]->value;
              env_getVal(osc, call.d)->value = yield_val;

              osc->code[insloc] = ocode_of(onone, {});
            } break;
            case builtin_fn: {
              assertMessage(call.len == 2, "fn expects 2 arguments");
              let params_val = tu_catch(tv, env_getVal(b->env, call.off)->value);
              let ret_val = tu_catch(tv, env_getVal(b->env, call.off + 1)->value);

              assert_type(c_ast, params_val.t);
              assert_type(c_ast, ret_val.t);

              ast *params_ast = params_val.v.c_ast;
              ast *ret_ast = ret_val.v.c_ast;

              env_getVal(b->env, call.d)->value = Op_tv(((gfn){{&tu_catch(astPBlock, *params_ast), ret_ast}}));

              b->env->code[insloc] = ocode_of(onone, {});
            } break;
          }
        }),
        (r_func, rf, {
          u32 nparams = mList_len(rf) - 1;
          assertMessage(call.len == nparams, "call expected %u args, got %u", nparams, call.len);

          for (u32 i = 0; i < nparams; i++) {
            let arg_op = &env_getVal(b->env, call.off + i)->value;
            let arg = tu_catch(tv, *arg_op);
            Type param_type = mList_arr(rf)[i];

            if (tu_is(c_ast, arg.t) && !tu_is(c_ast, param_type)) {
              arg = tu_catchr(tv, hv_handle_evaltype(b, arg.v.c_ast, env_findOwner(b, call.off + i)));
              *arg_op = Op_tv(arg);
            }
            assertMessage(!Type_cmp(arg.t, param_type), "argument %u type mismatch , %s", i, snprint(stdAlloc, "{Type} != {Type}", arg.t, param_type).ptr);
          }
          env_getVal(b->env, call.d)->value = Op_tv(((typed_value){mList_arr(rf)[nparams]}));
          // no replace
        }),
        (r_tag, _, TODO()),
        (r_arr, _, TODO()),
        (r_ptr, _, TODO()),
        (r_bitfield, _, TODO()),
        (r_struct, _, TODO()),
        // clang-format off
        (default, 
          assertMessage(
            false ,
            "%s does not support call" ,
            snprint(stdAlloc , "{Type}" , it.t).ptr
          );
        ),
        // clang-format on
    );
  } else unreachable();
}

bool runtime_truthy(typed_value v) {
  assertMessage(!Type_is_comptime(v.t));
  return fptr_isEmpty((fptr){Type_rsize(v.t), (u8 *)v.v.value});
}
bool tv_truthy(typed_value v) {
  return tu_match_exp(
      v.t,
      (builtin, _, assertMessage(false, "builtin has no value"), false),
      (r_void, _, false),
      (r_tag, t, tv_truthy((typed_value){*t.type, v.v})),
      (c_int, _, (bool)bigint_cmp(v.v.c_int, nullptr)),
      (c_type, t, (bool)Type_cmp((Type)tu_of(r_void, {}), *v.v.c_type)),
      (c_ast, _, (bool)!tu_is(astEmptyStatement, *v.v.c_ast)),
      (c_any, _, false),
      (default, runtime_truthy(v)),
  );
}
bigint mk_bool(allocfn a, bool v) { return v ? BInt.from.i64(a, 1) : nullptr; }
exec_signal ir_interpret_scope(IrBuilder *b) {
  let code = b->env->code;
  let allocator = (b->alloc);
  exec_signal sig = {0};

  for (u32 ipidx = 0; ipidx < msList_len(code);) {
    opcode *ip = &code[ipidx];
    u32 next = ipidx + 1;

    tu_match_void(
        *ip,
        (oop, opp, {
          let l = tu_catch(tv, env_getVal(b->env, opp.l)->value);
          let r = tu_catch(tv, env_getVal(b->env, opp.r)->value);
          let d_op = &env_getVal(b->env, opp.d)->value;
          let d = tu_catch(tv, *d_op);

          if (opp.o == opNumbers.pipe) {
            assertMessage(false, "pipe operator (%u) not implemented", opp.o);
          } else if (opp.o == opNumbers.set) {
            if (Type_cmp(l.t, r.t)) {
              assertMessage(tu_is(c_any, r.t), "type mismatch in omov assignment");
              r.t = l.t;
            }
            d = l;
          } else if (
              EQUAL_ANY(
                  opp.o,
                  opNumbers.eq,
                  opNumbers.neq,
                  opNumbers.les,
                  opNumbers.mor,
                  opNumbers.moq,
                  opNumbers.leq
              )
          ) {
            assertMessage(!Type_cmp(l.t, r.t), "type mismatch in comparison (op %u)", opp.o);
            i8 c = bigint_cmp(l.v.c_int, r.v.c_int);
            bool res =
                opp.o == opNumbers.eq    ? c == 0
                : opp.o == opNumbers.neq ? c != 0
                : opp.o == opNumbers.les ? c < 0
                : opp.o == opNumbers.mor ? c > 0
                : opp.o == opNumbers.leq ? c <= 0
                                         : c >= 0;
            d.t = (Type)tu_of(r_uint, 1);
            d.v.c_int = mk_bool(allocator, res);
          } else if (opp.o == opNumbers.mul || opp.o == opNumbers.div || opp.o == opNumbers.mod || opp.o == opNumbers.plus || opp.o == opNumbers.minus || opp.o == opNumbers.oand || opp.o == opNumbers.oor || opp.o == opNumbers.oband || opp.o == opNumbers.obor || opp.o == opNumbers.shr || opp.o == opNumbers.shl) {
            assertMessage(!Type_cmp(l.t, r.t) || tu_is(c_any, r.t), "type mismatch in binary operator (op %u)", opp.o);
            d.t = r.t;
            if (opp.o == opNumbers.plus) d.v.c_int = bigint_add(allocator, l.v.c_int, r.v.c_int);
            else if (opp.o == opNumbers.minus) d.v.c_int = bigint_sub(allocator, l.v.c_int, r.v.c_int);
            else if (opp.o == opNumbers.mul) d.v.c_int = bigint_mul(allocator, l.v.c_int, r.v.c_int);
            else if (opp.o == opNumbers.div) {
              let dm = bigint_div(allocator, l.v.c_int, r.v.c_int);
              msList_deInit(allocator, dm.mod);
              d.v.c_int = dm.div;
            } else if (opp.o == opNumbers.mod) {
              let dm = bigint_div(allocator, l.v.c_int, r.v.c_int);
              msList_deInit(allocator, dm.div);
              d.v.c_int = dm.mod;
            } else if (opp.o == opNumbers.oand) d.v.c_int = mk_bool(allocator, tv_truthy(l) && tv_truthy(r));
            else if (opp.o == opNumbers.oor) d.v.c_int = mk_bool(allocator, tv_truthy(l) || tv_truthy(r));
            else if (opp.o == opNumbers.shl || opp.o == opNumbers.shr) {
              bigint rt = bigint_copy(allocator, r.v.c_int);
              bigint_trim(&rt);
              assertMessage(msList_len(rt) == 1, "shift amount must fit in one limb");
              u32 n = rt[0];
              msList_deInit(allocator, rt);
              assertMessage(n < 63, "shift amount too large for this implementation");
              let pow2 = BInt.from.i64(allocator, (i64)1 << n);
              if (opp.o == opNumbers.shl) d.v.c_int = bigint_mul(allocator, l.v.c_int, pow2);
              else {
                let dm = bigint_div(allocator, l.v.c_int, pow2);
                msList_deInit(allocator, dm.mod);
                d.v.c_int = dm.div;
              }
              msList_deInit(allocator, pow2);
            } else assertMessage(false, "bitwise and/or (op %u) not implemented — see note", opp.o);
          } else if (opp.o == opNumbers.onot || opp.o == opNumbers.obnot) {
            assertMessage(tu_is(c_any, r.t), "type mismatch in omov assignment");
            d.t = r.t;
            if (opp.o == opNumbers.onot) d.v.c_int = mk_bool(allocator, !tv_truthy(r));
            else {
              let one = BInt.from.i64(allocator, 1);
              let inc = bigint_add(allocator, r.v.c_int, one);
              d.v.c_int = bigint_negate(allocator, inc);
              msList_deInit(allocator, one);
              msList_deInit(allocator, inc);
            }
          } else assertMessage(false, "unhandled or unknown operator %u in oop", opp.o);

          *d_op = Op_tv(d);
        }),
        (omov, m, ),

        (oret, l, {
          sig.value = env_getVal(b->env, l)->value;
          println("returning {op_tv}", sig.value);
          sig.returned = true;
        }),
        (obreak, _, sig.broke = true),
        (ojif, jmp, {
          if (!tv_truthy(tu_catch(tv, env_getVal(b->env, jmp.l)->value)))
            next = jmp.d;
        }),
        (ojmp, tgt, next = tgt;),

        (oscop, sco, {
          let old_env = b->env;
          b->env = sco;
          let sub = ir_interpret_scope(b);
          b->env = old_env;

          if (sub.returned || sub.broke) {
            sig.returned = sub.returned;
            sig.broke = sub.broke;
            sig.value = sub.value;
          } else {
            op_tv yield_val = Op_tv((r_void){});
            if_decl (let len, msList_len(sco->values))
              yield_val = sco->values[len - 1]->value;
            env_pushVal(allocator, b->env, yield_val, ipidx);
          }
        }),
        (oselem, _, TODO()),
        (ocall, _, TODO()),

        // handled by lower_i
        (onew, _, ),
        (oint, _, ),
        (ostr, _, ),
        (oast, _, ),
        (olelem, _, ),
        (obin, _, ),
        (oarg, _, ),

        (oconst, _, ),
        (onone, _, ),
    );

    if (sig.returned || sig.broke) return sig;
    ipidx = next;
  }
  return sig;
}
#undef assert_type
void lower_i(IrBuilder *b) {
  let allocator = (b->alloc);
  foreach (let const ipdix, range(0, msList_len(b->env->code))) {
    tu_match_void(
        (b->env->code)[ipdix],
        (oint, str, env_pushVal(allocator, b->env, Op_tv(convertInt(b, str)), ipdix)),
        (ostr, str, env_pushVal(allocator, b->env, Op_tv(convertString(b, str)), ipdix)),
        (obin, bin, env_pushVal(allocator, b->env, Op_tv(((typed_value){tu_of(builtin, bin)})), ipdix)),
        (oast, ats, env_pushVal(allocator, b->env, Op_tv(ats), ipdix)),
        (oarg, oag, {
          assertMessage(b->fnctx, "");
          let fnc = *b->fnctx;
          assertMessage(
              oag < mList_len(fnc),
              "tried to get %u arg, from fn with %u args",
              oag,
              mList_len(fnc)
          );
          env_pushVal(allocator, b->env, Op_tv(((typed_value){mList_arr(fnc)[oag]})), ipdix);
        }),
        (onew, nit, {
          if_decl (let x, nit) {
            Type t = value_asType(tu_catch(tv, env_getVal(b->env, nit)->value));
            value_store vs = {0};

            if (!Type_is_comptime(t)) {
              usize sz = Type_rsize(t);
              if (sz > 0) {
                let tmp = mList_init(b->alloc, u8, sz);
                for (usize _i = 0; _i < sz; _i++)
                  mList_push(tmp, 0);
                vs.value = mList_toOwned(b->alloc, tmp);
              }
            }
            env_pushVal(allocator, b->env, Op_tv(((typed_value){t, vs})), ipdix);
          } else env_pushVal(allocator, b->env, Op_tv((c_any){}), ipdix);
        }),
        (omov, mov, {
          let l_op = &env_getVal(b->env, mov.l)->value;
          let r_op = &env_getVal(b->env, mov.r)->value;
          let l = *l_op;
          let r = *r_op;

          if (tu_is(c_ast, tu_or(tv, l, {}).t) && !tu_is(c_ast, tu_or(tv, r, {}).t)) {
            l = hv_handle_evaltype(b, tu_catch(tv, l).v.c_ast, env_findOwner(b, mov.l));
            *l_op = l;
          }

          if (tu_is(tv, l) && tu_is(tv, r)) {
            let l_tv = tu_catch(tv, l);
            let r_tv = tu_catch(tv, r);

            if (Type_cmp(l_tv.t, r_tv.t)) {
              assertMessage(
                  tu_is(c_any, r_tv.t),
                  "type mismatch %s",
                  snprint(stdAlloc, "{Type} != {Type}", r_tv.t, l_tv.t).ptr
              );
            } else if (!Type_is_comptime(l_tv.t) && r_tv.v.value && l_tv.v.value)
              memcpy(r_tv.v.value, l_tv.v.value, Type_rsize(l_tv.t));
          }

          *r_op = l;
        }),
        (ocall, cal, ir_handle_call(b, cal, ipdix)),
        (olelem, ell, {
          let l = tu_catch(tv, env_getVal(b->env, ell.l)->value);

          if (tu_is(c_ast, l.t)) {
            ast *node = l.v.c_ast;
            typed_value yield_val = {tu_of(r_void, {})};

            if (node) {
              if (fptr_eq(ell.name, "id") && tu_is(astId, *node)) yield_val = l;
              else if (fptr_eq(ell.name, "number") && tu_is(astNumber, *node)) yield_val = l;
              else if (fptr_eq(ell.name, "string") && tu_is(astString, *node)) yield_val = l;
              else if (fptr_eq(ell.name, "decl") && tu_is(astDecl, *node)) yield_val = l;
              else if (fptr_eq(ell.name, "op") && tu_is(astOp, *node)) yield_val = l;
              else if (fptr_eq(ell.name, "pblock") && tu_is(astPBlock, *node)) yield_val = l;
              else if (fptr_eq(ell.name, "bblock") && tu_is(astBBlock, *node)) yield_val = l;
              else if (fptr_eq(ell.name, "ablock") && tu_is(astABlock, *node)) yield_val = l;

              else iflet_tu (let op_node, astOp, *node) {

                if (fptr_eq(ell.name, "left")) {
                  if (op_node.left) yield_val = (typed_value){tu_of(c_ast, {}), {.c_ast = op_node.left}};
                } else if (fptr_eq(ell.name, "right")) {
                  if (op_node.right) yield_val = (typed_value){tu_of(c_ast, {}), {.c_ast = op_node.right}};
                } else if (ell.name.len <= sizeof(u32)) {
                  u32 name_rep = 0;
                  memcpy(&name_rep, ell.name.ptr, ell.name.len);
                  if (op_node.number_rep == name_rep) yield_val = l;
                }
              } else iflet_tu (let decl_node, astDecl, *node) {
                let decl_node = tu_catch(astDecl, *node);

                if (fptr_eq(ell.name, "name")) {
                  if (decl_node.name) yield_val = (typed_value){tu_of(c_ast, {}), {.c_ast = decl_node.name}};
                } else if (fptr_eq(ell.name, "type")) {
                  if (decl_node.type) yield_val = (typed_value){tu_of(c_ast, {}), {.c_ast = decl_node.type}};
                } else if (fptr_eq(ell.name, "value")) {
                  if (decl_node.value) yield_val = (typed_value){tu_of(c_ast, {}), {.c_ast = decl_node.value}};
                } else if (fptr_eq(ell.name, "is_const")) {
                  yield_val = (typed_value){tu_of(c_int, {}), {.c_int = decl_node.is_const ? BInt.from.i64(allocator, 1) : nullptr}};
                }
              }
            }

            static ast base_empty = tu_of(astEmptyStatement, {});
            iflet_tu (let v, r_void, yield_val.t)
              yield_val = (typed_value){
                  tu_of(c_ast, {}),
                  &base_empty
              };
            println("( {ast} . {slice(c8)} = {ast} )", node, ell.name, yield_val.v.c_ast);

            env_pushVal(allocator, b->env, Op_tv(yield_val), ipdix);
          } else {
            Type t;
            bool found = false;
            typed_value const_val = {0};
            bool is_const = false;
            usize offset = 0;

            Type actual_type = l.t;
            if (tu_is(c_type, l.t)) actual_type = *l.v.c_type;

            tu_match_void(
                actual_type,
                (r_bitfield, bf, {
                  foreach (let v, mList_iter(bf))
                    if (fptr_eq(v.name, ell.name)) {
                      found = true;
                      t = v.init.t;
                      if (is_const) const_val = v.init;
                      break;
                    }
                }),
                (r_struct, st, {
                  foreach (let v, mList_iter(st))
                    if (fptr_eq(v.name, ell.name)) {
                      found = true;
                      t = v.init.t;
                      is_const = v.isconst;
                      offset = v.offset;
                      if (is_const) const_val = v.init;
                      break;
                    }
                }),
                (default, assertMessage(false, "%s does not support loading element", snprint(stdAlloc, "{Type}", actual_type).ptr))
            );

            if (!found) error_at(ell.name.ptr, "struct has no element with that name");

            if (is_const) {
              env_pushVal(allocator, b->env, Op_tv(const_val), ipdix);
            } else {
              assertMessage(!tu_is(c_type, l.t), "cannot access non-const instance member statically");
              value_store vs = {0};
              if (!Type_is_comptime(t) && l.v.value) {
                vs.value = ((u8 *)l.v.value) + offset;
              }
              env_pushVal(allocator, b->env, Op_tv(((typed_value){t, vs})), ipdix);
            }
          }
        }),
        (oscop, sco, {
          let old_env = b->env;
          b->env = sco;
          lower_i(b);
          b->env = old_env;

          op_tv yield_val = Op_tv((r_void){});
          if_decl (let len, msList_len(sco->values)) {
            op_tv last_val = sco->values[len - 1]->value;
            if_tu_is (let tval, tv, last_val) {
              if (!Type_is_comptime(tval.t)) tval.v = (value_store){0};
              yield_val = Op_tv(tval);
            } else yield_val = last_val;
          }
          env_pushVal(allocator, b->env, yield_val, ipdix);
        }),
        (oselem, sel, {
          let l = tu_catch(tv, env_getVal(b->env, sel.l)->value);
          let d_op = &env_getVal(b->env, sel.d)->value;
          let d = tu_catch(tv, *d_op);

          Type field_type;
          bool found = false;

          tu_match_void(
              l.t,
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

          if (Type_cmp(d.t, field_type)) {
            assertMessage(tu_is(c_any, d.t), "type mismatch in omov assignment");
            assertMessage(!d.v.value, "cannot mutate type of an initialized value");
            d.t = field_type;
            *d_op = Op_tv(d);
          }
        }),
        (ocpy, cpy, {
          let l = tu_catch(tv, env_getVal(b->env, cpy.l)->value);
          let r = tu_catch(tv, env_getVal(b->env, cpy.r)->value);

          assertMessage(tu_is(r_ptr, l.t), "ocpy source (l) must be a pointer");
          assertMessage(tu_is(r_ptr, r.t), "ocpy destination (r) must be a pointer");

          Type *l_pointee = tu_catch(r_ptr, l.t);
          Type *r_pointee = tu_catch(r_ptr, r.t);

          assertMessage(!Type_cmp(*l_pointee, *r_pointee), "ocpy pointee types do not match");
        }),
        (oop, opp, {
          let l = tu_catch(tv, env_getVal(b->env, opp.l)->value);
          let r = tu_catch(tv, env_getVal(b->env, opp.r)->value);
          let d_op = &env_getVal(b->env, opp.d)->value;
          let d = tu_catch(tv, *d_op);

          if (opp.o == opNumbers.pipe) {
            assertMessage(false, "pipe operator (%u) not implemented", opp.o);
          } else if (opp.o == opNumbers.set) {
            if (Type_cmp(l.t, r.t)) {
              assertMessage(tu_is(c_any, r.t), "type mismatch in omov assignment");
              r.t = l.t;
            }
            d.t = l.t;
          } else if (
              opp.o == opNumbers.eq || opp.o == opNumbers.neq || opp.o == opNumbers.les ||
              opp.o == opNumbers.mor || opp.o == opNumbers.leq || opp.o == opNumbers.moq
          ) {
            assertMessage(!Type_cmp(l.t, r.t), "type mismatch in comparison (op %u)", opp.o);
            assertMessage(tu_is(c_any, l.t), "type mismatch in omov assignment");
            d.t = (Type)tu_of(r_uint, 1);

            if (tu_is(c_int, l.t) && tu_is(c_int, r.t)) {
              i8 c = bigint_cmp(l.v.c_int, r.v.c_int);
              bool res = opp.o == opNumbers.eq ? c == 0 : opp.o == opNumbers.neq ? c != 0
                                                      : opp.o == opNumbers.les   ? c < 0
                                                      : opp.o == opNumbers.mor   ? c > 0
                                                      : opp.o == opNumbers.leq   ? c <= 0
                                                                                 : c >= 0;
              d.v.c_int = res ? BInt.from.i64(allocator, 1) : nullptr;
            }
          } else if (
              opp.o == opNumbers.mul || opp.o == opNumbers.div || opp.o == opNumbers.mod ||
              opp.o == opNumbers.plus || opp.o == opNumbers.minus || opp.o == opNumbers.oand ||
              opp.o == opNumbers.oor || opp.o == opNumbers.oband || opp.o == opNumbers.obor ||
              opp.o == opNumbers.shr || opp.o == opNumbers.shl
          ) {
            assertMessage(!Type_cmp(l.t, r.t) || tu_is(c_any, r.t), "type mismatch in binary operator (op %u)", opp.o);
            d.t = r.t;

            if (tu_is(c_int, l.t) && tu_is(c_int, r.t)) {
              if (opp.o == opNumbers.plus) d.v.c_int = bigint_add(allocator, l.v.c_int, r.v.c_int);
              else if (opp.o == opNumbers.minus) d.v.c_int = bigint_sub(allocator, l.v.c_int, r.v.c_int);
              else if (opp.o == opNumbers.mul) d.v.c_int = bigint_mul(allocator, l.v.c_int, r.v.c_int);
              else if (opp.o == opNumbers.div) {
                let dm = bigint_div(allocator, l.v.c_int, r.v.c_int);
                msList_deInit(allocator, dm.mod);
                d.v.c_int = dm.div;
              } else if (opp.o == opNumbers.mod) {
                let dm = bigint_div(allocator, l.v.c_int, r.v.c_int);
                msList_deInit(allocator, dm.div);
                d.v.c_int = dm.mod;
              } else if (opp.o == opNumbers.oand) {
                bool lv = bigint_cmp(l.v.c_int, nullptr) != 0;
                bool rv = bigint_cmp(r.v.c_int, nullptr) != 0;
                d.v.c_int = (lv && rv) ? BInt.from.i64(allocator, 1) : nullptr;
              } else if (opp.o == opNumbers.oor) {
                bool lv = bigint_cmp(l.v.c_int, nullptr) != 0;
                bool rv = bigint_cmp(r.v.c_int, nullptr) != 0;
                d.v.c_int = (lv || rv) ? BInt.from.i64(allocator, 1) : nullptr;
              } else if (opp.o == opNumbers.shl || opp.o == opNumbers.shr) {
                bigint rt = bigint_copy(allocator, r.v.c_int);
                if (rt) bigint_trim(&rt);
                assertMessage(rt == nullptr || msList_len(rt) == 1, "shift amount must fit in one limb");
                u32 n = rt ? rt[0] : 0;
                if (rt) msList_deInit(allocator, rt);

                let pow2 = BInt.from.i64(allocator, (i64)1 << n);
                if (opp.o == opNumbers.shl) d.v.c_int = bigint_mul(allocator, l.v.c_int, pow2);
                else {
                  let dm = bigint_div(allocator, l.v.c_int, pow2);
                  msList_deInit(allocator, dm.mod);
                  d.v.c_int = dm.div;
                }
                msList_deInit(allocator, pow2);
              } else {
                assertMessage(false, "bitwise and/or (op %u) not implemented", opp.o);
              }
            }
          } else if (opp.o == opNumbers.onot || opp.o == opNumbers.obnot) {
            assertMessage(tu_is(c_any, r.t), "type mismatch in omov assignment");
            d.t = r.t;

            if (tu_is(c_int, r.t)) {
              if (opp.o == opNumbers.onot) {
                bool rv = bigint_cmp(r.v.c_int, nullptr) != 0;
                d.v.c_int = rv ? nullptr : BInt.from.i64(allocator, 1);
              } else {
                let one = BInt.from.i64(allocator, 1);
                let inc = bigint_add(allocator, r.v.c_int, one);
                d.v.c_int = bigint_negate(allocator, inc);
                msList_deInit(allocator, one);
                msList_deInit(allocator, inc);
              }
            }
          } else assertMessage(false, "unhandled or unknown operator %u in oop", opp.o);

          *d_op = Op_tv(d);
        }),
        (oconst, con, {
          let o = env_findOwner(b, con);
          assertMessage(o.scope == b->env, "constant set in inferior scope");
          bool found = false;
          foreach (let kv, moxmap_iter(o.scope->locals, fptr, moxmap_vt(o.scope->locals)))
            if ((found = kv.val->idx == con)) {
              kv.val->isconst = true;
              break;
            }
          assertMessage(found);
        }),
        (onone, _, ),
        // not present before lowering
        (obreak, _, unreachable()),
        (oret, _, unreachable()),
        (ojif, _, unreachable()),
        (ojmp, _, unreachable()),
    );
  } //}
}

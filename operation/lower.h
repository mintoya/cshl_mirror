#include "common2.h"
#include "wheels/fptr.h"
#include "wheels/macros.h"
#include "wheels/mytypes.h"
#include "wheels/print/print_pre.h"
#include "wheels/sList.h"
#include <stdcountof.h>

typed_value temporary_interpret(superscope *env, scope *s, ast *ats) {
  let ns = scope_inferior(env, s);
  defer { scope_destroy(env, ns); };
  lower(env, ns, ats);
  return interpret(env, ns);
}

ast *full_unquote(superscope *env, scope *s, ast *x) {
  if (!x) return nullptr;
  return tu_match_exp(
      *x,
      (astEmptyStatement, _, x),
      (astId, id, x),
      (astNumber, n, x),
      (astString, str, x),
      // clang-format off
      (astDecl, decl, avalue( env->alloc, (ast)tu_of(astDecl, {
          .name  = full_unquote(env, s, decl.name),
          .type  = full_unquote(env, s, decl.type),
          .value = full_unquote(env, s, decl.value),
          .is_const =  decl.is_const,
          }))),
      (astPBlock , pb , avalue(env->alloc , (ast)tu_of(astPBlock , {
        .stmts = 
            *({
                let len = sentList_len(pb.stmts);
                let rest = acreate(env->alloc , arrof(ptrof(ast),len+1));
                for(let i = 0 ; i< len ; i ++)
                  (*rest)[i] = full_unquote(env, s, pb.stmts[i]);
                rest;
              }),
          )
        })),
      (astBBlock , pb , avalue(env->alloc , (ast)tu_of(astBBlock , {
        .stmts = 
            *({
                let len = sentList_len(pb.stmts);
                let rest = acreate(env->alloc , arrof(ptrof(ast),len+1));
                for(let i = 0 ; i< len ; i ++)
                  (*rest)[i] = full_unquote(env, s, pb.stmts[i]);
                rest;
              }),
          )
        })),
      (astABlock , pb , avalue(env->alloc , (ast)tu_of(astABlock , {
        .items = 
            *({
                let len = sentList_len(pb.items);
                let rest = acreate(env->alloc , arrof(ptrof(ast),len+1));
                for(let i = 0 ; i< len ; i ++)
                  (*rest)[i] = full_unquote(env, s, pb.items[i]);
                rest;
              }),
          )
        })),
      (astOp , op , ({
        ast *res;
        if (op.number_rep == opNumbers.call && op.left && fptr_eq(tu_catch(astId, *op.left), "@uqot")) {
          let yield_val = temporary_interpret(env, s, op.right);
          assert_type(c_ast, yield_val.t);
          res = yield_val.v.c_ast;
        } else res = avalue(env->alloc , (ast)tu_of(astOp , {
            .left = full_unquote(env, s, op.left),
            .right = full_unquote(env, s, op.right),
            .number_rep = op.number_rep
          }));
        
        res;
        }))
      // clang-format on
  );
}
void scope_add_inst(superscope *env, scope *s, ir i) { msList_push(env->alloc, s->instructions, i); }
u32 lower_builtin(superscope *env, scope *s, builtin_id id, ast *right) {
#define noargs() assertMessage(sentList_len(tu_catch(astPBlock, *right).stmts) == 0)
  switch (id) {
      // clang-format off
    case builtin__this :TODO("");
    case builtin_struct:TODO("");
    case builtin_at    :TODO("");
    case builtin_fn    :TODO("");
    case builtin_uqot: {
      astptr inner = tu_match_exp(
          *right,
          (astPBlock, pb, assertMessage(sentList_len(pb.stmts) == 1), pb.stmts[0]),
          (default, right),
      );
      assertMessage(tu_is(astId, *inner), "@uqot expects a variable");
      u32 idx = ss_get_named(env, s, tu_catch(astId, *inner));
      typed_value *v = scope_find(env, s, idx);
      return scope_add_value(env, s, (typed_full){v->t, v->v});
    } break;
      // clang-format on
      // type returning
      // clang-format off
    case builtin_type :noargs();return scope_add_value(env,s,(typed_full){tu_of(c_type,{}),ss_getType(env, (Type)tu_of(c_type,{}))});break;
    case builtin_ast  :noargs();return scope_add_value(env,s,(typed_full){tu_of(c_type,{}),ss_getType(env, (Type)tu_of(c_ast, {}))});break;
    case builtin_any  :noargs();return scope_add_value(env,s,(typed_full){tu_of(c_type,{}),ss_getType(env, (Type)tu_of(c_any, {}))});break;
    case builtin_int  :noargs();return scope_add_value(env,s,(typed_full){tu_of(c_type,{}),ss_getType(env, (Type)tu_of(c_int, {}))});break;
    case builtin_sint:
    case builtin_uint: {
      let arg = temporary_interpret(env, s,tu_match_exp(
          *right,
          (astPBlock, pb, assertMessage(sentList_len(pb.stmts) == 1), pb.stmts[0]),
          (default, right),
      ));
      assert_type(c_int, arg.t); assertMessage(arg.v.c_int);
      const u16 n = arg.v.c_int[0];
      return scope_add_value(env, s, (typed_full){tu_of(c_type,{}),ss_getType(env,
            id==builtin_sint
              ?(Type)tu_of(r_sint,n)
              :(Type)tu_of(r_uint,n))
      });
    } break;
                             // type returning, arg is itself a type
    case builtin_ptrof:
    case builtin_volatile:
    case builtin_const: {
      let arg = temporary_interpret(env, s, tu_match_exp(*right, (astPBlock, pb, assertMessage(sentList_len(pb.stmts) == 1), pb.stmts[0]), (default, right), ));
      Type t = value_asType(arg);
      return scope_add_value(env, s, (typed_full){tu_of(c_type, {}), 
          id == builtin_const    ? ss_getType(env, (Type)tu_of(r_decor, {ss_getType(env, t), 1, 0})) :
          id == builtin_volatile ? ss_getType(env, (Type)tu_of(r_decor, {ss_getType(env, t), 0, 1})) :
           ss_getType(env, (Type)tu_of(r_decor, {ss_getType(env, (Type)tu_of(r_ptr, ss_getType(env, t))), true, false})) 
      });
    } break;
      // clang-format on
    case builtin_typeof: {
      let arg = temporary_interpret(env, s, tu_match_exp(*right, (astPBlock, pb, assertMessage(sentList_len(pb.stmts) == 1), pb.stmts[0]), (default, right), ));
      return scope_add_value(env, s, (typed_full){tu_of(c_type, {}), ss_getType(env, arg.t)});
    } break;
    case builtin_arrof: {
      let args = sentList_vla(tu_catch(astPBlock, *right).stmts);
      assertMessage(countof(*args) == 2);
      let narg = temporary_interpret(env, s, (*args)[0]);
      assert_type(c_int, narg.t);
      assertMessage(narg.v.c_int);
      let targ = temporary_interpret(env, s, (*args)[1]);
      Type t = value_asType(targ);
      usize count = narg.v.c_int[0];
      return scope_add_value(env, s, (typed_full){tu_of(c_type, {}), ss_getType(env, (Type)tu_of(r_arr, {ss_getType(env, t), count}))});
    } break;
      // clang-format on
      // control
      // clang-format off
    case builtin_break    :noargs(), scope_add_inst(env, s, (ir)tu_of(ir_break, {})); break;
    case builtin_continue :noargs(), scope_add_inst(env, s, (ir)tu_of(ir_continue, {})); break;
    case builtin_return   :noargs(), scope_add_inst(env, s, (ir)tu_of(ir_ret, lower(env, s, right))); break;
    // clang-format on
    case builtin_if: {
      let args = sentList_vla(tu_catch(astPBlock, *right).stmts);
      assertMessage(countof(*args) == 3);
      // clang-format off
      let ifv = (ir)tu_of(ir_if, {
          .cnd = lower(env, s, (*args)[0]),
          .t = scope_inferior(env, s),
          .f = scope_inferior(env, s),
      });
      // clang-format on
      assertMessage(tu_catch(ir_if, ifv).cnd);
      lower(env, tu_catch(ir_if, ifv).t, (*args)[1]);
      lower(env, tu_catch(ir_if, ifv).f, (*args)[2]);
      scope_add_inst(env, s, ifv);
    } break;
    case builtin_while: {
      let args = sentList_vla(tu_catch(astPBlock, *right).stmts);
      assertMessage(countof(*args) == 2);
      // clang-format off
      let whilev = (ir)tu_of(ir_while, {
          .cnd = lower(env, s, (*args)[0]),
          .t = scope_inferior(env, s),
      });
      // clang-format on
      assertMessage(tu_catch(ir_while, whilev).cnd);
      lower(env, tu_catch(ir_while, whilev).t, (*args)[1]);
      scope_add_inst(env, s, whilev);
    } break;
      // misc
    case builtin_qot: {
      // clang-format off
      astptr inner =full_unquote(env,s,tu_match_exp(
          *right,
          (astPBlock, pb, assertMessage(sentList_len(pb.stmts) == 1), pb.stmts[0]),
          (default, right),
      ));
      // clang-format on
      return scope_add_value(env, s, (typed_full){tu_of(c_ast, {}), inner});
    } break;
  }
  return 0;
}
u32 lower_call(superscope *env, scope *s, ast *left, ast *right);
u32 lower_op(superscope *env, scope *s, astOp op) {
  assertMessage(op.number_rep != opNumbers.call);
  let iop = (ir_op){
      .l = lower(env, s, op.left),
      .r = lower(env, s, op.right),
      .op = op.number_rep,
  };
  iop.d = scope_add_value(env, s, (typed_full){ir_op_type(env, s, iop)});
  scope_add_inst(env, s, (ir)tu_of(ir_op, iop));
  if (scope_find(env, s, iop.l)->v.value && scope_find(env, s, iop.l)->v.value)
    interpret_op(env, s, iop);
  return iop.d;
}
u32 lower(superscope *env, scope *s, astptr ast) {
  if (!ast) return 0;
  return tu_match_exp(
      *ast,
      (astEmptyStatement, _, (u32)0),
      (astId, id, ss_get_named(env, s, id)),
      (astString, str, ss_compileString(env, s, str)),
      (astNumber, num, ss_compileString(env, s, num)),
      (astDecl, decl, ({
         let t = decl.type
                     ? value_asType(temporary_interpret(env, s, decl.type))
                     : (Type)tu_of(c_any, {});
         u32 val = 0;
         if (decl.value) {
           if (tu_is(c_ast, t)) val = scope_add_value(env, s, (typed_full){tu_of(c_ast, {}), decl.value});
           else val = lower(env, s, decl.value); // TODO: block initialization
         } else val = scope_add_value(env, s, (typed_full){t});

         assertMessage(
             val &&
             (tu_is(c_any, t) ||
              !Type_cmp(scope_find(env, s, val)->t, t))
         );

         if (decl.name) {
           tu_match_void(
               *decl.name,
               (astId, id, scope_bind(env, s, val, id)),
               (default, {
                 let res = temporary_interpret(env, s, decl.name);
                 assert_type(c_ast, res.t);
                 assertMessage(res.v.c_ast);
                 assertMessage(tu_is(astId, *res.v.c_ast));
                 scope_bind(env, s, val, tu_catch(astId, *res.v.c_ast));
               }),
           );
         }
         if (decl.is_const) TODO();
         val;
       })),
      (astBBlock, ast_block, ({
         let block = scope_inferior(env, s);
         scope_add_inst(env, s, (ir)tu_of(ir_block, block));
         u32 last = 0;
         foreach (let it, vlap(sentList_vla(ast_block.stmts)))
           last = lower(env, block, it);
         last ?: scope_add_value(env, s, (typed_full){tu_of(r_void, {})});
       })),
      (astPBlock, ast_block, ({
         let vla = sentList_vla(ast_block.stmts);
         assertMessage(countof(*vla) == 1, "no tuples");
         let block = scope_inferior(env, s);
         scope_add_inst(env, s, (ir)tu_of(ir_block, block));
         lower(env, block, (*vla)[0]) // TODO: shouldnt be 0
             ?: scope_add_value(env, s, (typed_full){tu_of(r_void, {})});
       })),
      (astABlock, _, assertMessage(false, "lone [...]"), (u32)0),
      (astOp, op, ({
         op.number_rep != opNumbers.call
             ? lower_op(env, s, op)
             : lower_call(env, s, op.left, op.right);
       })),
  );
}

#include "common2.h"
#include "wheels/macros.h"
#include "wheels/mytypes.h"
#include "wheels/sList.h"
#include <stdcountof.h>

u32 lower_builtin(superscope *env, scope *s, builtin_id id, astptr right);
u32 lower_call(superscope *env, scope *s, astOp op);
u32 lower_op(superscope *env, scope *s, astOp op) {
  assertMessage(op.number_rep != opNumbers.call);
  let iop = (ir_op){
      .l = lower(env, s, op.left),
      .r = lower(env, s, op.right),
      .op = op.number_rep,
  };
  iop.d = scope_add_value(env, s, (typed_full){ir_op_type(env, s, iop)});
  msList_push(env->alloc, s->instructions, tu_of(ir_op, iop));
  TODO("comptime");
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
         let t = (Type)tu_of(c_any, {});
         if (decl.type) {
           let sn = scope_inferior(env, s);
           defer { scope_destroy(env, sn); };
           lower(env, sn, decl.type);
           t = value_asType(interpret(env, sn));
         }
         decl.is_const; // TODO: const
         u32 val = 0;
         if (decl.value) {
           if (tu_is(c_ast, t)) val = scope_add_value(env, s, (typed_full){tu_of(c_ast, {}), decl.value});
           else val = lower(env, s, decl.value);
         } else val = scope_add_value(env, s, (typed_full){t});

         assertMessage(
             val &&
             (tu_is(c_any, t) ||
              Type_cmp(scope_find(env, s, val)->t, t))
         );

         if (decl.name) {
           tu_match_void(
               *decl.name,
               (astId, id, scope_bind(env, s, val, id)),
               (default, {
                 let sn = scope_inferior(env, s);
                 defer { scope_destroy(env, sn); };
                 lower(env, sn, decl.name);
                 let res = interpret(env, sn);
                 assert_type(c_ast, res.t);
                 assertMessage(res.v.c_ast);
                 assertMessage(tu_is(astId, *res.v.c_ast));
                 scope_bind(env, s, val, tu_catch(astId, *res.v.c_ast));
               }),
           );
         }
         val;
       })),
      (astBBlock, ast_block, ({
         let block = scope_inferior(env, s);
         msList_push(env->alloc, s->instructions, (ir)tu_of(ir_block, block));
         u32 last = 0;
         foreach (let it, vlap(sentList_vla(ast_block.stmts)))
           last = lower(env, block, it);
         last ?: scope_add_value(env, s, (typed_full){tu_of(r_void, {})});
       })),
      (astPBlock, ast_block, ({
         let vla = sentList_vla(ast_block.stmts);
         assertMessage(countof(*vla) == 1, "no tuples");
         let block = scope_inferior(env, s);
         msList_push(env->alloc, s->instructions, (ir)tu_of(ir_block, block));
         lower(env, block, (*vla)[0])
             ?: scope_add_value(env, s, (typed_full){tu_of(r_void, {})});
       })),
      (astABlock, _, assertMessage(false, "lone [...]"), (u32)0),
      (astOp, op, ({
         op.number_rep != opNumbers.call
             ? lower_op(env, s, op)
             : lower_call(env, s, op);
       })),
  );
}

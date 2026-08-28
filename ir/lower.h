#include "common.h"
#include "lower_op.h"
#include "scope.h"
#include "wheels/macros.h"
#include "wheels/mytypes.h"
static u32 coerce_type(superscope *ss, scope *s, u32 left, Type to) {
  let ot = scope_find(ss, s, left)->t;
  TODO("handle operator overloading");
}
u32 lower(superscope *ss, scope *s, ast *a) {
  if (!a)
    return (println("[WARNING] encountered a null ast in lower?"), scope_add_value(ss, s, void_value));
  tu_match_exp(
      *a,
      (astEmptyStatement, _, scope_add_value(ss, s, void_value)),
      (astOp, op, lower_op(ss, s, lower(ss, s, op.left), op.right, op.number_rep)),
      (astId, id, ss_get_named(ss, s, id)),
      (astNumber, n, ss_compileInt(ss, s, n)),
      (astString, f, ss_compileString(ss, s, f)),
      (astPBlock, pb, ({
         if (countof(*sentList_vla(pb.stmts))) error_print(a, "no tuples");
         let sc = scope_inferior(ss, s);
         let l = lower(ss, sc, pb.stmts[0]);
         let res = scope_add_value(ss, s, *scope_find(ss, sc, l));
         scope_add_inst(ss, sc, (ir)tu_of(ir_rawop, {.d = res, .r = res, .l = l, .o = opNumbers.set}));
         res;
       })),
      (astBBlock, bb, ({
         let sc = scope_inferior(ss, s);
         u32 l = 0;
         foreach (let s, vlap(sentList_vla(bb.stmts)))
           l = lower(ss, sc, s);
         let res = scope_add_value(ss, s, *scope_find(ss, sc, l));
         scope_add_inst(ss, sc, (ir)tu_of(ir_set, {.d = res, .l = l}));
         res;
       })),
      (astABlock, _, (error_print(a, "lone []"), (u32)0)),
      (astDecl, dcl, ({
         let type = T(tu_of(c_any, {}));
         if (dcl.type) {
           let t = temporary_interpret(ss, s, dcl.type);
           type = value_asType_loc(t, ast_loc(*dcl.type));
         }
         u32 value = 0;
         if (dcl.value) {
           value = lower(ss, s, dcl.value);
         } else {
           assertMessage(dcl.type, "must have type or value");
           value = scope_add_value(ss, s, TV(type));
         }
         value = coerce_type(ss, s, value, type);
         if (dcl.is_const) {
           let tv = scope_find(ss, s, value);
           tv->t.isconst = true;
         }
         value;
       })),
  );
  error_print(a, "unhandled ast\n{ast}", a);
}

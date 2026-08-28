#pragma once
#include "common.h"
#include "lower_op.h"
#include "scope.h"
#include "wheels/macros.h"
#include "wheels/mytypes.h"
static u32 coerce_type(superscope *ss, scope *s, u32 left, Type to) {
  let ot = scope_find(ss, s, left)->t;
  println("coersing {Type} to {Type}", ot, to);
  if (!Type_cmp(ot, to)) return left;
  TODO("handle type coersion");
  TODO("handle operator overloading");
}
u32 lower_op_wrap(superscope *ss, scope *s, ast *left, ast *right, u32 number) {
  if (number == opNumbers.colon) {
    let colon = tu_catch(astOp, *left, error_print(left, "colon must precede call"));
    u32 self_val = lower(ss, s, colon.left);
    u32 fn_val = lower_op(ss, s, self_val, colon.right, opNumbers.dot);
    let al = builtin_arg_list(&right);
    let args = slice_alloc(ss->alloc, u32, al.len + 1);
    args.ptr[0] = self_val;
    foreach (let i, range(0, al.len))
      args.ptr[i + 1] = lower(ss, s, al.ptr[i]);
    return lower_call(ss, s, fn_val, args);
  } else return lower_op(ss, s, lower(ss, s, left), right, number);
}
u32 lower(superscope *ss, scope *s, ast *a) {
  if (!a) return (println("[WARNING] encountered a null ast in lower?"), scope_add_value(ss, s, void_value));
  return tu_match_exp(
      *a,
      (astEmptyStatement, _, scope_add_value(ss, s, void_value)),
      (astOp, op, lower_op_wrap(ss, s, op.left, op.right, op.number_rep)),
      (astId, id, ss_get_named(ss, s, id)),
      (astNumber, n, ss_compileInt(ss, s, n)),
      (astString, f, ss_compileString(ss, s, f)),
      (astPBlock, pb, ({
         if (countof(*sentList_vla(pb.stmts)) != 1) error_print(a, "no tuples");
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
}

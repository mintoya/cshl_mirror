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
  if (!Type_cmp(to, T(tu_of(c_any, {})))) {
    if (!ot.isref) return left; // ?
    to = ot;
    to.isref = false;
    to.isconst = ot.isconst;
    let d = scope_add_value(ss, s, TV(to, scope_find(ss, s, left)->v));
    scope_add_inst(ss, s, (ir)tu_of(ir_cast, {.l = left, .d = d}));
    return d;
  }
  let dest = scope_add_value(ss, s, TV(to));
  scope_add_inst(ss, s, (ir)tu_of(ir_cast, {.l = left, .d = dest}));
  println("coersing {Type} to {Type}", ot, to);
  return dest;
}
u32 lower_op_wrap(superscope *ss, scope *s, ast *left, ast *right, u32 number) {
  if (number == opNumbers.call && tu_or(astOp, *left, {}).number_rep == opNumbers.colon) {
    let colon = tu_catch(astOp, *left);
    let a = lower(ss, s, colon.left);
    let c = lower_op(ss, s, a, colon.right, opNumbers.dot);
    return lower_call_extra(ss, s, c, a, right);
  } else return lower_op(ss, s, lower(ss, s, left), right, number);
}
u32 lower(superscope *ss, scope *s, ast *a) {
  if (!a) return (println("[WARNING] null ast in lower"), scope_add_value(ss, s, void_value));
  let res = tu_match_exp(
      *a,
      (astEmptyStatement, _, scope_add_value(ss, s, void_value)),
      (astOp, op, lower_op_wrap(ss, s, op.left, op.right, op.number_rep)),
      (astId, id, ss_get_named(ss, s, id)),
      (astNumber, n, ss_compileInt(ss, s, n)),
      (astString, f, ss_compileString(ss, s, f)),
      (astPBlock, pb, ({
         if (countof(*sentList_vla(pb.stmts)) != 1)
           error_print(a, "no tuples");
         // (a ~= 1); -> {a ~= 1;}; //leak decls on pourpose
         // lower(ss, s, &(ast)tu_of(astBBlock, {.loc = pb.loc, .stmts = pb.stmts}));
         // or
         // (a ~= 1); -> a ~= 1;
         lower(ss, s, pb.stmts[0]);
       })),
      (astBBlock, bb, ({
         let res = scope_add_value(ss, s, TV(T(tu_of(c_any, {}))));
         let sc = scope_inferior(ss, s);
         scope_add_inst(ss, s, (ir)tu_of(ir_block, sc));
         foreach (let s, vlap(sentList_vla(bb.stmts)))
           lower(ss, sc, s);
         scope_yield(ss, sc, res);
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
           assertMessage(value);
         } else {
           assertMessage(dcl.type, "must have type or value");
           value = scope_add_value(ss, s, TV(type));
         }
         value = coerce_type(ss, s, value, type);

         if (dcl.is_const)
           scope_find(ss, s, value)->t.isconst = true;
         if (dcl.name) scope_bind(ss, s, value, id_from_ast(ss, s, dcl.name));
         value;
       })),
  );
  if (!res) error_print(a, "something is wrong {ast}", a);
  return res;
}

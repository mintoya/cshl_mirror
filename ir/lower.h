#pragma once
#include "common.h"
#include "lower_op.h"
#include "scope.h"
#include "wheels/macros.h"
#include "wheels/mytypes.h"
#include "wheels/print/print_pre.h"
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
u32 lower_op_wrap errs(superscope *ss, scope *s, ast *left, ast *right, u32 number) {
  if (number == opNumbers.call && tu_or(astOp, *left, {}).number_rep == opNumbers.colon) {
    let colon = tu_catch(astOp, *left);
    let a = try_err(lower, (ss, s, colon.left));
    let c = try_err(lower_op, (ss, s, a, colon.right, opNumbers.dot));
    return try_err(lower_call_extra, (ss, s, c, a, right));
  }
  if (number == opNumbers.set && tu_is(astId, *left)) {
    // assignment to a plain name: write through to the bound register, so
    // comptime vars actually get updated (ss_get_named would copy the value)
    let dst = ss_get_named_mut(ss, s, tu_catch(astId, *left));
    scope_add_inst(ss, s, (ir)tu_of(ir_set, {.d = dst, .l = try_err(lower, (ss, s, right))}));
    return dst;
  }
  return try_err(lower_op, (ss, s, try_err(lower, (ss, s, left)), right, number));
}
u32 lower errs(superscope *ss, scope *s, ast *a) {
  if (!a) return (println("[WARNING] null ast in lower"), scope_add_value(ss, s, void_value));
  let res = tu_match_exp(
      *a,
      (astEmptyStatement, _, scope_add_value(ss, s, void_value)),
      (astOp, op, try_err(lower_op_wrap, (ss, s, op.left, op.right, op.number_rep))),
      (astId, id, ss_get_named(ss, s, id)),
      (astNumber, n, ss_compileInt(ss, s, n)),
      (astString, f, ss_compileString(ss, s, f)),
      (astPBlock, pb, ({
         if (countof(*sentList_vla(pb.stmts)) != 1)
           return_err("TUPLE");
         // (a ~= 1); -> {a ~= 1;}; //leak decls on pourpose
         // lower(ss, s, &(ast)tu_of(astBBlock, {.loc = pb.loc, .stmts = pb.stmts}));
         // or
         // (a ~= 1); -> a ~= 1;
         try_err(lower, (ss, s, pb.stmts[0]));
       })),
      (astBBlock, bb, ({
         let res = scope_add_value(ss, s, TV(T(tu_of(c_any, {}))));
         let sc = scope_inferior(ss, s);
         scope_add_inst(ss, s, (ir)tu_of(ir_block, sc));
         foreach (let s, vlap(sentList_vla(bb.stmts)))
           try_err(lower, (ss, sc, s));
         scope_yield(ss, sc, res);
         res;
       })),
      (astABlock, _, (return_err("LONE_AB"), (u32)0)),
      (astDecl, dcl, ({
         let type = T(tu_of(c_any, {}));
         if (dcl.type) {
           let t = try_err(temporary_interpret, (ss, s, dcl.type));            type = try_err(value_asType, (ss, t));
         }
         u32 value = 0;
         if (dcl.value) {
           value = try_err(lower, (ss, s, dcl.value));
           assertMessage(value);
         } else {
           if (!dcl.type) return_err_extras("DECL_NO_TYPE_OR_VALUE", ss_errprint(ss, a, "must have type or value").ptr);
           value = scope_add_value(ss, s, TV(type));
         }
         value = coerce_type(ss, s, value, type);

         if (dcl.is_const)
           scope_find(ss, s, value)->t.isconst = true;
         if (dcl.name) {
           let nn = try_err(id_from_ast, (ss, s, dcl.name));
           scope_bind(ss, s, value, nn);
           println("[LOG:decl] bound {*:typed_value} to {slice(c8)}", scope_find(ss, s, value), nn);
         }
         value;
       })),
  );
  if (!res) return_err_extras("STATEMENT_NULL", ss_errprint(ss, a, "{ast} resulted in null somehow", a).ptr);
  return res;
}

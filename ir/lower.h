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
u32 lower_op_wrap errs(superscope *ss, scope *s, astidx left, astidx right, u32 number) {
  if (number == opNumbers.call && tu_or(astOp, ast2_at(&ss->asts, left), {}).number_rep == opNumbers.colon) {
    let left_node = ast2_at(&ss->asts, left);
    let colon = tu_catch(astOp, left_node);
    let a = try_err(lower, (ss, s, ast2_child(&ss->asts, colon.left_right)));
    let c = try_err(lower_op, (ss, s, a, ast2_child(&ss->asts, colon.left_right + 1), opNumbers.dot));
    return try_err(lower_call_extra, (ss, s, c, a, right));
  }
  if (number == opNumbers.set && tu_is(astId, ast2_at(&ss->asts, left))) {
    // assignment to a plain name: write through to the bound register, so
    // comptime vars actually get updated (ss_get_named would copy the value)
    let left_node = ast2_at(&ss->asts, left);
    let dst = ss_get_named_mut(ss, s, ast2_text(ss, tu_catch(astId, left_node)));
    scope_add_inst(ss, s, (ir)tu_of(ir_set, {.d = dst, .l = try_err(lower, (ss, s, right))}));
    return dst;
  }
  return try_err(lower_op, (ss, s, try_err(lower, (ss, s, left)), right, number));
}
u32 lower errs(superscope *ss, scope *s, astidx a) {
  if (!a) return (println("[WARNING] null ast in lower"), scope_add_value(ss, s, void_value));
  let res = tu_match_exp(
      ast2_at(&ss->asts, a),
      (astNull, _, (return_err("ENCOUNTERED_NULL_AST"), (u32)0)),
      (astEmpty, _, scope_add_value(ss, s, void_value)),
      (astOp, op, try_err(lower_op_wrap, (ss, s, ast2_child(&ss->asts, op.left_right), ast2_child(&ss->asts, op.left_right + 1), op.number_rep))),
      (astId, id, ss_get_named(ss, s, ast2_text(ss, id))),
      (astNumber, n, ss_compileInt(ss, s, ast2_text(ss, n))),
      (astString, f, ss_compileString(ss, s, ast2_text(ss, f))),
      (astPBlock, pb, ({
         let stmts = ast2_block_children(ss->alloc, &ss->asts, pb.start);
         if (stmts.len != 1)
           return_err("TUPLE");
         // (a ~= 1); -> {a ~= 1;}; //leak decls on pourpose
         // lower(ss, s, &(ast)tu_of(astBBlock, {.loc = pb.loc, .stmts = pb.stmts}));
         // or
         // (a ~= 1); -> a ~= 1;
         try_err(lower, (ss, s, stmts.ptr[0]));
       })),
      (astBBlock, bb, ({
         let res = scope_add_value(ss, s, TV(T(tu_of(c_any, {}))));
         let sc = scope_inferior(ss, s);
         scope_add_inst(ss, s, (ir)tu_of(ir_block, sc));
         let stmts = ast2_block_children(ss->alloc, &ss->asts, bb.start);
         foreach (let s, span(stmts.ptr, stmts.len))
           try_err(lower, (ss, sc, *s));
         scope_yield(ss, sc, res);
         res;
       })),
      (astABlock, _, (return_err("LONE_AB"), (u32)0)),
      (astDecl, dcl, ({
         let type = T(tu_of(c_any, {}));
         astidx name_idx = ast2_child(&ss->asts, dcl.name_type_value);
         astidx type_idx = ast2_child(&ss->asts, dcl.name_type_value + 1);
         astidx value_idx = ast2_child(&ss->asts, dcl.name_type_value + 2);
         if (type_idx) {
           let t = try_err(temporary_interpret, (ss, s, type_idx));
           type = try_err(value_asType, (ss, t));
         }
         u32 value = 0;
         if (value_idx) {
           value = try_err(lower, (ss, s, value_idx));
           assertMessage(value);
         } else {
           if (!type_idx) return_err_extras("DECL_NO_TYPE_OR_VALUE", ss_errprint(ss, a, "must have type or value").ptr);
           value = scope_add_value(ss, s, TV(type));
         }
         value = coerce_type(ss, s, value, type);

         if (dcl.isconst)
           scope_find(ss, s, value)->t.isconst = true;
         if (name_idx) {
           let nn = try_err(id_from_ast, (ss, s, name_idx));
           scope_bind(ss, s, value, nn);
           println("[LOG:decl] bound {*:typed_value} to {slice(c8)}", scope_find(ss, s, value), nn);
         }
         value;
       })),
  );
  if (!res) return_err_extras("STATEMENT_NULL", ss_errprint(ss, a, "{astidx_full} resulted in null somehow", ast2_full(ss, a)).ptr);
  return res;
}

#pragma once
#include "common.h"
#include "lower_op.h"
#include "scope.h"
#include "wheels/macros.h"
#include "wheels/mytypes.h"
#include "wheels/print/print_pre.h"
u32 lower_op_wrap errs(superscope *ss, scope *s, astidx left, astidx right, u32 number) {
  if (number == opNumbers.call && tu_or(astOp, ast_at(ss->asts, left), {}).number_rep == opNumbers.colon) {
    let left_node = ast_at(ss->asts, left);
    let colon = tu_catch(astOp, left_node);
    let a = try_err(lower, (ss, s, ast_child(ss->asts, colon.left_right)), 0);
    let c = try_err(lower_op, (ss, s, a, ast_child(ss->asts, colon.left_right + 1), opNumbers.dot), 0);
    return try_err(lower_call_extra, (ss, s, c, a, right), 0);
  }
  if (number == opNumbers.set && tu_is(astId, ast_at(ss->asts, left))) {
    let left_node = ast_at(ss->asts, left);
    let dst = try_err(ss_get_named_mut, (ss, s, ast_text(ss, tu_catch(astId, left_node))), 0);
    try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_set, {.d = dst, .l = try_err(lower, (ss, s, right), 0)})), 0);
    return dst;
  }
  return try_err(lower_op, (ss, s, try_err(lower, (ss, s, left), 0), right, number), 0);
}
u32 lower errs(superscope *ss, scope *s, astidx a) {
  if (!a) return (println("[WARNING] null ast in lower"), scope_add_value(ss, s, TV_void(ss)));
  let res = tu_match_exp(
      ast_at(ss->asts, a),
      (astNull, _, (return_err("ENCOUNTERED_NULL_AST", 0), (u32)0)),
      (astEmpty, _, scope_add_value(ss, s, TV_void(ss))),
      (astOp, op, try_err(lower_op_wrap, (ss, s, ast_child(ss->asts, op.left_right), ast_child(ss->asts, op.left_right + 1), op.number_rep), 0)),
      (astId, id, try_err(ss_get_named, (ss, s, ast_text(ss, id)), 0)),
      (astNumber, n, ss_compileInt(ss, s, ast_text(ss, n))),
      (astString, f, ss_compileString(ss, s, ast_text(ss, f))),
      (astPBlock, pb, ({
         let stmts = ast_block_children(ss->asts, a);
         if (stmts.len != 1)
           return_err("TUPLE", 0);
         // (a ~= 1); -> {a ~= 1;};
         // lower(ss, s, &(ast)tu_of(astBBlock, {.loc = pb.loc, .stmts = pb.stmts}));
         // or
         // (a ~= 1); -> a ~= 1;//leak decls on pourpose
         try_err(lower, (ss, s, stmts.start), 0);
       })),
      (astBBlock, bb, ({
         let res = scope_add_value(ss, s, TV_any(ss));
         let sc = scope_inferior(ss, s);
         try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_block, sc)), 0);
         let stmts = ast_block_children(ss->asts, a);
         foreach (let i, span(stmts.start, stmts.len))
           try_err(lower, (ss, sc, i), 0);
         try_err(scope_yield, (ss, sc, res), 0);
         res;
       })),
      (astABlock, _, (return_err("LONE_AB", 0), (u32)0)),
      (astDecl, dcl, ({
         let type = T(tu_of(c_any, {}));
         astidx name_idx = ast_child(ss->asts, dcl.name_type_value);
         astidx type_idx = ast_child(ss->asts, dcl.name_type_value + 1);
         astidx value_idx = ast_child(ss->asts, dcl.name_type_value + 2);
         if (type_idx) {
           let t = try_err(temporary_interpret, (ss, s, type_idx), 0);
           type = try_err(value_asType, (ss, t), 0);
         }
         u32 value = 0;
         if (value_idx) {
           value = try_err(lower, (ss, s, value_idx), 0);
           assertMessage(value);
         } else {
           if (!type_idx) return_err("DECL_NO_TYPE_OR_VALUE", 0);
           value = scope_add_value(ss, s, TV_of(ss, type));
         }
         value = try_err(cast_value, (ss, s, value, type), 0);

         if (dcl.isconst) {
           // types are values here, so a modifier change has to be re-interned
           let v = scope_find(ss, s, value);
           let vt = ss_type(ss, v->t);
           vt.isconst = true;
           v->t = ss_type_intern(ss, vt);
         }
         if (name_idx) {
           let nn = try_err(id_from_ast, (ss, s, name_idx), 0);
           try_err(scope_bind, (ss, s, value, nn), 0);
           println("[LOG:decl] bound {typed_value_full} to {slice(c8)}", ((typed_value_full){*scope_find(ss, s, value), ss_typeenv(ss)}), nn);
         }
         value;
       })),
  );
  res
      ? (void)({ return res; })
      : return_err("STATEMENT_NULL", 0);
  return res;
}

#pragma once
#include "common.h"
#include "lower_op.h"

u32 lower_op_wrap errs(superscope *ss, scope *s, astidx left, astidx right, u32 number) {
  if (number == opNumbers.call && tu_or(astOp, ast_at(ss, left), {}).number_rep == opNumbers.colon) {
    let left_node = ast_at(ss, left);
    let colon = tu_catch(astOp, left_node);
    let a = try_err(lower, (ss, s, ast_child(ss, colon.left_right)), 0);
    let c = try_err(lower_op, (ss, s, a, ast_child(ss, colon.left_right + 1), opNumbers.dot), 0);
    return try_err(lower_call_extra, (ss, s, c, a, right), 0);
  }
  if (number == opNumbers.set && tu_is(astId, ast_at(ss, left))) {
    let left_node = ast_at(ss, left);
    let dst = try_err(ss_get_named_mut, (ss, s, ast_text(ss, tu_catch(astId, left_node))), 0);
    try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_set, {.d = dst, .l = try_err(lower, (ss, s, right), 0)})), 0);
    return dst;
  }
  return try_err(lower_op, (ss, s, try_err(lower, (ss, s, left), 0), right, number), 0);
}
u32 lower_low errs(superscope *ss, scope *s, astidx a) {
  if (!a) return scope_add_value(ss, s, TV_void(ss));
  let res = tu_match_exp(
      ast_at(ss, a),
      (astNull, _, (return_err("ENCOUNTERED_NULL_AST", 0), (u32)0)),
      (astEmpty, _, scope_add_value(ss, s, TV_void(ss))),
      (astOp, op, try_err(lower_op_wrap, (ss, s, ast_child(ss, op.left_right), ast_child(ss, op.left_right + 1), op.number_rep), 0)),
      (astId, id, try_err(ss_get_named, (ss, s, ast_text(ss, id)), 0)),
      (astNumber, n, ss_compileInt(ss, s, ast_text(ss, n))),
      (astString, f, ss_compileString(ss, s, ast_text(ss, f))),
      (astPBlock, pb, ({
         let stmts = ast_block_children(ss, a);
         if (stmts.len != 1)
           return_err("TUPLE", 0);
         // (a ~= 1); -> a ~= 1;
         try_err(lower, (ss, s, stmts.start), 0);
       })),
      (astBBlock, bb, ({
         let res = scope_add_value(ss, s, TV_any(ss));
         try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_blocks, {})), 0);
         let stmts = ast_block_children(ss, a);
         u32 last = s->start + 1;
         foreach (let i, span(stmts.start, stmts.len))
           last = try_err(lower, (ss, s, i), 0);
         try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_set, {.d = res, .l = last})), 0);
         try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_blocke, {})), 0);
         res;
       })),
      (astABlock, _, return_err("LONE_AB", 0), (u32)0),
      (astDecl, dcl, ({
         astidx nameidx = ast_child(ss, dcl.name_type_value);
         astidx typeidx = ast_child(ss, dcl.name_type_value + 1);
         astidx valueidx = ast_child(ss, dcl.name_type_value + 2);
         let type = T(tu_of(c_any, {}));
         if (typeidx) {
           let t = try_err(temporary_interpret, (ss, s, typeidx), 0);
           type = try_err(value_asType, (ss, t), 0);
         }
         u32 value = 0;
         if (valueidx) {
           value = try_err(lower, (ss, s, valueidx), 0);
           assertMessage(value);
           if (!dcl.isconst && tag_set_member(ss, type).owner) {
             let slot = scope_add_value(ss, s, TV_of(ss, type));
             try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_set, {.l = value, .d = slot})), 0);
             value = slot;
           }
         } else {
           if (!typeidx) return_err("DECL_NO_TYPE_OR_VALUE", 0);
           value = scope_add_value(ss, s, TV_of(ss, type));
         }
         value = try_err(scope_cast_value, (ss, s, value, type), 0);

         if (dcl.isconst) {
           let v = scope_find(ss, s, value);
           let vt = ss_type(ss, v->t);
           vt.isconst = true;
           v->t = ss_type_intern(ss, vt);
         }
         if (nameidx) {
           let nn = try_err(id_from_ast, (ss, s, nameidx), 0);
           try_err(scope_bind, (ss, s, value, nn), 0);
         }
         value;
       })),
  );
  assertMessage(res);
  return res;
}
u32 lower errs(superscope *ss, scope *s, astidx a) {
  return s->result = catch_errcall(
             (lower_low, (ss, s, a)), e, ({
               let full = ((srcpos_full){a ? srcpos_ast(ast_at(ss, a), ss->asts) : 0, ss->plist, ss->flist});
               pass_err(error_loc_add(ss->alloc, e, full), 0);
               0;
             })
         );
}

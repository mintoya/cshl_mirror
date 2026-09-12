#pragma once
#include "common.h"
#include "lower_op.h"
#include "scope.h"
#include "wheels/macros.h"
#include "wheels/mytypes.h"
#include "wheels/print/print_pre.h"
// two types are the same once refs/qualifiers/comptime-ness are ignored
static bool Type_similar(Type a, Type b) {
  return a.ptr_level == b.ptr_level && !TypeTree_cmp(a.t, b.t);
}
static bool type_is_integer_like(Type t) {
  return !t.ptr_level && (tu_is(r_bool, t.t) || Type_isint(t));
}
static u32 lower_cast_overload errs(superscope *ss, scope *s, u32 left, Type to) { // TODO: move this somewhere else
  let ot = scope_find(ss, s, left)->t;
  if (tu_is(r_tag, ot.t)) {
    let tag = tu_catch(r_tag, ot.t);
    if_decl (let info, moxmap_get(ss->type_info, tag.name)) {
      if_decl (let item, moxmap_get((*info)->named, fp("@cast"))) {
        let method = scope_add_value(ss, s, *scope_find(ss, (*info), *item));
        let target = scope_add_value(ss, s, Type_asValue(ss, to));
        u32 vals[2] = {left, target};
        return try_err(lower_function_call_values, (ss, s, method, ((slice(u32)){2, vals})), 0);
      }
    }
  }
  return_err("CANNOT_CAST", 0);
}
static u32 coerce_type errs(superscope *ss, scope *s, u32 left, Type to) {
  let ot = scope_find(ss, s, left)->t;
  println("coersing {Type} to {Type}", ot, to);
  if (!Type_cmp(ot, to)) return left;

  // `any` absorbs anything, but a referance still needs to be loaded out
  if (!Type_cmp(to, T(tu_of(c_any, {})))) {
    if (!ot.isref) return left;
    let d = scope_add_value(ss, s, TV(P$(ot, ($.isref = false, $))));
    scope_add_inst(ss, s, (ir)tu_of(ir_set, {.l = left, .d = d}));
    return d;
  }
  if (!to.isref && Type_similar(ot, to)) {
    if (!ot.isref) return left;
    let d = scope_add_value(ss, s, TV(to));
    scope_add_inst(ss, s, (ir)tu_of(ir_set, {.l = left, .d = d}));
    return d;
  }
  // all integer cast allowed
  if (type_is_integer_like(ot) && type_is_integer_like(to)) {
    let dest = scope_add_value(ss, s, TV(to));
    scope_add_inst(ss, s, (ir)tu_of(ir_cast, {.l = left, .d = dest}));
    return dest;
  }
  return try_err(lower_cast_overload, (ss, s, left, to), 0);
}
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
    scope_add_inst(ss, s, (ir)tu_of(ir_set, {.d = dst, .l = try_err(lower, (ss, s, right), 0)}));
    return dst;
  }
  return try_err(lower_op, (ss, s, try_err(lower, (ss, s, left), 0), right, number), 0);
}
u32 lower errs(superscope *ss, scope *s, astidx a) {
  if (!a) return (println("[WARNING] null ast in lower"), scope_add_value(ss, s, void_value));
  let res = tu_match_exp(
      ast_at(ss->asts, a),
      (astNull, _, (return_err("ENCOUNTERED_NULL_AST", 0), (u32)0)),
      (astEmpty, _, scope_add_value(ss, s, void_value)),
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
         let res = scope_add_value(ss, s, TV(T(tu_of(c_any, {}))));
         let sc = scope_inferior(ss, s);
         scope_add_inst(ss, s, (ir)tu_of(ir_block, sc));
         let stmts = ast_block_children(ss->asts, a);
         foreach (let i, span(stmts.start, stmts.len))
           try_err(lower, (ss, sc, i), 0);
         scope_yield(ss, sc, res);
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
           value = scope_add_value(ss, s, TV(type));
         }
         value = try_err(coerce_type, (ss, s, value, type), 0);

         if (dcl.isconst)
           scope_find(ss, s, value)->t.isconst = true;
         if (name_idx) {
           let nn = try_err(id_from_ast, (ss, s, name_idx), 0);
           scope_bind(ss, s, value, nn);
           println("[LOG:decl] bound {*:typed_value} to {slice(c8)}", scope_find(ss, s, value), nn);
         }
         value;
       })),
  );
  res
      ? (void)({ return res; })
      : return_err("STATEMENT_NULL", 0);
  return res;
}

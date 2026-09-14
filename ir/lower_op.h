#pragma once
#include "common.h"
#include "function.h"
#include "ir.h"
#include "lower_builtins.h"
#include "scope.h"
#include "wheels/fptr.h"
#include "wheels/macros.h"
#include "wheels/mytypes.h"
#include "wheels/print/print_pre.h"
#include <string.h>
u32 lower_call_extra errs(superscope *ss, scope *s, u32 left, u32 first, astidx args) {
  let l = scope_find(ss, s, left);
  iflet_tu (let fn, c_fun, ss_type(ss, l->t).t)
    return try_err(lower_function_call_with, (ss, s, left, args, first), 0);
  println("calling {Type_full}", ss_tf(ss, ss_type(ss, l->t)));
  return_err("UNSUPPORTED_CALL", 0);
}
u32 lower_call errs(superscope *ss, scope *s, u32 left, astidx args) {
  let l = scope_find(ss, s, left);
  iflet_tu (let builtin, builtin, ss_type(ss, l->t).t)
    return try_err(lower_builtin, (ss, s, builtin, args), 0);
  iflet_tu (let fn, c_fun, ss_type(ss, l->t).t)
    return try_err(lower_function_call, (ss, s, left, args), 0);
  let alist = builtin_arg_list(ss->asts, args);
  slice(u32) vals = alist.len ? slice_alloc(ss->alloc, u32, alist.len) : (slice(u32)){};
  foreach (let i, range(0, alist.len))
    vals.ptr[i] = try_err(lower, (ss, s, alist.start + i), 0);
  let d = scope_add_value(ss, s, TV_any(ss));
  try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_call, {.l = left, .d = d, .args = vals})), 0);
  return d;
}
tu_def(
    (astLit, u8),
    (astIdLit, srcpos),
    (astStrLit, srcpos),
    (astNumLit, srcpos),
);
static inline fptr astLit_str(superscope *ss, astLit l) {
  return tu_match_exp(l, (astIdLit, l, ast_text(ss, l)), (astStrLit, l, ast_text(ss, l)), (astNumLit, l, ast_text(ss, l)));
}
u32 lower_overload_possible errs(superscope *ss, scope *s, u32 left, astidx right, u32 opNumber) {
  let t = ss_type(ss, scope_find(ss, s, left)->t);
  if (t.ptr_level > 1) return 0;
  let result = tu_match_exp(
      t.t,
      (default, (u32)0),
      (r_arr, arr, ({ // array index
                      // no bounds checking, because there is no pointer arithmatic
         if (opNumber != opNumbers.call) return_err("ARRAY_NO_OP", 0);
         let right_node = ast_at(ss->asts, right);
         (void)tu_catch(astABlock, right_node, return_err("ARRAY_NEEDS_ABLOCK", 0));
         let items = ast_block_children(ss->asts, right);
         if (items.len != 1) return_err("ARRAY_INDEX_ONE_ARG", 0);
         let r = try_err(lower, (ss, s, items.start), 0);
         let d = scope_add_value(ss, s, TV_any(ss));
         try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_array, {.d = d, .r = r, .l = left})), 0);
         d;
       })),
      (c_ast, _, ({
         u32 ovl = 0;
         if (opNumber == opNumbers.dot) {
           let self = scope_find(ss, s, left)->v.c_ast;
           let right_node = ast_at(ss->asts, right);
           iflet_tu (let m, astString, right_node) {
             let ms = ast_text(ss, m);
             astidx res = 0;
             if (self && tu_is(astOp, ast_at(ss->asts, self)) && ms.len >= 2 && ms.ptr[0] == '"' && ms.ptr[ms.len - 1] == '"') {
               let self_node = ast_at(ss->asts, self);
               let op = tu_catch(astOp, self_node);
               union {
                 u32 n;
                 char s[sizeof(u32)];
               } spelling = {};
               memcpy(spelling.s, ms.ptr + 1, ms.len - 2);
               if (op.number_rep == spelling.n) res = self;
             }
             ovl = scope_add_value(ss, s, TV_ast(ss, res ?: ast_make_empty(ss)));
           } else iflet_tu (let m, astId, right_node) {
             let ms = ast_text(ss, m);
             astidx res = 0;
             if (self)
               iflet_tu (let op, astOp, ast_at(ss->asts, self)) {
                 if (fptr_eq(ms, "opleft")) res = ast_child(ss->asts, op.left_right);
                 else if (fptr_eq(ms, "opright")) res = ast_child(ss->asts, op.left_right + 1);
               }
             if (!res && self)
               iflet_tu (let d, astDecl, ast_at(ss->asts, self)) {
                 if (fptr_eq(ms, "name")) res = ast_child(ss->asts, d.name_type_value);
                 else if (fptr_eq(ms, "type")) res = ast_child(ss->asts, d.name_type_value + 1);
                 else if (fptr_eq(ms, "value")) res = ast_child(ss->asts, d.name_type_value + 2);
               }
             if (!res && self)
               res = tu_match_exp(
                   ast_at(ss->asts, self),
                   (astOp, _, /*    */ fptr_eq(ms, "op") /*    */ ? self : (astidx)0),
                   (astId, _, /*    */ fptr_eq(ms, "id") /*    */ ? self : (astidx)0),
                   (astDecl, _, /*  */ fptr_eq(ms, "decl") /*  */ ? self : (astidx)0),
                   (astNumber, _, /**/ fptr_eq(ms, "number") /**/ ? self : (astidx)0),
                   (astString, _, /**/ fptr_eq(ms, "string") /**/ ? self : (astidx)0),
                   (astPBlock, _, /**/ fptr_eq(ms, "pblock") /**/ ? self : (astidx)0),
                   (astBBlock, _, /**/ fptr_eq(ms, "bblock") /**/ ? self : (astidx)0),
                   (astABlock, _, /**/ fptr_eq(ms, "ablock") /**/ ? self : (astidx)0),
                   (astEmpty, _, (astidx)0),
                   (astNull, _, (astidx)0),
               );
             ovl = scope_add_value(ss, s, TV_ast(ss, res ?: ast_make_empty(ss)));
           }
         } else if (opNumber == opNumbers.eq) {
           let self = scope_find(ss, s, left)->v.c_ast;
           let other = scope_find(ss, s, try_err(lower, (ss, s, right), 0));
           bool res = false;
           iflet_tu (let arr, r_arr, ss_type(ss, other->t).t) {
             if (self) {
               fptr word = tu_match_exp(
                   ast_at(ss->asts, self),
                   (astId, id, /*       */ ast_text(ss, id)),
                   (astNumber, num, /*  */ ast_text(ss, num)),
                   (astString, str, /*  */ ast_text(ss, str)),
                   (default, (fptr){})
               );
               if (tu_is(astString, ast_at(ss->asts, self)))
                 word = string_escape(ss->alloc, word);
               res = fptr_eq(word, ((fptr){arr.count, (u8 *)other->v.value}));
             }
           } else if (tu_is(c_ast, ss_type(ss, other->t).t))
             res = self == other->v.c_ast;
           ovl = scope_add_value(
               ss,
               s,
               TV_with(ss, T(tu_of(r_bool, {})), VS(avalue(ss->alloc, (bool)res)))
           );
         }
         ovl;
       })),
  );
  // if (!result) println("[LOG] failed overload {opNumber} for {Type}", opNumber, t);
  return result;
}

u32 lower_dot errs(superscope *ss, scope *s, u32 left, astidx right, astLit lit) {
  let l = scope_find(ss, s, left);
  let name = astLit_str(ss, lit);

  let lt = ss_type(ss, l->t);
  if (tu_is(c_type, lt.t)) {
    let target = try_err(value_asType, (ss, *l), 0);
    let m = ss_tag_named(ss, target.t, name);
    if (m.owner)
      return scope_add_value(ss, s, *scope_find(ss, m.owner, m.idx));
    return_err("NO_MEMBER", 0);
  }

  if (lt.ptr_level > 1) return_err("DOT_UNSUPPORTED", 0);

  let m = ss_tag_named(ss, lt.t, name);
  if (m.owner)
    return scope_add_value(ss, s, *scope_find(ss, m.owner, m.idx));

  let e = ss_typeenv(ss);
  let st = TypeTree_inner_struct(lt.t, e);
  let bf = TypeTree_inner_bitfield(lt.t, e);
  if (st.start) {
    foreach (let i, range(0, type_struct_len(e, st))) {
      let item = type_struct_item(e, st, i);
      if (!fptr_cmp(type_name(e, item->name), name)) {
        if (Type_is_comptime(item->t))
          return_err("NO_STORAGE", 0);
        let d = scope_add_value(ss, s, TV_of(ss, item->t));
        try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_field, {.l = left, .d = d, .i = (u32)i})), 0);
        return d;
      }
    }
  } else if (bf.start) {
    foreach (let i, range(0, type_bitf_len(e, bf))) {
      let item = type_bitf_item(e, bf, i);
      if (!fptr_cmp(type_name(e, item->name), name)) {
        Type rt = item->t;
        rt.isref = true;
        let d = scope_add_value(ss, s, TV_of(ss, rt));
        try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_field, {.l = left, .d = d, .i = (u32)i})), 0); // maybe it's own ir entry, if porting to c without bitfields
        return d;
      }
    }
  }

  return_err("NO_MEMBER", 0);
}
typedef enum {
  math_mul = enop_mul,
  math_div = enop_div,
  math_mod = enop_mod,
  math_add = enop_plus,
  math_sub = enop_minus,
  math_shr = enop_shr,
  math_shl = enop_shl,
  math_and = enop_oand,
  math_or = enop_oor,
  math_band = enop_oband,
  math_bor = enop_obor,
  math_mor = enop_mor,
  math_les = enop_les,
  math_moq = enop_moq,
  math_leq = enop_leq,
  math_neq = enop_neq,
  math_eq = enop_eq,
} math_operation;
u32 lower_math errs(superscope *ss, scope *s, u32 left, u32 right, math_operation op) {
  let opNumber = enum_as_op((enum_op)op);
  let l = scope_find(ss, s, left);
  let r = scope_find(ss, s, right);
  let d = scope_add_value(ss, s, TV_any(ss));
  try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_rawop, {.r = left, .l = right, .d = d, .o = opNumber})), 0);
  return d;
}
u32 lower_op errs(superscope *ss, scope *s, u32 left, astidx right, u32 opNumber) {
  // these are not overloadable
  // dot probably coming soon
  switch_tuple(
      op_as_enum(opNumber),
      (enop_pipe, {
        let result = scope_add_value(ss, s, TV_any(ss));

        let pipscope = scope_inferior(ss, s);
        try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_block, pipscope)), 0);
        try_err(scope_bind, (ss, pipscope, left, fp("_")), 0);
        try_err(lower, (ss, pipscope, right), 0);
        try_err(scope_yield, (ss, pipscope, result), 0);

        return result;
      }),
      (enop_colon, assertMessage(false, "unreachable")),
      (enop_iexpand, ({
         // a'b
         // ->
         // @uqot a @qot b
         let argument = scope_find(ss, s, try_err(lower_builtin_qot, (ss, s, right), 0));
         if (Type_cmp(ss_typeenv(ss), ss_type(ss, ss->t_ast), ss_type(ss, argument->t)))
           return_err("EXPECTED_AST", 0);
         let result = scope_find(ss, s, try_err(lower_call, (ss, s, left, argument->v.c_ast), 0));
         if (Type_cmp(ss_typeenv(ss), ss_type(ss, ss->t_ast), ss_type(ss, result->t)))
           return_err("EXPECTED_AST", 0);
         if (!result->v.c_ast) return_err("EXPECTED_AST", 0);
         return try_err(lower, (ss, s, result->v.c_ast), 0);
       })),
      (default, ),
  );
  // mask out so that copy constructor stuff can be applied later
  switch_tuple(
      op_as_enum(opNumber),
      (enop_set, {
        let src = try_err(lower, (ss, s, right), 0);
        try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_set, {.d = left, .l = src})), 0);
        return left;
      }),
      (default),
  );
  if_decl (let ovl, try_err(lower_overload_possible, (ss, s, left, right, opNumber), 0)) return ovl;
  let res = switch_exp(
      (op_as_enum(opNumber)),
      (enop_pipe /*   */, (assertMessage(false, "unreachable"), (u32)0)),
      (enop_colon /*  */, (assertMessage(false, "unreachable"), (u32)0)),
      (enop_set /*    */, (assertMessage(false, "unreachable"), (u32)0)),
      (enop_iexpand /**/, (assertMessage(false, "unreachable"), (u32)0)),
      (enop_as, ({
         let arg = try_err(lower, (ss, s, right), 0);
         let d = scope_add_value(ss, s, TV_any(ss));
         try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_rawop, {.r = left, .l = arg, .d = d, .o = opNumbers.as})), 0);
         d;
       })),
      (enop_call, try_err(lower_call, (ss, s, left, right), 0)),
      (enop_dot, ({
         P$(tu_match_exp(
                ast_at(ss->asts, right),
                (astId, id, (astLit)tu_of(astIdLit, id)),
                (astString, str, (astLit)tu_of(astStrLit, str)),
                (astNumber, num, (astLit)tu_of(astNumLit, num)),
                (default, (assertMessage(false, "non-overloaded dot only works on literals"), (astLit){}))
            ),
            try_err(lower_dot, (ss, s, left, right, $), 0));
       })),
      // operators are handled, so it should just be math now
      (enop_mul, /*     */ try_err(lower_math, (ss, s, left, try_err(lower, (ss, s, right), 0), math_mul), 0)),
      (enop_div, /*     */ try_err(lower_math, (ss, s, left, try_err(lower, (ss, s, right), 0), math_div), 0)),
      (enop_mod, /*     */ try_err(lower_math, (ss, s, left, try_err(lower, (ss, s, right), 0), math_mod), 0)),
      (enop_plus, /*    */ try_err(lower_math, (ss, s, left, try_err(lower, (ss, s, right), 0), math_add), 0)),
      (enop_minus, /*   */ try_err(lower_math, (ss, s, left, try_err(lower, (ss, s, right), 0), math_sub), 0)),
      (enop_shr, /*     */ try_err(lower_math, (ss, s, left, try_err(lower, (ss, s, right), 0), math_shr), 0)),
      (enop_shl, /*     */ try_err(lower_math, (ss, s, left, try_err(lower, (ss, s, right), 0), math_shl), 0)),
      (enop_oand, /*    */ try_err(lower_math, (ss, s, left, try_err(lower, (ss, s, right), 0), math_and), 0)),
      (enop_oor, /*     */ try_err(lower_math, (ss, s, left, try_err(lower, (ss, s, right), 0), math_or), 0)),
      (enop_oband, /*   */ try_err(lower_math, (ss, s, left, try_err(lower, (ss, s, right), 0), math_band), 0)),
      (enop_obor, /*    */ try_err(lower_math, (ss, s, left, try_err(lower, (ss, s, right), 0), math_bor), 0)),
      (enop_les, /*     */ try_err(lower_math, (ss, s, left, try_err(lower, (ss, s, right), 0), math_les), 0)),
      (enop_mor, /*     */ try_err(lower_math, (ss, s, left, try_err(lower, (ss, s, right), 0), math_mor), 0)),
      (enop_moq, /*     */ try_err(lower_math, (ss, s, left, try_err(lower, (ss, s, right), 0), math_moq), 0)),
      (enop_eq, /*      */ try_err(lower_math, (ss, s, left, try_err(lower, (ss, s, right), 0), math_eq), 0)),
      (enop_neq, /*     */ try_err(lower_math, (ss, s, left, try_err(lower, (ss, s, right), 0), math_neq), 0)),
      (enop_leq, /*     */ try_err(lower_math, (ss, s, left, try_err(lower, (ss, s, right), 0), math_leq), 0)),
  );
  if (!res) return_err("LOWER_FAILED", 0);
  return res;
}

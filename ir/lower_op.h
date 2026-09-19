#pragma once
#include "common.h"
#include "lower_builtins.h"

u32 lower_call_extra errs(superscope *ss, scope *s, u32 left, u32 first, astidx args) {
  let l = scope_find(ss, s, left);
  iflet_tu (let fn, c_fun, ss_type(ss, l->t).t)
    return try_err(lower_function_call, (ss, s, left, first, args), 0);
  // println("calling {Type_full}", ss_tf(ss, ss_type(ss, l->t)));
  return_err("UNSUPPORTED_CALL", 0);
}
u32 lower_call errs(superscope *ss, scope *s, u32 left, astidx args) {
  let l = scope_find(ss, s, left);
  iflet_tu (let builtin, builtin, ss_type(ss, l->t).t)
    return try_err(lower_builtin, (ss, s, builtin, args), 0);
  iflet_tu (let fn, c_fun, ss_type(ss, l->t).t)
    return try_err(lower_function_call, (ss, s, left, 0, args), 0);

  // ast to coerce to the right values
  let start = msList_len(s->values);
  let alist = builtin_arg_list(ss, args);
  foreach (let it, span(alist.start, alist.len))
    scope_add_value(ss, s, TV_ast(ss, it));
  let end = msList_len(s->values);

  let d = scope_add_value(ss, s, TV_any(ss));
  try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_call, {.l = left, .d = d, .r = start, .e = end})), 0);
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
         let right_node = ast_at(ss, right);
         (void)tu_catch(astABlock, right_node, return_err("ARRAY_NEEDS_ABLOCK", 0));
         let items = ast_block_children(ss, right);
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
           let right_node = ast_at(ss, right);
           iflet_tu (let m, astString, right_node) {
             let ms = ast_text(ss, m);
             astidx res = 0;
             if (self && tu_is(astOp, ast_at(ss, self)) && ms.len >= 2 && ms.ptr[0] == '"' && ms.ptr[ms.len - 1] == '"') {
               let self_node = ast_at(ss, self);
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
               iflet_tu (let op, astOp, ast_at(ss, self)) {
                 if (fptr_eq(ms, "opleft")) res = ast_child(ss, op.left_right);
                 else if (fptr_eq(ms, "opright")) res = ast_child(ss, op.left_right + 1);
               }
             if (!res && self)
               iflet_tu (let d, astDecl, ast_at(ss, self)) {
                 if (fptr_eq(ms, "name")) res = ast_child(ss, d.name_type_value);
                 else if (fptr_eq(ms, "type")) res = ast_child(ss, d.name_type_value + 1);
                 else if (fptr_eq(ms, "value")) res = ast_child(ss, d.name_type_value + 2);
               }
             if (!res && self)
               res = tu_match_exp(
                   ast_at(ss, self),
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
                   ast_at(ss, self),
                   (astId, id, /*       */ ast_text(ss, id)),
                   (astNumber, num, /*  */ ast_text(ss, num)),
                   (astString, str, /*  */ ast_text(ss, str)),
                   (default, (fptr){})
               );
               if (tu_is(astString, ast_at(ss, self)))
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
  let d = scope_add_value(ss, s, TV_any(ss));
  let r = scope_add_value(ss, s, TV_ast(ss, right));
  try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_rawop, {.l = left, .r = r, .d = d, .o = opNumbers.dot})), 0);
  return d;
}
u32 lower_math errs(superscope *ss, scope *s, u32 left, u32 right, u32 op) {
  let d = scope_add_value(ss, s, TV_any(ss));
  try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_rawop, {.l = left, .r = right, .d = d, .o = op})), 0);
  return d;
}
u32 lower_op errs(superscope *ss, scope *s, u32 left, astidx right, u32 opNumber) {
  // these are not overloadable
  // dot probably coming soon
  switch_tuple(
      op_as_enum(opNumber),
      (enop_pipe, {
        let result = scope_add_value(ss, s, TV_any(ss));
        try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_blocks, {})), 0);
        try_err(scope_bind, (ss, s, left, fp("_")), 0);
        try_err(lower, (ss, s, right), 0);
        try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_set, {.d = result, .l = scope_last_item(ss, s)})), 0);
        try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_blocke, {})), 0);
        return result;
      }),
      (enop_colon, assertMessage(false, "unreachable")),
      (enop_iexpand, ({
         // a'b ...
         // ->
         // @uqot a @qot b ...
         let argument = scope_find(ss, s, try_err(lower_builtin_qot, (ss, s, right), 0));
         if (Type_cmp(ss_typeenv(ss), ss_type(ss, ss->t_ast), ss_type(ss, argument->t)))
           return_err("EXPECTED_AST", 0);
         let result = scope_find(ss, s, try_err(lower_call, (ss, s, left, argument->v.c_ast), 0));
         if (Type_cmp(ss_typeenv(ss), ss_type(ss, ss->t_ast), ss_type(ss, result->t)))
           return_err("EXPECTED_AST", 0);
         if (!result->v.c_ast) return_err("EXPECTED_AST", 0);
         return try_err(lower, (ss, s, result->v.c_ast), 0);
       })),
      (default),
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
         try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_rawop, {.l = left, .r = arg, .d = d, .o = opNumbers.as})), 0);
         d;
       })),
      (enop_call, try_err(lower_call, (ss, s, left, right), 0)),
      (enop_dot, ({
         P$(tu_match_exp(
                ast_at(ss, right),
                (astId, id, (astLit)tu_of(astIdLit, id)),
                (astString, str, (astLit)tu_of(astStrLit, str)),
                (astNumber, num, (astLit)tu_of(astNumLit, num)),
                (default, (assertMessage(false, "non-overloaded dot only works on literals"), (astLit){}))
            ),
            try_err(lower_dot, (ss, s, left, right, $), 0));
       })),
      // operators are handled, so it should just be math now
      (enop_mul, /*  */ try_err(lower_math, (ss, s, left, try_err(lower, (ss, s, right), 0), opNumber), 0)),
      (enop_div, /*  */ try_err(lower_math, (ss, s, left, try_err(lower, (ss, s, right), 0), opNumber), 0)),
      (enop_mod, /*  */ try_err(lower_math, (ss, s, left, try_err(lower, (ss, s, right), 0), opNumber), 0)),
      (enop_plus, /* */ try_err(lower_math, (ss, s, left, try_err(lower, (ss, s, right), 0), opNumber), 0)),
      (enop_minus, /**/ try_err(lower_math, (ss, s, left, try_err(lower, (ss, s, right), 0), opNumber), 0)),
      (enop_shr, /*  */ try_err(lower_math, (ss, s, left, try_err(lower, (ss, s, right), 0), opNumber), 0)),
      (enop_shl, /*  */ try_err(lower_math, (ss, s, left, try_err(lower, (ss, s, right), 0), opNumber), 0)),
      (enop_oand, /* */ try_err(lower_math, (ss, s, left, try_err(lower, (ss, s, right), 0), opNumber), 0)),
      (enop_oor, /*  */ try_err(lower_math, (ss, s, left, try_err(lower, (ss, s, right), 0), opNumber), 0)),
      (enop_oband, /**/ try_err(lower_math, (ss, s, left, try_err(lower, (ss, s, right), 0), opNumber), 0)),
      (enop_obor, /* */ try_err(lower_math, (ss, s, left, try_err(lower, (ss, s, right), 0), opNumber), 0)),
      (enop_les, /*  */ try_err(lower_math, (ss, s, left, try_err(lower, (ss, s, right), 0), opNumber), 0)),
      (enop_mor, /*  */ try_err(lower_math, (ss, s, left, try_err(lower, (ss, s, right), 0), opNumber), 0)),
      (enop_moq, /*  */ try_err(lower_math, (ss, s, left, try_err(lower, (ss, s, right), 0), opNumber), 0)),
      (enop_eq, /*   */ try_err(lower_math, (ss, s, left, try_err(lower, (ss, s, right), 0), opNumber), 0)),
      (enop_neq, /*  */ try_err(lower_math, (ss, s, left, try_err(lower, (ss, s, right), 0), opNumber), 0)),
      (enop_leq, /*  */ try_err(lower_math, (ss, s, left, try_err(lower, (ss, s, right), 0), opNumber), 0)),
  );
  if (!res) return_err("LOWER_FAILED", 0);
  return res;
}

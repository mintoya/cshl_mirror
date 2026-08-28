#pragma once
#include "common.h"
#include "lower_builtins.h"
#include "scope.h"
#include "wheels/macros.h"
#include "wheels/mytypes.h"
#include "wheels/sList.h"
u32 lower_call_extra(superscope *ss, scope *s, u32 left, u32 first, ast *args) {
  TODO();
}
u32 lower_call(superscope *ss, scope *s, u32 left, ast *args) {
  iflet_tu (let builtin, builtin, scope_find(ss, s, left)->t.t)
    return lower_builtin(ss, s, builtin, args);
  TODO();
}
u32 lower_overload_possible(superscope *ss, scope *s, u32 left, ast *right, u32 opNumber) {
  let t = scope_find(ss, s, left)->t;
  if (t.ptr_level > 1) return 0;
  return tu_match_exp(
      t.t,
      (default, (u32)0),
      (r_struct, _, ({
         TODO("opwerator overloading");
         (u32)0;
       })),
  );
}

tu_def(
    (astLit, u8),
    (astIdLit, astId),
    (astStrLit, astString),
    (astNumLit, astNumber),
);
u32 lower_dot(superscope *ss, scope *s, u32 left, astLit) { TODO(); }
typedef enum {
  math_mul,
  math_div,
  math_mod,
  math_add,
  math_sub,
  math_shr,
  math_shl,

  math_and,
  math_or,
  math_band,
  math_bor,

  math_mor,
  math_les,
  math_moq,
  math_leq,
  math_neq,
  math_eq,
} math_operation;
u32 lower_math(superscope *ss, scope *s, u32 left, u32 right, math_operation) { TODO(); }
u32 lower_op(superscope *ss, scope *s, u32 left, ast *right, u32 opNumber) {
  // these are not overloadable
  // dot probably coming soon
  switch_tuple(
      op_as_enum(opNumber),
      (enop_pipe, {
        let result = scope_add_value(ss, s, TV_any());

        let pipscope = scope_inferior(ss, s);
        scope_add_inst(ss, s, (ir)tu_of(ir_block, pipscope));
        scope_bind(ss, pipscope, left, fp("_"));
        lower(ss, pipscope, right);
        scope_yield(ss, pipscope, result);

        return result;
      }),
      (enop_colon, assertMessage(false, "unreachable")),
      (default, ),
  );
  // mask out so that copy constructor stuff can be applied later
  switch_tuple(
      op_as_enum(opNumber),
      (enop_set, {
        TODO("should have it's own lower function, to make sure comptime values and const values and such work");
        scope_add_inst(ss, s, (ir)tu_of(ir_set, {.d = left, .l = lower(ss, s, right)}));
        return left;
      }),
      (default, ),
  );
  if_decl (let ovl, lower_overload_possible(ss, s, left, right, opNumber)) return ovl;
  return switch_exp(
      (op_as_enum(opNumber)),
      (enop_pipe, (assertMessage(false, "unreachable"), (u32)0)),
      (enop_colon, (assertMessage(false, "unreachable"), (u32)0)),
      (enop_set, (assertMessage(false, "unreachable"), (u32)0)),
      (enop_call, lower_call(ss, s, left, right)),
      (enop_iexpand, lower_call(ss, s, left, right)),
      (enop_dot, ({
         P$(tu_match_exp(
                *right,
                (astId, id, (astLit)tu_of(astIdLit, id)),
                (astString, str, (astLit)tu_of(astStrLit, str)),
                (astNumber, num, (astLit)tu_of(astNumLit, num)),
                (default, (assertMessage(false, "non-overloaded dot only works on literals"), (astLit){}))
            ),
            lower_dot(ss, s, left, $));
       })),
      // operators are handled, so it should just be math now
      (enop_mul, /*     */ lower_math(ss, s, left, lower(ss, s, right), math_mul)),
      (enop_div, /*     */ lower_math(ss, s, left, lower(ss, s, right), math_div)),
      (enop_mod, /*     */ lower_math(ss, s, left, lower(ss, s, right), math_mod)),
      (enop_plus, /*    */ lower_math(ss, s, left, lower(ss, s, right), math_add)),
      (enop_minus, /*   */ lower_math(ss, s, left, lower(ss, s, right), math_sub)),
      (enop_shr, /*     */ lower_math(ss, s, left, lower(ss, s, right), math_shr)),
      (enop_shl, /*     */ lower_math(ss, s, left, lower(ss, s, right), math_shl)),
      (enop_oand, /*    */ lower_math(ss, s, left, lower(ss, s, right), math_and)),
      (enop_oor, /*     */ lower_math(ss, s, left, lower(ss, s, right), math_or)),
      (enop_oband, /*   */ lower_math(ss, s, left, lower(ss, s, right), math_band)),
      (enop_obor, /*    */ lower_math(ss, s, left, lower(ss, s, right), math_bor)),
      (enop_les, /*     */ lower_math(ss, s, left, lower(ss, s, right), math_les)),
      (enop_mor, /*     */ lower_math(ss, s, left, lower(ss, s, right), math_mor)),
      (enop_moq, /*     */ lower_math(ss, s, left, lower(ss, s, right), math_moq)),
      (enop_eq, /*      */ lower_math(ss, s, left, lower(ss, s, right), math_eq)),
      (enop_neq, /*     */ lower_math(ss, s, left, lower(ss, s, right), math_neq)),
      (enop_leq, /*     */ lower_math(ss, s, left, lower(ss, s, right), math_leq)),
  );
}

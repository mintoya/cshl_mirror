#pragma once
#include "common.h"
#include "lower_builtins.h"
#include "scope.h"
#include "wheels/macros.h"
#include "wheels/mytypes.h"
#include "wheels/sList.h"
u32 lower_call_extra(superscope *ss, scope *s, u32 left, u32 first, ast *args){
  TODO();
}
u32 lower_call(superscope *ss, scope *s, u32 left, ast *args) {
  iflet_tu (let builtin, builtin, scope_find(ss, s, left)->t.t)
    return lower_builtin(ss, s, builtin, args);
  TODO();
}
static u32 lower_overload_possible(superscope *ss, scope *s, u32 left, ast *right, u32 opNumber) {
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
u32 lower_op(superscope *ss, scope *s, u32 left, ast *right, u32 opNumber) {
  // these are not overloadable
  // dot probably coming soon lol
  switch_tuple(
      op_as_enum(opNumber),
      (enop_pipe, ({
         let result = scope_add_value(ss, s, TV_any());
         let pipscope = scope_inferior(ss, s);
         scope_add_inst(ss, s, (ir)tu_of(ir_block, pipscope));
         scope_bind(ss, pipscope, left, fp("_"));
         lower(ss, pipscope, right);
         scope_yield(ss, s, result);
         return result;
       })),
      (enop_colon, assertMessage(false, "unreachable")),
      (default, ),
  );
  if_decl (let ovl, lower_overload_possible(ss, s, left, right, opNumber)) return ovl;
  return switch_exp(
      (op_as_enum(opNumber)),
      (enop_call, lower_call(ss, s, left, right)),
      (enop_pipe, (assertMessage(false, "unreachable"), (u32)0)),
      (enop_colon, (assertMessage(false, "unreachable"), (u32)0)),
  );
}

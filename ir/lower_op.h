#pragma once
#include "common.h"
#include "lower_builtins.h"

u32 lower_call_extra errs(superscope *ss, scope *s, u32 left, u32 first, astidx args) {
  let l = scope_find(ss, s, left);
  iflet_tu (let fn, c_fun, ss_type(ss, l->t).t)
    return try_err(lower_function_call, (ss, s, left, first, args), 0);
  return_err("UNSUPPORTED_CALL", 0);
}
u32 lower_call errs(superscope *ss, scope *s, u32 left, astidx args) {
  let l = scope_find(ss, s, left);
  iflet_tu (let builtin, builtin, ss_type(ss, l->t).t)
    return try_err(lower_builtin, (ss, s, builtin, args), 0);
  iflet_tu (let fn, c_fun, ss_type(ss, l->t).t)
    return try_err(lower_function_call, (ss, s, left, 0, args), 0);

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

u32 lower_dot errs(superscope *ss, scope *s, u32 left, astidx right) {
  let r = scope_add_value(ss, s, TV_ast(ss, right));
  let d = scope_add_value(ss, s, TV_any(ss));
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
  switch_tuple(
      op_as_enum(opNumber),
      (enop_pipe, {
        let result = scope_add_value(ss, s, TV_any(ss));
        try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_blocks, {})), 0);
        try_err(scope_bind, (ss, s, left, fp("_")), 0);
        let value = try_err(lower, (ss, s, right), 0);
        try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_set, {.d = result, .l = value})), 0);
        try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_blocke, {})), 0);
        return result;
      }),
      (enop_colon, assertMessage(false, "unreachable")),
      (enop_set, {
        let src = try_err(lower, (ss, s, right), 0);
        try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_set, {.d = left, .l = src})), 0);
        return left;
      }),
      (default),
  );
  let res = switch_exp(
      (op_as_enum(opNumber)),
      (enop_as, ({
         let arg = try_err(lower, (ss, s, right), 0);
         let d = scope_add_value(ss, s, TV_any(ss));
         try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_rawop, {.l = left, .r = arg, .d = d, .o = opNumbers.as})), 0);
         d;
       })),
      (enop_call, try_err(lower_call, (ss, s, left, right), 0)),
      (enop_dot, try_err(lower_dot, (ss, s, left, right), 0)),
      (default, ({
         assertMessage(enum_op_is_math(op_as_enum(opNumber)), "unhandled operator");
         try_err(lower_math, (ss, s, left, try_err(lower, (ss, s, right), 0), opNumber), 0);
       })),
  );
  assertMessage(
      res,
      "should be non-zero , %s",
      snprint(
          stdAlloc,
          "\n\t{Type_full}\n"
          "\t{u32}\n"
          "\t{astidx_full}\n",
          ss_typeF(ss, ss_type(ss, scope_find(ss, s, left)->t)),
          op_as_enum(opNumber),
          ((astidx_full){
              .alist = ss->asts,
              .flist = ss->flist,
              .plist = ss->plist,
              .idx = right
          })
      )
          .ptr
  );
  return res;
}

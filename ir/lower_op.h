#include "common.h"
#include "lower_builtins.h"
#include "scope.h"
#include "wheels/macros.h"
#include "wheels/mytypes.h"
#include "wheels/sList.h"
u32 lower_call(superscope *ss, scope *s, u32 left, slice(u32) args);
u32 lower_op(superscope *ss, scope *s, u32 left, ast *right, u32 opNumber) {
  return switch_exp(
      op_as_enum(opNumber),
      (enop_pipe, ({
         let pipscope = scope_inferior(ss, s);
         scope_bind(ss, pipscope, left, fp("_"));
         lower(ss, pipscope, right);
         assertMessage(msList_len(pipscope->values), "scope must have some value");
         u32 result = scope_add_value(ss, s, msList_last(pipscope->values));
         scope_add_inst(ss, s, (ir)tu_of(ir_block, pipscope));
         scope_add_inst(ss, pipscope, (ir)tu_of(ir_set, {.d = result, .l = pipscope->start + msList_len(pipscope->values) + 1}));
         result;
       })),
      (enop_call, ({
         let al = builtin_arg_list(&right);
         let args = slice_alloc(ss->alloc, u32, al.len);
         foreach (let i, range(0, al.len))
           args.ptr[i] = lower(ss, s, al.ptr[i]);
         lower_call(ss, s, left, args);
       })),
  );
}

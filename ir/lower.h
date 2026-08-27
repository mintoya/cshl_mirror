#include "common.h"
#include "lower_builtins.h"
#include "scope.h"
#include "wheels/macros.h"
u32 lower(superscope *ss, scope *s, ast *a) {
  if (!a)
    return (println("[WARNING] encountered a null ast in lower?"), scope_add_value(ss, s, void_value));
  tu_match_exp(
      *a,
      (astEmptyStatement, _, scope_add_value(ss, s, void_value)),
      (astId, id, ss_get_named(ss, s, id)),
      (astNumber, n, ss_compileInt(ss, s, n)),
      (astString, f, ss_compileString(ss, s, f)),
      (astPBlock, pb, ({
         let args = builtin_arg_list(&a);
         if (args.len != 1) error_print(a, "no tuples");
         let sc = scope_inferior(ss, s);
         let l = lower(ss, sc, args.ptr[0]);
         let res = scope_add_value(ss, s, *scope_find(ss, sc, l));
         scope_add_inst(ss, sc, (ir)tu_of(ir_rawop, {.d = res, .r = res, .l = l, .o = opNumbers.set}));
         res;
       })),
      (astBBlock, bb, ({
         let sc = scope_inferior(ss, s);
         u32 l = 0;
         foreach (let s, vlap(sentList_vla(bb.stmts)))
           l = lower(ss, sc, s);
         let res = scope_add_value(ss, s, *scope_find(ss, sc, l));
         scope_add_inst(ss, sc, (ir)tu_of(ir_rawop, {.d = res, .r = res, .l = l, .o = opNumbers.set}));
         res;
       })),
      (astABlock, _, (error_print(a, "lone []"), (u32)0))
  );
  error_print(a, "unhandled ast\n{ast}", a);
}

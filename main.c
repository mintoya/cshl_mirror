#include <string.h>
#include <wheels/cmdline_parser.h>
#include <wheels/macros.h>
#include <wheels/smap.h>

#include "ast.h"
#include "indentprinter.h"
#include "wheels/fptr.h"
#include <wheels/allocators/arenaAllocator.h>
#include <wheels/allocators/debugallocator.h>

#include "cout.h"
#include "ir/interpret.h"
#include "ir/lower.h"

astidx_full parse_source errs(allocfn out_allocator, fptr input, const char *filename);

// { util
void on_allocation(void *, usize in, usize out, void *, const char *file, uint line) {
}
// }
cmd_main(
    int nargs,
    char *args[nargs],
    (bool, help /*      */, ("-h", "--help") /*  */, "print help message" /*      */, false),
    (bool, lowerast /*  */, ("-l", "--lower") /* */, "lower the ast" /*           */, false),
    (bool, execute /*   */, ("-r", "--run") /*   */, "interpret lowered ast" /*   */, false),
    (bool, c_output /*  */, ("-C", "--cout") /*  */, "c output" /*                */, false),
    (bool, echo_ast /*  */, ("--log-ast") /*     */, "log the ast" /*             */, false),
    (bool, echo_file /* */, ("--log-file") /*    */, "echo read file" /*          */, false),
) {
  let allocator = debugAllocator(.allocator = stdAlloc, .onalloc = on_allocation);
  defer {
    println("(log) stats : {dbga_stats}", debugAllocator_stats(allocator));
    debugAllocatorDeInit(allocator);
  };

  char *progname = nargs ? *args : "cshl";

  if (help)
    return cmd_usage(progname), 0;
  else if (nargs != 2)
    return println("no input file provided"), cmd_usage(progname), 1;

  fptr buffer = {};
  defer {
    if (buffer.len)
      slice_free(allocator, buffer);
  };
  if (echo_file) {
    buffer = read_file(allocator, args[1]);
    println("file : \n{slice(c8)}\n", buffer);
  }
  println("(log) after fread : {dbga_stats}", debugAllocator_stats(allocator));

  // parse_source reads the file itself when given a filename, so pass exactly one source
  let ctx = catch_errcall((parse_source, (allocator, (fptr){}, args[1])), e, (err_panic(e), (astidx_full){}));
  println("(log) after ast : {dbga_stats}", debugAllocator_stats(allocator));

  if (echo_ast)
    print_wfO(sexplike, nullptr, "{astidx_full}\n", ctx);

  if (lowerast || execute || c_output) {
    let env = ss_init(allocator, ctx);
    catch_errcall((lower, (env, env->outermost, ctx.idx)), e, (err_panic(e), (u32)0));
    scope_finish(env, env->outermost);
    if (lowerast || execute)
      print_wf(sexplike, "{scope_full}\n", ((scope_full){env->outermost, env}));
    if (c_output) {
      let out = fopen(strcat(strcat((char[255]){}, args[1]), ".c"), "w");
      assertMessage(out);
      defer { assertMessage(fclose(out) == 0, "failed to close C output"); };

      printerfunction_context pctx = {.put = fileprint, .arb = out, .allocator = allocator};
      // makeTypes(env, fileprint, pctx);
      // makeFuncs(env, fileprint, pctx);
      // makeScope(env, env->outermost, fileprint, pctx);
    }
    if (execute) {
      let res = catch_errcall((interpret, (env, env->outermost)), e, (err_panic(e), (typed_value){}));
      print_wf(sexplike, "result: {typed_value_full}\n", ((typed_value_full){res, ss_typeenv(env)}));
      // print_wf(sexplike, "after interpret\n{scope_full}\n", ((scope_full){env->outermost, env}));
    }
  }

  return 0;
}
#include <wheels/wheels.h>

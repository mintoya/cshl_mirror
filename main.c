#include <wheels/cmdline_parser.h>
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
    (bool, no_debug_alloc /* */, ("--no-debug-alloc") /* */, "skip the debug allocator (much faster, no leak report)" /* */, false),
) {
  // the debug allocator hashes every allocation it hands out, so it is worth
  // turning off when the leak report is not what is being measured
  bool dbga = !no_debug_alloc;
  allocfn allocator = stdAlloc;
  if (dbga) allocator = debugAllocator(.allocator = stdAlloc, .onalloc = on_allocation);
  defer {
    if (dbga) {
      println("(log) stats : {dbga_stats}", debugAllocator_stats(allocator));
      debugAllocatorDeInit(allocator);
    }
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
  if (dbga) println("(log) after fread : {dbga_stats}", debugAllocator_stats(allocator));

  // parse_source reads the file itself when given a filename, so pass exactly one source
  let ctx = catch_errcall((parse_source, (allocator, (fptr){}, args[1])), e, (err_panic(e), (astidx_full){}));
  if (dbga) println("(log) after ast : {dbga_stats}", debugAllocator_stats(allocator));

  if (echo_ast)
    print_wfO(sexplike, nullptr, "{astidx_full}\n", ctx);

  if (lowerast || execute || c_output) {
    let env = ss_init(allocator, ctx);
    defer { ss_deInit(env); };
    catch_errcall((lower, (env, env->outermost, ctx.idx)), e, (err_panic(e), (u32)0));
    if (lowerast || execute)
      print_wf(sexplike, "{scope_full}\n", ((scope_full){env->outermost, env}));
    if (c_output) {
      let name = snprint(allocator, "{cstr}.c{c8}", args[1], (c8)0);
      defer { slice_free(allocator, name); };
      let out = fopen(name.ptr, "w");
      assertMessage(out);
      defer { assertMessage(fclose(out) == 0, "failed to close C output"); };

      P$(((printerfunction_context){.put = fileprint, .arb = out, .allocator = allocator}),
         (makeC(env, $), 0));
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

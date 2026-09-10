#include <stdlib.h>
#include <wheels/cmdline_parser.h>
#include <wheels/macros.h>
#include <wheels/smap.h>

#include "ast.h"
#include "indentprinter.h"
#include "wheels/fptr.h"
#include "wheels/print/print_pre.h"
#include <wheels/allocators/arenaAllocator.h>
#include <wheels/allocators/debugallocator.h>
#include <wheels/mytypes.h>
//
#include "ir/interpret.h"
#include "ir/lower.h"

// { util
fptr *filectx = nullptr;
__attribute__((noreturn)) void(error_at)(u8 *location, fptr message) {
  assertMessage(filectx);
  usize loc = (u8 *)location - filectx->ptr;
  usize line = 0;
  usize col = 0;
  if (loc <= filectx->len) {
    usize i = 0, k = 0;
    for (; k < loc; k++) {
      if (filectx[0].ptr[k] == '\n') {
        line++;
        i = 0;
        continue;
      }
      i++;
    }
    col = i;
  }
  println("error : {slice(c8)} , at line {} column {}", message, line + 1, col + 1);
  exit(1);
}
void on_allocation(void *, usize in, usize out, void *, const char *file, uint line) {
  // println("(alloc) {} -> {},{cstr}:{}", in, out, file, line);
}
// }
cmd_main(
    int nargs,
    char **args,
    (bool, help /*      */, ("-h", "--help") /**/, "print help message" /*  */, false),
    (bool, execute /*   */, ("-r", "--run") /* */, "interpret the ast" /*   */, false),

    (bool, echo_ast /*  */, ("--log-ast") /*   */, "log the ast" /*         */, false),
    (bool, echo_file /* */, ("--log-file") /*  */, "echo read file" /*      */, false),
) {
  let allocator = debugAllocator(.allocator = stdAlloc, .onalloc = on_allocation);
  defer {
    println("(log) stats : {dbga_stats}", debugAllocator_stats(allocator));
    debugAllocatorDeInit(allocator);
  };

  char *progname = nargs ? *args : "cshl";

  if (help) {
    cmd_usage(progname);
    return 0;
  } else if (nargs < 2) {
    println("no input file provided");
    cmd_usage(progname);
    return 1;
  }

  fptr buffer = {};
  defer {
    if (buffer.len && buffer.ptr)
      slice_free(allocator, buffer);
  };

  buffer = readFileAlloc(allocator, args[1]);
  filectx = &buffer;
  if (echo_file)
    println("file : \n{slice(c8)}\n", buffer);
  println("(log) after fread : {dbga_stats}", debugAllocator_stats(allocator));

  let ast = catch_errcall((parse_source, (allocator, buffer)), (e, _), (err_panic(e), nullptr));
  println("(log) after ast : {dbga_stats}", debugAllocator_stats(allocator));

  if (echo_ast)
    print_wfO(sexplike, nullptr, "{ast}\n", ast);

  if (execute) {
    let env = ss_init(allocator);
    env->file = buffer;
    catch_errcall((lower, (env, env->outermost, ast)), (e, _), (err_panic(e), (u32)0));
    print_wf(sexplike, "{scope}\n", *env->outermost);
    // interpret(env,env->outermost);
    // print_wf(sexplike, "{scope}\n", *env->outermost);
  }

  return 0;
}
#include <wheels/wheels.h>

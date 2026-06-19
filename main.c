#include "int.h"
#include <stdlib.h>
#include <wheels/allocators/arenaAllocator.h>
#include <wheels/allocators/debugallocator.h>
#include <wheels/allocators/fbaAllocator.h>
#include <wheels/cmdline_parser.h>
#include <wheels/macros.h>
#include <wheels/mytypes.h>
#include <wheels/print.h>
#include <wheels/shmap.h>
#include <wheels/tu_macros.h>

#include "ast.h"
#include "tok1.h"
#include "tok2.h"
#include "wheels/sList.h"

fptr *filectx = nullptr;
__attribute__((noreturn)) void error_at(u8 *location, slice(c8) message) {
  assertMessage(filectx);
  usize loc = (u8 *)location - filectx->ptr;
  assertMessage(loc <= filectx->len);
  usize line = 0;
  usize col = 0;
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
  println("error : {} , at line {} column {}", message, line + 1, col + 1);
  exit(1);
}

void on_allocation(allocationType *t) {
  println("allocation from {} line {} , {}->{}", t->trace.fn, t->trace.ln, t->insize, t->outsize);
}
cmd_main(
    int nargs,
    char **args,
    (bool, help, ("-h", "--help"), "print help message", false),
    (bool, log_alloc, ("--log-allocations"), "print help message", false),
    (bool, echo_file, ("--echo-file"), "echo read file", false),
    (bool, log_tokens, ("--log-tokens"), "emmit list of tokens", false)
) {
  char *progname = nargs ? *args : "cshl";

  if (help) {
    cmd_usage(progname);
    return 0;
  } else if (nargs < 2) {
    println("no input file provided");
    cmd_usage(progname);
    return 1;
  }
  var_ s = fba_new(stdAlloc, 1024 * 1024);
  defer { fba_del(stdAlloc, s); };

  AllocatorV allocator =
      log_alloc
          ? debugAllocator(
                    .allocator = s,
                    .log = stdout,
                    .on_call = on_allocation,
            )
          : s;
  defer {
    if (log_alloc) debugAllocatorDeInit(allocator);
  };

  fptr buffer = {};
  defer {
    if (buffer.len && buffer.ptr)
      slice_free(allocator, buffer);
  };

  buffer = readFileAlloc(allocator, args[1]);
  filectx = &buffer;
  if (echo_file)
    println("file : \n{slice(c8)}\n", buffer);

  var_ tokens = tokenize(allocator, buffer);
  defer { msList_deInit(allocator, tokens); };
  if (log_tokens)
    foreach (var_ token, vla(*msList_vla(tokens)))
      println(" {Token} ", token);

  AllocatorV arena = arena_new_ext(allocator, 48 * 255);
  defer { arena_cleanup(arena); };
  var_ ast = parseBBlock(arena, (slice(Token)){.len = msList_len(tokens), tokens});
  println("{ast}", ast);
  println("current place : {}", FBA_current(s));

  bool r;
  var_ st = state_init(s);
  eval(REF(st), nullptr, ast[0], &r);
  return 0;
}

#include <wheels/wheels.h>

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

var_ cmd_options = sentList(
    cmdline_option,
    {"--help" /*      */, CMDARG_BOOLEAN, "print help message"},
    {"-h" /*          */, CMDARG_BOOLEAN, "print help message"},
    {"--log-allocations", CMDARG_BOOLEAN, "enable allocation logging"},
    {"--echo-file" /* */, CMDARG_BOOLEAN, "echo read file"},
    {"--log-tokens" /* */, CMDARG_BOOLEAN, "emmit list of tokens"}
);
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
CMDLINE_GENERATE_MAIN(cmd_options);

void on_allocation(allocationType *t) {
  println("allocation from {} line {} , {}->{}", t->trace.fn, t->trace.ln, t->insize, t->outsize);
}
int cmdline_main(cmdline_args args) {

  msList(char *) positionals = cmdLine_positionals(args);

  var_ help =
      cmdline_get(args, "--help", bool) ?: cmdline_get(args, "-h", bool);
  if (help && *help) {
    cmdLine_printUsage("cshl", cmd_options);
    return 0;
  } else if (msList_len(positionals) < 2) {
    println("no input file provided");
    cmdLine_printUsage("cshl", cmd_options);
    return 1;
  }
  var_ log_alloc = cmdline_get(args, "--log-allocations", bool);
  var_ echo_file = cmdline_get(args, "--echo-file", bool);
  var_ list_tokens = cmdline_get(args, "--log-tokens", bool);

  FBA_State s;
  var_ fbuffer = &aCreate(stdAlloc, u8, 1024 * 1024);
  defer { aFree(stdAlloc, *fbuffer, sizeof(*fbuffer)); };
  FBA_init(*fbuffer, sizeof(*fbuffer), &s);

  AllocatorV allocator =
      (log_alloc && *log_alloc)
          ? debugAllocator(
                    .allocator = s.allocator,
                    .log = stdout,
                    .on_call = on_allocation,
            )
          : s.allocator;
  defer {
    if (log_alloc && *log_alloc) debugAllocatorDeInit(allocator);
  };

  fptr buffer = {};
  defer {
    if (buffer.len && buffer.ptr)
      slice_free(allocator, buffer);
  };

  buffer = readFileAlloc(allocator, positionals[1]);
  filectx = &buffer;
  if (echo_file && *echo_file)
    println("file : \n{slice(c8)}\n", buffer);

  var_ tokens = tokenize(allocator, buffer);
  defer { msList_deInit(allocator, tokens); };
  if (list_tokens && *list_tokens)
    foreach (var_ token, vla(*msList_vla(tokens)))
      println(" {Token} ", token);

  AllocatorV arena = arena_new_ext(allocator, 48 * 255);
  defer { arena_cleanup(arena); };
  println("{ast}", parseBBlock(arena, (slice(Token)){.len = msList_len(tokens), tokens}));
  println("current place : {}", FBA_current(s.allocator));
  return 0;
}

#include <wheels/wheels.h>

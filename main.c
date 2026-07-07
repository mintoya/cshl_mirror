#include <stdlib.h>
#include <wheels/cmdline_parser.h>
#include <wheels/examples/includeAll.c>

#include "ast.h"
#include "pool.h"
#include "tok1.h"
#include "tok2.h"

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
    (bool, echo_ast, ("--log-ast"), "log the ast", false),
    (bool, echo_file, ("--log-file"), "echo read file", false),
    (bool, log_tokens, ("--log-tokens"), "emmit list of tokens", false),
    (bool, execute, ("-r", "--run"), "interpret the ast", false)
) {
  var_ allocator = arena_new_ext(stdAlloc, 1024);
  defer {
    println("blocks    : {}", arena_countBlocks(allocator));
    println("footprint : {}", arena_footprint(allocator));
    println("total mem : {}", arena_totalMem(allocator));
    arena_cleanup(allocator);
  };
  // var_ allocator = stdAlloc;

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

  var_ tokens = tokenize(allocator, buffer);
  defer { msList_deInit(allocator, tokens); };
  if (log_tokens)
    foreach (var_ token, vla(*msList_vla(tokens)))
      println(" {Token} ", token);

  var_ astArena = arena_new_ext(allocator, 1024);
  defer { arena_cleanup(astArena); };

  var_ ast = parseBBlock(astArena, buffer.ptr, (slice(Token)){.len = msList_len(tokens), tokens});
  if (echo_ast)
    println("{ast}", ast);

  if (execute) {
    assertMessage(false, "unimplemented");
    // var_ state = state_init(allocator);
    // eval(&state, nullptr, *ast, REF(false));
    // println("stack place : {}", msList_len(state.stack));
  }

  return 0;
}

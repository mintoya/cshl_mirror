#include <stdlib.h>
#include <wheels/cmdline_parser.h>
#include <wheels/macros.h>

#include "ast.h"
#include "operations.h"
#include "tok1.h"
#include "tok2.h"
#include "wheels/fptr.h"
#include <wheels/allocators/debugallocator.h>
#include <wheels/mytypes.h>

fptr *filectx = nullptr;
__attribute__((noreturn)) void(error_at)(u8 *location, fptr message) {
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
  println("error : {slice(c8)} , at line {} column {}", message, line + 1, col + 1);
  exit(1);
}

void on_allocation(void *, usize in, usize out, void *, const char *file, uint line) {
  // println("(alloc) {} -> {},{cstr}:{}", in, out, file, line);
}
cmd_main(
    int nargs,
    char **args,
    (bool, help /*      */, ("-h", "--help") /**/, "print help message" /*  */, false),
    (bool, execute /*   */, ("-r", "--run") /* */, "interpret the ast" /*   */, false),

    (bool, echo_ast /*  */, ("--log-ast") /*   */, "log the ast" /*         */, false),
    (bool, echo_file /* */, ("--log-file") /*  */, "echo read file" /*      */, false),
    (bool, log_tokens /**/, ("--log-tokens") /**/, "emmit list of tokens" /**/, false),
) {
  var_ allocator = debugAllocator(.allocator = stdAlloc, .onalloc = on_allocation);
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

  var_ tokens = tokenize(allocator, buffer);
  defer { msList_deInit(allocator, tokens); };
  println("(log) after tokenization : {dbga_stats}", debugAllocator_stats(allocator));
  if (log_tokens)
    foreach (var_ token, vla(*msList_vla(tokens)))
      println(" {Token} ", token);

  var_ astArena = arena_new_ext(allocator, 1024);
  defer { arena_cleanup(astArena); };

  var_ ast = parseBBlock(astArena, buffer.ptr, (slice(Token)){.len = msList_len(tokens), tokens});
  println("(log) after ast : {dbga_stats}", debugAllocator_stats(allocator));

  let dstate = (DepthState){
      .inner_put = fileprint,
      .inner_arb = stdout,
      .depth = 0,
      .indent_spaces = 1,
      .at_line_start = true
  };

  if (echo_ast)
    print_wfO(depth_output, &dstate, "{ast}\n", ast);

  if (execute) {
    var_ builder = IrBuilder_init(allocator);
    defer { IrBuilder_deinit(&builder); };
    emit_ir(&builder, ast, 0);
    println("(log) after first ir : {dbga_stats}", debugAllocator_stats(allocator));
    print_wfO(depth_output, &dstate, "{IrBuilder}\n", builder);
    lower_i(&builder);
    println("(log) after second ir : {dbga_stats}", debugAllocator_stats(allocator));
    print_wfO(depth_output, &dstate, "{IrBuilder}\n", builder);
  }

  return 0;
}
#include <wheels/wheels.h>

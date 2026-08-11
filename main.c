#include <stdlib.h>
#include <wheels/cmdline_parser.h>
#include <wheels/macros.h>

#include "ast.h"
#include "wheels/fptr.h"
#include <wheels/allocators/arenaAllocator.h>
#include <wheels/allocators/debugallocator.h>
#include <wheels/mytypes.h>

// { util
[[gnu::constructor]] void initOpNumbers() {
  typedef union {
    char s[sizeof(u32)];
    u32 n;
  } opS;
  opNumbers.call = /* */ ((opS){"()"}).n;
  opNumbers.dot = /*  */ ((opS){"."}).n;
  opNumbers.mul = /*  */ ((opS){"*"}).n;
  opNumbers.div = /*  */ ((opS){"/"}).n;
  opNumbers.mod = /*  */ ((opS){"%"}).n;
  opNumbers.plus = /* */ ((opS){"+"}).n;
  opNumbers.minus = /**/ ((opS){"-"}).n;
  opNumbers.oand = /* */ ((opS){"and"}).n;
  opNumbers.oor = /*  */ ((opS){"or"}).n;
  opNumbers.oband = /**/ ((opS){"band"}).n;
  opNumbers.obor = /* */ ((opS){"bor"}).n;
  opNumbers.shr = /*  */ ((opS){"shr"}).n;
  opNumbers.shl = /*  */ ((opS){"shl"}).n;
  opNumbers.mor = /*  */ ((opS){">"}).n;
  opNumbers.les = /*  */ ((opS){"<"}).n;
  opNumbers.moq = /*  */ ((opS){">="}).n;
  opNumbers.leq = /*  */ ((opS){"<="}).n;
  opNumbers.eq = /*   */ ((opS){"=="}).n;
  opNumbers.neq = /*  */ ((opS){"!="}).n;
  opNumbers.pipe = /* */ ((opS){"|>"}).n;
  opNumbers.set = /*  */ ((opS){"="}).n;
}
void depth_output(const c8 *str, void *arb, usize length, bool is_last) {
  DepthState *state = (DepthState *)arb;
  if (!state || !state->inner_put) return;
  if (!length) {
    if (is_last) state->inner_put("", state->inner_arb, 0, true);
    return;
  }

  usize last_flush = 0;

  for (usize i = 0; i < length; ++i) {
    // 1. Handle our control characters
    if (str[i] == '\x11' || str[i] == '\x12') {
      // Flush everything before the control character
      if (i > last_flush) {
        state->inner_put(str + last_flush, state->inner_arb, i - last_flush, false);
      }

      if (str[i] == '\x11') state->depth++;
      if (str[i] == '\x12' && state->depth) state->depth--;

      last_flush = i + 1; // Skip the control character
      continue;
    }

    // 2. Handle newlines and indentation
    if (state->at_line_start && str[i] != '\n' && str[i] != '\r') {
      for (usize d = 0; d < state->depth * state->indent_spaces; ++d) {
        state->inner_put(" ", state->inner_arb, 1, false);
      }
      state->at_line_start = false;
    }

    if (str[i] == '\n') {
      state->inner_put(str + last_flush, state->inner_arb, (i - last_flush) + 1, false);
      last_flush = i + 1;
      state->at_line_start = true;
    }
  }

  if (last_flush < length) {
    if (state->at_line_start) {
      for (usize d = 0; d < state->depth * state->indent_spaces; ++d) {
        state->inner_put(" ", state->inner_arb, 1, false);
      }
      state->at_line_start = false;
    }
    state->inner_put(str + last_flush, state->inner_arb, length - last_flush, is_last);
  } else if (is_last) {
    state->inner_put("", state->inner_arb, 0, true);
  }
}
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


  let ast = parse_source(allocator, buffer);
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
    TODO("write execute");
  }

  return 0;
}
#include <wheels/wheels.h>

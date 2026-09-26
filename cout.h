#pragma once
#include "ir/common.h"
#include "wheels/print/print_pre.h"
#include <limits.h>

void out_runtime(printerfunction_context _ctx);
void makeRuntime(printerfunction_context _ctx);
void makeTypes(superscope *ss, printerfunction_context _ctx);
void makeFuncs(superscope *ss, printerfunction_context _ctx);
void makeMain(superscope *ss, printerfunction_context _ctx);
static void makeVariables(superscope *ss, printerfunction_context _ctx, scope *s);
void makeC(superscope *ss, outputFunction _put, printerfunction_context _ctx) {
  (void)_put;
  makeRuntime(_ctx);
  makeTypes(ss, _ctx);
  makeVariables(ss, _ctx, ss->outermost);
  makeFuncs(ss, _ctx);
  makeMain(ss, _ctx);
}
// A text-mode stream turns every '\n' into CRLF, so a source that already
// carries CRLF would reach the compiler as CRCRLF and break the backslash line
// continuations the runtime relies on. Emit LF only.
static void out_runtime_text(
    printerfunction_context _ctx,
    const unsigned char *bytes,
    usize len
) {
  usize begin = 0;
  foreach (let i, range(0, len))
    if (bytes[i] == '\r') {
      if (i > begin) _ctx.put((const char *)bytes + begin, _ctx.arb, i - begin, 0);
      begin = i + 1;
    }
  if (len > begin) _ctx.put((const char *)bytes + begin, _ctx.arb, len - begin, 0);
}
void out_runtime(printerfunction_context _ctx) {
#if !defined(__GNUC__) || defined(__clang__)
  static const unsigned char source[] = {
#embed "cshl.lib.c"
  };
  usize len = sizeof(source);
  // #embed appends a terminating null byte; it is not part of the runtime.
  if (len && source[len - 1] == '\0') len--;
  out_runtime_text(_ctx, source, len);
#else
  FILE *f = fopen("cshl.lib.c", "r");
  assertMessage(f);
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  unsigned char *dbuf = malloc((usize)len);
  assertMessage(fread(dbuf, 1, (usize)len, f) == (usize)len);
  fclose(f);
  out_runtime_text(_ctx, dbuf, (usize)len);
  free(dbuf);
#endif
  PUTS("\n");
}
void makeRuntime(printerfunction_context _ctx) {
  out_runtime(_ctx);
}

#include "cout/make_funcs.h"
#include "cout/make_types.h"

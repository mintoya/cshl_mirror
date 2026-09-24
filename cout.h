#pragma once
#include "ir/common.h"
#include "wheels/print/print_pre.h"
#include <limits.h>

void out_runtime(printerfunction_context _ctx);
void makeRuntime(printerfunction_context _ctx);
void makeTypes(superscope *ss, outputFunction _put, printerfunction_context _ctx);
void makeFuncs(superscope *ss, printerfunction_context _ctx);
void makeMain(superscope *ss, printerfunction_context _ctx);
static void makeVariables(superscope *ss, printerfunction_context _ctx, scope *s);
void makeC(superscope *ss, outputFunction _put, printerfunction_context _ctx) {
  makeRuntime(_ctx);
  makeTypes(ss, _put, _ctx);
  makeVariables(ss, _ctx, ss->outermost);
  makeFuncs(ss, _ctx);
  makeMain(ss, _ctx);
}
void out_runtime(printerfunction_context _ctx) {
#if !defined(__GNUC__) || defined(__clang__)
  char buf[] = {
#embed "cshl.lib.c"
  };
  PUTS(buf);
#else
  FILE *f = fopen("cshl.lib.c", "rb");
  assertMessage(f);
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *dbuf = malloc((usize)len);
  assertMessage(fread(dbuf, 1, (usize)len, f) == (usize)len);
  fclose(f);
  _ctx.put(dbuf, _ctx.arb, (usize)len, 0);
  free(dbuf);
#endif
  PUTS("\n");
}
void makeRuntime(printerfunction_context _ctx) {
  out_runtime(_ctx);
}

#include "cout/make_types.h"
#include "cout/make_funcs.h"

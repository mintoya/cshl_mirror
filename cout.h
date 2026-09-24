#pragma once
#include "ir/common.h"
#include "wheels/print/print_pre.h"
#include <limits.h>

void out_runtime(printerfunction_context _ctx);
void makeRuntime(printerfunction_context _ctx);
static void makeCIncludes(superscope *ss, printerfunction_context _ctx) {
  foreach (let header, span(ss->c_includes, msList_len(ss->c_includes))) {
    PUTS("#include \"");
    PUTS(*VLAP((const c8 *)header->ptr, header->len + 1));
    PUTS("\"\n");
  }
}
void makeTypes(superscope *ss, printerfunction_context _ctx);
void makeFuncs(superscope *ss, printerfunction_context _ctx);
void makeMain(superscope *ss, printerfunction_context _ctx);
static void makeVariables(superscope *ss, printerfunction_context _ctx, scope *s);
static void makeNamespaces(superscope *ss, printerfunction_context _ctx);
void makeC(superscope *ss, outputFunction _put, printerfunction_context _ctx) {
  makeCIncludes(ss, _ctx);
  makeRuntime(_ctx);
  makeTypes(ss, _ctx);
  makeVariables(ss, _ctx, ss->outermost);
  foreach (let entry, span(ss->imports, msList_len(ss->imports))) {
    makeVariables(ss, _ctx, entry->ns);
    PUTS("static void cshl_ns_init"), USETYPEPRINTER(u32, entry->ns->module_id), PUTS("(void);\n");
  }
  makeFuncs(ss, _ctx);
  makeNamespaces(ss, _ctx);
  makeMain(ss, _ctx);
}
void out_runtime(printerfunction_context _ctx) {
#if !defined(__GNUC__) || defined(__clang__)
  char buf[] = {
#embed "cshl.lib.c"
  };
  PUTS(buf);
#else
  FILE *f = fopen("cshl.lib.c", "r");
  assertMessage(f);
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *dbuf = malloc((usize)len);
  assertMessage(fread(dbuf, 1, (usize)len, f) == (usize)len);
  fclose(f);
  PUTS(*VLAP(dbuf, (usize)len + 1));
  free(dbuf);
#endif
  PUTS("\n");
}
void makeRuntime(printerfunction_context _ctx) {
  out_runtime(_ctx);
}

#include "cout/make_types.h"
#include "cout/make_funcs.h"

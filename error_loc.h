#pragma once
#include "srcpos.h"
#include "wheels/errable.h"

// Error nodes use the compilation allocator; source tables must outlive diagnostics.
typedef struct error_loc {
  err_t cause;
  srcpos_full source;
  allocfn allocator;
  char *formatted;
} error_loc;

// Define ERROR_LOC_IMPLEMENTATION once; callback identity is shared across translation units.
char *error_loc_message(void *data);

static inline const error_loc *error_loc_get(err_t error) {
  return error.fn == error_loc_message ? error.fnd : nullptr;
}
static inline const error_loc *error_loc_next(const error_loc *node) {
  return node ? error_loc_get(node->cause) : nullptr;
}
static inline err_t error_loc_add(allocfn allocator, err_t error, srcpos_full source) {
  if (!source.pidx) return error;
  error_loc *node = avalue(allocator, ((error_loc){.cause = error, .source = source, .allocator = allocator}));
  error.fn = error_loc_message;
  error.fnd = node;
  return error;
}
static inline const error_loc *error_loc_primary(err_t error) {
  const error_loc *node = error_loc_get(error);
  if (!node) return nullptr;
  while (error_loc_next(node)) node = error_loc_next(node);
  return node;
}

typePrinter(error_loc) {
  const error_loc *primary = &in;
  while (error_loc_next(primary)) primary = error_loc_next(primary);
  PUTS("  at ");
  USETYPEPRINTER(srcpos_full, primary->source);
  PUTS("\n");
  for (const error_loc *node = &in; node != primary; node = error_loc_next(node)) {
    PUTS("  via ");
    USETYPEPRINTER(srcpos_full, node->source);
    PUTS("\n");
  }
  if (primary->cause.fn) {
    USENAMEDPRINTER("cstr", primary->cause.fn(primary->cause.fnd));
    PUTS("\n");
  }
}
static inline void error_loc_print(FILE *out, err_t error) {
  fprintf(out, "error: %s\n", error.err_code);
  if (error.fn) fputs(error.fn(error.fnd), out);
  else fprintf(out, "  at %s:%zu (%s)\n", error.file, error.line, error.function);
}

#ifdef ERROR_LOC_IMPLEMENTATION
char *error_loc_message(void *data) {
  error_loc *node = data;
  if (!node->formatted)
    node->formatted = snprint(node->allocator, "{error_loc}{c8}", *node, (c8)0).ptr;
  return node->formatted;
}
#endif

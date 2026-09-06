#pragma once
#include "wheels/macros.h"
#include "wheels/mytypes.h"
#include "wheels/print.h"
#include "wheels/print/print_pre.h"

// a resolved source location. the raw u8* a token points at is barely usable
// on its own, so anything that wants to talk about a position in the source
// carries one of these instead. the fields and representation are expected to
// change; the point is that there is now a named type to change.
typedef struct {
  const u8 *ptr;
  u32 line;
  u32 col;
} srcpos;

static inline srcpos srcpos_nowhere() { return (srcpos){}; }

typePrinter(srcpos) {
  if (!in.ptr) {
    PUTS("(nowhere)");
    return;
  }
  PUTS(":");
  USETYPEPRINTER(u32, in.line);
  PUTS(":");
  USETYPEPRINTER(u32, in.col);
}

#pragma once
#include "common.h"

// called once a scope has finished lowering
void scope_finish(superscope *ss, scope *s) {
  // remove comptime math
  // remove writes and reads to r_void
}

#pragma once
#include "../type.h"

typedef struct scope scope;
tu_def(
    (ir, u8),
    // expression
    (ir_op /*      */, struct { u32 l, r, op; }),
    (ir_call /*    */, struct { u32 l, r, off, len; }),

    // control
    (ir_ret /*     */, u32),
    (ir_break /*   */, struct {}),
    (ir_continue /**/, struct {}),

    // scope
    (ir_if /*      */, struct { u32 cnd; typeof(scope*) t, f; }),
    (ir_while /*   */, struct { u32 cnd; typeof(scope*) t; }),
    (ir_block /*   */, scope *),
);
struct scope {
  msList(ir) instructions;
  msList(typed_value) values;
};
_Static_assert(sizeof(ir));

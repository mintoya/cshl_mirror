#pragma once
#include "../type.h"

typedef struct ir ir;
sliceDef(ir);
tu_def(
    (ir, u8),
    (ir_op /*      */, struct { u32 l, r, op; }),
    (ir_call /*    */, struct { u32 l, r, off, len; }),
    // control
    (ir_break /*   */, struct {}),
    (ir_if /*      */, struct { u32 cnd; slice(ir) t, f; }),
    (ir_while /*   */, struct { u32 cnd; slice(ir) t; }),
);

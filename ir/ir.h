#pragma once
#include <wheels/macros.h>
#include <wheels/mytypes.h>

sliceDef(u32);

typedef struct scope scope;

// clang-format off
tu_def(
    // l: left, r:right , d:destination , s:source
    (ir, u8),
    (ir_getref, /* */ struct { u32 l, d; }),                 // d = &l; // but a referance
    (ir_getptr, /* */ struct { u32 l, d; }),                 // d = &l;
    (ir_putptr, /* */ struct { u32 l, d; }),                 // d = *l;
    (ir_field, /*  */ struct { u32 base, member, d; }),      // d = &base.member; // ref to a member
    (ir_rawop, /*  */ struct { u32 l, r, d, o; }),           // d = l (o) r;
    (ir_call, /*   */ struct { u32 l, d; slice(u32) args; }),// d = l(...args)

    // control
    // The block evaluates to its child scope's last value.
    (ir_block, /* */ scope *),

    (ir_ret, /*   */ u32),
    (ir_if, /*    */ struct { u32 cnd; scope *t, *f; }),
    (ir_while, /* */ struct { scope *cnd, *body; }),
    (ir_break, /* */ struct {}),
    (ir_cont, /*  */ struct {}),
);
// clang-format on

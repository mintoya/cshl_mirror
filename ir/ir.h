#include <wheels/macros.h>
#include <wheels/mytypes.h>
typedef struct scope scope;
tu_def(
    // l: left, r:right , d:destination
    (ir, u8),
    (ir_putref, /**/ struct { u32 l, d; }),
    (ir_getref, /**/ struct { u32 l, d; }),
    // operations supported by c, no overloading and such
    // o is same packed opnumber
    (ir_rawop, /* */ struct { u32 l, r, d, o; }),

    // control
    (ir_if, /*    */ struct {u32 l;scope*r; }),
    // fine since scopes return their last value
    (ir_while, /* */ struct { scope *l, *r; }),
    (ir_break, /* */ struct {}),
    (ir_cont, /*  */ struct {}),
    // return register n
    (ir_ret, /*   */ u32),
);

#pragma once
#include <wheels/mytypes.h>
#include <assert.h>
tu_def(
    // l: left, r:right , d:destination , s:source
    (ir, u8),
    (ir_getref, /* */ struct { u32 l, d; }),       // d = &l; // but a referance
    (ir_getptr, /* */ struct { u32 l, d; }),       // d = &l;
    (ir_putptr, /* */ struct { u32 l, d; }),       // d = *l;
    (ir_cast, /*   */ struct { u32 l, d; }),       // d = (typeof(d))l; // integer cast, the only cast the interpreter sees
    (ir_rawop, /*  */ struct { u32 l, r, d, o; }), // d = l (o) r;
    (ir_field, /*  */ struct { u32 l, d, i; }),    // d = ref(l.i), n-th field
    (ir_array, /*  */ struct { u32 l, d, r; }),    // d = ref(l[r]), r is index of variable not index itself
    (ir_call, /*   */ struct { u32 l, d, r, e; }), // d = l(r ... e)
    (ir_farg, /*   */ struct { u32 l, d; }),       // fn(1...l) d = l
    (ir_set, /*    */ struct { u32 l, d; }),       // d = l

    (ir_blocks, /*  */ struct {}), // blockstart
    (ir_blocke, /*  */ struct {}), // blockend

    (ir_ret, /*    */ u32),        // return r
    (ir_ifs, /*     */ u32),       // if(r) {
    (ir_ifd, /*     */ struct {}), // }else{
    (ir_ife, /*     */ struct {}), //}
                                   //
    (ir_whils, /*   */ u32),       // while(r) {
    (ir_while, /*   */ struct {}), // }
    (ir_break, /*   */ struct {}), // break;
    (ir_cont, /*    */ struct {}), // continue;
);
static_assert(sizeof(ir));

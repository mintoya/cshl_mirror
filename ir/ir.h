#pragma once
#include <assert.h>
#include <wheels/fptr.h>
#include <wheels/macros.h>
#include <wheels/mytypes.h>

sliceDef(u32);

typedef struct scope scope;
typedef scope *scopeptr;

// clang-format off
tu_def(
    // l: left, r:right , d:destination , s:source
    (ir, u8),
    (ir_getref, /* */ struct { u32 l, d; }),                 // d = &l; // but a referance
    (ir_getptr, /* */ struct { u32 l, d; }),                 // d = &l;
    (ir_putptr, /* */ struct { u32 l, d; }),                 // d = *l;
    (ir_cast, /*   */ struct { u32 l, d; }),                 // d = (typeof(d))l; // integer cast
    (ir_rawop, /*  */ struct { u32 l, r, d, o; }),           // d = l (o) r;
    (ir_field, /*  */ struct { u32 l, d, i; }),              // d = ref(l.i), specifically runtime, struct members are named but also ordered so u32
    (ir_array, /*  */ struct { u32 l, d, r; }),              // d = ref(l[r]), r is index of variable not index itself

    (ir_call, /*   */ struct { u32 l, d; slice(u32) args; }),// d = l(...args)
    (ir_farg ,/*   */ struct { u32 l, d; }), // load function argument l into d
    (ir_set ,/*    */ struct { u32 l, d; }), // same as set without a d

    (ir_block, /*  */ scopeptr),

    (ir_ret, /*    */ u32),
    (ir_if, /*     */ struct { scopeptr cnd,t, f; }),
    (ir_while, /*  */ struct { scopeptr cnd, body; }),
    // switches maybe never, unlike the rest must inline value
    (ir_break, /*  */ struct {}),
    (ir_cont, /*   */ struct {}),
    // functoin
);
static_assert(sizeof(ir));
// clang-format on
/*
   representations
    reg(l,r,d)
      reg##N// by the time the ir gets here, there  are no named variables
    ir_getref
      reg(d) = &reg(l); //remember, referances always unfold as (*ref)
    ir_getptr
      reg(d) = &reg(l);
    ir_putptr
      reg(d) = *reg(l);
    ir_cast
      reg(d) = (typeof(reg(l)))reg(l);
    ir_rawop // o is subset of operations supported by c. all binary
      reg(d) = reg(r) (o) reg(l);
    ir_call
      reg(d) = reg(l) (args...)
    ir_block
    ir_ret
      return reg;// always a variable
    ir_if
      { //dont leak condition
        bool condition = false;
        { cnd... } // load falsiness into condition
        if(condition ){t...}else{f...};
      }
    ir_while
      while(true){
        {
          bool condition = false;
          cnd block...
          // scope yields last value, conditon is loaded with falsiness
          if(condition)break;
        }
        { body... }
      }
    ir_break
      break;
    ir_cont
      continue;
*/

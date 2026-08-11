#pragma once
#include "../type.h"
#include "wheels/mytypes.h"

typedef ast *astptr;
sliceDef(astptr);
typedef struct scope scope;
typedef struct ir ir;
typedef struct function function;
typedef struct superscope superscope;

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
  scope *parent;
  msList(ir) instructions;
  struct {
    msList(typed_value) values;
    moxmap(fptr, u32) named;
    u32 start;
  };
};
struct superscope {
  moxmap(Type, msList(struct { u32 fn ; fptr name; })) tfunc;
  msList(function) all_functionsii;
  scope *outermost;
};

// wrapper for slice(astptr)
struct function {
  slice(astptr) _;
};
static inline slice(astptr) function_args(function in) {
  assertMessage(in._.len >= 2);
  return slice_split(in._, (0, in._.len - 2))[0];
}
static inline astptr *function_return(function in) {
  assertMessage(in._.len >= 2);
  return &in._.ptr[in._.len - 2];
}
static inline astptr *function_body(function in) {
  assertMessage(in._.len >= 2);
  return &in._.ptr[in._.len - 1];
}

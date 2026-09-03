#include "common.h"
#include "wheels/macros.h"
#include "wheels/print/print_pre.h"
#include "wheels/sList.h"
#include <string.h>
typedef struct {
  u32 idx;
} idxtv;
static struct {
  superscope *ss;
  scope *s;
} scopeCtx;
typedef struct {
  typeof(scopeCtx) sc;
} setitemscope;
typePrinter(setitemscope) { scopeCtx = in.sc; }
typePrinter(idxtv) { assertMessage(scopeCtx.s && scopeCtx.s), USETYPEPRINTER(typed_value, *scope_find(scopeCtx.ss, scopeCtx.s, in.idx)); }
// interpreter signals
//  if they are null, that means that this item is no-longer break-able
//  mask out to add context
typedef struct {
  bool * /*       */ brk;
  bool * /*       */ cnt;
  typed_value * /**/ ret;
} irsig;
typedef struct {
  superscope *ss;
  scope *s;
  scope *root;
  irsig sig;
} irstate;

typed_value *irstate_find(irstate irs, u32 idx) {
  assertMessage(idx > irs.root->start);
  TODO();
}
// same but value should already exist
typed_value *irstate_findInitialized(irstate irs, u32 idx) {
  assertMessage(idx > irs.root->start);
  TODO();
}

void irsig_break(irsig *_sig) {
  if (!_sig->brk) error_print(nullptr, "broke in non-breakable context");
  *_sig->brk = 1;
  return;
}
void irsig_continue(irsig *_sig) {
  if (!_sig->cnt) error_print(nullptr, "broke in non-breakable context");
  *_sig->cnt = 1;
  return;
}
void irsig_return(irsig *_sig, typed_value v) {
  if (!_sig->ret) error_print(nullptr, "broke in non-breakable context");
  if (Type_cmp(v.t, _sig->ret->t)) error_print(nullptr, "comptime return wrong type'd value");
  assertMessage(v.v.value, "returning an unknonwn value");
  _sig->ret->v = v.v;
  return;
}
void interpret_set(irstate st, ir_set c) {
  println("{setitemscope}{idxtv} , set , {idxtv}", ((setitemscope){{st.ss, st.s}}), c.d, c.l);
  TODO("set");
}
void interpret_cast(irstate st, ir_cast c) {
  println("{setitemscope}{idxtv} , set , {idxtv}", ((setitemscope){{st.ss, st.s}}), c.d, c.l);
  TODO();
}
typed_value *ir_initTv(typed_value *tv) { TODO(); }
void interpret_ret(irstate st, ir_ret c) { irsig_return(&st.sig, *irstate_find(st, c)); }
void interpret_break(irstate st, ir_break c) { return irsig_break(&st.sig); }
void interpret_cont(irstate st, ir_cont c) { return irsig_continue(&st.sig); }
void interpret_if(irstate st, ir_if c) { TODO(); }
void interpret_while(irstate st, ir_while c) { TODO(); }
void interpret_getptr(irstate st, ir_getptr c) {
  *(void **)ir_initTv(irstate_find(st, c.d))->v.value = (void *)irstate_findInitialized(st, c.l)->v.value;
}
void interpret_getref(irstate st, ir_getref c) {
  interpret_getptr(st, (ir_getptr){.l = c.l, .d = c.d});
}
void interpret_putptr(irstate st, ir_putptr c) { TODO(); }
void interpret_call(irstate st, ir_call c) { TODO(); }
void interpret_rawop(irstate st, ir_rawop c) { TODO(); }
void interpret_block(irstate st, ir_block c) { TODO(); }
void interpret_farg(irstate st, ir_farg c) { TODO(); }
void interpret_array(irstate st, ir_array c) { TODO(); }
void interpret_field(irstate st, ir_field c) { TODO(); }
void interpret_op(irstate st, ir inst) {
  tu_match_void(
      inst,
      (ir_if, /*    */ op, interpret_if /*    */ (st, op)),
      (ir_while, /* */ op, interpret_while /* */ (st, op)),
      (ir_getref, /**/ op, interpret_getref /**/ (st, op)),
      (ir_getptr, /**/ op, interpret_getptr /**/ (st, op)),
      (ir_putptr, /**/ op, interpret_putptr /**/ (st, op)),
      (ir_cast, /*  */ op, interpret_cast /*  */ (st, op)),
      (ir_set, /*   */ op, interpret_set /*   */ (st, op);),
      (ir_call, /*  */ op, interpret_call /*  */ (st, op)),
      (ir_rawop, /* */ op, interpret_rawop /* */ (st, op)),
      (ir_farg, /*  */ op, interpret_farg /*  */ (st, op)),
      (ir_ret, /*   */ op, interpret_ret /*   */ (st, op)),
      (ir_break, /* */ op, interpret_break /* */ (st, op)),
      (ir_cont, /*  */ op, interpret_cont /*  */ (st, op)),
      (ir_block, /* */ op, interpret_block /* */ (st, op)),
      (ir_array, /* */ op, interpret_array /* */ (st, op)),
      (ir_field, /* */ op, interpret_field /* */ (st, op)),
  );
}
typed_value interpret(superscope *ss, scope *s) {
  let state = (irstate){ss, s, {}};
  foreach (let ip, span(s->instructions, msList_len(s->instructions)))
    interpret_op(state, *ip);
  let result = msList_last(s->values);
  assertMessage(result.v.value, "interpreting ir yielded an unknown value");
  return result;
}

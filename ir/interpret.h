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
  bool
      *brk;
  bool
      *cnt;
  typed_value
      *ret;
} irsig;
typedef struct {
  superscope* ss;
  scope* s;
}irstate ;
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
static void interpret_set(superscope *ss, scope *s, ir_set c, irsig sig) {
  println("{setitemscope}{idxtv} , set , {idxtv}", ((setitemscope){{ss, s}}), c.d, c.l);
  TODO("set");
}
static void interpret_cast(superscope *ss, scope *s, ir_cast c, irsig sig) {
  println("{setitemscope}{idxtv} , set , {idxtv}", ((setitemscope){{ss, s}}), c.d, c.l);
  TODO();
}
static void interpret_ret(superscope *ss, scope *s, ir_ret c, irsig sig) { irsig_return(&sig, *scope_find(ss, s, c)); }
static void interpret_break(superscope *ss, scope *s, ir_break c, irsig sig) { return irsig_break(&sig); }
static void interpret_cont(superscope *ss, scope *s, ir_cont c, irsig sig) { return irsig_continue(&sig); }
static void interpret_if(superscope *ss, scope *s, ir_if c, irsig sig) { TODO(); }
static void interpret_while(superscope *ss, scope *s, ir_while c, irsig sig) { TODO(); }
static void interpret_getref(superscope *ss, scope *s, ir_getref c, irsig sig) { TODO(); }
static void interpret_getptr(superscope *ss, scope *s, ir_getptr c, irsig sig) { TODO(); }
static void interpret_putptr(superscope *ss, scope *s, ir_putptr c, irsig sig) { TODO(); }
static void interpret_call(superscope *ss, scope *s, ir_call c, irsig sig) { TODO(); }
static void interpret_rawop(superscope *ss, scope *s, ir_rawop c, irsig sig) { TODO(); }
static void interpret_block(superscope *ss, scope *s, ir_block c, irsig sig) { TODO(); }
static void interpret_farg(superscope *ss, scope *s, ir_farg c, irsig sig) { TODO(); }
static void interpret_array(superscope *ss, scope *s, ir_array c, irsig sig) { TODO(); }
static void interpret_field(superscope *ss, scope *s, ir_field c, irsig sig) { TODO(); }
static void interpret_op(superscope *ss, scope *s, ir inst, irsig sig) {
  tu_match_void(
      inst,
      (ir_if, /*    */ op, interpret_if /*    */ (ss, s, op, sig)),
      (ir_while, /* */ op, interpret_while /* */ (ss, s, op, sig)),
      (ir_getref, /**/ op, interpret_getref /**/ (ss, s, op, sig)),
      (ir_getptr, /**/ op, interpret_getptr /**/ (ss, s, op, sig)),
      (ir_putptr, /**/ op, interpret_putptr /**/ (ss, s, op, sig)),
      (ir_cast, /*  */ op, interpret_cast /*  */ (ss, s, op, sig)),
      (ir_set, /*   */ op, interpret_set /*   */ (ss, s, op, sig);),
      (ir_call, /*  */ op, interpret_call /*  */ (ss, s, op, sig)),
      (ir_rawop, /* */ op, interpret_rawop /* */ (ss, s, op, sig)),
      (ir_farg, /*  */ op, interpret_farg /*  */ (ss, s, op, sig)),
      (ir_ret, /*   */ op, interpret_ret /*   */ (ss, s, op, sig)),
      (ir_break, /* */ op, interpret_break /* */ (ss, s, op, sig)),
      (ir_cont, /*  */ op, interpret_cont /*  */ (ss, s, op, sig)),
      (ir_block, /* */ op, interpret_block /* */ (ss, s, op, sig)),
      (ir_array, /* */ op, interpret_array /* */ (ss, s, op, sig)),
      (ir_field, /* */ op, interpret_field /* */ (ss, s, op, sig)),
  );
}
typed_value interpret(superscope *ss, scope *s) {
  foreach (let ip, span(s->instructions, msList_len(s->instructions)))
    interpret_op(ss, s, *ip, (irsig){});
  let result = msList_last(s->values);
  assertMessage(result.v.value, "interpreting ir yielded an unknown value");
  return result;
}

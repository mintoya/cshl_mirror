#pragma once
#include "common.h"
#include "wheels/macros.h"
#include "wheels/print/print_pre.h"
#include "wheels/sList.h"
#include <string.h>
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
  struct {
    c_fun func;
    typeof(value_store[/*mList_len(c_fun)-1*/]) *args;
  } fnctx;
} irstate;

typed_value *irstate_find(irstate irs, u32 idx) {
  let tc = scope_find(irs.ss, irs.s, idx);
  if (!(idx > irs.root->start))
    println("[LOG] ir getting {*:typed_value} from out of scope", tc);
  return tc;
}

static inline void *irstate_mem(irstate st, u32 idx) {
  let tv = irstate_find(st, idx);
  let size = Type_sizeof(tv->t);
  return tv->v.value = tv->v.value ?: acreate(st.ss->alloc, u8[size ?: 1]);
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
void interpret_ret(irstate st, ir_ret c) {
  let x = *irstate_find(st, c);
  typed_value res =
      (x.t.isref)
          ? ({
              x.v.value = irstate_mem(st, c);
              x.t.isref = false;
              x;
            })
          : x;
  irsig_return(&st.sig, res);
}
void interpret_break(irstate st, ir_break c) { return irsig_break(&st.sig); }
void interpret_cont(irstate st, ir_cont c) { return irsig_continue(&st.sig); }
void interpret_set(irstate st, ir_set c) {
  let d = irstate_find(st, c.d);
  let l = irstate_find(st, c.l);
  void *src = irstate_mem(st, c.l);
  tu_match_void(
      d->t.t,
      (c_ast, _, d->v.c_ast = l->v.value),
      (c_int, _, d->v.c_int = BInt.copy(st.ss->alloc, l->v.c_int)),
      (builtin, _, assertMessage(false)),
      (c_fun, _, d->v.c_fun = l->v.c_fun),
      (c_type, _, d->v.c_type = l->v.c_type),
      (default, {
        Type dt = d->t;
        dt.isref = false;
        memcpy(irstate_mem(st, c.d), src, Type_sizeof(dt));
      })
  );
}
void interpret_cast(irstate st, ir_cast c) {
  let d = irstate_find(st, c.d);
  let l = irstate_find(st, c.l);
  TODO();
}
bool istrue(typed_value t) {
  assertMessage(!Type_cmp(T(tu_of(r_bool, {})), t.t));
  return *(bool *)t.v.value;
}
void interpret_block(irstate st, ir_block c);
void interpret_if(irstate st, ir_if c) {
  if (istrue(interpret(st.ss, c.cnd)))
    interpret_block(st, c.t);
  else interpret_block(st, c.f);
}
void interpret_while(irstate st, ir_while c) {
  let o = st.sig.brk;
  defer { st.sig.brk = o; };
  let oc = st.sig.brk;
  defer { st.sig.brk = oc; };
  bool brk = false;
  bool cnt = false;
  st.sig.brk = &brk;
  st.sig.cnt = &cnt;
  while (istrue(interpret(st.ss, c.cnd))) {
    brk = false;
    cnt = false;
    interpret_block(st, c.body);
    if (brk) return;
    if (cnt) continue;
    if (st.sig.ret && st.sig.ret->v.value) return;
  }
}
void interpret_getptr(irstate st, ir_getptr c) {
  let d = irstate_find(st, c.d);
  let l = irstate_find(st, c.l);
  *(void **)irstate_mem(st, c.d) = irstate_mem(st, c.l);
}
void interpret_getref(irstate st, ir_getref c) { interpret_getptr(st, (ir_getptr){.l = c.l, .d = c.d}); }
void interpret_putptr(irstate st, ir_putptr c) { TODO(); }
void interpret_call(irstate st, ir_call c) { TODO(); }
void interpret_rawop(irstate st, ir_rawop c) { TODO(); }
void interpret_block(irstate st, ir_block c) {
  if (!c) return;
  void interpret_op(irstate, ir);
  let o = st.s;
  defer { st.s = o; };
  st.s = c;
  foreach (let i, span(c->values, msList_len(c->values)))
    if (!Type_is_comptime(i->t) && !i->v.value)
      if_decl (let s, Type_sizeof(i->t))
        i->v.value = acreate(st.ss->alloc, u8[s]);
  foreach (let i, span(c->instructions, msList_len(c->instructions))) {
    interpret_op(st, *i);
    if (st.sig.brk && *st.sig.brk) return;
    if (st.sig.cnt && *st.sig.cnt) return;
    if (st.sig.ret && st.sig.ret->v.value) return;
  }
}
void interpret_farg(irstate st, ir_farg c) {
  if (scope_find(st.ss, st.s, c.d)->v.value) return;
  assertMessage(st.fnctx.func && st.fnctx.args);
  let arr = mList_vla(st.fnctx.func);
  assertMessage(countof(*arr) - 1 > c.l);
  TODO();
}
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
typed_value interpret_sig(superscope *ss, scope *s, typed_value *ret) { // rediculous way to implement functions for now
  interpret_block((irstate){.ss = ss, .s = s, .root = s, .sig = {.ret = ret}}, s);
  if (ret && ret->v.value) return *ret;
  let result = msList_last(s->values);
  assertMessage(
      result.v.value,
      "interpreting ir yielded an unknown value, %s",
      snprint(stdAlloc, "{typed_value}", result).ptr
  );
  return result;
}
typed_value interpret(superscope *ss, scope *s) { return interpret_sig(ss, s, nullptr); }

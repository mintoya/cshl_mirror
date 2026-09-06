#pragma once
#include "common.h"
#include "wheels/macros.h"
#include "wheels/print/print_pre.h"
#include "wheels/sList.h"
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

typedef struct {
  TypeTree t;
  u32 ptr_level : 32;
  bool isconst : 1;
  bool iscomptime : 1;
  bool isvolatile : 1;
} Type_noref;
typedef struct {
  Type_noref t;
  value_store v;
} typed_value_noref;
Type Type_fnr(Type_noref tnr) {
  return (Type){
      .t = tnr.t,
      .ptr_level = tnr.ptr_level,
      .isconst = tnr.isconst,
      .iscomptime = tnr.iscomptime,
      .isvolatile = tnr.isvolatile,
  };
}
typed_value irstate_findR(irstate irs, u32 idx) {
  let res = scope_find(irs.ss, irs.s, idx);
  if (!(idx > irs.root->start))
    println("[LOG] ir getting {*:typed_value} from out of scope", res);
  // assertMessage(idx > irs.root->start);
  return *res;
}
typed_value_noref irstate_find(irstate irs, u32 idx) {
  let tc = irstate_findR(irs, idx);
  typed_value_noref result = {
      {
          .t = tc.t.t,
          .ptr_level = tc.t.ptr_level,
          .isconst = tc.t.isconst,
          .iscomptime = tc.t.iscomptime,
          .isvolatile = tc.t.isvolatile,
      }

  };
  result.v.value =
      tc.t.isref
          ? ({
              assertMessage(tc.v.value);
              *(void **)tc.v.value;
            })
          : tc.v.value;
  return result;
}

// storage helpers -----------------------------------------------------------
// a register whose scope was never interpreted (e.g. a decl in an outer scope)
// has no storage yet; the helpers below materialize it on first touch, so
// instructions always operate on actual memory and never need to know whether a
// register is a reference, a comptime payload, or still unallocated.

static inline typed_value *irstate_reg(irstate st, u32 idx) { return scope_find(st.ss, st.s, idx); }

// the register's own allocation: the data for a plain value, the pointer cell for
// a reference
static inline void *irstate_cell(irstate st, u32 idx) {
  let tv = irstate_reg(st, idx);
  if (tv->v.value) return tv->v.value;
  assertMessage(!Type_is_comptime(tv->t), "comptime values have no storage cell");
  usize sz = Type_sizeof(tv->t);
  return sz ? (tv->v.value = acreate(st.ss->alloc, u8[sz])) : nullptr;
}

// the memory holding a register's value, following a reference once; materializes
// the pointee on demand
static inline void *irstate_value(irstate st, u32 idx) {
  let tv = irstate_reg(st, idx);
  if (tv->t.isref) {
    void *cell = irstate_cell(st, idx);
    if (!*(void **)cell) {
      Type pointee = tv->t;
      pointee.isref = false;
      *(void **)cell = acreate(st.ss->alloc, u8[Type_sizeof(pointee)]);
    }
    return *(void **)cell;
  }
  return tv->v.value ?: irstate_cell(st, idx);
}

// where a store lands: through the reference's cell when the register is a
// reference, otherwise its own storage
static inline void *irstate_target(irstate st, u32 idx) {
  let tv = irstate_reg(st, idx);
  if (Type_is_comptime(tv->t)) return nullptr; // comptime values are copied by payload
  if (tv->t.isref) {
    void *cell = irstate_cell(st, idx);
    if (!*(void **)cell) {
      Type pointee = tv->t;
      pointee.isref = false;
      *(void **)cell = acreate(st.ss->alloc, u8[Type_sizeof(pointee)]);
    }
    return *(void **)cell;
  }
  return irstate_cell(st, idx);
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
void interpret_ret(irstate st, ir_ret c) { irsig_return(&st.sig, irstate_findR(st, c)); }
void interpret_break(irstate st, ir_break c) { return irsig_break(&st.sig); }
void interpret_cont(irstate st, ir_cont c) { return irsig_continue(&st.sig); }
void interpret_set(irstate st, ir_set c) {
  let d = irstate_reg(st, c.d);
  let l = irstate_reg(st, c.l);
  void *src = irstate_value(st, c.l);
  if (Type_is_comptime(d->t)) {
    // comptime values live in their payload pointer: alias it
    d->v.value = src;
    return;
  }
  Type dt = d->t;
  if (dt.isref) dt.isref = false;
  usize sz = Type_sizeof(dt);
  if (sz) memcpy(irstate_target(st, c.d), src, sz);
}
void interpret_cast(irstate st, ir_cast c) {
  let d = irstate_find(st, c.d);
  let l = irstate_find(st, c.l);
  assertMessage(Type_isint(Type_fnr(d.t)) && Type_isint(Type_fnr(l.t)));
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
    interpret_block(st, c.body);
    if (brk) return;
    if (cnt) continue;
    if (st.sig.ret && st.sig.ret->v.value) return;
  }
}
void interpret_getptr(irstate st, ir_getptr c) {
  // the destination's cell holds the address of the source's storage
  *(void **)irstate_cell(st, c.d) = irstate_cell(st, c.l);
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
  // comptime calls bind their args into the registers during lowering, in which case
  // there is no runtime frame to load from
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
typed_value interpret_sig(superscope *ss, scope *s, typed_value *ret) {
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

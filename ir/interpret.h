#pragma once
#include "common.h"
#include "scope.h"
#include "wheels/macros.h"
#include "wheels/mytypes.h"
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
    slice(Type) ftype;
    typeof(value_store[/*ftype.len-1*/]) *args;
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

// returns a typed value with isref grunteed to be false
static typed_value ir_readthrough(irstate st, u32 idx) {
  typed_value *tv = irstate_find(st, idx);
  if (!tv->t.isref) return *tv;
  Type t = tv->t;
  t.isref = false;
  return TV(t, *(void **)irstate_mem(st, idx));
}
static r_struct ir_find_struct(TypeTree t) {
  return tu_match_exp(t, (r_struct, r, r), (r_tag, r, ir_find_struct(*r.t)), (default, (r_struct) nullptr));
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
  // bare-id returns lower as references; unfold them to the pointee's storage
  let x = ir_readthrough(st, c);
  irsig_return(&st.sig, x);
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
  let b = int_as_bigint(st.ss->alloc, ir_readthrough(st, c.l));
  if (tu_is(c_int, d->t.t)) {
    d->v.c_int = b;
    return;
  }
  typed_value t = *d;
  t.v.value = irstate_mem(st, c.d);
  if (tu_is(r_bool, d->t.t)) *(u8 *)t.v.value = !!BInt.cmp(b, nullptr);
  else bigint_into_int(st.ss, &t, b);
  d->v = t.v;
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
  bool brk = false, cnt = false;
  st.sig.brk = &brk, st.sig.cnt = &cnt;
  while (istrue(interpret(st.ss, c.cnd))) {
    brk = false, cnt = false;
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
void interpret_putptr(irstate st, ir_putptr c) {
  let d = irstate_find(st, c.d);
  d->v.value = *(void **)ir_readthrough(st, c.l).v.value;
}
void interpret_call(irstate st, ir_call c) {
  let it = st.ss->functions[c.l.fn - 1].instances[c.l.spec];

  // fnctx
  let ftype = slice_alloc(st.ss->alloc, Type, c.args.len + 1);
  defer { slice_free(st.ss->alloc, ftype); };
  slice_last(ftype) = it.ret;
  foreach (let it, span(ftype.ptr, ftype.len - 1))
    *it = irstate_find(st, c.args.ptr[it - ftype.ptr])->t;
  st.fnctx.ftype = ftype;
  st.fnctx.args = acreate(st.ss->alloc, value_store[st.fnctx.ftype.len - 1]);
  defer { adestroy(st.ss->alloc, (value_store(*)[st.fnctx.ftype.len - 1]) st.fnctx.args); };
  foreach (let it, span(*st.fnctx.args, st.fnctx.ftype.len - 1))
    *it = irstate_find(st, c.args.ptr[it - *st.fnctx.args])->v;
  typed_value call_ret = TV(it.ret);
  st.sig.ret = &call_ret;

  let ns = scope_copy(st.ss, it.spec);
  defer { scope_destroy(st.ss, ns); };
  interpret_block(st, ns);

  typed_value result = {};
  if (call_ret.v.value) {
    result = call_ret;
  } else {
    result = msList_last(ns->values);
    assertMessage(
        !Type_cmp(T(tu_of(r_void, {})), result.t),
        "comptime function did not return"
    );
  }

  let destination = irstate_find(st, c.d);
  assertMessage(!Type_cmp(destination->t, result.t), "function returned the wrong type");
  tu_match_void(
      destination->t.t,
      (c_ast, _, destination->v.c_ast = result.v.c_ast),
      (c_int, _, {
        msList_deInit(st.ss->alloc, destination->v.c_int);
        destination->v.c_int = BInt.copy(st.ss->alloc, result.v.c_int);
      }),
      (c_fun, _, destination->v.c_fun = result.v.c_fun),
      (c_type, _, destination->v.c_type = result.v.c_type),
      (builtin, _, assertMessage(false)),
      (default, {
        memcpy(irstate_mem(st, c.d), result.v.value, Type_sizeof(destination->t));
      })
  );
}
void interpret_rawop(irstate st, ir_rawop c) {
  bigint out = nullptr;
  allocfn al = arena_new_ext(st.ss->alloc, 1024);
  defer { arena_cleanup(al); };
  bigint lhs = int_as_bigint(al, ir_readthrough(st, c.r)); // r holds the left operand, see ir.h
  bigint rhs = int_as_bigint(al, ir_readthrough(st, c.l));
  out = switch_exp(
      op_as_enum(c.o),
      (enop_mul /*  */, BInt.mul(al, lhs, rhs)),
      (enop_div /*  */, BInt.div(al, lhs, rhs).div),
      (enop_mod /*  */, BInt.div(al, lhs, rhs).mod),
      (enop_plus /* */, BInt.add(al, lhs, rhs)),
      (enop_minus /**/, BInt.sub(al, lhs, rhs)),
      (enop_shl /*  */, BInt.bit.shift(al, lhs, rhs)),
      (enop_shr /*  */, BInt.bit.shift(al, lhs, BInt.negate(al, rhs))),
      (enop_oband /**/, BInt.bit.band(al, lhs, rhs)),
      (enop_obor /* */, BInt.bit.bor(al, lhs, rhs)),
      (enop_oand /* */, BInt.from.i64(al, !!(BInt.cmp(lhs, nullptr) && BInt.cmp(rhs, nullptr)))),
      (enop_oor /*  */, BInt.from.i64(al, !!(BInt.cmp(lhs, nullptr) || BInt.cmp(rhs, nullptr)))),
      (enop_eq /*   */, BInt.from.i64(al, !BInt.cmp(lhs, rhs))),
      (enop_neq /*  */, BInt.from.i64(al, !!BInt.cmp(lhs, rhs))),
      (enop_les /*  */, BInt.from.i64(al, BInt.cmp(lhs, rhs) < 0)),
      (enop_mor /*  */, BInt.from.i64(al, BInt.cmp(lhs, rhs) > 0)),
      (enop_leq /*  */, BInt.from.i64(al, BInt.cmp(lhs, rhs) <= 0)),
      (enop_moq /*  */, BInt.from.i64(al, BInt.cmp(lhs, rhs) >= 0)),
      (default, (assertMessage(false, "unsupported rawop"), (bigint) nullptr)),
  );
  assertMessage(out);
  out = BInt.copy(st.ss->alloc, out);
  let d = irstate_find(st, c.d);
  bigint_into_int(st.ss, d, out);
}
void interpret_block(irstate st, ir_block c) {
  if (!c) return;
  void interpret_op(irstate, ir);
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
  assertMessage(st.fnctx.ftype.len && st.fnctx.args);
  assertMessage(c.l + 1 < st.fnctx.ftype.len);
  typed_value v = {st.fnctx.ftype.ptr[c.l], (*st.fnctx.args)[c.l]};
  let d = irstate_find(st, c.d);

  assertMessage(!Type_cmp(v.t, d->t));
  tu_match_void(
      d->t.t,
      (c_ast, _, d->v.c_ast = v.v.value),
      (c_int, _, d->v.c_int = BInt.copy(st.ss->alloc, v.v.c_int)),
      (builtin, _, assertMessage(false)),
      (c_fun, _, d->v.c_fun = v.v.c_fun),
      (c_type, _, d->v.c_type = v.v.c_type),
      (default, memcpy(irstate_mem(st, c.d), v.v.value, Type_sizeof(d->t)))
  );
}
void interpret_array(irstate st, ir_array c) {
  let l = ir_readthrough(st, c.l);
  let arr = tu_catch(r_arr, l.t.t);
  let idx = int_as_bigint(st.ss->alloc, ir_readthrough(st, c.r));
  bigint_expand(st.ss->alloc, &idx, sizeof(usize) / sizeof(bigint_unit) + 1);
  // no bounds checking, matching the lowerer; a zero-length array has nothing to check against
  *(void **)irstate_mem(st, c.d) = (u8 *)l.v.value + *(usize *)idx * Type_sizeof(T(*arr.t));
}
void interpret_field(irstate st, ir_field c) {
  let l = ir_readthrough(st, c.l);
  let strct = ir_find_struct(l.t.t);
  assertMessage(strct, "field access on non-struct (bitfields unsupported)");
  let item = mList_arr(strct)[c.i];
  *(void **)irstate_mem(st, c.d) = (u8 *)l.v.value + item.offset;
}
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
typed_value interpret(superscope *ss, scope *s) {
  return interpret_sig(ss, s, nullptr);
}

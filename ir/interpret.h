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

// reads a value through a reference; a ref stores the pointee's storage in its own
static typed_value ir_load(irstate st, u32 idx) {
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
  let b = int_as_bigint(st.ss, ir_load(st, c.l));
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
  d->v.value = *(void **)ir_load(st, c.l).v.value;
}
void interpret_call(irstate st, ir_call c) {
  let f = irstate_find(st, c.l);
  let fun = tu_catch(c_fun, f->t.t);
  let fstruct = (st.ss->functions)[f->v.c_fun - 1];
  assertMessage(c.args.len + 1 == mList_len(fun));
  value_store *args = (value_store *)acreate(st.ss->alloc, value_store[c.args.len ?: 1]);
  foreach (let a, span(c.args.ptr, c.args.len))
    args[a - c.args.ptr] = ir_load(st, *a).v;

  // rebuild the specialization key the lowerer used: declared param types + resolved return type
  let keyscope = scope_inferior(st.ss, st.s);
  defer { scope_destroy(st.ss, keyscope); };
  let key = *acreate(st.ss->alloc, Type[c.args.len + 1]);
  foreach (let p, span(fstruct.params.ptr, fstruct.params.len)) {
    let idx = p - fstruct.params.ptr;
    let dcl = tu_catch(astDecl, **p, error_print(*p, "function parameter must be a decl"));
    key[idx] = value_asType_loc(temporary_interpret(st.ss, keyscope, dcl.type), ast_loc(*dcl.type));
    if (Type_is_comptime(key[idx]))
      scope_bind(
          st.ss, keyscope, scope_add_value(st.ss, keyscope, TV(key[idx], args[idx])),
          id_from_ast(st.ss, keyscope, dcl.name)
      );
  }
  key[c.args.len] = value_asType_loc(temporary_interpret(st.ss, keyscope, fstruct.retT), ast_loc(*fstruct.retT));

  if_decl (let spec, moxmap_get(fstruct.mp, ((slice(Type)){c.args.len + 1, key}))) {
    // run a private copy of the specialization so recursive activations dont share storage
    let run = scope_copy(st.ss, spec->spec);
    foreach (let v, span(run->values, msList_len(run->values)))
      if (!Type_is_comptime(v->t)) v->v.value = nullptr;
    let inner = st;
    inner.s = inner.root = run;
    inner.fnctx.func = fun;
    inner.fnctx.args = (typeof(inner.fnctx.args))args;
    typed_value ret = TV(key[c.args.len]); // the C stack frame owns the return slot
    inner.sig = (irsig){.ret = &ret};
    interpret_block(inner, run);
    let res = ret.v.value ? ret : msList_last(run->values);
    assertMessage(res.v.value, "call yielded an unknown value");
    irstate_find(st, c.d)->v = res.v;
    return;
  }
  assertMessage(false, "no specialization was lowered for this call");
}
void interpret_rawop(irstate st, ir_rawop c) {
  let d = irstate_find(st, c.d);
  allocfn al = st.ss->alloc;
  bigint lhs = int_as_bigint(st.ss, ir_load(st, c.r)); // r holds the left operand, see ir.h
  bigint rhs = int_as_bigint(st.ss, ir_load(st, c.l));
  bool bres = false;
  bigint out = nullptr;
  switch (op_as_enum(c.o)) {
    case enop_mul: out = BInt.mul(al, lhs, rhs); break;
    case enop_div: out = BInt.div(al, lhs, rhs).div; break;
    case enop_mod: out = BInt.div(al, lhs, rhs).mod; break;
    case enop_plus: out = BInt.add(al, lhs, rhs); break;
    case enop_minus: out = BInt.sub(al, lhs, rhs); break;
    case enop_shl: out = BInt.bit.shift(al, lhs, rhs); break;
    case enop_shr: out = BInt.bit.shift(al, lhs, BInt.negate(al, rhs)); break;
    case enop_oband: out = BInt.bit.band(al, lhs, rhs); break;
    case enop_obor: out = BInt.bit.bor(al, lhs, rhs); break;
    case enop_oand: bres = !!(BInt.cmp(lhs, nullptr) && BInt.cmp(rhs, nullptr)); break;
    case enop_oor: bres = !!(BInt.cmp(lhs, nullptr) || BInt.cmp(rhs, nullptr)); break;
    case enop_eq: bres = !BInt.cmp(lhs, rhs); break;
    case enop_neq: bres = !!BInt.cmp(lhs, rhs); break;
    case enop_les: bres = BInt.cmp(lhs, rhs) < 0; break;
    case enop_mor: bres = BInt.cmp(lhs, rhs) > 0; break;
    case enop_leq: bres = BInt.cmp(lhs, rhs) <= 0; break;
    case enop_moq: bres = BInt.cmp(lhs, rhs) >= 0; break;
    default: assertMessage(false, "unsupported rawop");
  }
  if (!out) out = BInt.from.i64(al, bres);
  if (tu_is(c_int, d->t.t)) {
    d->v.c_int = out;
    return;
  }
  typed_value t = *d;
  t.v.value = irstate_mem(st, c.d);
  bigint_into_int(st.ss, &t, out);
  d->v = t.v;
}
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
  if (!st.fnctx.func) return; // comptime instantiations bind their args directly
  assertMessage(st.fnctx.args);
  assertMessage(c.l + 1 < mList_len(st.fnctx.func));
  let d = irstate_find(st, c.d);
  d->v = ((value_store *)st.fnctx.args)[c.l];
  if_decl (let sz, Type_sizeof(d->t)) { // private storage so recursive frames dont alias the caller's
    void *mem = acreate(st.ss->alloc, u8[sz]);
    memcpy(mem, d->v.value, sz);
    d->v.value = mem;
  }
}
void interpret_array(irstate st, ir_array c) {
  let l = ir_load(st, c.l);
  let arr = tu_catch(r_arr, l.t.t);
  let idx = int_as_bigint(st.ss, ir_load(st, c.r));
  bigint_expand(st.ss->alloc, &idx, sizeof(usize) / sizeof(bigint_unit) + 1);
  // no bounds checking, matching the lowerer; a zero-length array has nothing to check against
  *(void **)irstate_mem(st, c.d) = (u8 *)l.v.value + *(usize *)idx * Type_sizeof(T(*arr.t));
}
void interpret_field(irstate st, ir_field c) {
  let l = ir_load(st, c.l);
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
typed_value interpret(superscope *ss, scope *s) { return interpret_sig(ss, s, nullptr); }

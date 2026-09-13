#pragma once
#include "common.h"
#include "scope.h"
#include "wheels/macros.h"
#include "wheels/mytypes.h"
#include "wheels/print/print_pre.h"
#include "wheels/sList.h"
#include <string.h>
typedef struct {
  enum : u8 {
    ir_brk_normal = 0,
    ir_brk_break = 1,
    ir_brk_continue = 2
  } *
      brk;
  typed_value *ret;
} irsig;
typedef struct {
  superscope *ss;
  scope *s;
  scope *root;
  irsig sig;
  struct {
    // indices, so that an argument value can carry its type by index like any other
    slice(typeidx) ftype;
    typeof(value_store[/*ftype.len-1*/]) *args;
  } fnctx;
} irstate;

typed_value *irstate_find(irstate irs, u32 idx) {
  let tc = scope_find(irs.ss, irs.s, idx);
  // if (!(idx > irs.root->start))
  //   println("[LOG] ir getting {*:typed_value} from out of scope", tc);
  return tc;
}

static inline void *irstate_mem(irstate st, u32 idx) {
  let tv = irstate_find(st, idx);
  let size = Type_sizeof(ss_type(st.ss, tv->t), ss_typeenv(st.ss));
  return tv->v.value = tv->v.value ?: acreate(st.ss->alloc, u8[size ?: 1]);
}

// returns a typed value with isref grunteed to be false
static typed_value ir_readthrough(irstate st, u32 idx) {
  typed_value *tv = irstate_find(st, idx);
  Type t = ss_type(st.ss, tv->t);
  if (!t.isref) return *tv;
  t.isref = false;
  return TV_with(st.ss, (typed_value){.t = tv->t, .v = {.value = *(void **)irstate_mem(st, idx)}}, t);
}
static inline void *irstate_data(irstate st, u32 idx) {
  typed_value tv = ir_readthrough(st, idx);
  return tv.v.value ? tv.v.value : irstate_mem(st, idx);
}

void irsig_break errs(irsig *_sig) {
  if (!_sig->brk) return_err("BROKE_NOT_BREAKABLE");
  *_sig->brk = ir_brk_break;
  return;
}
void irsig_continue errs(irsig *_sig) {
  if (!_sig->brk) return_err("BROKE_NOT_BREAKABLE");
  *_sig->brk = ir_brk_continue;
  return;
}
void irsig_return errs(irsig *_sig, typeenv env, typed_value v) {
  println("[LOG:ret]  {typed_value_full}", ((typed_value_full){v, env}));
  if (!_sig->ret) return_err("RETURN_NOT_RETURNABLE");
  let ta = type_at(env, v.t);
  let tb = type_at(env, _sig->ret->t);
  if (Type_cmp(env, ta, tb) && Type_cmp(env, T(tu_of(c_any, {})), tb))
    return_err("RETURN_WRONG_TYPE");
  if (!v.v.value) return_err("RETURN_UNKNOWN");
  _sig->ret->v = v.v;
  return;
}
void interpret_op errs(irstate st, ir inst);
void interpret_ret errs(irstate st, ir_ret c) { try_err(irsig_return, (&st.sig, ss_typeenv(st.ss), ir_readthrough(st, c))); }
void interpret_break errs(irstate st, ir_break c) { try_err(irsig_break, (&st.sig)); }
void interpret_cont errs(irstate st, ir_cont c) { try_err(irsig_continue, (&st.sig)); }
void interpret_set errs(irstate st, ir_set c) {
  let d = irstate_find(st, c.d);
  let l = irstate_find(st, c.l);
  tu_match_void(
      ss_type(st.ss, d->t).t,
      (c_ast, _, d->v.c_ast = (astidx)(usize)l->v.value),
      (c_int, _, d->v.c_int = BInt.copy(st.ss->alloc, l->v.c_int)),
      (builtin, _, return_err("SET_BUILTIN")),
      (c_fun, _, d->v.c_fun = l->v.c_fun),
      (c_type, _, d->v.c_type = l->v.c_type),
      (default, {
        Type dt = ss_type(st.ss, d->t);
        dt.isref = false;
        memcpy(irstate_data(st, c.d), irstate_data(st, c.l), Type_sizeof(dt, ss_typeenv(st.ss)));
      })
  );
}
// at this point no non-integer casts are supported
void interpret_cast(irstate st, ir_cast c) {
  let d = irstate_find(st, c.d);
  let dt = ss_type(st.ss, d->t);
  assertMessage(Type_isint(dt, ss_typeenv(st.ss)) || tu_is(r_bool, dt.t), "non-integer cast reached the interpreter");
  let b = int_as_bigint(st.ss->alloc, ir_readthrough(st, c.l), ss_typeenv(st.ss));
  if (tu_is(c_int, dt.t)) {
    d->v.c_int = b;
    return;
  }
  typed_value t = *d;
  t.v.value = irstate_mem(st, c.d);
  if (tu_is(r_bool, dt.t)) *(u8 *)t.v.value = !!BInt.cmp(b, nullptr);
  else bigint_into_int(st.ss, &t, b);
  d->v = t.v;
}
bool istrue errs(typed_value t, typeenv e) {
  if (Type_cmp(e, T(tu_of(r_bool, {})), type_at(e, t.t)))
    return_err("CONDITION_NOT_BOOL", false);
  return *(bool *)t.v.value;
}
void interpret_block errs(irstate st, ir_block c);
void interpret_if errs(irstate st, ir_if c) {
  if (try_err(istrue, (try_err(interpret, (st.ss, c.cnd)), ss_typeenv(st.ss))))
    try_err(interpret_block, (st, c.t));
  else try_err(interpret_block, (st, c.f));
}
void interpret_while errs(irstate st, ir_while c) {
  ptrstype(itypeof(irsig, brk)) brk = 0;
  st.sig.brk = &brk;
  while (try_err(istrue, (try_err(interpret, (st.ss, c.cnd)), ss_typeenv(st.ss)))) {
    brk = ir_brk_normal;
    try_err(interpret_block, (st, c.body));
    if (brk == ir_brk_break) break;
    if (brk == ir_brk_continue) continue;
    if (st.sig.ret && st.sig.ret->v.value) return;
  }
}
void interpret_getptr(irstate st, ir_getptr c) { *(void **)irstate_mem(st, c.d) = irstate_data(st, c.l); }
void interpret_getref(irstate st, ir_getref c) { interpret_getptr(st, (ir_getptr){.l = c.l, .d = c.d}); }
// d = *l, l is a pointer, so the address is read straight out of its storage. unfolding a
// referance here would dereference twice
void interpret_putptr(irstate st, ir_putptr c) {
  void *target = *(void **)irstate_mem(st, c.l);
  // a referance keeps its address in storage, the way getref writes it, so that reading it
  // unfolds to the thing. a plain value is the thing itself, and points straight at it
  if (ss_type(st.ss, irstate_find(st, c.d)->t).isref) *(void **)irstate_mem(st, c.d) = target;
  else irstate_find(st, c.d)->v.value = target;
}
void interpret_call errs(irstate st, ir_call c) {
  let it = st.ss->functions[c.l.fn - 1].instances[c.l.spec];

  let ftype = slice_alloc(st.ss->alloc, typeidx, c.args.len + 1);
  defer { slice_free(st.ss->alloc, ftype); };
  slice_last(ftype) = ss_type_intern(st.ss, it.ret);
  foreach (let it, span(ftype.ptr, ftype.len - 1))
    *it = irstate_find(st, c.args.ptr[it - ftype.ptr])->t;
  st.fnctx.ftype = ftype;
  usize nargs = st.fnctx.ftype.len - 1;
  st.fnctx.args = nargs ? acreate(st.ss->alloc, value_store[nargs]) : nullptr;
  defer {
    if (nargs) adestroy(st.ss->alloc, (value_store(*)[nargs])st.fnctx.args);
  };
  if (nargs)
    foreach (let it, span(*st.fnctx.args, nargs))
      *it = irstate_find(st, c.args.ptr[it - *st.fnctx.args])->v;
  typed_value result = TV_of(st.ss, it.ret);
  st.sig = (typeof(st.sig)){0, &result};

  let ns = scope_copy(st.ss, it.spec);
  defer { scope_destroy(st.ss, ns); };
  try_err(interpret_block, (st, ns));

  if (!result.v.value && Type_cmp(ss_typeenv(st.ss), T(tu_of(r_void, {})), ss_type(st.ss, result.t)))
    return_err("NO_RETURN_NONVOID");

  let destination = irstate_find(st, c.d);
  tu_match_void(
      ss_type(st.ss, destination->t).t,
      (c_ast, _, destination->v.c_ast = result.v.c_ast),
      (c_int, _, {
        msList_deInit(st.ss->alloc, destination->v.c_int);
        destination->v.c_int = BInt.copy(st.ss->alloc, result.v.c_int);
      }),
      (c_fun, _, destination->v.c_fun = result.v.c_fun),
      (c_type, _, destination->v.c_type = result.v.c_type),
      (builtin, _, return_err("SET_BUILTIN")),
      (default, memcpy(irstate_mem(st, c.d), result.v.value, Type_sizeof(ss_type(st.ss, destination->t), ss_typeenv(st.ss))))
  );
}
void interpret_rawop errs(irstate st, ir_rawop c) {
  bigint out = nullptr;
  allocfn al = arena_new_ext(st.ss->alloc, 1024);
  defer { arena_cleanup(al); };
  bigint lhs = int_as_bigint(al, ir_readthrough(st, c.r), ss_typeenv(st.ss)); // r holds the left operand, see ir.h
  bigint rhs = int_as_bigint(al, ir_readthrough(st, c.l), ss_typeenv(st.ss));
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
      (default, (return_err("UNSUPPORTED_INSTRUCTION"), (bigint) nullptr)),
  );
  if (!out) return_err("MATH_UNKNOWN_RESULT");
  out = BInt.copy(st.ss->alloc, out);
  let d = irstate_find(st, c.d);
  bigint_into_int(st.ss, d, out);
}
void interpret_block errs(irstate st, ir_block c) {
  if (!c) return;

  st.s = c;
  foreach (let i, span(c->values, msList_len(c->values)))
    if (!Type_is_comptime(ss_type(st.ss, i->t)) && !i->v.value)
      if_decl (let s, Type_sizeof(ss_type(st.ss, i->t), ss_typeenv(st.ss)))
        i->v.value = acreate(st.ss->alloc, u8[s]);
  foreach (let i, span(c->instructions, msList_len(c->instructions))) {
    try_err(interpret_op, (st, *i));
    if (st.sig.brk && *st.sig.brk) return;
    if (st.sig.ret && st.sig.ret->v.value) return;
  }
}
void interpret_farg errs(irstate st, ir_farg c) {
  if (!(st.fnctx.ftype.len && st.fnctx.args)) return_err("INVALID_FUNCTION");
  if (!(c.l + 1 < st.fnctx.ftype.len)) return_err("FUNCTION_ARG_OOB");
  typed_value v = {st.fnctx.ftype.ptr[c.l], (*st.fnctx.args)[c.l]};
  let d = irstate_find(st, c.d);
  if (Type_cmp(ss_typeenv(st.ss), ss_type(st.ss, v.t), ss_type(st.ss, d->t)))
    return_err("FUNCTION_WRONG_RETURN");
  tu_match_void(
      ss_type(st.ss, d->t).t,
      (c_ast, _, d->v.c_ast = (astidx)(usize)v.v.value),
      (c_int, _, d->v.c_int = BInt.copy(st.ss->alloc, v.v.c_int)),
      (builtin, _, return_err("SET_BUILTIN")),
      (c_fun, _, d->v.c_fun = v.v.c_fun),
      (c_type, _, d->v.c_type = v.v.c_type),
      (default, memcpy(irstate_mem(st, c.d), v.v.value, Type_sizeof(ss_type(st.ss, d->t), ss_typeenv(st.ss))))
  );
}
void interpret_array(irstate st, ir_array c) {
  let l = ir_readthrough(st, c.l);
  let arr = tu_catchr(r_arr, ss_type(st.ss, l.t).t);
  let idx = int_as_bigint(st.ss->alloc, ir_readthrough(st, c.r), ss_typeenv(st.ss));
  bigint_expand(st.ss->alloc, &idx, sizeof(usize) / sizeof(bigint_unit) + 1);
  // no bounds checking, matching the lowerer; a zero-length array has nothing to check against.
  // the access register aliases the element the way a field access aliases a member, so it is the
  // element's storage rather than a cell holding its address
  irstate_find(st, c.d)->v.value =
      (u8 *)l.v.value +
      *(usize *)idx * Type_sizeof(type_at(ss_typeenv(st.ss), arr.t), ss_typeenv(st.ss));
}
void interpret_field errs(irstate st, ir_field c) {
  let l = ir_readthrough(st, c.l);
  let e = ss_typeenv(st.ss);
  let strct = TypeTree_inner_struct(ss_type(st.ss, l.t).t, e);
  if (!strct.start) return_err("FIELD_ACESS");
  let item = *type_struct_item(e, strct, c.i);
  // the access register aliases the member, so it is the member's storage rather than a cell
  // holding its address: a member whose type is a referance keeps that referance in storage and
  // unfolds on use, a plain member is the thing itself
  irstate_find(st, c.d)->v.value = (u8 *)l.v.value + item.offset;
}
void interpret_op errs(irstate st, ir inst) {
  tu_match_void(
      inst,
      (ir_if, /*    */ op, try_err(interpret_if, /*    */ (st, op))),
      (ir_while, /* */ op, try_err(interpret_while, /* */ (st, op))),
      (ir_set, /*   */ op, try_err(interpret_set, /*   */ (st, op))),
      (ir_call, /*  */ op, try_err(interpret_call, /*  */ (st, op))),
      (ir_block, /* */ op, try_err(interpret_block, /* */ (st, op))),
      (ir_field, /* */ op, try_err(interpret_field, /* */ (st, op))),
      (ir_farg, /*  */ op, try_err(interpret_farg, /*  */ (st, op))),
      (ir_ret, /*   */ op, try_err(interpret_ret, /*   */ (st, op))),
      (ir_break, /* */ op, try_err(interpret_break, /* */ (st, op))),
      (ir_cont, /*  */ op, try_err(interpret_cont, /*  */ (st, op))),
      (ir_rawop, /* */ op, try_err(interpret_rawop, /* */ (st, op))),
      (ir_getref, /**/ op, interpret_getref /**/ (st, op)),
      (ir_getptr, /**/ op, interpret_getptr /**/ (st, op)),
      (ir_putptr, /**/ op, interpret_putptr /**/ (st, op)),
      (ir_cast, /*  */ op, interpret_cast /*  */ (st, op)),
      (ir_array, /* */ op, interpret_array /* */ (st, op)),
  );
}

static inline bool try_reg_known errs(superscope *ss, scope *s, u32 idx) {
  let it = scope_find(ss, s, idx);
  let it_t = ss_type(ss, it->t);
  if (Type_is_comptime(it_t) && !it->v.value && Type_cmp(ss_typeenv(ss), it_t, ss_type(ss, ss->t_ast)))
    return_err("COMPTIME_UNKNOWN", false);
  return Type_is_comptime(it_t) && it->v.value;
}
bool try_interpret errs(superscope *ss, scope *s, ir inst) {
  irstate st = {.ss = ss, .s = s, .root = s, .sig = {}};
  let fold = tu_match_exp(
      inst,
      (ir_rawop, c, (bool)(try_err(try_reg_known, (ss, s, c.l), 0) && try_err(try_reg_known, (ss, s, c.r), 0))),
      (ir_set, c, (bool)(try_err(try_reg_known, (ss, s, c.l), 0) && !ss_type(ss, irstate_find(st, c.d)->t).isref)),
      (ir_call, c, ({
         let it = &ss->functions[c.l.fn - 1].instances[c.l.spec];
         if (!Type_is_comptime(it->ret)) return false;
         foreach (let a, span(c.args.ptr, c.args.len))
           if (!try_err(try_reg_known, (ss, s, *a), 0)) return false;
         true;
       })),
      (ir_cast, _, false),
      (ir_farg, _, false),
      (ir_getref, _, false),
      (ir_getptr, _, false),
      (ir_putptr, _, false),
      (ir_field, _, false),
      (ir_array, _, false),
      (ir_block, _, false),
      (ir_ret, _, false),
      (ir_if, _, false),
      (ir_while, _, false),
      (ir_break, _, false),
      (ir_cont, _, false),
  );
  if (!fold) return false;
  tu_match_void(
      inst,
      (ir_rawop, c, {
        let d = irstate_find(st, c.d);
        if (!Type_is_comptime(ss_type(ss, d->t)) && !d->v.value) irstate_mem(st, c.d);
      }),
      (default),
  );
  try_err(interpret_op, (st, inst), 0);
  return true;
}
typed_value interpret errs(superscope *ss, scope *s) {
  try_err(interpret_block, ((irstate){.ss = ss, .s = s, .root = s, .sig = {}}, s), (typed_value){});
  let result = msList_last(s->values);
  if (!result.v.value) return_err("YIELD_EMPTY", (typed_value){});
  return result;
}

#pragma once
#include "common.h"
#include "scope.h"
#include "wheels/macros.h"
#include "wheels/mytypes.h"
#include "wheels/print/print_pre.h"
#include "wheels/sList.h"
#include <string.h>
static const usize ir_no_frame = (usize)-1;
typedef struct {
  enum : u8 {
    ir_brk_normal = 0,
    ir_brk_break = 1,
    ir_brk_continue = 2,
    ir_brk_return = 3
  } *
      brk;
  // the caller's destination slot of a call and the frame that holds it, resolved when the call is
  // made: a scope has one frame per activation, so looking the slot up at return time could land in
  // the frame a nested call of the same function pushed
  typed_value *ret;
  usize retframe;
  allocfn retalloc; // TEMP probe
} irsig;
sliceDef(typed_value);
typedef struct {
  allocfn arena;
  scope *s;
  slice(typed_value) items;
} tstack;
typedef struct {
  superscope *ss;
  scope *s;
  irsig sig;
  msList(tstack) * stacks; // push and pop
  struct {
    slice(typeidx) ftype;
    typeof(value_store[/*ftype.len-1*/]) *args;
  } fnctx;
} irstate;

static inline irstate irs_init(superscope *ss, scope *s) {
  return (irstate){
      .ss = ss,
      .s = s,
      .stacks = avalue(ss->alloc, msList_init(ss->alloc, tstack)),
  };
}
static inline bool tstack_bigint_owned(superscope *ss, typed_value v) {
  let vt = ss_type(ss, v.t);
  return tu_is(c_int, vt.t) && !vt.ptr_level && !vt.isref;
}
// the innermost frame of a scope, or ir_no_frame. a frame is named by index because a push
// reallocates the stack and moves every frame in it
static inline usize irstate_frame_index_of(irstate *st, scope *s) {
  for (usize sl = msList_len(*st->stacks); sl--;)
    if ((*st->stacks)[sl].s == s) return sl;
  return ir_no_frame;
}
static inline tstack *irstate_frame_of(irstate *st, scope *s) {
  let sl = irstate_frame_index_of(st, s);
  return sl == ir_no_frame ? nullptr : &(*st->stacks)[sl];
}
static tstack tstack_enter(irstate *st, scope *s) {
  let ss = st->ss;
  let len = msList_len(s->values);
  // a module level scope runs once, so it can interpret straight into its own values: what it wrote
  // stays readable afterwards and the dump shows it. a scope that is already active is copied
  // instead, because one activation must not see another's values
  if (s->rootable && !irstate_frame_of(st, s))
    return (tstack){
        .items = (slice(typed_value)){.len = len, .ptr = s->values},
        .s = s,
        .arena = nullptr,
    };
  usize total = 0;
  foreach (let v, span(s->values, len))
    if (!tstack_bigint_owned(ss, *v))
      if_decl (let sz, Type_sizeof(ss_type(ss, v->t), ss_typeenv(ss)))
        total += lineup(sz, alignof(myAlign));
  total = total ?: 1;
  // one scratch arena per frame: the block, the items and the ints it copies are all freed at once
  let arena = arena_new_ext(ss->alloc, MAX$(total, 64));
  let items = len ? slice_alloc(arena, typed_value, len) : (slice(typed_value)){};
  if (len) memcpy(items.ptr, s->values, len * sizeof(typed_value));
  let mem = (u8 *)acreate(arena, u8[total]);
  usize offset = 0;
  foreach (let v, span(items.ptr, items.len)) {
    let vt = ss_type(ss, v->t);
    if (tstack_bigint_owned(ss, *v)) {
      if (v->v.c_int) v->v.c_int = BInt.copy(arena, v->v.c_int);
      continue;
    }
    if_decl (let sz, Type_sizeof(vt, ss_typeenv(ss))) {
      let src = v->v.value;
      v->v.value = mem + offset; // types, asts and functions are copied by value already
      if (src) memcpy(v->v.value, src, sz);
      offset += lineup(sz, alignof(myAlign));
    }
  }
  return (tstack){
      .items = items,
      .s = s,
      .arena = arena,
  };
}
static inline void irstate_enter(irstate *st, scope *s) {
  // the frame is built first on purpose: msList_push bumps the length while it evaluates the value
  // it stores, and building a frame reads the frames that are already there
  let frame = tstack_enter(st, s);
  msList_push(st->ss->alloc, *st->stacks, frame);
}
static inline void irstate_leave(irstate *st) {
  // a frame that ran in place owns nothing, the others hold only their arena, so leaving the scope
  // is one free
  let frame = msList_pop(*st->stacks);
  if (frame.arena) arena_cleanup(frame.arena);
}
static inline bool irstate_frame_is(irstate *st, scope *s) {
  return msList_len(*st->stacks) && msList_last(*st->stacks).s == s;
}
static typed_value *irstate_find_in(irstate irs, scope *s, u32 idx) {
  let owner = scope_owner(s, idx);
  if (owner)
    if_decl (let frame, irstate_frame_of(&irs, owner))
      return &frame->items.ptr[idx - owner->start - 1];
  return scope_find(irs.ss, s, idx);
}
static inline typed_value *irstate_find(irstate irs, u32 idx) { return irstate_find_in(irs, irs.s, idx); }

static inline void *irstate_mem_in(irstate st, scope *s, u32 idx) {
  let tv = irstate_find_in(st, s, idx);
  let size = Type_sizeof(ss_type(st.ss, tv->t), ss_typeenv(st.ss));
  // zero sized types have nothing to point at, so only a value that needs storage can be missing it
  if (size && !tv->v.value)
    assertMessage(
        false,
        "interpreting requires %s",
        snprint(stdAlloc, "{typed_value_full}", ((typed_value_full){.env = ss_typeenv(st.ss), .v = *tv})).ptr
    );
  return tv->v.value;
}
static inline void *irstate_mem(irstate st, u32 idx) { return irstate_mem_in(st, st.s, idx); }

// the arena of a frame, or the long lived one for a slot no frame holds. a module level frame runs
// in place and owns no arena, so it falls back too
static inline allocfn irstate_frame_alloc(irstate st, usize frame) {
  return frame == ir_no_frame ? st.ss->alloc : ((*st.stacks)[frame].arena ?: st.ss->alloc);
}
// the frame that holds a slot, whose storage it also pays for: the arena a write allocates from is
// the slot's own, and it is reclaimed with the frame
static inline usize irstate_slot_frame(irstate st, scope *s, u32 idx) {
  let owner = scope_owner(s, idx);
  return owner ? irstate_frame_index_of(&st, owner) : ir_no_frame;
}
static inline allocfn irstate_slot_alloc_in(irstate st, scope *s, u32 idx) {
  return irstate_frame_alloc(st, irstate_slot_frame(st, s, idx));
}
static inline allocfn irstate_slot_alloc(irstate st, u32 idx) { return irstate_slot_alloc_in(st, st.s, idx); }
// a value leaving its scope moves its storage into another allocator, because the arena of the
// frame it was made in goes away with that frame
static typed_value irstate_with_alloc(superscope *ss, allocfn alloc, typed_value v) {
  let t = ss_type(ss, v.t);
  if (tstack_bigint_owned(ss, v)) {
    if (v.v.c_int) v.v.c_int = BInt.copy(alloc, v.v.c_int);
  } else if_decl (let sz, Type_sizeof(t, ss_typeenv(ss))) {
    let mem = acreate(alloc, u8[sz]);
    if (v.v.value) memcpy(mem, v.v.value, sz);
    v.v.value = mem;
  }
  return v;
}
// out of the interpreter run entirely
static typed_value irstate_escape(superscope *ss, typed_value v) {
  return irstate_with_alloc(ss, ss->alloc, v);
}

// returns a typed value without isref
static typed_value ir_readthrough_in(irstate st, scope *s, u32 idx) {
  typed_value tv = *irstate_find_in(st, s, idx);
  Type t = ss_type(st.ss, tv.t);
  if (!t.isref) return tv;
  t.isref = false;

  return TV_with(st.ss, t, VS(*(void **)irstate_mem_in(st, s, idx)));
}
static inline typed_value ir_readthrough(irstate st, u32 idx) { return ir_readthrough_in(st, st.s, idx); }
static inline void *irstate_data_in(irstate st, scope *s, u32 idx) {
  typed_value tv = ir_readthrough_in(st, s, idx);
  return tv.v.value ? tv.v.value : irstate_mem_in(st, s, idx);
}
static inline void *irstate_data(irstate st, u32 idx) { return irstate_data_in(st, st.s, idx); }

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
void interpret_op errs(irstate st, ir inst);
void interpret_set_slot errs(irstate st, typed_value *d, allocfn dalloc, u32 l);
void interpret_ret errs(irstate st, ir_ret c) {
  if (!st.sig.ret || !st.sig.brk) return_err("RETURN_NOT_RETURNABLE");
  let v = ir_readthrough(st, c);
  let e = ss_typeenv(st.ss);
  if (getenv("DBGRET")) {
    let owner = scope_owner(st.s, c);
    fprintf(
        stderr,
        "[DBG] ret c=%u t=%u val=%p frames=%zu owner=%p ost=%u frame=%zd nvals=%u\n",
        c,
        (unsigned)v.t,
        v.v.value,
        msList_len(*st.stacks),
        (void *)owner,
        owner ? owner->start : 0,
        (ssize_t)(owner ? (ssize_t)irstate_frame_index_of(&st, owner) : -1),
        st.s ? (unsigned)msList_len(st.s->values) : 0
    );
  }
  if (!v.v.value) return_err("RETURN_UNKNOWN");
  // println("[LOG:ret] {typed_value_full} ", ((typed_value_full){v, ss_typeenv(st.ss)}));
  if (Type_cmp_value(ss_type(st.ss, v.t), ss_type(st.ss, st.sig.ret->t), e) &&
      Type_cmp(e, T(tu_of(c_any, {})), ss_type(st.ss, st.sig.ret->t)))
    return_err("RETURN_WRONG_TYPE"); // TODO: better coersion
  try_err(interpret_set_slot, (st, st.sig.ret, getenv("RETALLOC") ? st.sig.retalloc : irstate_frame_alloc(st, st.sig.retframe), c));
  *st.sig.brk = ir_brk_return;
}
void interpret_break errs(irstate st, ir_break c) { try_err(irsig_break, (&st.sig)); }
void interpret_cont errs(irstate st, ir_cont c) { try_err(irsig_continue, (&st.sig)); }
void interpret_set_slot errs(irstate st, typed_value *d, allocfn dalloc, u32 l) {
  let src = irstate_find(st, l);
  tu_match_void(
      ss_type(st.ss, d->t).t,
      (c_int, _, d->v.c_int = BInt.copy(dalloc, src->v.c_int)),
      (builtin, _, return_err("SET_BUILTIN")),
      (c_ast, _, d->v.c_ast = src->v.c_ast),
      (c_fun, _, d->v.c_fun = src->v.c_fun),
      (c_type, _, d->v.c_type = src->v.c_type),
      (default, {
        Type dt = ss_type(st.ss, d->t);
        // a destination that is a ref is written through, exactly like irstate_data reads it
        bool dref = dt.isref;
        dt.isref = false;
        // a zero sized value has nothing to copy, and its slot points nowhere
        if_decl (let sz, Type_sizeof(dt, ss_typeenv(st.ss)))
          memcpy(dref && d->v.value ? *(void **)d->v.value : d->v.value, irstate_data(st, l), sz);
      })
  );
}
void interpret_set errs(irstate st, ir_set c) {
  try_err(interpret_set_slot, (st, irstate_find(st, c.d), irstate_slot_alloc(st, c.d), c.l));
}
void interpret_cast(irstate st, ir_cast c) {
  let d = irstate_find(st, c.d);
  let dt = ss_type(st.ss, d->t);
  assertMessage(Type_isint(dt, ss_typeenv(st.ss)) || tu_is(r_bool, dt.t), "non-integer cast reached the interpreter");
  let slot_alloc = irstate_slot_alloc(st, c.d);
  let b = int_as_bigint(slot_alloc, ir_readthrough(st, c.l), ss_typeenv(st.ss));
  if (tu_is(c_int, dt.t)) {
    d->v.c_int = b;
    return;
  }
  typed_value t = *d;
  t.v.value = irstate_mem(st, c.d);
  if (tu_is(r_bool, dt.t)) *(u8 *)t.v.value = !!BInt.cmp(b, nullptr);
  else bigint_into_int(st.ss, slot_alloc, &t, b);
  d->v = t.v;
}
bool istrue errs(typed_value t, typeenv e) {
  if (Type_cmp(e, T(tu_of(r_bool, {})), type_at(e, t.t)))
    return_err("CONDITION_NOT_BOOL", false);
  return *(bool *)t.v.value;
}
void interpret_block errs(irstate st, ir_block c);
static typed_value interpret_scope errs(irstate *st, scope *s) {
  bool entered = !irstate_frame_is(st, s);
  if (entered) irstate_enter(st, s);
  defer {
    if (entered) irstate_leave(st);
  };
  st->s = s;
  try_err(interpret_block, (*st, s), (typed_value){});
  let result = *irstate_find(*st, s->start + msList_len(s->values));
  if (!result.v.value) return_err("YIELD_EMPTY", (typed_value){});
  return irstate_escape(st->ss, result);
}
void interpret_if errs(irstate st, ir_if c) {
  let cnd = try_err(interpret_scope, (&st, c.cnd));
  if (try_err(istrue, (cnd, ss_typeenv(st.ss))))
    try_err(interpret_block, (st, c.t));
  else try_err(interpret_block, (st, c.f));
}
void interpret_while errs(irstate st, ir_while c) {
  let outer = st.sig.brk;
  ptrstype(itypeof(irsig, brk)) brk = 0;
  st.sig.brk = &brk;
  while (true) {
    let cnd = try_err(interpret_scope, (&st, c.cnd));
    if (brk == ir_brk_return) break;
    if (!try_err(istrue, (cnd, ss_typeenv(st.ss)))) return;
    brk = ir_brk_normal;
    try_err(interpret_block, (st, c.body));
    if (brk == ir_brk_break) return;
    if (brk == ir_brk_continue) continue;
    if (brk == ir_brk_return) break;
  }
  if (outer) *outer = ir_brk_return;
}
void interpret_getptr(irstate st, ir_getptr c) { *(void **)irstate_mem(st, c.d) = irstate_data(st, c.l); }
void interpret_getref(irstate st, ir_getref c) { interpret_getptr(st, (ir_getptr){.l = c.l, .d = c.d}); }
void interpret_putptr(irstate st, ir_putptr c) {
  void *target = *(void **)irstate_mem(st, c.l);
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
  // the callee writes into the call's destination register directly, so the call keeps no result of
  // its own; the flow register says whether the callee reached a return at all. the destination is
  // resolved before the callee frame is pushed, so a recursive call cannot resolve it to its own
  ptrstype(itypeof(irsig, brk)) flow = ir_brk_normal;
  st.sig = (typeof(st.sig)){
      .brk = &flow,
      .ret = irstate_find(st, c.d),
      .retframe = irstate_slot_frame(st, st.s, c.d),
      .retalloc = irstate_slot_alloc(st, c.d),
  };

  // the callee's scope is shared and only read: the frame holds the per call values, and it has to
  // outlive the store into the destination, so it is entered here rather than by interpret_block
  irstate_enter(&st, it.spec);
  defer { irstate_leave(&st); };
  try_err(interpret_block, (st, it.spec));

  if (flow == ir_brk_break || flow == ir_brk_continue) return_err("BROKE_NOT_BREAKABLE");
  if (flow != ir_brk_return &&
      Type_cmp(ss_typeenv(st.ss), T(tu_of(r_void, {})), ss_type(st.ss, st.sig.ret->t)))
    return_err("NO_RETURN_NONVOID");
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
  let slot_alloc = irstate_slot_alloc(st, c.d);
  out = BInt.copy(slot_alloc, out);
  let d = irstate_find(st, c.d);
  bigint_into_int(st.ss, slot_alloc, d, out);
}
void interpret_block errs(irstate st, ir_block c) {
  if (!c) return;
  bool entered = !irstate_frame_is(&st, c);
  if (entered) irstate_enter(&st, c);
  defer {
    if (entered) irstate_leave(&st);
  };
  st.s = c;
  foreach (let i, span(c->instructions, msList_len(c->instructions))) {
    try_err(interpret_op, (st, *i));
    if (st.sig.brk && *st.sig.brk) return;
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
      (c_int, _, d->v.c_int = BInt.copy(irstate_slot_alloc(st, c.d), v.v.c_int)),
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
  irstate_find(st, c.d)->v.value =
      (u8 *)l.v.value + *(usize *)idx * Type_sizeof(type_at(ss_typeenv(st.ss), arr.t), ss_typeenv(st.ss));
}
void interpret_field errs(irstate st, ir_field c) {
  let l = ir_readthrough(st, c.l);
  let e = ss_typeenv(st.ss);
  let strct = TypeTree_inner_struct(ss_type(st.ss, l.t).t, e);
  if (!strct.start) return_err("FIELD_ACESS");
  let item = *type_struct_item(e, strct, c.i);
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
  irstate st = irs_init(ss, s);
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
  try_err(interpret_op, (st, inst), 0);
  return true;
}
typed_value interpret errs(superscope *ss, scope *s) {
  irstate st = irs_init(ss, s);
  return try_err(interpret_scope, (&st, s), (typed_value){});
}

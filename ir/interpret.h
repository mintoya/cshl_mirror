#pragma once
#include "common.h"
#include "scope.h"

sliceDef(typed_value);
typedef struct {
  usize pc;
  // corresponding scope
  const scope *s;
  // items copy,  last n items are functions arguments
  slice(typed_value) vals;
} intframe;
typedef struct {
  // stack
  msList(intframe) cs;
  // map of scope to the currently active value list
  mxmap(const scope *, msList(slice(typed_value))) vmap;
  // what is written to return
  typed_value *ret;
  superscope *ss;
} *intstate;

typed_value interpret errs(superscope *ss, scope *s);

//
//
//

static typed_value *intstate_reg(intstate st, const scope *_s, u32 idx);
static void *intstate_data(intstate st, intframe *f, u32 idx);
static void intstate_copy errs(intstate st, typed_value *from, typed_value *to);

//
//
//

static void interpret_ret errs(intstate st, intframe *f, u32 r) {
  if (!st->ret) return_err("RETURN_NOT_RETURNABLE");
  try_err(intstate_copy, (st, intstate_reg(st, f->s, r), st->ret));
}
static void interpret_set errs(intstate st, intframe *f, ir_set c) {
  try_err(intstate_copy, (st, intstate_reg(st, f->s, c.l), intstate_reg(st, f->s, c.d)));
}
static void interpret_farg errs(intstate st, intframe *f, ir_farg c) {
  let base = msList_len(f->s->values);
  if (base + c.l >= f->vals.len) return_err("FUNCTION_ARG_OOB");
  try_err(intstate_copy, (st, &f->vals.ptr[base + c.l], intstate_reg(st, f->s, c.d)));
}
static void interpret_getptr errs(intstate st, intframe *f, ir_getptr c) {
  *(void **)intstate_reg(st, f->s, c.d)->v.value = intstate_data(st, f, c.l);
}
static void interpret_getref errs(intstate st, intframe *f, ir_getref c) { try_err(interpret_getptr, (st, f, (ir_getptr){.l = c.l, .d = c.d})); }
static void interpret_putptr errs(intstate st, intframe *f, ir_putptr c) {
  void *target = *(void **)intstate_reg(st, f->s, c.l)->v.value;
  intstate_reg(st, f->s, c.d)->v.value = target;
}
static void interpret_field errs(intstate st, intframe *f, ir_field c) {
  let e = ss_typeenv(st->ss);
  let strct = TypeTree_inner_struct(ss_type(st->ss, intstate_reg(st, f->s, c.l)->t).t, e);
  if (!strct.start) return_err("FIELD_ACCESS");
  intstate_reg(st, f->s, c.d)->v.value = (u8 *)intstate_data(st, f, c.l) + type_struct_item(e, strct, c.i)->offset;
}
static void interpret_cast errs(intstate st, intframe *f, ir_cast c) {
  let d = intstate_reg(st, f->s, c.d);
  assertMessage(Type_is_integer_like(ss_type(st->ss, d->t), ss_typeenv(st->ss)), "non-integer cast reached the interpreter");
  let al = arena_new_ext(st->ss->alloc, 256);
  defer { arena_cleanup(al); };
  let src = *intstate_reg(st, f->s, c.l);
  src.v.value = intstate_data(st, f, c.l);
  let bits = int_as_bigint(al, src, ss_typeenv(st->ss));
  if (tu_is(c_int, ss_type(st->ss, d->t).t)) d->v.c_int = BInt.copy(st->ss->alloc, bits);
  else bigint_into_int(st->ss, st->ss->alloc, d, bits); // writes through the destination's storage
}
static void interpret_rawop errs(intstate st, intframe *f, ir_rawop c) {
  let al = arena_new_ext(st->ss->alloc, 1024);
  defer { arena_cleanup(al); };

  let left = *intstate_reg(st, f->s, c.r);
  left.v.value = intstate_data(st, f, c.r);
  let right = *intstate_reg(st, f->s, c.l);
  right.v.value = intstate_data(st, f, c.l);
  let lhs = int_as_bigint(al, left, ss_typeenv(st->ss));
  let rhs = int_as_bigint(al, right, ss_typeenv(st->ss));
  let out = switch_exp(
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
      (default, (assertMessage("unsupported_instruction"), (bigint) nullptr)),
  );
  assertMessage(out);
  bigint_into_int(st->ss, st->ss->alloc, intstate_reg(st, f->s, c.d), out);
}
static void interpret_array errs(intstate st, intframe *f, ir_array c) {
  let e = ss_typeenv(st->ss);
  let arr = TypeTree_inner_array(ss_type(st->ss, intstate_reg(st, f->s, c.l)->t).t, e);
  let al = arena_new_ext(st->ss->alloc, 128);
  defer { arena_cleanup(al); };
  let index = *intstate_reg(st, f->s, c.r);
  index.v.value = intstate_data(st, f, c.r);
  let at = int_as_bigint(al, index, e);

  bigint_expand(al, &at, sizeof(usize) / sizeof(bigint_unit) + 1);
  intstate_reg(st, f->s, c.d)->v.value =
      (u8 *)intstate_data(st, f, c.l) + *(usize *)at * Type_sizeof(type_at(e, arr.t), e);
}
static void interpret_call errs(intstate st, intframe *f, ir_call c) { return_err("UNIMPLEMENTED_CALL"); }
// }

static void intstate_push(intstate st, const scope *s, slice(typed_value) args) {
  usize len = msList_len(s->values);
  slice(typed_value) vals = (len + args.len) ? slice_alloc(st->ss->alloc, typed_value, len + args.len) : (slice(typed_value)){};
  if (len) memcpy(vals.ptr, s->values, len * sizeof(typed_value));
  if (args.len) memcpy(vals.ptr + len, args.ptr, args.len * sizeof(typed_value));

  let ml = mxmap_get(st->vmap, s);
  if (!ml) mxmap_set(st->vmap, s, msList_initArr(st->ss->alloc, (slice(typed_value)[]){vals}));
  else msList_push(st->ss->alloc, *ml, vals);

  msList_push(st->ss->alloc, st->cs, (intframe){.s = s, .vals = vals});
}
static void intstate_pop(intstate st) {
  let frame = msList_pop(st->cs);
  let list = *mxmap_get(st->vmap, frame.s);
  msList_pop(list);
  mxmap_set(st->vmap, frame.s, msList_len(list) ? list : nullptr);
  if (!msList_len(list)) msList_deInit(st->ss->alloc, list);

  if (frame.vals.ptr && frame.vals.ptr != frame.s->values) slice_free(st->ss->alloc, frame.vals);
}

static usize intstate_after_branch(intframe *f, usize from) {
  let ins = f->s->instructions;
  usize depth = 0;
  foreach (let i, span(from + 1, msList_len(ins) - from - 1)) {
    if (tu_is(ir_ifs, ins[i])) depth++;
    else if (tu_is(ir_ife, ins[i])) {
      if (!depth) return i + 1;
      depth--;
    } else if (tu_is(ir_ifd, ins[i]) && !depth) return i + 1;
  }
  assertMessage(false, "if without an end");
  return msList_len(ins);
}
static usize intstate_loop_head(intframe *f, usize from) {
  let ins = f->s->instructions;
  usize depth = 0;
  for (usize i = from; i--;) {
    if (tu_is(ir_loope, ins[i])) depth++;
    else if (tu_is(ir_loops, ins[i])) {
      if (!depth) return i + 1;
      depth--;
    }
  }
  assertMessage(false, "continue outside of a loop");
  return 0;
}
static usize intstate_leave_loop(intframe *f, usize from) {
  let ins = f->s->instructions;
  usize depth = 0;
  foreach (let i, span(from + 1, msList_len(ins) - from - 1)) {
    if (tu_is(ir_loops, ins[i])) depth++;
    else if (tu_is(ir_loope, ins[i])) {
      if (!depth) return i + 1;
      depth--;
    }
  }
  assertMessage(false, "break outside of a loop");
  return msList_len(ins);
}
static void intstate_run errs(intstate st) {
  while (true) {
    intframe *f = &msList_last(st->cs);
    if (f->pc >= msList_len(f->s->instructions)) return;
    usize at = f->pc;
    ir inst = f->s->instructions[at];
    f->pc++;
    tu_match_void(
        inst,
        (ir_blocks, _, ), // a wrapped child keeps its registers in this activation
        (ir_blocke, _, ),
        (ir_ife, /*  */ _, ),
        // a true condition runs the body, which the pc has already reached, a false one starts past it
        (ir_ifs, cond, if (!*(bool *)intstate_reg(st, f->s, cond)->v.value) f->pc = intstate_after_branch(f, at)),
        (ir_ifd, /*   */ _, f->pc = intstate_after_branch(f, at)),
        (ir_loops, /* */ _, ),
        (ir_loope, /* */ _, f->pc = intstate_loop_head(f, at)),
        (ir_break, /* */ _, f->pc = intstate_leave_loop(f, at)),
        (ir_cont, /*  */ _, f->pc = intstate_loop_head(f, at)),
        (ir_ret, /*   */ r, {
          try_err(interpret_ret, (st, f, r));
          f->pc = msList_len(f->s->instructions);
        }),
        (ir_set, /*   */ c, try_err(interpret_set, (st, f, c))),
        (ir_farg, /*  */ c, try_err(interpret_farg, (st, f, c))),
        (ir_getref, /**/ c, try_err(interpret_getref, (st, f, c))),
        (ir_getptr, /**/ c, try_err(interpret_getptr, (st, f, c))),
        (ir_putptr, /**/ c, try_err(interpret_putptr, (st, f, c))),
        (ir_field, /* */ c, try_err(interpret_field, (st, f, c))),
        (ir_cast, /*  */ c, try_err(interpret_cast, (st, f, c))),
        (ir_rawop, /* */ c, try_err(interpret_rawop, (st, f, c))),
        (ir_array, /* */ c, try_err(interpret_array, (st, f, c))),
        (ir_call, /*  */ c, try_err(interpret_call, (st, f, c))),
    );
  }
}

typed_value interpret errs(superscope *ss, scope *s) {
  intstate st = avalue(
      ss->alloc,
      ((ptrstype(intstate)){
          .cs = msList_init(ss->alloc, intframe),
          .vmap = mxmap_init(ss->alloc, const scope *, msList(slice(typed_value))),
          .ss = ss,
      })
  );
  defer {
    while (msList_len(st->cs))
      intstate_pop(st);
    mxmap_deinit(st->vmap);
    msList_deInit(ss->alloc, st->cs);
  };
  intstate_push(st, s, (slice(typed_value)){});
  intframe *entry = &msList_last(st->cs);
  typed_value *yield = &entry->vals.ptr[entry->vals.len - 1];
  st->ret = yield;
  try_err(intstate_run, (st), (typed_value){});
  if (!yield->v.value) return_err("YIELD_EMPTY", (typed_value){});
  return *yield;
}

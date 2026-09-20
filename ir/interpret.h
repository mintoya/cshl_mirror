#pragma once
#include "common.h"
#include "scope.h"
#include "wheels/print/print_pre.h"
extern ptrstype(outputFunction) sexplike;

sliceDef(typed_value);
typedef struct {
  usize pc;
  // corresponding scope
  const scope *s;
  // items copy,  last n items are functions arguments
  slice(typed_value) vals;
  slice(u8) store;
  typed_value *ret;
} intframe;
typedef struct {
  // stack
  msList(intframe) cs;
  mxmap(const scope *, msList(slice(typed_value))) vmap;
  allocfn scratch;
  superscope *ss;
} *intstate;
static allocfn intstate_scratch(intstate st) {
  if (!st->scratch) st->scratch = arena_new_ext(st->ss->alloc, 1024);
  else arena_clear(st->scratch);
  return st->scratch;
}
static void intstate_track(intstate st, const scope *s, slice(typed_value) vals) {
  let ml = mxmap_get(st->vmap, s);
  if (!ml || !*ml) mxmap_set(st->vmap, s, msList_initArr(st->ss->alloc, (slice(typed_value)[]){vals}));
  else msList_push(st->ss->alloc, *ml, vals);
}
static void intstate_untrack(intstate st, const scope *s) {
  let ml = mxmap_get(st->vmap, s);
  if (!ml || !*ml) return;
  let list = *ml;
  msList_pop(list);
  mxmap_set(st->vmap, s, msList_len(list) ? list : nullptr);
  if (!msList_len(list)) msList_deInit(st->ss->alloc, list);
}

typed_value interpret errs(superscope *ss, scope *s);
typed_value interpret_shared errs(superscope *ss, scope *s);

//
//
//

static intframe *intstate_push(intstate st, const scope *s, slice(typed_value) args);
static intframe *intstate_push_shared(intstate st, const scope *s);
static void intstate_pop(intstate st);
static void intstate_run errs(intstate st);

static typed_value *intstate_reg(intstate st, const scope *_s, u32 idx) {
  let owner = scope_owner((scope *)_s, idx);
  if (owner) {
    // nearly every access names a register of the frame that is running, so check that first
    if (msList_len(st->cs) && msList_last(st->cs).s == owner)
      return &msList_last(st->cs).vals.ptr[idx - owner->start - 1];
    let ml = mxmap_get(st->vmap, (const scope *)owner);
    if (ml && *ml && msList_len(*ml))
      return &msList_last(*ml).ptr[idx - owner->start - 1];
  }
  return scope_find(st->ss, (scope *)_s, idx);
}
static void *intstate_data(intstate st, intframe *f, u32 idx) {
  let tv = intstate_reg(st, f->s, idx);
  if (ss_type(st->ss, tv->t).isref) return tv->v.value ? *(void **)tv->v.value : nullptr;
  return tv->v.value;
}
static void intstate_copy errs(intstate st, typed_value *from, typed_value *to) {
  tu_match_void(
      ss_type(st->ss, to->t).t,
      (c_int, _, to->v.c_int = BInt.copy(st->ss->alloc, from->v.c_int)),
      (builtin, _, return_err("SET_BUILTIN")),
      (c_ast, _, to->v.c_ast = from->v.c_ast),
      (c_fun, _, to->v.c_fun = from->v.c_fun),
      (c_type, _, to->v.c_type = from->v.c_type),
      (default, {
        Type dt = ss_type(st->ss, to->t);
        bool dref = dt.isref;
        dt.isref = false;
        void *src = ss_type(st->ss, from->t).isref
                        ? (from->v.value ? *(void **)from->v.value : nullptr)
                        : from->v.value;
        void *dst = dref ? (to->v.value ? *(void **)to->v.value : nullptr) : to->v.value;
        if (dref && !dst) {
          if (to->v.value) memcpy(to->v.value, &src, sizeof(src));
        } else if (dst && src) {
          if_decl (let sz, Type_sizeof(dt, ss_typeenv(st->ss)))
            memcpy(dst, src, sz);
        }
      })
  );
}

//
//
//

static void interpret_ret errs(intstate st, intframe *f, u32 r) {
  if (!f->ret) return_err("RETURN_NOT_RETURNABLE");
  try_err(intstate_copy, (st, intstate_reg(st, f->s, r), f->ret));
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
  *(void **)intstate_reg(st, f->s, c.d)->v.value = target;
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
  let al = intstate_scratch(st);
  let src = *intstate_reg(st, f->s, c.l);
  src.v.value = intstate_data(st, f, c.l);
  let bits = int_as_bigint(al, src, ss_typeenv(st->ss));
  if (tu_is(c_int, ss_type(st->ss, d->t).t)) d->v.c_int = BInt.copy(st->ss->alloc, bits);
  else bigint_into_int(st->ss, st->ss->alloc, d, bits); // writes through the destination's storage
}
static bigint bigint_xor(allocfn allocator, bigint a, bigint b) {
  return BInt.sub(allocator, BInt.bit.bor(allocator, a, b), BInt.bit.band(allocator, a, b));
}
static void interpret_rawop errs(intstate st, intframe *f, ir_rawop c) {
  let al = intstate_scratch(st);

  let left = *intstate_reg(st, f->s, c.l);
  left.v.value = intstate_data(st, f, c.l);
  let right = *intstate_reg(st, f->s, c.r);
  right.v.value = intstate_data(st, f, c.r);
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
      (enop_bxor /**/, bigint_xor(al, lhs, rhs)),
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
  let al = intstate_scratch(st);
  let index = *intstate_reg(st, f->s, c.r);
  index.v.value = intstate_data(st, f, c.r);
  let at = int_as_bigint(al, index, e);

  bigint_expand(al, &at, sizeof(usize) / sizeof(bigint_unit) + 1);
  intstate_reg(st, f->s, c.d)->v.value =
      (u8 *)intstate_data(st, f, c.l) + *(usize *)at * Type_sizeof(type_at(e, arr.t), e);
}
static void interpret_call errs(intstate st, intframe *f, ir_call c) {
  let fnv = intstate_reg(st, f->s, c.l);
  if (!tu_is(c_fun, ss_type(st->ss, fnv->t).t)) return_err("CALL_NOT_FUNCTION");
  u32 fn = fnv->v.c_fun.fn;
  u32 specn = fnv->v.c_fun.spec;
  if (!fn) return_err("CALL_NOT_FUNCTION");
  scope *spec = st->ss->functions[fn - 1].instances[specn].spec;

  usize nargs = c.e > c.r ? (usize)(c.e - c.r) : 0;
  slice(typed_value) args = nargs ? slice_alloc(st->ss->alloc, typed_value, nargs) : (slice(typed_value)){};
  defer {
    if (nargs) slice_free(st->ss->alloc, args);
  };
  foreach (let i, range(0, nargs))
    args.ptr[i] = f->vals.ptr[c.r + i];
  intstate_push(st, spec, args)->ret = intstate_reg(st, f->s, c.d);
}

static intframe *intstate_push(intstate st, const scope *s, slice(typed_value) args) {
  usize len = msList_len(s->values);
  usize total = len + args.len;
  slice(typed_value) vals = total ? slice_alloc(st->ss->alloc, typed_value, total) : (slice(typed_value)){};
  if (len) memcpy(vals.ptr, s->values, len * sizeof(typed_value));
  if (args.len) memcpy(vals.ptr + len, args.ptr, args.len * sizeof(typed_value));

  usize olds_size = total ? lineup(total * sizeof(u64), alignof(myAlign)) : 0;
  usize store_size = 0;
  foreach (let i, range(0, total)) {
    typed_value *v = &vals.ptr[i];
    if (Type_is_comptime(ss_type(st->ss, v->t))) continue;
    if_decl (let sz, Type_sizeof(ss_type(st->ss, v->t), ss_typeenv(st->ss)))
      store_size += lineup(sz, alignof(myAlign));
  }
  slice(u8) store = (olds_size + store_size) ? slice_alloc(st->ss->alloc, u8, olds_size + store_size) : (slice(u8)){};
  u64 *olds = (u64 *)store.ptr;
  u8 *cursor = store.ptr + olds_size;
  foreach (let i, range(0, total)) {
    typed_value *v = &vals.ptr[i];
    if (olds) olds[i] = 0;
    if (Type_is_comptime(ss_type(st->ss, v->t))) continue;
    if_decl (let sz, Type_sizeof(ss_type(st->ss, v->t), ss_typeenv(st->ss))) {
      let old = v->v.value;
      void *fresh = nullptr;
      if (old)
        foreach (let j, range(0, i))
          if (olds[j] == (u64)(uintptr_t)old) {
            fresh = vals.ptr[j].v.value;
            break;
          }
      if (!fresh) {
        fresh = cursor;
        cursor += lineup(sz, alignof(myAlign));
        if (old) memcpy(fresh, old, sz);
      }
      if (olds) olds[i] = (u64)(uintptr_t)old;
      v->v.value = fresh;
    }
  }

  intstate_track(st, s, vals);
  msList_push(st->ss->alloc, st->cs, (intframe){.s = s, .vals = vals, .store = store});
  return &msList_last(st->cs);
}
static intframe *intstate_push_shared(intstate st, const scope *s) {
  slice(typed_value) vals = {.ptr = (typed_value *)s->values, .len = msList_len(s->values)};
  intstate_track(st, s, vals);
  msList_push(st->ss->alloc, st->cs, (intframe){.s = s, .vals = vals});
  return &msList_last(st->cs);
}
static void intstate_pop(intstate st) {
  let frame = msList_pop(st->cs);
  intstate_untrack(st, frame.s);
  if (frame.vals.ptr && frame.vals.ptr != frame.s->values) slice_free(st->ss->alloc, frame.vals);
  if (frame.store.ptr) slice_free(st->ss->alloc, frame.store);
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
    if (f->pc >= msList_len(f->s->instructions)) {
      if (msList_len(st->cs) == 1) return;
      intstate_pop(st);
      continue;
    }
    usize at = f->pc;
    ir inst = f->s->instructions[at];
    f->pc++;
    tu_match_void(
        inst,
        (ir_blocks, _, ),
        (ir_blocke, _, ),
        (ir_ife, _, ),
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

static intstate intstate_init(allocfn alloc, superscope *ss) {
  return avalue(
      alloc,
      ((ptrstype(intstate)){
          .cs = msList_init(alloc, intframe),
          .vmap = mxmap_init(alloc, const scope *, msList(slice(typed_value))),
          .ss = ss,
      })
  );
}
static void intstate_deInit(intstate st) {
  while (msList_len(st->cs))
    intstate_pop(st);
  mxmap_deinit(st->vmap);
  if (st->scratch) arena_cleanup(st->scratch);
  msList_deInit(st->ss->alloc, st->cs);
}

static typed_value interpret_retrieve(superscope *ss, typed_value out) {
  if (ss_type(ss, out.t).isref) {
    Type t = ss_type(ss, out.t);
    t.isref = false;
    out.t = ss_type_intern(ss, t);
    out.v.value = out.v.value ? *(void **)out.v.value : nullptr;
  }
  if (tu_is(c_int, ss_type(ss, out.t).t) && out.v.c_int)
    out.v.c_int = BInt.copy(ss->alloc, out.v.c_int);
  else if_decl (let sz, Type_sizeof(ss_type(ss, out.t), ss_typeenv(ss))) {
    let mem = acreate(ss->alloc, u8[sz]);
    if (out.v.value) memcpy(mem, out.v.value, sz);
    out.v.value = mem;
  }
  return out;
}

static typed_value interpret_into errs(superscope *ss, scope *s, typed_value *ret) {
  intstate st = intstate_init(ss->alloc, ss);
  defer { intstate_deInit(st); };
  intstate_push(st, s, (slice(typed_value)){})->ret = ret;
  try_err(intstate_run, (st), (typed_value){});
  if (!ret->v.value) {
    println("[INTO] empty ret type={Type_full}", ((Type_full){ss_type(ss, ret->t), ss_typeenv(ss)}));
    print_wf(sexplike, "[INTO] {scope_full}\n", ((scope_full){(scope *)s, ss}));
  }
  return interpret_retrieve(ss, *ret);
}

static typed_value interpret_impl errs(superscope *ss, scope *s, bool own_registers) {
  intstate st = intstate_init(ss->alloc, ss);
  defer { intstate_deInit(st); };
  intframe *entry = own_registers ? intstate_push_shared(st, s)
                                  : intstate_push(st, s, (slice(typed_value)){});
  typed_value *yield = &entry->vals.ptr[entry->vals.len - 1];
  entry->ret = yield;
  try_err(intstate_run, (st), (typed_value){});
  if (!yield->v.value && Type_sizeof(ss_type(ss, yield->t), ss_typeenv(ss)))
    return_err("YIELD_EMPTY", (typed_value){});
  return interpret_retrieve(ss, *yield);
}
typed_value interpret errs(superscope *ss, scope *s) {
  return try_err(interpret_impl, (ss, s, false), (typed_value){});
}
typed_value interpret_shared errs(superscope *ss, scope *s) {
  return try_err(interpret_impl, (ss, s, true), (typed_value){});
}

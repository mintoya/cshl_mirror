#pragma once
#include "../cshl.lib.h" // the bit run helpers the C output uses, so both read and write bits the same way
#include "common.h"
#include "scope.h"
#include "simplify.h"
#include "wheels/sList.h"
extern ptrstype(outputFunction) sexplike;
sliceDef(typed_value);

static inline bigint bigint_xor(allocfn allocator, bigint a, bigint b) {
  return BInt.sub(
      allocator,
      BInt.bit.bor(allocator, a, b),
      BInt.bit.band(allocator, a, b)
  );
}

typedef struct {
  usize pc;
  const scope *sp;
  value_store (*vals)[];
  u32 framesize;
  typeidx rt;
} intframe;

typedef struct {
  msList(intframe) cs;
  mxmap(const scope *, msList(itypeof(intframe, vals))) vmap;
  superscope *ss;
  allocfn temp;
  allocfn scratch;
  allocfn reads; // Materialized operands live until the next instruction.
  enum {
    intstate_normal,
    intstate_return,
  } rsig;
} *intstate;

static value_store *intstate_findslots_value(intstate is, u32 reg) {
  const scope *o = scope_owner(msList_last(is->cs).sp, reg);
  usize i = reg - o->start - 1;
  value_store *vs = &o->values[i].v;
  if_decl (let f, mxmap_get(is->vmap, o))
    vs = &(*msList_last(*f))[i];
  return vs;
}
typedef struct {
  typed_value value;
  dref storage;
} intplace;

static intplace intstate_place(intstate is, typed_value r) {
  let t = ss_type(is->ss, r.t);
  dref storage = {.ptr = r.v.value};
  if (t.ref_kind == ref_dref) storage = *(dref *)r.v.value;
  else if (t.ref_kind) storage.ptr = *(void **)r.v.value;
  if (t.ref_kind)
    r.t = ss_type_intern(is->ss, P$(t, ($.ref_kind = ref_none, $)));
  r.v.value = storage.ptr;
  return (intplace){r, storage};
}

static typed_value intstate_value(intstate is, typed_value r) {
  let p = intstate_place(is, r);
  r = p.value;
  if (p.storage.width) {
    let ref = p.storage;
    let t = ss_type(is->ss, r.t);
    usize size = Type_sizeof(t, ss_typeenv(is->ss));
    u8 *bits = *acreate(is->reads, u8[size]);
    memset(bits, 0, size);
    cshl_bits_load(ref, bits);
    if (tu_is(r_sint, Type_stripTag(t, ss_typeenv(is->ss)).t) &&
        (bits[(ref.width - 1) / 8] & (1u << ((ref.width - 1) % 8)))) {
      if (ref.width % 8) bits[ref.width / 8] |= (u8)(0xffu << (ref.width % 8));
      usize bytes = (ref.width + 7) / 8;
      memset(bits + bytes, 0xff, size - bytes);
    }
    r.v.value = bits;
  }
  return r;
}
static typed_value intstate_reg(intstate is, u32 reg) {
  return (typed_value){
      .t = scope_find(is->ss, msList_last(is->cs).sp, reg)->t,
      .v = *intstate_findslots_value(is, reg),
  };
}
static void intstate_store(intstate is, intplace d, typed_value value) {
  if (d.storage.width)
    cshl_bits_store(d.storage, value.v.value);
  else if_decl (let size, Type_sizeof(ss_type(is->ss, d.value.t), ss_typeenv(is->ss)))
    memcpy(d.storage.ptr, value.v.value, size);
}
typed_value intstate_findreg(intstate is, u32 reg) {
  let slot = intstate_findslots_value(is, reg);
  let t = scope_find(is->ss, msList_last(is->cs).sp, reg)->t;
  return intstate_value(is, (typed_value){.t = t, .v = *slot});
}
intstate intstate_new(superscope *ss) {
  return avalue(
      ss->alloc,
      ((ptrstype(intstate)){
          .vmap = mxmap_init(ss->alloc, const scope *, msList(itypeof(intframe, vals))),
          .cs = msList_init(ss->alloc, intframe),
          .ss = ss,
          .temp = arena_new_ext(ss->alloc, 1024),
          .reads = arena_new_ext(ss->alloc, 1024),
      })
  );
}
void intstate_pushs(intstate is, const scope *s, slice(typed_value) fnargs, typeidx retT) {
  let npl = P$(mxmap_get(is->vmap, s), ($ ? *$ : msList_init(is->temp, itypeof(intframe, vals))));
  defer { mxmap_set(is->vmap, s, npl); };

  usize sn = msList_len(s->values);
  usize argc = s->instance.fn ? is->ss->functions[s->instance.fn - 1].params.len : fnargs.len;
  usize n = sn + argc + 1;
  usize framesize = 0;

  foreach (let i, range(0, sn)) {
    let t = ss_type(is->ss, s->values[i].t);
    if (!Type_is_comptime(t))
      framesize += lineup(Type_sizeof(t, ss_typeenv(is->ss)), alignof(myAlign));
  }
  foreach (let i, range(0, fnargs.len)) {
    let t = ss_type(is->ss, fnargs.ptr[i].t);
    if (!Type_is_comptime(t))
      framesize += lineup(Type_sizeof(t, ss_typeenv(is->ss)), alignof(myAlign));
  }
  if (retT) {
    let t = ss_type(is->ss, retT);
    if (!Type_is_comptime(t))
      framesize += lineup(Type_sizeof(t, ss_typeenv(is->ss)), alignof(myAlign));
  }

  itypeof(intframe, vals) vals = (typeof(vals))acreate(is->temp, value_store[n]);
  u8 *store = framesize ? *acreate(is->temp, u8[framesize]) : nullptr;
  usize off = 0;

  foreach (let i, range(0, sn)) {
    let src = s->values[i];
    let t = ss_type(is->ss, src.t);

    if (Type_is_comptime(t)) {
      (*vals)[i] = src.v;
    } else {
      (*vals)[i].value = store + off;
      usize sz = Type_sizeof(t, ss_typeenv(is->ss));
      if (sz && src.v.value) memcpy((*vals)[i].value, src.v.value, sz);
      off += lineup(Type_sizeof(t, ss_typeenv(is->ss)), alignof(myAlign));
    }
  }

  foreach (let i, range(0, fnargs.len)) {
    let src = fnargs.ptr[i];
    let t = ss_type(is->ss, src.t);
    usize di = sn + i;

    if (Type_is_comptime(t)) {
      (*vals)[di] = src.v;
    } else {
      usize sz = Type_sizeof(t, ss_typeenv(is->ss));
      (*vals)[di].value = store + off;
      memcpy((*vals)[di].value, src.v.value, sz);
      off += lineup(sz, alignof(myAlign));
    }
  }

  if (retT) {
    let t = ss_type(is->ss, retT);
    usize di = n - 1;

    if (Type_is_comptime(t))
      (*vals)[di] = (value_store){};
    else
      (*vals)[di].value = store + off;
  }

  msList_push(is->temp, npl, vals);
  msList_push(is->ss->alloc, is->cs, ((intframe){.pc = 0, .sp = s, .vals = vals, .framesize = framesize}));
}
typed_value intstate_pop(intstate is) {
  let f = msList_last(is->cs);
  let s = f.sp;
  usize sn = msList_len(s->values);

  typed_value r;

  if (s->instance.fn) {
    let fn = &is->ss->functions[s->instance.fn - 1];
    let inst = &fn->instances[s->instance.index];

    usize argc = fn->params.len;

    r = (typed_value){
        .t = f.rt ? f.rt : ss_type_intern(is->ss, inst->ret),
        .v = (*f.vals)[sn + argc],
    };
  } else {
    r = (typed_value){
        .t = s->values[sn - 1].t,
        .v = (*f.vals)[sn - 1],
    };
  }

  let frames = mxmap_get(is->vmap, s);
  msList_pop(*frames);
  msList_pop(is->cs);

  return r;
}
static allocfn intstate_scratch(intstate is) {
  if (!is->scratch) is->scratch = arena_new_ext(is->ss->alloc, 1024);
  else arena_clear(is->scratch);
  return is->scratch;
}
void interpret_ret errs(intstate is, ir_ret r) {
  let v = intstate_findreg(is, r);
  let s = msList_last(is->cs).sp;
  let fn = &is->ss->functions[s->instance.fn - 1];
  usize argc = fn->params.len;
  if (fn->instances[s->instance.index].ret.ref_kind) {
    let slot = intstate_findslots_value(is, r);
    v = (typed_value){.t = scope_find(is->ss, s, r)->t, .v = *slot};
  }
  let dst = &(*msList_last(is->cs).vals)[msList_len(s->values) + argc];
  let t = ss_type(is->ss, v.t);
  if (Type_is_comptime(t)) *dst = v.v;
  else if_decl (let size, Type_sizeof(t, ss_typeenv(is->ss)))
    memcpy(dst->value, v.v.value, size);
  msList_last(is->cs).rt = v.t;
  is->rsig = intstate_return;
}
static usize intstate_loop_seek(intframe *f, usize at, isize inc, isize depth) {
  let ins = f->sp->instructions;

  for (isize i = (isize)at + inc;
       i >= 0 && i < (isize)msList_len(ins);
       i += inc) {
    if (tu_is(ir_loops, ins[i]))
      depth++;
    else if (tu_is(ir_loope, ins[i]))
      depth--;
    if (!depth)
      return (usize)i + 1;
  }
  assertMessage(false, "should be unreachable");
}
void interpret_set errs(intstate is, ir_set c) {
  let l = intstate_findreg(is, c.l);
  let place = intstate_place(is, intstate_reg(is, c.d));
  let d = place.value;

  let slot = intstate_findslots_value(is, c.d);
  tu_match_void(
      ss_type(is->ss, d.t).t,
      (builtin, _, return_err("SET_BUILTIN")),
      (c_int, _, slot->c_int = BInt.copy(is->ss->alloc, l.v.c_int)),
      (c_ast, _, slot->c_ast = l.v.c_ast),
      (c_inlineast, _, slot->c_ast = l.v.c_ast),
      (c_fun, _, slot->c_fun = l.v.c_fun),
      (c_type, _, slot->c_type = l.v.c_type),
      (default, {
        intstate_store(is, place, l);
      })
  );
}
void interpret_getptr errs(intstate is, ir_getptr c) {
  let l = intstate_findreg(is, c.l);
  let d = intstate_findreg(is, c.d);

  *(void **)d.v.value = l.v.value;
}
typed_value_full ss_tvf(superscope *ss, typed_value tv) {
  return (typed_value_full){
      .env = ss_typeenv(ss),
      .v = tv,
  };
}
void interpret_putptr errs(intstate is, ir_putptr c) {
  let l = intstate_findreg(is, c.l);
  let d = intstate_findslots_value(is, c.d);
  *(void **)d->value = *(void **)l.v.value;
}
void interpret_getref errs(intstate is, ir_getref c) {
  let l = intstate_reg(is, c.l);
  let t = ss_type(is->ss, l.t);
  let d = intstate_findslots_value(is, c.d);
  if (t.ref_kind) memcpy(d->value, l.v.value, Type_sizeof(t, ss_typeenv(is->ss)));
  else *(void **)d->value = l.v.value;
}
void interpret_field errs(intstate is, ir_field c) {
  let l = intstate_findreg(is, c.l);
  let lt = ss_type(is->ss, l.t);

  let e = ss_typeenv(is->ss);
  let strct = TypeTree_inner_struct(lt.t, e);
  let bf = TypeTree_inner_bitfield(lt.t, e);
  if (bf.start) {
    u32 offset = 0;
    foreach (let i, range(0, c.i)) offset += type_bitf_item(e, bf, i)->width;
    *(dref *)intstate_findslots_value(is, c.d)->value =
        (dref){(u8 *)l.v.value + offset / 8, offset % 8, type_bitf_item(e, bf, c.i)->width};
    return;
  }
  if (!strct.start) return_err("FIELD_ACCESS");

  let d = intstate_findslots_value(is, c.d);
  let member = type_struct_item(e, strct, c.i);
  void *address = (u8 *)l.v.value + member->offset;
  if (member->t.ref_kind) d->value = address;
  else *(void **)d->value = address;
}

void interpret_cast errs(intstate is, ir_cast c) {
  let d = intstate_findslots_value(is, c.d);
  let dt = ss_type(is->ss, scope_find(is->ss, msList_last(is->cs).sp, c.d)->t);

  assertMessage(
      Type_is_integer_like(dt, ss_typeenv(is->ss)),
      "non-integer cast reached the interpreter"
  );

  let al = intstate_scratch(is);

  let l = intstate_findreg(is, c.l);

  let bits = int_as_bigint(al, l, ss_typeenv(is->ss));
  let dv = d;

  if (tu_is(c_int, dt.t))
    dv->c_int = BInt.copy(is->ss->alloc, bits);
  else {
    typed_value out = intstate_findreg(is, c.d);
    bigint_into_int(is->ss, is->ss->alloc, &out, bits);
  }
}

void interpret_rawop errs(intstate is, ir_rawop c) {
  let al = intstate_scratch(is);

  let l = intstate_findreg(is, c.l);
  let r = intstate_findreg(is, c.r);

  let lhs = int_as_bigint(al, l, ss_typeenv(is->ss));
  let rhs = int_as_bigint(al, r, ss_typeenv(is->ss));

  let out = switch_exp(
      op_as_enum(c.o),
      (enop_mul, BInt.mul(al, lhs, rhs)),
      (enop_div, BInt.div(al, lhs, rhs).div),
      (enop_mod, BInt.div(al, lhs, rhs).mod),
      (enop_plus, BInt.add(al, lhs, rhs)),
      (enop_minus, BInt.sub(al, lhs, rhs)),
      (enop_shl, BInt.bit.shift(al, lhs, rhs)),
      (enop_shr, BInt.bit.shift(al, lhs, BInt.negate(al, rhs))),
      (enop_oband, BInt.bit.band(al, lhs, rhs)),
      (enop_obor, BInt.bit.bor(al, lhs, rhs)),
      (enop_bxor, bigint_xor(al, lhs, rhs)),
      (enop_oand, BInt.from.i64(al, !!(BInt.cmp(lhs, nullptr) && BInt.cmp(rhs, nullptr)))),
      (enop_oor, BInt.from.i64(al, !!(BInt.cmp(lhs, nullptr) || BInt.cmp(rhs, nullptr)))),
      (enop_eq, BInt.from.i64(al, !BInt.cmp(lhs, rhs))),
      (enop_neq, BInt.from.i64(al, !!BInt.cmp(lhs, rhs))),
      (enop_les, BInt.from.i64(al, BInt.cmp(lhs, rhs) < 0)),
      (enop_mor, BInt.from.i64(al, BInt.cmp(lhs, rhs) > 0)),
      (enop_leq, BInt.from.i64(al, BInt.cmp(lhs, rhs) <= 0)),
      (enop_moq, BInt.from.i64(al, BInt.cmp(lhs, rhs) >= 0)),
      (default, (assertMessage(false, "unsupported_instruction"), (bigint) nullptr)),
  );

  let d = intstate_findslots_value(is, c.d);
  let dt = ss_type(is->ss, scope_find(is->ss, msList_last(is->cs).sp, c.d)->t);
  let dv = d;

  if (tu_is(c_int, dt.t))
    dv->c_int = BInt.copy(is->ss->alloc, out);
  else {
    typed_value dst = intstate_findreg(is, c.d);
    bigint_into_int(is->ss, is->ss->alloc, &dst, out);
  }
}

void interpret_array errs(intstate is, ir_array c) {
  let e = ss_typeenv(is->ss);

  let l = intstate_findreg(is, c.l);
  let r = intstate_findreg(is, c.r);

  let lt = ss_type(is->ss, l.t);
  let rt = ss_type(is->ss, r.t);

  let arr = TypeTree_inner_array(lt.t, e);

  let al = intstate_scratch(is);
  let at = int_as_bigint(al, r, e);

  bigint_expand(
      al,
      &at,
      sizeof(usize) / sizeof(bigint_unit) + 1
  );

  let d = intstate_findslots_value(is, c.d);
  *(void **)d->value =
      (u8 *)l.v.value +
      *(usize *)at * Type_sizeof(Type_at(e, arr.t), e);
}
static usize intstate_seek(
    intframe *f,
    usize at,
    isize inc,
    isize depth,
    u8 start,
    u8 middle,
    u8 end
) {
  let ins = f->sp->instructions;

  for (isize i = (isize)at + inc;
       i >= 0 && i < (isize)msList_len(ins);
       i += inc) {
    depth += (ins[i].tag == start) - (ins[i].tag == end);

    if (!depth)
      return (usize)i + 1;

    if (middle && depth == (inc > 0 ? 1 : -1) && ins[i].tag == middle)
      return (usize)i + 1;
  }

  assertMessage(false, "unmatched control flow");
  return 0;
}
void interpret_farg errs(intstate is, ir_farg c) {
  let f = &msList_last(is->cs);
  let d = intstate_findslots_value(is, c.d);
  let l = &(*f->vals)[msList_len(f->sp->values) + c.l];
  let dv = d;

  tu_match_void(
      ss_type(is->ss, scope_find(is->ss, msList_last(is->cs).sp, c.d)->t).t,
      (c_int, _, dv->c_int = BInt.copy(is->ss->alloc, l->c_int)),
      (builtin, _, return_err("SET_BUILTIN")),
      (c_ast, _, dv->c_ast = l->c_ast),
      (c_inlineast, _, dv->c_ast = l->c_ast),
      (c_fun, _, dv->c_fun = l->c_fun),
      (c_type, _, dv->c_type = l->c_type),
      (default, {
        Type dt = ss_type(is->ss, scope_find(is->ss, msList_last(is->cs).sp, c.d)->t);
        if_decl (let sz, Type_sizeof(dt, ss_typeenv(is->ss)))
          memcpy(dv->value, l->value, sz);
      })
  );
}
void interpret_call errs(intstate is, ir_call c) {
  let fr = intstate_findreg(is, c.l);
  let fv = fr.v.c_fun;

  let fn = &is->ss->functions[fv.fn - 1];
  let inst = &fn->instances[fv.spec];

  usize nargs = c.e - c.r;
  typed_value *args =
      nargs ? *acreate(is->temp, typed_value[nargs]) : nullptr;

  foreach (let i, range(0, nargs)) {
    let v = intstate_findslots_value(is, c.r + i);
    args[i] = (typed_value){
        .t = scope_find(is->ss, msList_last(is->cs).sp, c.r + i)->t,
        .v = *v,
    };
  }

  typeidx retT =
      tu_is(r_null, inst->ret.t)
          ? 0
          : ss_type_intern(is->ss, inst->ret);

  intstate_pushs(
      is,
      inst->spec,
      (slice(typed_value)){.ptr = args, .len = nargs},
      retT
  );
}
static void intstate_inst errs(intstate is, usize at, ir inst) {
  let f = &msList_last(is->cs);

  tu_match_void(
      inst,

      (ir_ife, _, ),
      (ir_loops, _, ),
      (ir_blocke, _, ),
      (ir_blocks, _, ),
      (ir_ifs, cond, {
        let v = intstate_findreg(is, cond);
        if (!*(bool *)v.v.value) f->pc = intstate_seek(f, at, 1, 1, ir_ifs_enum, ir_ifd_enum, ir_ife_enum);
      }),
      (ir_ifd /*  */, _, f->pc = intstate_seek(f, at, +1, +1, ir_ifs_enum, 0, ir_ife_enum)),
      (ir_loope /**/, _, f->pc = intstate_seek(f, at, -1, -1, ir_loops_enum, 0, ir_loope_enum)),
      (ir_cont /* */, _, f->pc = intstate_seek(f, at, -1, -1, ir_loops_enum, 0, ir_loope_enum)),
      (ir_break /**/, _, f->pc = intstate_seek(f, at, +1, +1, ir_loops_enum, 0, ir_loope_enum)),
      (ir_ret /*   */, c, try_err(interpret_ret, (is, c))),
      (ir_set /*   */, c, try_err(interpret_set, (is, c))),
      (ir_farg /*  */, c, try_err(interpret_farg, (is, c))),
      (ir_cast /*  */, c, try_err(interpret_cast, (is, c))),
      (ir_field /* */, c, try_err(interpret_field, (is, c))),
      (ir_rawop /* */, c, try_err(interpret_rawop, (is, c))),
      (ir_array /* */, c, try_err(interpret_array, (is, c))),
      (ir_getref /**/, c, try_err(interpret_getref, (is, c))),
      (ir_getptr /**/, c, try_err(interpret_getptr, (is, c))),
      (ir_putptr /**/, c, try_err(interpret_putptr, (is, c))),
      (ir_call, c, try_err(interpret_call, (is, c))),
      (ir_rawdot /**/, _, assertMessage(false, "unreachable")),
      (default, assertMessage(false, "unhandled instruction")),
  );
}

static void intstate_run errs(intstate is) {
  while (true) {
    let f = &msList_last(is->cs);
    if (is->rsig == intstate_return || f->pc >= msList_len(f->sp->instructions)) {
      if (msList_len(is->cs) == 1) return;
      let result = intstate_pop(is);
      is->rsig = intstate_normal;
      let caller = &msList_last(is->cs);
      let call = tu_catchr(ir_call, caller->sp->instructions[caller->pc - 1]);
      let d = intstate_findslots_value(is, call.d);
      let dst = d;
      let dt = ss_type(is->ss, scope_find(is->ss, caller->sp, call.d)->t);
      if (Type_is_comptime(dt)) *dst = result.v;
      else if_decl (let sz, Type_sizeof(dt, ss_typeenv(is->ss)))
        memcpy(dst->value, result.v.value, sz);
      continue;
    }
    arena_clear(is->reads);
    usize at = f->pc++;
    ir inst = f->sp->instructions[at];
    try_err(intstate_inst, (is, at, inst));
  }
}

typed_value interpret errs(superscope *ss, scope *s) {
  scope_finish(ss, s);
  let is = intstate_new(ss);

  intstate_pushs(is, s, (slice(typed_value)){}, 0);
  try_err(intstate_run, (is), (typed_value){});

  let r = intstate_value(is, intstate_pop(is));
  let t = ss_type(ss, r.t);

  if (tu_is(c_int, t.t)) {
    if (r.v.c_int)
      r.v.c_int = BInt.copy(ss->alloc, r.v.c_int);
  } else if (!TypeTree_is_ast(t.t) && !tu_is(c_fun, t.t) && !tu_is(c_type, t.t) && !tu_is(builtin, t.t)) {
    if_decl (let sz, Type_sizeof(t, ss_typeenv(ss))) {
      let mem = *acreate(ss->alloc, u8[sz]);
      memcpy(mem, r.v.value, sz);
      r.v.value = mem;
    }
  }

  mxmap_deinit(is->vmap);
  msList_deInit(ss->alloc, is->cs);
  arena_cleanup(is->temp);
  arena_cleanup(is->reads);
  if (is->scratch)
    arena_cleanup(is->scratch);

  return r;
}

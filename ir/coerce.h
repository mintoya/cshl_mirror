#pragma once
#include "common.h"
#include "scope.h"

static inline Type Type_stripTag(Type t, typeenv e) {
  while (tu_is(r_tag, t.t))
    t.t = type_tree(e, tu_catch(r_tag, t.t).t);
  return t;
}
static bool Type_isint(Type t, typeenv en) {
  return !t.ptr_level &&
         tu_match_exp(
             Type_stripTag(t, en).t,
             (r_sint, _, (bool)true),
             (r_uint, _, (bool)true),
             (c_int, _, (bool)true),
             (default, false),
         );
}
static inline u32 Type_intLike_bitcount(Type t, typeenv e) {
  return tu_match_exp(
      Type_stripTag(t, e).t,
      (r_sint, s, s.bits),
      (r_uint, s, s.bits),
      (r_bool, s, (u16)1),
      (default, (u16)0),
  );
}
static bool Type_is_integer_like(Type t, typeenv e) {
  t = Type_stripTag(t, e);
  return !t.ptr_level && (tu_is(r_bool, t.t) || Type_isint(t, e));
}

static inline Type value_asType errs(superscope *ss, typed_value t) {
  if (!tu_is(c_type, ss_type(ss, t.t).t))
    return_err("TYPE_IS_NOT_VALUE", T());
  if (!t.v.c_type) return_err("UNKNOWN_TYPE", T());
  return type_at(ss_typeenv(ss), t.v.c_type);
}
static inline typed_value Type_asValue(superscope *s, Type t) { return TV_type(s, ss_type_intern(s, t)); }

static inline bigint int_as_bigint(allocfn allocator, typed_value t, typeenv e) {
  let tt = type_at(e, t.t);
  assertMessage(!tt.ptr_level);
  assertMessage(!tt.isref, "todo");
  assertMessage(t.v.value, "tried to convert unknown value");
  return tu_match_exp(
      tt.t,
      (c_int, _, BInt.copy(allocator, t.v.c_int)),
      (r_sint, si, BInt.from.bits(allocator, t.v.value, si.bits, 1)),
      (r_uint, si, BInt.from.bits(allocator, t.v.value, si.bits, 0)),
      (r_bool, _, BInt.from.i64(allocator, !!*(u8 *)t.v.value)),
      // the tag's child index is already interned, so no lookup is needed here
      (r_tag, tg, int_as_bigint(allocator, (typed_value){.t = tg.t, .v = t.v}, e)),
      (default,
       (({
          if (Type_isint(tt, e))
            assertMessage(
                false,
                "unhandled integer case , %s",
                snprint(stdAlloc, "{Type_full}", ((Type_full){tt, e})).ptr
            );
          assertMessage(
              false,
              "expeceted integer, found %s",
              snprint(stdAlloc, "{typed_value_full}", ((typed_value_full){t, e})).ptr
          );
        }),
        (bigint) nullptr)),
  );
}

static inline void bigint_into_slot(u8 *mem, usize len, bigint b) {
  foreach (let w, range(0, (lineup(len, sizeof(bigint_unit)) / sizeof(bigint_unit))))
    memcpy(mem, REF(bigint_get(b, w)), MIN$(len - w * sizeof(bigint_unit), sizeof(bigint_unit)));
}
static inline void bigint_into_int(superscope *ss, allocfn alloc, typed_value *t, bigint b) {
  let e = ss_typeenv(ss);
  let tt = type_at(e, t->t);
  tu_match_void(
      tt.t,
      (c_int, _, {
        let o = &t->v.c_int;
        msList_deInit(alloc, *o);
        *o = BInt.copy(alloc, b);
      }),
      (r_sint, si, bigint_into_slot(t->v.value, Type_sizeof(tt, e), b)),
      (r_uint, si, bigint_into_slot(t->v.value, Type_sizeof(tt, e), b)),
      (r_bool, _, { *(u8 *)t->v.value = !!BInt.cmp(b, nullptr); }),
      (r_tag, tg, {
        let temp = (typed_value){.t = tg.t, .v = t->v};
        return bigint_into_int(ss, alloc, &temp, b);
      }),
      (default, {
        if (Type_isint(tt, e))
          assertMessage(
              false,
              "unhandled integer case , %s",
              snprint(stdAlloc, "{Type_full}", ((Type_full){tt, e})).ptr
          );
        assertMessage(false, "should not be called with a non-int");
      }),
  );
}

static bool scope_cast_would_change(Type from, Type to, typeenv e) {
  // TODO: volatile and const?
  return !EQUAL_ANY(
      0,
      Type_cmp(e, to, T(tu_of(c_any, {}))),
      Type_cmp(e, Type_stripTag(from, e), Type_stripTag(to, e))
  );
}
static u32 scope_cast_value errs(superscope *ss, scope *s, u32 left, Type to) {
  let e = ss_typeenv(ss);
  let ot = ss_type(ss, scope_find(ss, s, left)->t);
  if (!scope_cast_would_change(ot, to, e)) return left;
  let d = scope_add_value(ss, s, TV_of(ss, to));
  try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_cast, {.l = left, .d = d})), 0);
  return d;
}
u32 lower_condition errs(superscope *ss, scope *s, astidx a) {
  let l = try_err(lower, (ss, s, a), 0);
  return try_err(scope_cast_value, (ss, s, l, T(tu_of(r_bool, {}))), 0);
}

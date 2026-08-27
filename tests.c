#define MY_TEST_FRAMEWORK_C (1)
#include <wheels/tests.h>
#include "ir/lower.h"

fptr *filectx = nullptr;
__attribute__((noreturn)) void(error_at)(u8 *location, fptr message) {
  assertMessage(false);
}

//{ new Type/IR helpers
test_fn(lower_type_helpers) {
  let u32t = T(tu_of(r_uint, (u16)32));
  test_assert(Type_isint(u32t));
  test_assert(!Type_is_comptime(u32t));

  let ct = (Type){.t = tu_of(c_int, {}), .iscomptime = true};
  test_assert(Type_is_comptime(ct));
  test_assert(Type_isint(ct));

  let r = Type_ref(u32t);
  test_assert(r.isref);
  test_assert(Type_isint(r)); // ref to int is int-like (deref at use)

  let p = Type_ptr(u32t);
  test_assert(p.ptr_level == 1);
  test_assert(Type_isint(p)); // pointer to int is int-like (deref at use)

  let peeled = Type_peel_auto(r);
  test_assert(!peeled.isref);

  let tag = T(tu_of(r_tag, {.name = fp("T"), .t = &(TypeTree)tu_of(r_uint, (u16)32)}));
  test_assert(Type_isint(tag));
  test_assert(tu_is(r_uint, Type_peel_auto(tag).t));
}

test_fn(lower_scope_ref_resolution) {
  let env = ss_init(allocator);
  defer { ss_deInit(env); };
  let s = env->outermost;

  u32 x = scope_add_value(env, s, (typed_value){T(tu_of(r_uint, (u16)32))});
  scope_bind(env, s, x, fp("x"));

  // the name resolver returns a *ref* to the storage, with an ir_getref inst
  u32 r = ss_get_named(env, s, fp("x"));
  test_assert(r != x);
  test_assert(scope_find(env, s, r)->t.isref);
  test_assert(msList_len(s->instructions) == 1);
  test_assert(tu_is(ir_getref, s->instructions[0]));

  // each use gets its own fresh ref (one ir_getref per use), all pointing at x
  u32 r2 = ss_get_named(env, s, fp("x"));
  test_assert(r2 != r);
  test_assert(msList_len(s->instructions) == 2);
  test_assert(scope_find(env, s, r2)->t.isref);

  // child scopes see parent names
  let child = scope_inferior(env, s);
  u32 rc = ss_get_named(env, child, fp("x"));
  test_assert(scope_find(env, child, rc)->t.isref);
  scope_destroy(env, child);
}

test_fn(lower_type_value_roundtrip) {
  let env = ss_init(allocator);
  defer { ss_deInit(env); };
  let s = env->outermost;

  let u32t = T(tu_of(r_uint, (u16)32));
  u32 tv = scope_add_value(env, s, Type_asValue(env, u32t));
  let got = value_asType_loc(*scope_find(env, s, tv), (u8 *)nullptr);
  test_assert(!Type_cmp(got, u32t));

  let reft = Type_ref(u32t);
  u32 tv2 = scope_add_value(env, s, Type_asValue(env, reft));
  let got2 = value_asType_loc(*scope_find(env, s, tv2), (u8 *)nullptr);
  test_assert(got2.isref);
  test_assert(!Type_cmp(got2, reft));
}

test_fn(lower_compile_int) {
  let env = ss_init(allocator);
  defer { ss_deInit(env); };
  let s = env->outermost;

  u32 v = ss_compileInt(env, s, fp("0x2a"));
  let tv = scope_find(env, s, v);
  test_assert(Type_is_comptime(tv->t) && tu_is(c_int, tv->t.t));
  test_assert(tv->v.c_int);
  let expect = BInt.from.i64(allocator, 42);
  test_assert(bigint_cmp(tv->v.c_int, expect) == 0);
  msList_deInit(allocator, expect);
}
//}

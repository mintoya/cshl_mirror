#include "ast.h"
#include "ir/interpret.h"
#include "ir/lower.h"
static typed_value interpret_source(allocfn arena, fptr src) {
  let env = ss_init(arena);
  lower(env, env->outermost, parse_source(arena, src));
  return interpret(env, env->outermost);
}

test_fn(interpret_literal_int) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  let r = interpret_source(
      arena,
      fp("5;")
  );

  test_assert(tu_is(c_int, r.t.t));
  test_assert(!BInt.cmp(r.v.c_int, BInt.from.i64(arena, 5)));
}
test_fn(interpret_block_yield) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  let r = interpret_source(
      arena,
      fp("{5;};")
  );

  test_assert(tu_is(c_int, r.t.t));
  test_assert(!BInt.cmp(r.v.c_int, BInt.from.i64(arena, 5)));
}

test_fn(interpret_string_value) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  let r = interpret_source(
      arena,
      fp(
          "hi ~= \"yo\";\n"
          "hi;\n"
      )
  );

  test_assert(tu_catch(r_arr, r.t.t, test_assert(false)).t);
  let arr = tu_catch(r_arr, r.t.t);
  test_inteq(tu_catch(r_sint, *arr.t), 8);
  test_fpeq(((fptr){arr.count, (u8 *)r.v.value}), "yo");
}

test_fn(interpret_type_fn) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  let r = interpret_source(
      arena,
      fp(
          "type ~~ @type();\n"
          "two ~~ @fn((t ~type),type,{\n"
          "  return @arr(2, t);\n"
          "});\n"
          "two(@sint(8));\n"
      )
  );

  test_assert(tu_is(c_type, r.t.t));
  let ty = *r.v.c_type;
  test_assert(tu_is(r_arr, ty.t));
  let arr = tu_catch(r_arr, ty.t);
  test_inteq(arr.count, 2);
  test_inteq(tu_catch(r_sint, *arr.t), 8);
}

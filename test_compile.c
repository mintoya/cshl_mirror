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

  println("{typed_value}", r);
  test_assert(tu_is(c_type, r.t.t));
  let ty = *r.v.c_type;
  test_assert(tu_is(r_arr, ty.t));
  let arr = tu_catch(r_arr, ty.t);
  test_inteq(arr.count, 2);
  test_inteq(tu_catch(r_sint, *arr.t), 8);
}

static typed_value ast_probe(allocfn arena, fptr dotwork, fptr arg) {
  let src = snprint(
      arena,
      "ast ~~ @ast();\n"
      "mac ~~ @fn((a ~ast),ast,/{ return {slice(c8)}; });\n"
      "mac({slice(c8)});\n",
      dotwork,
      arg
  );
  src.len--;
  return interpret_source(arena, fp(src));
}

test_fn(interpret_comptime_recursion) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  let r = interpret_source(
      arena,
      fp(
          "fact ~~ @fn((n ~@int),@int,{\n"
          "  @cif(n leq 1, {1;}, {n * fact(n - 1);});\n"
          "});\n"
          "fact(5);\n"
      )
  );

  test_assert(tu_is(c_int, r.t.t));
  test_assert(!BInt.cmp(r.v.c_int, BInt.from.i64(arena, 120)));
}

test_fn(interpret_runtime_recursion) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  let r = interpret_source(
      arena,
      fp(
          "sum ~~ @fn((n ~@sint(32)),@sint(32),{\n"
          "  @if(n leq 0, { zero ~ @sint(32) = 0; return zero; });\n"
          "  rest ~ @sint(32) = sum(n - 1);\n"
          "  total ~ @sint(32) = n + rest;\n"
          "  return total;\n"
          "});\n"
          "sum(4);\n"
      )
  );

  test_assert(tu_is(r_sint, r.t.t));
  test_inteq(tu_catch(r_sint, r.t.t), 32);
  test_inteq(*(i32 *)r.v.value, 10);
}

test_fn(ast_dot_operator_match) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  let r = ast_probe(arena, fp("a.\"+\""), fp("1 + 2"));
  test_assert(tu_is(c_ast, r.t.t));
  test_assert(tu_is(astOp, *r.v.c_ast));
  test_inteq(tu_catch(astOp, *r.v.c_ast).number_rep, opNumbers.plus);
}

test_fn(ast_dot_operator_mismatch) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  let r = ast_probe(arena, fp("a.\"+\""), fp("1 - 2"));
  test_assert(tu_is(c_ast, r.t.t));
  test_assert(tu_is(astEmptyStatement, *r.v.c_ast));
}

test_fn(ast_dot_operation_members) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  let l = ast_probe(arena, fp("a.opleft"), fp("1 + 2"));
  test_assert(tu_is(astNumber, *l.v.c_ast));
  test_fpeq(tu_catch(astNumber, *l.v.c_ast), "1");
  let rr = ast_probe(arena, fp("a.opright"), fp("1 + 2"));
  test_assert(tu_is(astNumber, *rr.v.c_ast));
  test_fpeq(tu_catch(astNumber, *rr.v.c_ast), "2");
}

test_fn(ast_dot_member_check_on_non_op) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  let l = ast_probe(arena, fp("a.opleft"), fp("7"));
  test_assert(tu_is(astEmptyStatement, *l.v.c_ast));
}

test_fn(ast_dot_kind_check) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  let i = ast_probe(arena, fp("a.id"), fp("foo"));
  test_assert(tu_is(astId, *i.v.c_ast));
  test_fpeq(tu_catch(astId, *i.v.c_ast), "foo");

  let o = ast_probe(arena, fp("a.op"), fp("1 + 2"));
  test_assert(tu_is(astOp, *o.v.c_ast));

  let e = ast_probe(arena, fp("a.id"), fp("7"));
  test_assert(tu_is(astEmptyStatement, *e.v.c_ast));
}

#include "ast.h"
#include "ir/interpret.h"
#include "ir/lower.h"
static typed_value interpret_source(allocfn arena, fptr src) {
  let env = ss_init(arena);
  catch_errcall((lower, (env, env->outermost, parse_source(arena, src))), (e, _), (err_panic(e), (u32)0));
  return catch_errcall((interpret, (env, env->outermost)), (e, _), (err_panic(e), (typed_value){}));
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
          "hi ~= \"yo\";"
          "hi;"
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
          "type ~~ @type();"
          "two ~~ @fn((t ~type),type,{"
          "  return @arr(2, t);"
          "});"
          "two(@sint(8));"
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
          "fact ~~ @fn((n ~@int()),@int(),{"
          "   @cif("
          "     (n <= 1),"
          "     (return ( 1 )),"
          "     (return ( n * fact(n - 1)))"
          "   );"
          " });"
          "fact(5);"
      )
  );

  test_assert(tu_is(c_int, r.t.t));
  test_assert(!BInt.cmp(r.v.c_int, BInt.from.i64(arena, 120)));
}
/*
// both branches are lowered, much rader to reconcile
// not meant to work for a while
test_fn(interpret_comptime_recursion_low) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let r = interpret_source(
      arena,
      fp(
          "fact ~~ @fn((n ~@int()),@int(),{"
          "   @if("
          "     (n <= 1),"
          "     (return ( 1 )),"
          "     (return ( n * fact(n - 1)))"
          "   );"
          " });"
          "fact(5);"
      )
  );

  test_assert(tu_is(c_int, r.t.t));
  test_assert(!BInt.cmp(r.v.c_int, BInt.from.i64(arena, 120)));
}
*/

test_fn(interpret_runtime_recursion) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  let r = interpret_source(
      arena,
      fp(
          "sum ~~ @fn((n ~@sint(32)),@sint(32),{"
          "  @if(n <= 0, { zero ~ @sint(32) = 0; return zero; });"
          "  rest ~ @sint(32) = sum(n - 1);"
          "  total ~ @sint(32) = n + rest;"
          "  return total;"
          "});"
          "sum(4);"
      )
  );

  test_assert(tu_is(r_sint, r.t.t));
  test_inteq(tu_catch(r_sint, r.t.t), 32);
  test_inteq(*(i32 *)r.v.value, 10);
}

test_fn(if_cshl) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  let r = interpret_source(
      arena,
      fp(
          "ast ~~ @ast();"
          "any ~~ @any();"
          ""
          "if  ~~ @fn (a ~ast),ast,{"
          "  call ~ast~ a.\"()\";"
          "  cond ~ast~ call.opleft;"
          "  rest ~ast~ call.opright;"
          ""
          "  @cif((rest.\"()\".opright.\"()\".opleft == \"else\"), {"
          "    then ~ast~ rest.opleft;"
          "    else ~ast~ rest.opright.\"()\".opright;"
          ""
          "    return @qot @if("
          "      @uqot(cond),"
          "      @uqot(then),"
          "      @uqot(else)"
          "    );"
          "  });"
          ""
          "  return @qot @if("
          "    @uqot(cond),"
          "    @uqot(rest)"
          "  );"
          "};"
          "cnd ~~ @fn (a ~any,b ~any,c ~(@typeof b)),(@typeof b),{"
          "  if' (a) (return b) else (return c);"
          "};"
          ""
          "one ~= cnd (1),"
          "  1,"
          "  0;"
          "// one == 1\n"
          "one = cnd (one),"
          "  3,"
          "  2;"
          "// one == 3\n"
          "one;"
      )
  );

  println("[if_cshl] r {typed_value} {bigint}", r, r.v.c_int);
  test_assert(tu_is(c_int, r.t.t));
  test_assert(!BInt.cmp(r.v.c_int, BInt.from.i64(arena, 3)));
}

test_fn(bool_bigint_roundtrip) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let env = ss_init(arena);

  u8 *mem = *acreate(arena, u8[1]);
  let tv = TV(T(tu_of(r_bool, {})), mem);

  *mem = 1;
  test_assert(!BInt.cmp(int_as_bigint(env->alloc, tv), BInt.from.i64(arena, 1)));
  *mem = 0;
  test_assert(!BInt.cmp(int_as_bigint(env->alloc, tv), BInt.from.i64(arena, 0)));

  bigint_into_int(env, &tv, BInt.from.i64(arena, 5));
  test_inteq(*mem, 1);
  bigint_into_int(env, &tv, BInt.from.i64(arena, 0));
  test_inteq(*mem, 0);
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

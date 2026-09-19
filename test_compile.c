#include "ast.h"
#include "ir/interpret.h"
#include "ir/lower.h"
#include "ir/scope.h"

astidx_full parse_source errs(allocfn out_allocator, fptr input, const char *filename);

// a lowered+interpreted value together with the types it indexes into
typedef struct {
  typeidx t;
  value_store v;
  typeenv env;
} interp;

static interp interpret_source(allocfn arena, fptr src, astidx_full *ctx_out) {
  let ctx = catch_errcall((parse_source, (arena, src, nullptr)), e, (err_panic(e), (astidx_full){}));
  let env = ss_init(arena, ctx);
  catch_errcall((lower, (env, env->outermost, ctx.idx)), e, (err_panic(e), (u32)0));
  let res = catch_errcall((interpret, (env, env->outermost)), e, (err_panic(e), (typed_value){}));
  if (ctx_out) {
    *ctx_out = ctx;
    ctx_out->alist = env->asts;
  }
  return (interp){.t = res.t, .v = res.v, .env = ss_typeenv(env)};
}
static Type tv_type(interp x) { return type_at(x.env, x.t); }
static typed_value_full tvf(interp x) { return (typed_value_full){(typed_value){x.t, x.v}, x.env}; }

test_fn(interpret_literal_int) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  let r = interpret_source(
      arena,
      fp("5;"),
      nullptr
  );

  test_assert(tu_is(c_int, tv_type(r).t));
  test_assert(!BInt.cmp(r.v.c_int, BInt.from.i64(arena, 5)));
}
test_fn(interpret_block_yield) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  let r = interpret_source(
      arena,
      fp("{5;};"),
      nullptr
  );

  test_assert(tu_is(c_int, tv_type(r).t));
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
      ),
      nullptr
  );

  test_assert(tu_catchr(r_arr, tv_type(r).t, test_assert(false)).t);
  let arr = tu_catchr(r_arr, tv_type(r).t);
  let elem = type_at(r.env, arr.t);
  test_inteq(tu_catch(r_sint, elem.t).bits, 8);
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
      ),
      nullptr
  );

  println("{typed_value_full}", tvf(r));
  test_assert(tu_is(c_type, tv_type(r).t));
  let ty = type_at(r.env, r.v.c_type);
  test_assert(tu_is(r_arr, ty.t));
  let arr = tu_catch(r_arr, ty.t);
  test_inteq(arr.count, 2);
  let elem = type_at(r.env, arr.t);
  test_inteq(tu_catch(r_sint, elem.t).bits, 8);
}

test_fn(interpret_zero_arg_call) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  let r = interpret_source(
      arena,
      fp(
          "seven ~~ @fn (),@int(),{\n"
          "  return 7;\n"
          "};\n"
          "seven();\n"
      ),
      nullptr
  );

  test_assert(tu_is(c_int, tv_type(r).t));
  test_assert(!BInt.cmp(r.v.c_int, BInt.from.i64(arena, 7)));
}
test_fn(interpret_global_comptime_mutation) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  let r = interpret_source(
      arena,
      fp(
          "asize ~= 1;\n"
          "type ~~ @type();\n"
          "increasing_array~~@fn((t~type),type,\n"
          "   return @arr { asize = asize + 1;},t );\n"
          "a ~increasing_array @sint 8;\n"
          "b ~increasing_array @uint 16;\n"
          "b;\n"
      ),
      nullptr
  );

  test_assert(tu_is(r_arr, tv_type(r).t));
  let arr = tu_catchr(r_arr, tv_type(r).t);
  test_inteq(arr.count, 3);
  let elem = type_at(r.env, arr.t);
  test_inteq(tu_catch(r_uint, elem.t).bits, 16);
}

static interp ast_probe(allocfn arena, fptr dotwork, fptr arg, astidx_full *ctx) {
  let src = snprint(
      arena,
      "ast ~~ @ast();\n"
      "mac ~~ @fn((a ~ast),ast,/{ return {slice(c8)}; });\n"
      "mac({slice(c8)});\n",
      dotwork,
      arg
  );
  src.len--;
  return interpret_source(arena, fp(src), ctx);
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
      ),
      nullptr
  );

  test_assert(tu_is(c_int, tv_type(r).t));
  test_assert(!BInt.cmp(r.v.c_int, BInt.from.i64(arena, 120)));
}
/*
// both branches are lowered, much rader to reconcile
// not meant to work for a while
// could work since that's n-1
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
      ),
      nullptr
  );

  test_assert(tu_is(c_int, tv_type(r).t));
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
      ),
      nullptr
  );

  test_assert(tu_is(r_sint, tv_type(r).t));
  test_inteq(tu_catchr(r_sint, tv_type(r).t).bits, 32);
  test_inteq(*(i32 *)r.v.value, 10);
}

// a const declaration binds the register it declares in place, so `~~ f()` retypes the call's
// destination register; the callee returns an unqualified value and const says nothing about the
// value's shape, so the return still has to be accepted
test_fn(const_declaration_retypes_call_destination) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  let r = interpret_source(
      arena,
      fp(
          "type ~~ @type();\n"
          "s32 ~~ @sint(32);\n"
          "f ~~ @fn (), s32, { return 7; };\n"
          "a ~~ f();\n"
          "a;\n"
      ),
      nullptr
  );

  test_assert(tu_is(r_sint, tv_type(r).t));
  test_assert(tv_type(r).isconst);
  test_inteq(*(i32 *)r.v.value, 7);
}

test_fn(if_cshl) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  let r = interpret_source(
      arena,
      fp(
          "ast ~~ @ast();any ~~ @any();"
          "if  ~~ @fn (a ~ast),ast,{"
          "  call ~ast~ a.\"()\";"
          "  cond ~ast~ call.opleft;"
          "  rest ~ast~ call.opright;"
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
          "  return @qot @if("
          "    @uqot(cond),"
          "    @uqot(rest)"
          "  );"
          "};"
          "cnd ~~ @fn (a ~any,b ~any,c ~(@typeof b)),(@typeof b),{"
          "  if' (a) (return b) else (return c);"
          "};"
          ""
          "one ~= cnd (1),  1,0;"
          "one =  cnd (one),3,2;"
          "one;"
      ),
      nullptr
  );

  println("[if_cshl] r {typed_value_full} {bigint}", tvf(r), r.v.c_int);
  test_assert(tu_is(c_int, tv_type(r).t));
  test_assert(!BInt.cmp(r.v.c_int, BInt.from.i64(arena, 3)));
}

test_fn(bool_bigint_roundtrip) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let env = ss_init(arena, (astidx_full){});

  u8 *mem = *acreate(arena, u8[1]);
  let tv = TV_with(env, T(tu_of(r_bool, {})), VS(mem));

  *mem = 1;
  test_assert(!BInt.cmp(int_as_bigint(env->alloc, tv, ss_typeenv(env)), BInt.from.i64(arena, 1)));
  *mem = 0;
  test_assert(!BInt.cmp(int_as_bigint(env->alloc, tv, ss_typeenv(env)), BInt.from.i64(arena, 0)));

  bigint_into_int(env, arena, &tv, BInt.from.i64(arena, 5));
  test_inteq(*mem, 1);
  bigint_into_int(env, arena, &tv, BInt.from.i64(arena, 0));
  test_inteq(*mem, 0);
}

// a bigint is only as wide as the value in it, so writing one into a wider integer has to extend it
// to the slot first: the write reads the slot's bytes straight out of the bigint's limbs
test_fn(wide_integer_write_extends_the_bigint) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  let r = interpret_source(
      arena,
      fp(
          "type ~~ @type();\n"
          "s128 ~~ @sint(128);\n"
          "big ~s128 = 111;\n"
          "big;\n"
      ),
      nullptr
  );
  test_assert(tu_is(r_sint, tv_type(r).t));
  test_inteq(tu_catchr(r_sint, tv_type(r).t).bits, 128);
  test_inteq(((u64 *)r.v.value)[0], 111);
  test_inteq(((u64 *)r.v.value)[1], 0);

  // and a negative one is sign extended, not zero extended
  let n = interpret_source(
      arena,
      fp(
          "type ~~ @type();\n"
          "s128 ~~ @sint(128);\n"
          "neg ~s128 = 0 - 1;\n"
          "neg;\n"
      ),
      nullptr
  );
  test_assert(tu_is(r_sint, tv_type(n).t));
  test_inteq(((u64 *)n.v.value)[0], (u64)-1);
  test_inteq(((u64 *)n.v.value)[1], (u64)-1);
}

test_fn(ast_dot_operator_match) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  astidx_full ctx = {};
  let r = ast_probe(arena, fp("a.\"+\""), fp("1 + 2"), &ctx);
  test_assert(tu_is(c_ast, tv_type(r).t));
  let node = ast_at_list(ctx.alist, r.v.c_ast);
  test_assert(tu_is(astOp, node));
  test_inteq(tu_catch(astOp, node).number_rep, opNumbers.plus);
}

test_fn(ast_dot_operator_mismatch) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  astidx_full ctx = {};
  let r = ast_probe(arena, fp("a.\"+\""), fp("1 - 2"), &ctx);
  test_assert(tu_is(c_ast, tv_type(r).t));
  test_assert(tu_is(astEmpty, ast_at_list(ctx.alist, r.v.c_ast)));
}

test_fn(ast_dot_operation_members) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  astidx_full lctx = {};
  let l = ast_probe(arena, fp("a.opleft"), fp("1 + 2"), &lctx);
  test_assert(tu_is(c_ast, tv_type(l).t));
  let lnode = ast_at_list(lctx.alist, l.v.c_ast);
  test_assert(tu_is(astNumber, lnode));
  test_fpeq(fp_from_srcpos((srcpos_full){.pidx = tu_catch(astNumber, lnode), .plist = lctx.plist, .flist = lctx.flist}), "1");

  astidx_full rctx = {};
  let rr = ast_probe(arena, fp("a.opright"), fp("1 + 2"), &rctx);
  test_assert(tu_is(c_ast, tv_type(rr).t));
  let rnode = ast_at_list(rctx.alist, rr.v.c_ast);
  test_assert(tu_is(astNumber, rnode));
  test_fpeq(fp_from_srcpos((srcpos_full){.pidx = tu_catch(astNumber, rnode), .plist = rctx.plist, .flist = rctx.flist}), "2");
}

test_fn(ast_dot_member_check_on_non_op) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  astidx_full ctx = {};
  let l = ast_probe(arena, fp("a.opleft"), fp("7"), &ctx);
  test_assert(tu_is(c_ast, tv_type(l).t));
  test_assert(tu_is(astEmpty, ast_at_list(ctx.alist, l.v.c_ast)));
}

test_fn(ast_dot_kind_check) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  astidx_full ictx = {};
  let i = ast_probe(arena, fp("a.id"), fp("foo"), &ictx);
  test_assert(tu_is(c_ast, tv_type(i).t));
  let inode = ast_at_list(ictx.alist, i.v.c_ast);
  test_assert(tu_is(astId, inode));
  test_fpeq(fp_from_srcpos((srcpos_full){tu_catch(astId, inode), ictx.plist, ictx.flist}), "foo");

  astidx_full octx = {};
  let o = ast_probe(arena, fp("a.op"), fp("1 + 2"), &octx);
  test_assert(tu_is(c_ast, tv_type(o).t));
  test_assert(tu_is(astOp, ast_at_list(octx.alist, o.v.c_ast)));

  astidx_full ectx = {};
  let e = ast_probe(arena, fp("a.id"), fp("7"), &ectx);
  test_assert(tu_is(c_ast, tv_type(e).t));
  test_assert(tu_is(astEmpty, ast_at_list(ectx.alist, e.v.c_ast)));
}

// @ast_op builds any operator out of its spelling, so a macro does not need a builtin per operator
// and an operand can be another ast it computed
test_fn(ast_op_builds_operator_from_spelling) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  astidx_full ctx = {};
  let r = ast_probe(arena, fp("@ast_op(\"+\", a, @qot(7))"), fp("3"), &ctx);
  test_assert(tu_is(c_ast, tv_type(r).t));
  let node = ast_at_list(ctx.alist, r.v.c_ast);
  test_assert(tu_is(astOp, node));
  let op = tu_catch(astOp, node);
  test_inteq(op.number_rep, opNumbers.plus);
  test_assert(tu_is(astNumber, ast_at_list(ctx.alist, op.left_right)));
  test_assert(tu_is(astNumber, ast_at_list(ctx.alist, op.left_right + 1)));
}

test_fn(struct_operator_overload_vector_equality_true) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  let r = interpret_source(
      arena,
      fp(
          "type ~~ @type();\n"
          "i ~~ @sint;\n"
          "vec2 ~type~ @struct{\n"
          "  Self ~type~ @this();\n"
          "  x ~ i 32;\n"
          "  y ~ i 32;\n"
          "  @id(\"==\") ~~ @fn (self~Self,other~Self),@bool(),{\n"
          "    return (self.x == other.x) and (self.y == other.y);\n"
          "  };\n"
          "};\n"
          "a ~vec2; a.x = 1; a.y = 2;\n"
          "b ~vec2; b.x = 1; b.y = 2;\n"
          "(a == b);\n"
      ),
      nullptr
  );

  test_assert(tu_is(r_bool, tv_type(r).t));
  test_inteq(*(u8 *)r.v.value, 1);
}
test_fn(struct_cast_ovl) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  let r = interpret_source(
      arena,
      fp(
          "type ~~ @type();\n"
          "i ~~ @sint;\n"
          "vec2 ~type~ @struct{\n"
          "  Self ~type~ @this();\n"
          "  x ~ i 32;\n"
          "  y ~ i 32;\n"
          // the return type is the dependent type parameter, and the body branches on it
          "  @id(\"as\") ~~ @fn (self~Self,to ~type),to,{\n"
          "    @cif (to == (i 8)),{ return (self.x) + (self.y); };\n"
          "  };\n"
          "};\n"
          "a ~vec2; a.x = 1; a.y = 2;\n"
          "a as (i 8) == 3 as (i 8);\n"
      ),
      nullptr
  );

  test_assert(tu_is(r_bool, tv_type(r).t));
  test_inteq(*(u8 *)r.v.value, 1);
}

test_fn(type_equality_comptime) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  let r = interpret_source(
      arena,
      fp(
          "i ~~ @sint;\n"
          "((i 32) == (i 32)) and ((i 32) == @sint(32)) and ((i 32) != (i 64));\n"
      ),
      nullptr
  );

  test_assert(tu_is(r_bool, tv_type(r).t));
  test_inteq(*(u8 *)r.v.value, 1);
}

test_fn(type_equality_comptime_false) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  let r = interpret_source(
      arena,
      fp(
          "i ~~ @sint;\n"
          "((i 32) == (i 64)) or ((i 32) != (i 32));\n"
      ),
      nullptr
  );

  test_assert(tu_is(r_bool, tv_type(r).t));
  test_inteq(*(u8 *)r.v.value, 0);
}

test_fn(struct_operator_overload_vector_equality_false) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  let r = interpret_source(
      arena,
      fp(
          "type ~~ @type();\n"
          "i ~~ @sint;\n"
          "vec2 ~type~ @struct{\n"
          "  Self ~type~ @this();\n"
          "  x ~ i 32;\n"
          "  y ~ i 32;\n"
          "  @id(\"==\") ~~ @fn (self~Self,other~Self),@bool(),{\n"
          "    return (self.x == other.x) and (self.y == other.y);\n"
          "  };\n"
          "};\n"
          "a ~vec2; a.x = 1; a.y = 2;\n"
          "c ~vec2; c.x = 3; c.y = 4;\n"
          "(a == c);\n"
      ),
      nullptr
  );

  test_assert(tu_is(r_bool, tv_type(r).t));
  test_inteq(*(u8 *)r.v.value, 0);
}

test_fn(struct_operator_overload_self_in_body) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  let r = interpret_source(
      arena,
      fp(
          "type ~~ @type();\n"
          "i ~~ @sint;\n"
          "vec2 ~type~ @struct{\n"
          "  Self ~type~ @this();\n"
          "  x ~ i 32;\n"
          "  y ~ i 32;\n"
          "  @id(\"+\") ~~ @fn (self~Self,other~Self),Self,{\n"
          "    r ~Self;\n"
          "    r.x = self.x + other.x;\n"
          "    r.y = self.y + other.y;\n"
          "    return r;\n"
          "  };\n"
          "};\n"
          "a ~vec2; a.x = 1; a.y = 2;\n"
          "b ~vec2; b.x = 3; b.y = 4;\n"
          "c ~vec2~ a + b;\n"
          "c.x + c.y;\n"
      ),
      nullptr
  );

  test_assert(tu_is(r_sint, tv_type(r).t));
  test_inteq(tu_catchr(r_sint, tv_type(r).t).bits, 32);
  test_inteq(*(i32 *)r.v.value, 10);
}

test_fn(struct_as_overload) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  let r = interpret_source(
      arena,
      fp(
          "type ~~ @type();\n"
          "i ~~ @sint;\n"
          "wrapped ~type~ @struct{\n"
          "  Self ~type~ @this();\n"
          "  v ~ i 32;\n"
          "  @id(\"as\") ~~ @fn (self ~Self, target ~type), @sint(32), {\n"
          "    return self.v;\n"
          "  };\n"
          "};\n"
          "a ~wrapped; a.v = 7;\n"
          "b ~i 32~ a as @sint(32);\n"
          "b;\n"
      ),
      nullptr
  );
  test_assert(tu_is(r_sint, tv_type(r).t));
  test_inteq(tu_catchr(r_sint, tv_type(r).t).bits, 32);
  test_inteq(*(i32 *)r.v.value, 7);
}

// `as` with no overload member is just a cast to the type the right hand side names
test_fn(as_without_overload_is_a_cast) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  let r = interpret_source(
      arena,
      fp(
          "type ~~ @type();\n"
          "s8 ~~ @sint(8);\n"
          "s32 ~~ @sint(32);\n"
          "x ~s8 = 5;\n"
          "x as s32;\n"
      ),
      nullptr
  );

  test_assert(tu_is(r_sint, tv_type(r).t));
  test_inteq(tu_catchr(r_sint, tv_type(r).t).bits, 32);
  test_inteq(*(i32 *)r.v.value, 5);
}

// a function cannot capture the frame it was written in, so a closure carries its captures in a
// struct and calls itself through a "()" overload
test_fn(struct_closure_capture) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  let r = interpret_source(
      arena,
      fp(
          "type ~~ @type();\n"
          "s32 ~~ @sint(32);\n"
          "adder ~type~ @struct{\n"
          "  Self ~type~ @this();\n"
          "  captured ~ s32;\n"
          "  @id(\"()\") ~~ @fn (self ~Self, extra ~s32), s32, {\n"
          "    base ~s32 = self.captured;\n"
          "    return base + extra;\n"
          "  };\n"
          "};\n"
          "make ~~ @fn (start ~s32), adder, {\n"
          "  c ~adder;\n"
          "  c.captured = start;\n"
          "  return c;\n"
          "};\n"
          "add5 ~adder = make(5);\n"
          "add5(2);\n"
      ),
      nullptr
  );

  test_assert(tu_is(r_sint, tv_type(r).t));
  test_inteq(tu_catchr(r_sint, tv_type(r).t).bits, 32);
  test_inteq(*(i32 *)r.v.value, 7);
}

// a "()" overload called as the last expression has to yield the call result, not a cast that
// coerced one of its arguments; the call allocates those casts above the result register
test_fn(overload_call_yields_the_result_after_arg_coercion) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  let r = interpret_source(
      arena,
      fp(
          "type ~~ @type();\n"
          "s8 ~~ @sint(8);\n"
          "s32 ~~ @sint(32);\n"
          "box ~type~ @struct{\n"
          "  Self ~type~ @this();\n"
          "  v ~s32;\n"
          "  @id(\"()\") ~~ @fn (self ~Self, x ~s32, y ~s32), s32, {\n"
          "    return self.v + x + y;\n"
          "  };\n"
          "};\n"
          "make ~~ @fn (), box, { b ~box; b.v = 30; return b; };\n"
          "b ~box = make();\n"
          "small ~s8 = 1;\n"
          "tiny ~s8 = 2;\n"
          "b(small, tiny);\n"
      ),
      nullptr
  );

  test_assert(tu_is(r_sint, tv_type(r).t));
  test_inteq(tu_catchr(r_sint, tv_type(r).t).bits, 32);
  test_inteq(*(i32 *)r.v.value, 33);
}

// an explicit @ref capture stores the address, so the call aliases the source
test_fn(struct_ref_capture) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  let r = interpret_source(
      arena,
      fp(
          "type ~~ @type();\n"
          "s32 ~~ @sint(32);\n"
          "ref ~~ @refT;\n"
          "counter ~s32 = 5;\n"
          "cell ~type~ @struct{\n"
          "  Self ~type~ @this();\n"
          "  r ~ (ref s32);\n"
          "  @id(\"()\") ~~ @fn (self ~Self, j ~s32), s32, {\n"
          "    v ~(ref s32) = self.r;\n"
          "    v = v + j;\n"
          "    return v;\n"
          "  };\n"
          "};\n"
          "make ~~ @fn (), cell, {\n"
          "  c ~cell;\n"
          "  c.r = @ref counter;\n"
          "  return c;\n"
          "};\n"
          "result ~~ @fn (), s32, {\n"
          "  k ~cell = make();\n"
          "  k(2);\n"
          "  return counter;\n"
          "};\n"
          "result();\n"
      ),
      nullptr
  );

  test_assert(tu_is(r_sint, tv_type(r).t));
  test_inteq(*(i32 *)r.v.value, 7);
}

// a lowered lvalue is a ref, so @where must not address the ref register itself, and @at must give
// back an alias of the pointee
test_fn(where_at_alias) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  let r = interpret_source(
      arena,
      fp(
          "type ~~ @type();\n"
          "s32 ~~ @sint(32);\n"
          "ref ~~ @refT;\n"
          "counter ~s32 = 5;\n"
          "run ~~ @fn (), s32, {\n"
          "  p ~(@ptr s32) = @where counter;\n"
          "  v ~(ref s32)~ @at p;\n"
          "  counter = 9;\n"
          "  return v - 9;\n"
          "};\n"
          "run();\n"
      ),
      nullptr
  );

  test_assert(tu_is(r_sint, tv_type(r).t));
  test_inteq(*(i32 *)r.v.value, 0);
}

test_fn(any_return_adopts_concrete_type) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  // a function declared to return `any` resolves to the concrete type of what it returns,
  // so callers can use the result as that type instead of an erased `any`
  let r = interpret_source(
      arena,
      fp(
          "type ~~ @type();\n"
          "s32 ~~ @sint(32);\n"
          "any ~~ @any();\n"
          "S ~type~ @struct { a ~s32; };\n"
          "mk ~~ @fn (), any, { s ~S; s.a = 5; return s; };\n"
          "use ~~ @fn (), s32, { return mk().a + 2; };\n"
          "use();\n"
      ),
      nullptr
  );

  test_assert(tu_is(r_sint, tv_type(r).t));
  test_inteq(*(i32 *)r.v.value, 7);
}

// integer math lands on the weaker of the two operand types, so a runtime int beats a comptime
// literal and the wider int wins. coercing the operands allocates temporaries above the result, so
// this also pins down that the block still yields the result and not one of those casts
test_fn(math_coerces_to_the_weaker_int) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  let r = interpret_source(
      arena,
      fp(
          "type ~~ @type();\n"
          "s8 ~~ @sint(8);\n"
          "s32 ~~ @sint(32);\n"
          "x ~s32 = 4;\n"
          "y ~s8 = 3;\n"
          "y + x;\n"
      ),
      nullptr
  );

  test_assert(tu_is(r_sint, tv_type(r).t));
  test_inteq(tu_catchr(r_sint, tv_type(r).t).bits, 32);
  test_inteq(*(i32 *)r.v.value, 7);
}

// a comptime literal next to a runtime int must not drag the result back to the comptime int
// (the `3 + x` case)
test_fn(comptime_literal_math_adopts_runtime_type) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  let r = interpret_source(
      arena,
      fp(
          "type ~~ @type();\n"
          "s32 ~~ @sint(32);\n"
          "x ~s32 = 4;\n"
          "3 + x;\n"
      ),
      nullptr
  );

  test_assert(tu_is(r_sint, tv_type(r).t));
  test_inteq(*(i32 *)r.v.value, 7);
}

test_fn(math_unsigned_is_weaker_at_the_same_width) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  let r = interpret_source(
      arena,
      fp(
          "type ~~ @type();\n"
          "s32 ~~ @sint(32);\n"
          "u32 ~~ @uint(32);\n"
          "a ~s32 = 5;\n"
          "b ~u32 = 2;\n"
          "a + b;\n"
      ),
      nullptr
  );

  test_assert(tu_is(r_uint, tv_type(r).t));
  test_inteq(*(u32 *)r.v.value, 7);
}

// two comptime operands have nothing to coerce and still fold to a comptime int
test_fn(comptime_math_stays_comptime) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  let r = interpret_source(arena, fp("3 + 4;"), nullptr);

  test_assert(tu_is(c_int, tv_type(r).t));
  test_assert(!BInt.cmp(r.v.c_int, BInt.from.i64(arena, 7)));
}

test_fn(field_assignment_writes_into_the_struct) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  // a.b = c has to write into the struct, and reading it back has to see that same member, so the
  // access register is the member rather than a copy of it. a variable on the right is copied into
  // the member, so s.a is 5 after y changes, not an alias of y
  let r = interpret_source(
      arena,
      fp(
          "type ~~ @type();\n"
          "s32 ~~ @sint(32);\n"
          "S ~type~ @struct { a ~s32; b ~s32; };\n"
          "s ~S;\n"
          "s.b = 7;\n"
          "y ~s32 = 5;\n"
          "s.a = y;\n"
          "y = 9;\n"
          "s.a + s.b;\n"
      ),
      nullptr
  );

  test_assert(tu_is(r_sint, tv_type(r).t));
  test_inteq(tu_catchr(r_sint, tv_type(r).t).bits, 32);
  test_inteq(*(i32 *)r.v.value, 12);
}

test_fn(field_access_is_addressable) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  // the access is an lvalue inside the struct, so @where on it addresses the member and @at writes
  // back through that address, the same way it does for a named local
  let r = interpret_source(
      arena,
      fp(
          "type ~~ @type();\n"
          "s32 ~~ @sint(32);\n"
          "S ~type~ @struct { a ~s32; b ~s32; };\n"
          "s ~S;\n"
          "s.a = 5;\n"
          "p ~(@ptr s32)~ @where s.a;\n"
          "@at p = 9;\n"
          "s.a - 9;\n"
      ),
      nullptr
  );

  test_assert(tu_is(r_sint, tv_type(r).t));
  test_inteq(*(i32 *)r.v.value, 0);
}

// x[i] is an lvalue inside the array, so writing it writes the element, and a variable on the
// right is copied in rather than bound to the access register and thrown away with it
test_fn(array_assignment_writes_into_the_array) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  let r = interpret_source(
      arena,
      fp(
          "type ~~ @type();\n"
          "s32 ~~ @sint(32);\n"
          "A ~type~ @arr(3, s32);\n"
          "x ~A;\n"
          "y ~s32 = 5;\n"
          "x[0] = y;\n"
          "y = 9;\n"
          "x[2] = x[0] + 2;\n"
          "x[0] + x[2];\n"
      ),
      nullptr
  );

  test_assert(tu_is(r_sint, tv_type(r).t));
  test_inteq(tu_catchr(r_sint, tv_type(r).t).bits, 32);
  test_inteq(*(i32 *)r.v.value, 12);
}

test_fn(array_access_is_addressable) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  let r = interpret_source(
      arena,
      fp(
          "type ~~ @type();\n"
          "s32 ~~ @sint(32);\n"
          "A ~type~ @arr(3, s32);\n"
          "x ~A;\n"
          "p ~(@ptr s32)~ @where x[1];\n"
          "@at p = 9;\n"
          "x[1] - 9;\n"
      ),
      nullptr
  );

  test_assert(tu_is(r_sint, tv_type(r).t));
  test_inteq(*(i32 *)r.v.value, 0);
}
test_fn(ast_array) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  let r = interpret_source(
      arena,
      fp(
          "a ~~ @qot [0,1,2,3,4,5];\n"
          "b ~~ @qot (0,1,2,3,4,5);\n"
          "c ~~ @qot {0;1;2;3;4;5;};\n"
          "i ~@sint(32)~@uqot(a[3]) + @uqot(b[1]) + @uqot(c[2]);\n"
          "i\n"
      ),
      nullptr
  );

  test_assert(tu_is(r_sint, tv_type(r).t));
  test_inteq(*(i32 *)r.v.value, 6);
}
test_fn(sib_fn_call) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  let r = interpret_source(
      arena,
      fp(

          "i ~~ @sint;\n"
          "outer ~~ @fn (run ~(i 32),in ~(i 32)),(i 32),{\n"
          " run = run + 1;\n"
          " odd ~~ @fn (a ~(i 32),o ~(i 32)),(i 32),\n"
          "   (return outer(a,o * 3 + 1));\n"
          " even ~~ @fn (a ~(i 32),e ~(i 32)),(i 32),\n"
          "   (return outer(a,e / 2));\n"
          " resolve ~~ @fn (r ~(i 32),n ~(i 32)),(i 32),{\n"
          "   @if((n == 1), (return r - 1));\n"
          "   @if((n band 1), (return odd(r,n)));\n"
          "   return even(r,n);\n"
          " };\n"
          " return resolve(run,in);\n"
          "};\n"
          "\n"
          "outer(0,1000);\n"
      ),
      nullptr
  );

  test_assert(tu_is(r_sint, tv_type(r).t));
  test_inteq(*(i32 *)r.v.value, 111);
}
test_fn(referance_types){
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  let r = interpret_source(
      arena,
      fp(
          "i32 ~~ @sint(32);\n"
          "i~i32=5;\n"
          "i2~@refT(@sint(32))=i;\n"
          "@at(@where(i2))=67;\n"
          "i;\n"
      ),
      nullptr
  );

  test_assert(tu_is(r_sint, tv_type(r).t));
  test_inteq(*(i32 *)r.v.value, 67);
}
test_fn(referance_types_i){
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  let r = interpret_source(
      arena,
      fp(
          "i32 ~~ @sint(32);\n"
          "i~i32=5;\n"
          "i2~@refT(@sint(32))=i;\n"
          "i2 = 67;\n"
          "i;\n"
      ),
      nullptr
  );

  test_assert(tu_is(r_sint, tv_type(r).t));
  test_inteq(*(i32 *)r.v.value, 67);
}

test_fn(forward_declarations_fill_in) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  let r = interpret_source(
      arena,
      fp(
          "i ~~ @sint;\n"
          "i32 ~~ i(32);\n"
          "outer ~~ @fn (n ~i32),i32,{\n"
          "  isodd ~~ @fn (n ~i32),i32;\n"
          "  iseven ~~ @fn (n ~i32),i32;\n"
          "  isodd ~~ @fn (n ~i32),i32,{\n"
          "    @if((n == 0), (return 0));\n"
          "    return iseven(n - 1);\n"
          "  };\n"
          "  iseven ~~ @fn (n ~i32),i32,{\n"
          "    @if((n == 0), (return 1));\n"
          "    return isodd(n - 1);\n"
          "  };\n"
          "  return isodd(n);\n"
          "};\n"
          "result ~= outer(7);\n"
          "result;\n"
      ),
      nullptr
  );

  test_assert(tu_is(r_sint, tv_type(r).t));
  test_inteq(*(i32 *)r.v.value, 1);
}

//
// error tests
//

static err_t lower_source_err(allocfn arena, fptr src) {
  let ctx = catch_errcall((parse_source, (arena, src, nullptr)), e, (({ return e; }), (astidx_full){}));
  let env = ss_init(arena, ctx);
  return call_err(lower, (env, env->outermost, ctx.idx)).err;
}

// a declaration without a definition is only a symbol, so there is nothing to instantiate
test_fn(lower_error_function_not_defined) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let e = lower_source_err(
      arena,
      fp(
          "i ~~ @sint;\n"
          "i32 ~~ i(32);\n"
          "f ~~ @fn (),i32;\n"
          "f();\n"
      )
  );
  test_assert(e.err_code);
  test_streq(e.err_code, "FUNCTION_NOT_DEFINED");
}

test_fn(lower_error_too_many_args) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let e = lower_source_err(
      arena,
      fp(
          "type ~~ @type();\n"
          "two ~~ @fn((t ~type),type,{\n"
          "  return @arr(2, t);\n"
          "});\n"
          "two(@sint(8), @sint(16));"
      )
  );
  test_assert(e.err_code);
  test_streq(e.err_code, "WRONG_ARG_COUNT");
  test_assert(e.line > 0);
  test_assert(e.file != nullptr);
}

test_fn(lower_error_builtin_arg_count) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let e = lower_source_err(arena, fp("@cif(1);"));
  test_assert(e.err_code);
  test_streq(e.err_code, "WRONG_ARG_COUNT");
}

test_fn(lower_error_type_not_value) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let e = lower_source_err(arena, fp("@arr(2, 5);"));
  test_assert(e.err_code);
  test_streq(e.err_code, "TYPE_IS_NOT_VALUE");
}

// @at is *l, so it takes a pointer. a referance to an int is not a pointer: reading the value
// out of it first leaves an int, which has nothing to dereference
test_fn(lower_error_at_on_int_reference) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let e = lower_source_err(
      arena,
      fp(
          "type ~~ @type();\n"
          "s32 ~~ @sint(32);\n"
          "x ~s32 = 1;\n"
          "@at x;\n"
      )
  );
  test_assert(e.err_code);
  test_streq(e.err_code, "AT_NOT_REF");
}

test_fn(lower_error_function_cannot_capture_frame) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  // a @fn roots at the module, so a body can never name a variable of the frame it
  // was written inside of
  let e = lower_source_err(
      arena,
      fp(
          "outer ~~ @fn (x ~@int()),@int(),{\n"
          "  hidden ~ @int() = 7;\n"
          "  inner ~~ @fn (y ~@int()),@int(),{ return hidden; };\n"
          "  return inner(1);\n"
          "};\n"
          "outer(1);\n"
      )
  );
  test_assert(e.err_code);
  test_streq(e.err_code, "UNBOUND_ID");
}

test_fn(lower_error_tuple) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let e = lower_source_err(arena, fp("(1, 2);"));
  test_assert(e.err_code);
  test_streq(e.err_code, "TUPLE");
}

test_fn(lower_error_id_requires_astid) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let e = lower_source_err(
      arena,
      fp(
          "type ~~ @type();\n"
          "point ~type~ @struct{ x ~@int(); };\n"
          "@offsetof(point, 5);"
      )
  );
  test_assert(e.err_code);
  test_streq(e.err_code, "ID_REQUIRES_ASTID");
}

test_fn(lower_error_lone_ab) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let e = lower_source_err(arena, fp("[1, 2];"));
  test_assert(e.err_code);
  test_streq(e.err_code, "LONE_AB");
}

test_fn(lower_error_compare_type_with_number) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let e = lower_source_err(arena, fp("(@sint(8) == 3);"));
  test_assert(e.err_code);
  test_streq(e.err_code, "OP_UNSUPPORTED_OPERANDS");
}

test_fn(lower_error_order_types) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let e = lower_source_err(arena, fp("(@sint(8) < @sint(16));"));
  test_assert(e.err_code);
  test_streq(e.err_code, "OP_UNSUPPORTED_OPERANDS");
}

test_fn(lower_error_op_on_struct_without_overload) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let e = lower_source_err(
      arena,
      fp(
          "type ~~ @type();\n"
          "point ~type~ @struct{ x ~@sint(32); };\n"
          "p ~point;\n"
          "(p + p);\n"
      )
  );
  test_assert(e.err_code);
  test_streq(e.err_code, "OP_UNSUPPORTED_OPERANDS");
}

test_fn(interpret_error_yield_empty) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let env = ss_init(arena, (astidx_full){});
  let e = call_err(interpret, (env, env->outermost)).err;
  test_assert(e.err_code);
  test_streq(e.err_code, "YIELD_EMPTY");
}

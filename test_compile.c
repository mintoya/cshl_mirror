#include "ast.h"
#include "ir/interpret.h"
#include "ir/lower.h"
#include "ir/scope.h"
#include "wheels/fptr.h"
#include "wheels/macros.h"

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

// a test whose program stands on its own lives in test/<name>.cshl and runs through this macro,
// which requires the file's last expression to yield a plain, true bool
#define test_file_1(filename)                             \
  test_fn(filename) {                                     \
    let arena = arena_new_ext(allocator, 1 << 16);        \
    defer { arena_cleanup(arena); };                      \
    let ct = parse_source(                                \
        nullptr,                                          \
        arena,                                            \
        nullFptr,                                         \
        "test/" #filename ".cshl"                         \
    );                                                    \
    let ctx = ss_init(arena, ct);                         \
    lower(nullptr, ctx, ctx->outermost, ct.idx);          \
    let result = interpret(nullptr, ctx, ctx->outermost); \
    let rt = ss_type(ctx, result.t);                      \
    test_assert(tu_is(r_bool, rt.t));                     \
    test_assert(!rt.decorations);                         \
    test_assert(*(bool *)result.v.value);                 \
  };

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

test_file_1(interpret_type_fn);

test_file_1(interpret_zero_arg_call);

test_file_1(interpret_global_comptime_mutation);

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

test_file_1(interpret_comptime_recursion);
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

test_file_1(interpret_runtime_recursion);

// a const declaration binds the register it declares in place, so `~~ f()` retypes the call's
// destination register; the callee returns an unqualified value and const says nothing about the
// value's shape, so the return still has to be accepted
test_file_1(const_declaration_retypes_call_destination);

test_file_1(if_cshl);

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
test_file_1(wide_integer_write_extends_the_bigint);

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

test_file_1(struct_operator_overload_vector_equality_true);

test_file_1(struct_cast_ovl);

test_file_1(type_equality_comptime);

test_file_1(type_equality_comptime_false);

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

test_file_1(struct_operator_overload_self_in_body);

test_file_1(struct_as_overload);

// `as` with no overload member is just a cast to the type the right hand side names
test_file_1(as_without_overload_is_a_cast);

// a function cannot capture the frame it was written in, so a closure carries its captures in a
// struct and calls itself through a "()" overload
test_file_1(struct_closure_capture);

// a "()" overload called as the last expression has to yield the call result, not a cast that
// coerced one of its arguments; the call allocates those casts above the result register
test_file_1(overload_call_yields_the_result_after_arg_coercion);

// an explicit @ref capture stores the address, so the call aliases the source
test_file_1(struct_ref_capture);

// a lowered lvalue is a ref, so @where must not address the ref register itself, and @at must give
// back an alias of the pointee
test_file_1(where_at_alias);

// a function declared to return `any` resolves to the concrete type of what it returns,
// so callers can use the result as that type instead of an erased `any`
test_file_1(any_return_adopts_concrete_type);

// integer math lands on the weaker of the two operand types, so a runtime int beats a comptime
// literal and the wider int wins. coercing the operands allocates temporaries above the result, so
// this also pins down that the block still yields the result and not one of those casts
test_file_1(math_coerces_to_the_weaker_int);

// a comptime literal next to a runtime int must not drag the result back to the comptime int
// (the `3 + x` case)
test_file_1(comptime_literal_math_adopts_runtime_type);

test_file_1(math_unsigned_is_weaker_at_the_same_width);

// bxor is a bitwise operator like band and bor: it lands on the weaker of the two ints and toggles
// the bits that only one of the operands carries
test_file_1(bxor_toggles_the_bits);

// two comptime operands have nothing to coerce and still fold to a comptime int
test_file_1(comptime_math_stays_comptime);

// a.b = c has to write into the struct, and reading it back has to see that same member, so the
// access register is the member rather than a copy of it. a variable on the right is copied into
// the member, so s.a is 5 after y changes, not an alias of y
test_file_1(field_assignment_writes_into_the_struct);

// the access is an lvalue inside the struct, so @where on it addresses the member and @at writes
// back through that address, the same way it does for a named local
test_file_1(field_access_is_addressable);

// x[i] is an lvalue inside the array, so writing it writes the element, and a variable on the
// right is copied in rather than bound to the access register and thrown away with it
test_file_1(array_assignment_writes_into_the_array);

test_file_1(array_access_is_addressable);

test_file_1(ast_array);

test_file_1(ast_array_falsy);

test_file_1(sib_fn);

test_file_1(referance_types);

test_file_1(referance_types_i);


test_file_1(forward_declarations_fill_in);

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

// a definition may only complete a declaration that spelled the same signature: the declaration's
// register carries the signature callers coerce their arguments against, so a definition with other
// types or another count would leave the call checked against one and the body written against the
// other
static err_t lower_error_signature_mismatch(allocfn arena, fptr definition) {
  let src = snprint(
      arena,
      "type ~~ @type();\n"
      "i32 ~~ @sint(32);\n"
      "i64 ~~ @sint(64);\n"
      "f ~~ @fn (x ~i32), i32;\n"
      "{slice(c8)}\n",
      definition
  );
  src.len--;
  return lower_source_err(arena, fp(src));
}

test_fn(lower_error_function_signature_mismatch) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };

  let typed = lower_error_signature_mismatch(arena, fp("f ~~ @fn (x ~i64), i64, { return x; };"));
  test_assert(typed.err_code);
  test_streq(typed.err_code, "FUNCTION_SIGNATURE_MISMATCH");

  let arity = lower_error_signature_mismatch(arena, fp("f ~~ @fn (x ~i32, y ~i32), i32, { return x; };"));
  test_assert(arity.err_code);
  test_streq(arity.err_code, "FUNCTION_SIGNATURE_MISMATCH");
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

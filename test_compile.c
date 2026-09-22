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

test_file_1(interpret_type_fn);
test_file_1(interpret_zero_arg_call);
test_file_1(interpret_global_comptime_mutation);
test_file_1(interpret_runtime_recursion);
test_file_1(const_declaration_retypes_call_destination);
test_file_1(if_cshl);
test_file_1(wide_integer_write_extends_the_bigint);
test_file_1(struct_operator_overload_vector_equality_true);
test_file_1(struct_cast_ovl);
test_file_1(type_equality_comptime);
test_file_1(type_equality_comptime_false);
test_file_1(struct_operator_overload_self_in_body);
test_file_1(struct_as_overload);
test_file_1(dot);
test_file_1(union_members_share_offset_zero);
test_file_1(bitstruct_packs_members_by_bit_width);
test_file_1(bitstruct_bit_total_rounds_up_to_bytes);
test_file_1(bitstruct_layout_ignores_base_type_size);
test_file_1(bitstruct_members_read_back_what_was_written);
test_file_1(bitstruct_writes_leave_neighbors_alone);
test_file_1(bitstruct_members_span_byte_boundaries);
test_file_1(bitstruct_signed_member_sign_extends);
test_file_1(bitstruct_packs_least_significant_bit_first);
test_file_1(bitstruct_member_update_reads_the_old_bits);
test_file_1(bitstruct_member_read_is_a_copy);
test_file_1(as_without_overload_is_a_cast);
test_file_1(struct_closure_capture);
test_file_1(overload_call_yields_the_result_after_arg_coercion);
test_file_1(struct_ref_capture);
test_file_1(where_at_alias);
test_file_1(any_return_adopts_concrete_type);
test_file_1(math_coerces_to_the_weaker_int);
test_file_1(comptime_literal_math_adopts_runtime_type);
test_file_1(math_unsigned_is_weaker_at_the_same_width);
test_file_1(bxor_toggles_the_bits);
test_file_1(comptime_math_stays_comptime);
test_file_1(field_assignment_writes_into_the_struct);
test_file_1(field_access_is_addressable);
test_file_1(array_assignment_writes_into_the_array);
test_file_1(array_access_is_addressable);
test_file_1(ast_array);
test_file_1(ast_array_falsy);
test_file_1(sib_fn);
test_file_1(referance_types);
test_file_1(referance_types_i);

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

test_fn(function_can_capture_comptime) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let r = interpret_source(
      arena,
      fp(
          "outer ~~ @fn (x ~@int()),@int(),{\n"
          "  hidden ~ @int() = 7;\n"
          "  inner ~~ @fn (y ~@int()),@int(),{ return hidden + x + y; };\n"
          "  return inner(1);\n"
          "};\n"
          "outer(2) + outer(3);\n"
      ),
      nullptr
  );
  test_assert(tu_is(c_int, tv_type(r).t));
  test_assert(!BInt.cmp(r.v.c_int, BInt.from.i64(arena, 21)));
}

test_fn(lower_error_function_cannot_capture_frame) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  // Runtime locals belong to the enclosing activation, even with a literal initializer.
  let e = lower_source_err(
      arena,
      fp(
          "outer ~~ @fn (x ~@sint(32)),@sint(32),{\n"
          "  hidden ~ @sint(32) = 7;\n"
          "  inner ~~ @fn (y ~@sint(32)),@sint(32),{ return hidden; };\n"
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

test_fn(lower_error_union_member_initializer) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let e = lower_source_err(
      arena,
      fp(
          "type ~~ @type();\n"
          "s32 ~~ @sint(32);\n"
          "U ~type~ @union{ a ~s32 = 1; };\n"
      )
  );
  test_assert(e.err_code);
  test_streq(e.err_code, "UNION_MEMBER_INITIALIZER");
}

// a bitstruct member is a name, a base type and a bit width, so a member that only names a
// type has nothing to lay out
test_fn(lower_error_bitstruct_member_without_width) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let e = lower_source_err(
      arena,
      fp(
          "type ~~ @type();\n"
          "s32 ~~ @sint(32);\n"
          "B ~type~ @bitstruct{ a ~s32; };\n"
      )
  );
  test_assert(e.err_code);
  test_streq(e.err_code, "BITSTRUCT_MEMBER_NO_WIDTH");
}

// a bitfield is a run of bits of one integer, so a member that is not an integer has no bits
// to take
test_fn(lower_error_bitstruct_member_not_integer) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let e = lower_source_err(
      arena,
      fp(
          "type ~~ @type();\n"
          "B ~type~ @bitstruct{ a ~@void():3; };\n"
      )
  );
  test_assert(e.err_code);
  test_streq(e.err_code, "BITSTRUCT_MEMBER_NOT_INTEGER");
}

// a width of zero would name a member with no bits at all
// (the width is spelled the same way a method call is, so it is taken apart here)
test_fn(lower_error_bitstruct_member_zero_width) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let e = lower_source_err(
      arena,
      fp(
          "type ~~ @type();\n"
          "s32 ~~ @sint(32);\n"
          "B ~type~ @bitstruct{ a ~s32:0; };\n"
      )
  );
  test_assert(e.err_code);
  test_streq(e.err_code, "BAD_BIT_WIDTH");
}

// the base type is what a member is read as, so it has to be wide enough to hold the bits
test_fn(lower_error_bitstruct_member_wider_than_base_type) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let e = lower_source_err(
      arena,
      fp(
          "type ~~ @type();\n"
          "s32 ~~ @sint(32);\n"
          "B ~type~ @bitstruct{ a ~s32:33; };\n"
      )
  );
  test_assert(e.err_code);
  test_streq(e.err_code, "BITSTRUCT_MEMBER_TOO_WIDE");
}

// a member of a bitstruct is not addressable: its bits are part of a run, so there is no byte
// address to hand out for it
test_fn(lower_error_bitfield_member_not_addressable) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let e = lower_source_err(
      arena,
      fp(
          "type ~~ @type();\n"
          "u8 ~~ @uint(8);\n"
          "B ~type~ @bitstruct{ a ~u8:3; };\n"
          "x ~B;\n"
          "@where x.a;\n"
      )
  );
  test_assert(e.err_code);
  test_streq(e.err_code, "BITFIELD_NOT_ADDRESSABLE");
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

// Exercise the same heap-backed allocation path as the CLI: an outer arena
// masks attempts to resize temporary-arena storage with the wrong allocator.
test_fn(interpret_recursion_heap_allocator) {
  let heap = debugAllocator(.allocator = stdAlloc);
  defer { debugAllocatorDeInit(heap); };
  let ct = catch_errcall(
      (parse_source, (heap, nullFptr, "test/sib_fn.cshl")),
      e, (err_panic(e), (astidx_full){})
  );
  let ss = ss_init(heap, ct);
  catch_errcall((lower, (ss, ss->outermost, ct.idx)), e, (err_panic(e), (u32)0));
  let result = catch_errcall((interpret, (ss, ss->outermost)), e, (err_panic(e), (typed_value){}));
  test_assert(tu_is(r_bool, ss_type(ss, result.t).t));
  test_assert(*(bool *)result.v.value);
}

test_fn(simplify_void_instructions) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let ss = ss_init(arena, (astidx_full){});
  let s = ss->outermost;
  let v = scope_add_value(ss, s, TV_void(ss));
  let ref = scope_add_value(ss, s, TV_of(ss, P$(ss_type(ss, ss->t_void), ($.isref = true, $))));
  let ptr = scope_add_value(ss, s, TV_of(ss, P$(ss_type(ss, ss->t_void), ($.ptr_level = 1, $))));
  let source = scope_add_value(ss, s, TV_bigint(ss, BInt.from.i64(arena, 5)));
  msList_push(ss->alloc, s->instructions, (ir)tu_of(ir_set, {.l = v, .d = v}));
  msList_push(ss->alloc, s->instructions, (ir)tu_of(ir_getref, {.l = v, .d = ref}));
  msList_push(ss->alloc, s->instructions, (ir)tu_of(ir_putptr, {.l = ptr, .d = ref}));
  msList_push(ss->alloc, s->instructions, (ir)tu_of(ir_farg, {.l = 0, .d = v}));
  msList_push(ss->alloc, s->instructions, (ir)tu_of(ir_getptr, {.l = source, .d = ptr}));
  msList_push(ss->alloc, s->instructions, (ir)tu_of(ir_call, {.l = source, .d = ref, .r = v, .e = v + 1}));
  msList_push(ss->alloc, s->instructions, (ir)tu_of(ir_ret, ref));
  usize values = msList_len(s->values);
  scope_finish(ss, s);
  test_inteq(msList_len(s->instructions), 3);
  test_inteq(msList_len(s->values), values);
  test_inteq(scope_find(ss, s, ref)->t, ss->t_void);
  test_inteq(ss_type(ss, scope_find(ss, s, ptr)->t).ptr_level, 1);
  test_assert(tu_is(ir_getptr, s->instructions[0]));
  test_inteq(s->instructions[1].ir_call.d, s->start + 1);
  test_inteq(s->instructions[1].ir_call.r, v);
  test_inteq(s->instructions[1].ir_call.e, v + 1);
  test_inteq(s->instructions[2].ir_ret, s->start + 1);
  scope_finish(ss, s);
  test_inteq(msList_len(s->instructions), 3);
}

test_fn(simplify_void_calls_keep_effects_and_returns) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let r = interpret_source(
      arena,
      fp(
          "counter ~@sint(32) = 0;"
          "bump ~~ @fn((unused ~@void(), amount ~@sint(32)), @void(), {"
          "  counter = counter + amount;"
          "  return();"
          "  counter = 99;"
          "});"
          "bump({;}, 3);"
          "bump({;}, 4);"
          "counter == 7;"
      ),
      nullptr
  );
  test_assert(tu_is(r_bool, tv_type(r).t));
  test_assert(*(bool *)r.v.value);
}

test_fn(simplify_preserves_void_scope_result) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let r = interpret_source(arena, fp("5; {;};"), nullptr);
  test_assert(tu_is(r_void, tv_type(r).t));
}

test_fn(comptime_type_decorations) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let r = interpret_source(
      arena,
      fp("@comptime(@const(@sint(32)));"),
      nullptr
  );
  test_assert(tu_is(c_type, tv_type(r).t));
  let t = type_at(r.env, r.v.c_type);
  test_assert(t.iscomptime);
  test_assert(t.isconst);
  test_assert(tu_is(r_sint, t.t));
  test_inteq(t.t.r_sint.bits, 32);
}

test_fn(quote_operator_is_removed) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let e = lower_source_err(arena, fp("macro' (1);"));
  test_assert(e.err_code);
  test_streq(e.err_code, "PARSE_ERROR");
}

test_fn(inlineast_expands_at_call_site) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let r = interpret_source(arena, fp(
      "twice ~~ @fn((a ~@ast()), @inlineast(), {"
      "  return @qot(@uqot(a) + @uqot(a));"
      "});"
      "x ~@sint(32) = 6;"
      "twice(x + 1) == 14;"
  ), nullptr);
  test_assert(tu_is(r_bool, tv_type(r).t));
  test_assert(*(bool *)r.v.value);
}

test_fn(inlineast_cached_calls_use_caller_scope) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let r = interpret_source(arena, fp(
      "read ~~ @fn((), @inlineast(), { return @qot(local); });"
      "first ~~ @fn((), @sint(32), { local ~@sint(32) = 3; return read(); });"
      "second ~~ @fn((), @sint(32), { local ~@sint(32) = 7; return read(); });"
      "first() + second() == 10;"
  ), nullptr);
  test_assert(tu_is(r_bool, tv_type(r).t));
  test_assert(*(bool *)r.v.value);
}

test_fn(inlineast_can_return_from_caller) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let r = interpret_source(arena, fp(
      "exit ~~ @fn((a ~@ast()), @inlineast(), { return @qot(return @uqot(a)); });"
      "f ~~ @fn((x ~@sint(32)), @sint(32), { exit(x + 1); return 99; });"
      "f(8) == 9;"
  ), nullptr);
  test_assert(tu_is(r_bool, tv_type(r).t));
  test_assert(*(bool *)r.v.value);
}

test_fn(inlineast_apply_and_nested_expansion) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let r = interpret_source(arena, fp(
      "identity ~~ @fn((a ~@ast()), @inlineast(), { return a; });"
      "wrap ~~ @fn((a ~@ast()), @inlineast(), { return @qot(identity(@uqot(a))); });"
      "@apply(wrap, @qot(2 + 3)) == 5;"
  ), nullptr);
  test_assert(tu_is(r_bool, tv_type(r).t));
  test_assert(*(bool *)r.v.value);
}

test_fn(ordinary_ast_return_stays_data) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let r = interpret_source(arena, fp(
      "quote ~~ @fn((a ~@ast()), @ast(), { return a; });"
      "quote(undefined_name);"
  ), nullptr);
  test_assert(tu_is(c_ast, tv_type(r).t));
  test_assert(r.v.c_ast);
}

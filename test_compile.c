#include "ast.h"
#include "cout.h"
#include "ir/interpret.h"
#include "ir/lower.h"
#include "ir/scope.h"
#include "wheels/allocators/debugallocator.h"
#include "wheels/fptr.h"
#include "wheels/macros.h"

astidx_full parse_source errs(allocfn out_allocator, fptr input, const char *filename);

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
static Type tv_type(interp x) { return Type_at(x.env, x.t); }

#define test_file_1(filename)                                                            \
  test_fn(filename) {                                                                    \
    let arena = arena_new_ext(allocator, 1 << 16);                                       \
    defer { arena_cleanup(arena); };                                                     \
    let ct = parse_source(                                                               \
        nullptr,                                                                         \
        arena,                                                                           \
        nullFptr,                                                                        \
        "test/" #filename ".cshl"                                                        \
    );                                                                                   \
    let ctx = ss_init(arena, ct);                                                        \
    lower(nullptr, ctx, ctx->outermost, ct.idx);                                         \
    scope_finish(ctx, ctx->outermost);                                                   \
    char cpath[256], exe_path[256], command[1024];                                       \
    snprintf(cpath, sizeof(cpath), "test/" #filename ".cshl.c");                         \
    snprintf(exe_path, sizeof(exe_path), "%s.exe", cpath);                               \
    FILE *out = fopen(cpath, "w");                                                       \
    test_assert(out);                                                                    \
    printerfunction_context pctx =                                                       \
        {.put = fileprint, .arb = out, .allocator = arena};                              \
    makeC(ctx, fileprint, pctx);                                                         \
    test_inteq(0, fclose(out));                                                          \
    snprintf(                                                                            \
        command, sizeof(command), "clang -std=c99 -O0 -o \"%s\" \"%s\"", exe_path, cpath \
    );                                                                                   \
    test_inteq(0, system(command));                                                      \
    snprintf(command, sizeof(command), "\"%s\"", exe_path);                              \
    test_inteq(0, system(command));                                                      \
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
  let elem = Type_at(r.env, arr.t);
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
test_file_1(interpret_curried_array_type);
test_file_1(interpret_zero_arg_call);
test_file_1(interpret_global_comptime_mutation);
test_file_1(interpret_runtime_recursion);
test_file_1(const_declaration_retypes_call_destination);
test_file_1(if_cshl);
test_file_1(external_c_binding);
test_file_1(include);
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
test_file_1(bitstruct_reference_alias);
test_file_1(bitstruct_nested_member);
test_file_1(bitstruct_inline_member_gets_layout);
test_file_1(as_without_overload_is_a_cast);
test_file_1(explicit_pointer_cast);
test_file_1(reference_value_reads);
test_file_1(typeinfo_namespaces);
test_file_1(return_type_in_macro);

test_fn(interpret_return_type_in_macro) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let ctx = parse_source(nullptr, arena, nullFptr, "test/return_type_in_macro.cshl");
  let env = ss_init(arena, ctx);
  lower(nullptr, env, env->outermost, ctx.idx);
  let result = interpret(nullptr, env, env->outermost);
  test_assert(tu_is(r_bool, ss_type(env, result.t).t));
  test_assert(*(bool *)result.v.value);
}

test_fn(interpret_typeinfo_namespaces) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let ctx = parse_source(nullptr, arena, nullFptr, "test/typeinfo_namespaces.cshl");
  let env = ss_init(arena, ctx);
  lower(nullptr, env, env->outermost, ctx.idx);
  let result = interpret(nullptr, env, env->outermost);
  test_assert(tu_is(r_bool, ss_type(env, result.t).t));
  test_assert(*(bool *)result.v.value);
}

test_fn(interpret_reference_value_reads) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let ctx = parse_source(nullptr, arena, nullFptr, "test/reference_value_reads.cshl");
  let env = ss_init(arena, ctx);
  lower(nullptr, env, env->outermost, ctx.idx);
  let result = interpret(nullptr, env, env->outermost);
  test_assert(tu_is(r_bool, ss_type(env, result.t).t));
  test_assert(*(bool *)result.v.value);
}

test_fn(interpret_explicit_pointer_cast) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let ctx = parse_source(nullptr, arena, nullFptr, "test/explicit_pointer_cast.cshl");
  let env = ss_init(arena, ctx);
  lower(nullptr, env, env->outermost, ctx.idx);
  let result = interpret(nullptr, env, env->outermost);
  test_assert(*(bool *)result.v.value);
}

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
test_file_1(anon_struct_named_members);
test_file_1(anon_struct_numbered_members);
test_file_1(anon_struct_member_holds_a_type);
test_file_1(struct_assignment_calls_the_eq_overload);
test_file_1(field_access_is_addressable);
test_file_1(array_assignment_writes_into_the_array);
test_file_1(array_access_is_addressable);
test_file_1(ast_array);
test_file_1(ast_array_falsy);
test_file_1(sib_fn);
test_file_1(referance_types);
test_file_1(referance_types_i);
test_file_1(cout_loop_control);
test_file_1(cout_signed_shifts);
test_file_1(cout_native_wide_casts);
test_file_1(cout_packed_return);
test_file_1(cout_non_power_alignment);
test_file_1(cout_union_bool);

static err_t lower_source_err(allocfn arena, fptr src) {
  let ctx = catch_errcall((parse_source, (arena, src, nullptr)), e, (({ return e; }), (astidx_full){}));
  let env = ss_init(arena, ctx);
  return call_err(lower, (env, env->outermost, ctx.idx)).err;
}

static char *error_loc_test_detail(void *data) { return data; }

test_fn(error_locations_keep_independent_errors) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let first = lower_source_err(arena, fp("missing_one;"));
  test_assert(error_loc_get(first));
  let primary = error_loc_primary(first);
  test_assert(primary);
  test_fpeq(fp_from_srcpos(primary->source), "missing_one");
  char *saved = first.fn(first.fnd);
  let second = lower_source_err(arena, fp("missing_two;"));
  test_assert(error_loc_get(second));
  test_fpeq(fp_from_srcpos(error_loc_primary(second)->source), "missing_two");
  test_assert(strstr(second.fn(second.fnd), "line 1"));
  test_assert(first.fn(first.fnd) == saved);
  test_fpeq(fp_from_srcpos(error_loc_primary(first)->source), "missing_one");
  test_streq(first.err_code, "UNBOUND_ID");
}

test_fn(error_locations_keep_long_chains_and_foreign_details) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let ctx = parse_source(nullptr, arena, fp("x;"), nullptr);
  srcpos_full source = {srcpos_ast(sglist_get(ctx.alist, ctx.idx - 1), ctx.alist), ctx.plist, ctx.flist};
  err_t original = {.err_code = "TEST_ERROR", .file = __FILE__, .line = __LINE__,
                   .function = __FUNCTION__, .fn = error_loc_test_detail, .fnd = "original detail"};
  err_t error = error_loc_add(arena, original, source);
  let primary = error_loc_primary(error);
  err_t saved = error;
  for (usize i = 0; i < 128; i++) error = error_loc_add(arena, error, source);
  usize count = 0;
  for (let node = error_loc_get(error); node; node = error_loc_next(node)) count++;
  test_inteq(count, 129);
  test_assert(error_loc_primary(error) == primary);
  test_assert(!error_loc_next(error_loc_get(saved)));
  char *message = error.fn(error.fnd);
  test_assert(strlen(message) > 1024);
  test_assert(strstr(message, "original detail"));
  test_assert(error.fn(error.fnd) == message);
  test_streq(error.err_code, original.err_code);
  test_inteq(error.line, original.line);
}

test_fn(parser_errors_use_structured_locations) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let parsed = call_err(parse_source, (arena, fp("x ~@sint(32) = );"), nullptr));
  test_streq(parsed.err.err_code, "PARSE_ERROR");
  // parse_source lives in another translation unit; its payload is still recognized.
  let primary = error_loc_primary(parsed.err);
  test_assert(primary);
  test_assert(primary->source.pidx && primary->source.flist.len == 1);
  test_assert(strstr(parsed.err.fn(parsed.err.fnd), "line 1"));
  let wrapped = error_loc_add(arena, parsed.err, primary->source);
  test_assert(error_loc_next(error_loc_get(wrapped)) == primary);
}

test_fn(array_call_checks_generated_index_instruction) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let e = lower_source_err(arena, fp("a ~@arr(2, @sint(32)); x ~@sint(32); a[@where(x)];"));
  test_assert(e.err_code);
  test_streq(e.err_code, "ARRAY_INDEX_NON_INT");
}

test_fn(pointer_casts_are_never_implicit) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  fptr cases[] = {
    fp("x ~@sint(32); p ~@ptr(@uint(8)) = @where(x);"),
    fp("x ~@sint(32); p ~@ptr(@uint(8)); p = @where(x);"),
    fp("f ~~ @fn((p ~@ptr(@uint(8))), @void(), {}); x ~@sint(32); f(@where(x));"),
    fp("f ~~ @fn((p ~@ptr(@sint(32))), @ptr(@uint(8)), { return p; }); x ~@sint(32); f(@where(x));"),
    fp("x ~@sint(32); @if(@where(x), {});"),
  };
  foreach (let src, span(cases, countof(cases))) {
    let e = lower_source_err(arena, *src);
    test_assert(e.err_code);
    test_streq(e.err_code, "CANNOT_CAST");
  }
}

test_fn(pointer_cast_requires_pointer_values) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  fptr cases[] = {
    fp("1 as @ptr(@uint(8));"),
    fp("x ~@sint(32); @where(x) as @uint(64);"),
    fp("x ~@sint(32); @where(x) as @refT(@ptr(@uint(8)));"),
  };
  foreach (let src, span(cases, countof(cases))) {
    let e = lower_source_err(arena, *src);
    test_assert(e.err_code);
    test_streq(e.err_code, "CANNOT_CAST_POINTER");
  }
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

test_fn(lower_error_return_outside_function) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let e = lower_source_err(
      arena,
      fp(
          "ret ~~ @fn((a ~@ast()), @inlineast(), { return @qot{ return @uqot(a); }; });\n"
          "ret(1);\n"
      )
  );
  test_assert(e.err_code);
  test_streq(e.err_code, "RETURN_OUTSIDE_FUNCTION");
  test_assert(e.line > 0);
  test_assert(e.file != nullptr);
}

test_fn(lower_error_bare_top_level_return) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let e = lower_source_err(arena, fp("return 1;"));
  test_assert(e.err_code);
  test_streq(e.err_code, "RETURN_OUTSIDE_FUNCTION");
}

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

test_fn(lower_error_bitfield_to_normal_reference) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let e = lower_source_err(arena, fp("type ~~ @type();\n"
                                     "u8 ~~ @uint(8);\n"
                                     "B ~type~ @bitstruct { a ~u8:3; };\n"
                                     "x ~B;\n"
                                     "r ~@refT(u8) = x.a;\n"));
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

test_fn(lower_error_set_through_const_destination) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let e = lower_source_err(
      arena,
      fp(
          "type ~~ @type();\n"
          "u32 ~~ @uint(32);\n"
          "void ~~ @void();\n"
          "S ~~ @struct{\n"
          "  Self ~type~ @this();\n"
          "  v ~ u32;\n"
          "  @id(\"=\") ~~ @fn (this ~@refT(Self), val ~u32), @void(), { this.v = val; };\n"
          "};\n"
          "s ~ @const(S);\n"
          "s = 5;\n"
      )
  );
  test_assert(e.err_code);
  test_streq(e.err_code, "CANNOT_CAST");
}

// An outer arena would mask temporary-storage allocator mismatches.
test_fn(interpret_recursion_heap_allocator) {
  let heap = debugAllocator(.allocator = stdAlloc);
  defer { debugAllocatorDeInit(heap); };
  let ct = catch_errcall(
      (parse_source, (heap, nullFptr, "test/sib_fn.cshl")),
      e,
      (err_panic(e), (astidx_full){})
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
  let ref = scope_add_value(ss, s, TV_of(ss, P$(ss_type(ss, ss->t_void), ($.ref_kind = ref_ref, $))));
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
  let t = Type_at(r.env, r.v.c_type);
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

test_fn(lower_error_comptime_return_runtime_argument) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  fptr sources[] = {
      fp("f ~~ @fn((n ~@sint(32)), @ast(), { return @qot(x); }); f(1);"),
      fp("f ~~ @fn((n ~@sint(32)), @any(), { return @qot(x); }); f(1);"),
      fp("f ~~ @fn((n ~@sint(32)), @inlineast(), { return @qot(1); }); f(1);"),
      fp("f ~~ @fn((n ~@sint(32)), @type(), { return @sint(8); }); f(1);"),
      fp("T ~~ @struct { x ~@sint(32); }; x ~T;"
         "f ~~ @fn((self ~@refT(T)), @any(), { return @qot(self.x); }); f(x);"),
  };
  foreach (let src, span(sources, countof(sources))) {
    let e = lower_source_err(arena, *src);
    test_assert(e.err_code);
    test_streq(e.err_code, "COMPTIME_RETURN_RUNTIME_ARGUMENT");
  }
}

test_fn(inlineast_expands_at_call_site) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let r = interpret_source(arena, fp("twice ~~ @fn((a ~@ast()), @inlineast(), {"
                                     "  return @qot(@uqot(a) + @uqot(a));"
                                     "});"
                                     "x ~@sint(32) = 6;"
                                     "twice(x + 1) == 14;"),
                           nullptr);
  test_assert(tu_is(r_bool, tv_type(r).t));
  test_assert(*(bool *)r.v.value);
}

test_fn(inlineast_cached_calls_use_caller_scope) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let r = interpret_source(arena, fp("read ~~ @fn((), @inlineast(), { return @qot(local); });"
                                     "first ~~ @fn((), @sint(32), { local ~@sint(32) = 3; return read(); });"
                                     "second ~~ @fn((), @sint(32), { local ~@sint(32) = 7; return read(); });"
                                     "first() + second() == 10;"),
                           nullptr);
  test_assert(tu_is(r_bool, tv_type(r).t));
  test_assert(*(bool *)r.v.value);
}

test_fn(inlineast_can_return_from_caller) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let r = interpret_source(arena, fp("exit ~~ @fn((a ~@ast()), @inlineast(), { return @qot(return @uqot(a)); });"
                                     "f ~~ @fn((x ~@sint(32)), @sint(32), { exit(x + 1); return 99; });"
                                     "f(8) == 9;"),
                           nullptr);
  test_assert(tu_is(r_bool, tv_type(r).t));
  test_assert(*(bool *)r.v.value);
}

test_fn(inlineast_apply_and_nested_expansion) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let r = interpret_source(arena, fp("identity ~~ @fn((a ~@ast()), @inlineast(), { return a; });"
                                     "wrap ~~ @fn((a ~@ast()), @inlineast(), { return @qot(identity(@uqot(a))); });"
                                     "@apply(wrap, @qot(2 + 3)) == 5;"),
                           nullptr);
  test_assert(tu_is(r_bool, tv_type(r).t));
  test_assert(*(bool *)r.v.value);
}

test_fn(ordinary_ast_return_stays_data) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let r = interpret_source(arena, fp("quote ~~ @fn((a ~@ast()), @ast(), { return a; });"
                                     "quote(undefined_name);"),
                           nullptr);
  test_assert(tu_is(c_ast, tv_type(r).t));
  test_assert(r.v.c_ast);
}

test_fn(interpret_packed_reads_bounded_memory) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let ctx = parse_source(nullptr, arena, fp("B ~~ @bitstruct { a ~@sint(32):5; b ~@sint(32):7; };"
                                            "x ~B; x.a = 7; x.b = 9; i ~@sint(32) = 0;"
                                            "@loop({ @if(i >= 10000, { break(); });"
                                            "x.a = x.b - 2; i = i + 1; });"
                                            "(x.a == 7) and (x.b == 9);"),
                         nullptr);
  let ss = ss_init(arena, ctx);
  lower(nullptr, ss, ss->outermost, ctx.idx);
  scope_finish(ss, ss->outermost);
  let is = intstate_new(ss);
  intstate_pushs(is, ss->outermost, (slice(typed_value)){}, 0);
  usize initial = arena_totalMem(is->temp);
  intstate_run(nullptr, is);
  test_inteq(arena_totalMem(is->temp), initial);
  test_assert(arena_totalMem(is->reads) <= 4096);
  let result = intstate_value(is, intstate_pop(is));
  test_assert(*(bool *)result.v.value);
  mxmap_deinit(is->vmap);
  msList_deInit(ss->alloc, is->cs);
  arena_cleanup(is->reads);
  arena_cleanup(is->temp);
  if (is->scratch) arena_cleanup(is->scratch);
}

test_fn(typeinfo_reads_integer_widths) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let r = interpret_source(
      arena,
      fp("s ~~ @sint(12);\n"
         "u ~~ @uint(20);\n"
         "@typeinfo(s).sint.bits + @typeinfo(u).uint.bits;\n"),
      nullptr
  );
  test_assert(tu_is(c_int, tv_type(r).t));
  test_assert(!BInt.cmp(r.v.c_int, BInt.from.i64(arena, 32)));
}

static char *typeinfo_struct_source =
    "S ~~ @struct{ a ~@sint(32); b ~@sint(8); };\n"
    "@sizeof(@typeinfo(S).struct.0) + @sizeof(@typeinfo(S).struct.1);\n";

static char *typeinfo_fn_source =
    "f ~~ @fn((a ~@sint(8)), @sint(64), { return a; });\n"
    "(@typeinfo(@typeof(f)).fn.args * 10)\n"
    "  + @sizeof(@typeinfo(@typeof(f)).fn.ret)\n"
    "  + @sizeof(@typeinfo(@typeof(f)).fn.0);\n";

test_fn(typeinfo_reads_struct_members) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let r = interpret_source(arena, fp(typeinfo_struct_source), nullptr);
  test_assert(tu_is(c_int, tv_type(r).t));
  test_assert(!BInt.cmp(r.v.c_int, BInt.from.i64(arena, 5)));
}

test_fn(typeinfo_reads_function_signature) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let r = interpret_source(arena, fp(typeinfo_fn_source), nullptr);
  test_assert(tu_is(c_int, tv_type(r).t));
  test_assert(!BInt.cmp(r.v.c_int, BInt.from.i64(arena, 19)));
}

static char *typeinfo_selector_source =
    "ti ~@typeinfo() = @typeinfo(@sint(9));\n"
    "ti.sint.sint.bits;\n";

test_fn(typeinfo_selector_keeps_matching_kind) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let r = interpret_source(arena, fp(typeinfo_selector_source), nullptr);
  test_assert(tu_is(c_int, tv_type(r).t));
  test_assert(!BInt.cmp(r.v.c_int, BInt.from.i64(arena, 9)));
}

static char *typeinfo_kind_condition_source =
    "r ~@sint(32) = 0;\n"
    "@cif(@typeinfo(@sint(9)).sint, { r = 1; }, { r = 2; });\n"
    "@cif(@typeinfo(@sint(9)).uint, { r = r + 10; }, { r = r + 20; });\n"
    "r;\n";

test_fn(typeinfo_kind_is_a_condition) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let r = interpret_source(arena, fp(typeinfo_kind_condition_source), nullptr);
  let rt = tv_type(r);
  test_assert(tu_is(r_sint, rt.t));
  test_inteq(tu_catch(r_sint, rt.t).bits, 32);
  test_assert(*(i32 *)r.v.value == 21);
}

test_fn(return_type_in_compile_time_child_scope) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let result = interpret_source(arena, fp(
      "outer ~~ @fn((), @sint(32), {"
      "  inner ~~ @fn((), @sint(8), {"
      "    @cif(@returnT() == @sint(8), { return 7; }); return 0;"
      "  });"
      "  @cif(@returnT() == @sint(32), {"
      "    @cif(@typeinfo(@returnT()).sint.bits == 32, { return inner(); });"
      "  }); return 0;"
      "}); outer() == 7;"), nullptr);
  test_assert(tu_is(r_bool, tv_type(result).t));
  test_assert(*(bool *)result.v.value);
}

test_fn(return_type_outside_function_still_errors) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let e = lower_source_err(arena, fp("@cif(@returnT() == @void(), {});"));
  test_assert(e.err_code);
  test_streq(e.err_code, "RETURN_TYPE_OUTSIDE_FUNCTION");
}

test_fn(lower_error_typeinfo_on_non_type) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let e = lower_source_err(arena, fp("@typeinfo(5);"));
  test_assert(e.err_code);
  test_streq(e.err_code, "TYPE_IS_NOT_VALUE");
}

test_fn(lower_error_typeinfo_arg_count) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let e = lower_source_err(arena, fp("@typeinfo(@sint(8), @sint(8));"));
  test_assert(e.err_code);
  test_streq(e.err_code, "WRONG_ARG_COUNT");
}

static char *typeinfo_error_sources[] = {
    "@typeinfo(@struct{ x ~@sint(8); }).struct.bits;",
    "@typeinfo(@sint(8)).sint.7;",
};
static char *typeinfo_error_codes[] = {
    "TYPEINFO_NOT_INTEGER",
    "TYPEINFO_NO_MEMBER",
};

test_fn(lower_error_typeinfo_missing_payload) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  foreach (let i, range(0, countof(typeinfo_error_sources))) {
    let e = lower_source_err(arena, fp(typeinfo_error_sources[i]));
    test_assert(e.err_code);
    test_streq(e.err_code, typeinfo_error_codes[i]);
  }
}

static char *reserved_id_sources[] = {
    "_ ~@sint(32) = 1;",
    "#_ ~@sint(32) = 1;",
    "@id(\"_\") ~@sint(32) = 1;",
    "@id(\"#_\") ~@sint(32) = 1;",
    "f ~~ @fn((_ ~@sint(32)), @void(), {;});",
    "S ~~ @struct{ #_ ~@sint(32) ~ 1; };",
};

test_fn(lower_error_binding_reserved_ids) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  foreach (let i, range(0, countof(reserved_id_sources))) {
    let e = lower_source_err(arena, fp(reserved_id_sources[i]));
    test_assert(e.err_code);
    test_streq(e.err_code, "RESERVED_ID");
    test_assert(e.line > 0);
  }
}

test_fn(pipe_binds_its_own_underscore) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let result = interpret_source(arena, fp("double ~~ @fn((x ~@sint(32)), @sint(32), { return x * 2; });"
                                          "3 |> double(_) == 6;"),
                                nullptr);
  test_assert(*(bool *)result.v.value);
}

test_fn(interpret_packed_return_survives_next_instruction) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let result = interpret_source(arena, fp("B ~~ @bitstruct { a ~@sint(32):5; };"
                                          "f ~~ @fn((n ~@sint(32)), @sint(32), {"
                                          "x ~B; x.a = n; return x.a; });"
                                          "(f(0 - 3) + f(7)) == 4;"),
                                nullptr);
  test_assert(*(bool *)result.v.value);
}

test_file_1(namespace_import);

test_fn(namespace_import_and_using) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let result = interpret_source(arena, fp(
      "lib ~~ @import(\"test/namespaces/wrapper.cshl\");"
      "@using(lib); @using(lib.math);"
      "wrapped() + lib.math.add(2);"), nullptr);
  test_inteq(*(i32 *)result.v.value, 84);
}

test_fn(namespace_values_and_cache) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let result = interpret_source(arena, fp(
      "a ~~ @import(\"test/namespaces/math.cshl\");"
      "b ~~ @import(\"test/namespaces/./math.cshl\");"
      "@using(a); @using(b);"
      "read ~~ @fn (ns ~@typeof(a)), @int(), { return ns.answer; };"
      "read(a) + answer;"), nullptr);
  test_assert(!BInt.cmp(result.v.c_int, BInt.from.i64(arena, 80)));
}

test_fn(namespace_using_lexical_precedence) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let result = interpret_source(arena, fp(
      "answer ~~ 2;"
      "inside ~~ { @using(@import(\"test/namespaces/math.cshl\")); answer; };"
      "inside + answer;"), nullptr);
  test_assert(!BInt.cmp(result.v.c_int, BInt.from.i64(arena, 42)));
}

test_fn(namespace_errors) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  struct { const char *src, *error; } cases[] = {
    {"@using(1);", "USING_NOT_NAMESPACE"},
    {"@import(\"test/namespaces\");", "IMPORT_NOT_FILE"},
    {"@import(1);", "IMPORT_NOT_STRING"},
    {"@import(\"test/namespaces/missing.cshl\");", "IMPORT_FILE_NOT_FOUND"},
    {"caller_only ~~ 1; @import(\"test/namespaces/caller.cshl\");", "UNBOUND_ID"},
    {"lib ~~ @import(\"test/namespaces/math.cshl\"); lib.missing;", "NO_MEMBER"},
    {"lib ~~ @import(\"test/namespaces/math.cshl\"); lib.@int;", "NO_MEMBER"},
    {"lib ~~ @import(\"test/namespaces/math.cshl\"); answer;", "UNBOUND_ID"},
    {"{ @using(@import(\"test/namespaces/math.cshl\")); }; answer;", "UNBOUND_ID"},
    {"@using(@import(\"test/namespaces/math.cshl\")); answer = 2;", "ASSIGN_IMPORTED_NAME"},
    {"@using(@import(\"test/namespaces/math.cshl\"));"
     "@using(@import(\"test/namespaces/other.cshl\")); answer;", "AMBIGUOUS_NAME"},
  };
  foreach (let item, vla(cases)) {
    let error = lower_source_err(arena, fp((char *)item.src));
    test_assert(error.err_code);
    test_streq(error.err_code, item.error);
  }
}

test_file_1(namespace_runtime);

test_fn(namespace_runtime_interpret) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let result = interpret_source(arena, fp(
      "state ~~ @import(\"test/namespaces/state.cshl\");"
      "before ~state.i32 = state.value;"
      "first ~~ @import(\"test/namespaces/first.cshl\");"
      "middle ~state.i32 = state.value;"
      "@using(@import(\"test/namespaces/second.cshl\"));"
      "again ~~ @import(\"test/namespaces/first.cshl\");"
      "bump();"
      "before + middle + value;"), nullptr);
  test_inteq(*(i32 *)result.v.value, 137);
}

#define MY_TEST_FRAMEWORK_C (1)
#define TESTS_SUBPROCESSES (1)
#include <wheels/tests.h>

#if __has_include(<windows.h>)
  #define popen _popen
  #define pclose _pclose
#endif
// so output survives a crash mid-test
__attribute__((constructor)) static void unbuffer_test_output(void) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  setvbuf(stderr, nullptr, _IONBF, 0);
}

#include "cout.h"
#include "indentprinter.h"
#include "test_compile.c"

test_fn(owned_allocator_reclaims_and_cleans_up) {
  let heap = debugAllocator(.allocator = stdAlloc);
  defer { debugAllocatorDeInit(heap); };
  let baseline = debugAllocator_stats(heap);
  let owned = owned_allocator_new(heap);
  let a = acreate(owned, u8[32]);
  let b = acreate(owned, u8[64]);
  let c = acreate(owned, u8[96]);
  (*b)[0] = 42;
  test_inteq(0, (uptr)b % alignof(myAlign));
  let grown = aresize(owned, b, u8[65536]);
  test_inteq(42, (*grown)[0]);
  test_inteq(0, (uptr)grown % alignof(myAlign));
  let before_free = debugAllocator_stats(heap);
  adestroy(owned, a);
  let after_free = debugAllocator_stats(heap);
  test_assert(after_free.current_memory < before_free.current_memory);
  test_inteq(before_free.total_active_allocations - 1, after_free.total_active_allocations);
  let shrunk = aresize(owned, grown, u8[16]);
  test_inteq(42, (*shrunk)[0]);
  // Leave the resized block and c live: bulk cleanup must release both.
  (void)c;
  owned_allocator_cleanup(owned);
  let after = debugAllocator_stats(heap);
  test_inteq(baseline.current_memory, after.current_memory);
  test_inteq(baseline.total_active_allocations, after.total_active_allocations);
}

test_fn(compiler_owned_allocator_cleanup) {
  let heap = debugAllocator(.allocator = stdAlloc);
  defer { debugAllocatorDeInit(heap); };
  let ctx = parse_source(nullptr, heap,
      fp("f ~~ @fn((a ~@ast()), @inlineast(), { return a; }); f(2 + 3);"), nullptr);
  let before = debugAllocator_stats(heap);
  let ss = ss_init(heap, ctx);
  lower(nullptr, ss, ss->outermost, ctx.idx);
  let result = interpret(nullptr, ss, ss->outermost);
  test_assert(tu_is(c_int, ss_type(ss, result.t).t));
  test_assert(!BInt.cmp(result.v.c_int, BInt.from.i64(ss->alloc, 5)));
  ss_deInit(ss);
  let after = debugAllocator_stats(heap);
  test_inteq(before.current_memory, after.current_memory);
  test_inteq(before.total_active_allocations, after.total_active_allocations);
}

test_fn(cout_struct_layout) {
  let arena = arena_new_ext(allocator, 1024);
  defer { arena_cleanup(arena); };
  typestructlist items = {.allocator = arena};
  let word = T(TypeTree_uint(32));
  sglist_push(items, (r_struct_item){.t = word, .alignment = 4, .offset = 8});
  sglist_push(items, (r_struct_item){.t = word, .alignment = 4, .offset = 0});
  sglist_push(items, (r_struct_item){.t = T(tu_of(c_type, {})), .offset = 1});
  typeenv e = {.sitems = &items};
  r_struct s = {.start = 1};
  let a = type_struct_item(e, s, 0);
  let b = type_struct_item(e, s, 1);
  test_inteq(false, out_structIsUnnatural(e, (r_struct){}));
  test_inteq(false, out_structIsUnnatural(e, s)); // unordered, with a gap
  a->offset = 4;
  test_inteq(false, out_structIsUnnatural(e, s)); // touching ranges
  a->offset = 2;
  test_inteq(true, out_structIsUnnatural(e, s)); // partial overlap
  a->offset = 0;
  test_inteq(false, out_structIsUnnatural(e, s)); // union, ignoring comptime
  b->alignment = 1;
  test_inteq(true, out_structIsUnnatural(e, s)); // packed union
  b->alignment = 4;
  a->offset = b->offset = 4;
  test_inteq(true, out_structIsUnnatural(e, s)); // overlap away from zero
  b->offset = 8;
  a->alignment = 8;
  test_inteq(true, out_structIsUnnatural(e, s)); // over-aligned member
}

// Compile the declarations themselves: their static assertions check every
// member offset, size, and alignment, including layouts source syntax cannot
// currently construct.
test_fn(cout_layout_declarations) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let ctx = parse_source(nullptr, arena, fp(";"), nullptr);
  let ss = ss_init(arena, ctx);
  let word = T(TypeTree_uint(32));
  ss_type_intern(ss, word);
  const u32 offsets[][3] = {{8, 0, 16}, {0, 0, 0}, {0, 0, 8}, {0, 2, 4}, {0, 4, 8}};
  foreach (let k, range(0, countof(offsets))) {
    let start = ss_member_reserve(ss, 3);
    foreach (let j, range(0, 3)) {
      sglist_get(ss->sitems, start - 1 + j) = (r_struct_item){
          .t = word,
          .name = ss_srcpos_of_text(ss, (fptr){.ptr = (u8 *)&"abc"[j], .len = 1}),
          .offset = offsets[k][j],
          .alignment = k == 3 ? 1 : k == 4 ? 8
                                           : 4,
      };
    }
    ss_type_intern(ss, T(tu_of(r_struct, {.start = start})));
  }
  ss_type_intern(ss, T(tu_of(r_struct, {.start = ss_member_reserve(ss, 0)})));
  foreach (let offset, range(0, 5)) {
    let start = ss_member_reserve(ss, 2);
    sglist_get(ss->sitems, start - 1) = (r_struct_item){
        .t = word,
        .name = ss_srcpos_of_text(ss, fp("word")),
        .alignment = 4,
    };
    sglist_get(ss->sitems, start) = (r_struct_item){
        .t = T(tu_of(r_void, {})),
        .name = ss_srcpos_of_text(ss, fp("empty")),
        .alignment = 1,
        .offset = offset,
    };
    let t = T(tu_of(r_struct, {.start = start}));
    test_assert(!out_structIsUnnatural(ss_typeenv(ss), t.t.r_struct));
    ss_type_intern(ss, t);
  }
  let pipe = popen("clang -std=c23 -x c -fsyntax-only -", "w");
  test_assert(pipe);
  printerfunction_context pctx = {.put = fileprint, .arb = pipe, .allocator = arena};
  out_runtime(pctx);
  let names = makeTypes(ss, pctx);
  defer { freeTypeNames(arena, names); };
  test_inteq(0, pclose(pipe));
}

test_fn(cout_preserves_ir_values) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let ctx = parse_source(nullptr, arena, fp("x ~@sint(32) = 3 + 4; x == 7;"), nullptr);
  let ss = ss_init(arena, ctx);
  lower(nullptr, ss, ss->outermost, ctx.idx);
  let copy = slice_alloc(arena, typed_value, msList_len(ss->outermost->values));
  memcpy(copy.ptr, ss->outermost->values, copy.len * sizeof(typed_value));
  let out = tmpfile();
  test_assert(out);
  printerfunction_context pctx = {.put = fileprint, .arb = out, .allocator = arena};
  makeC(ss, pctx);
  test_inteq(0, fclose(out));
  test_inteq(0, memcmp(copy.ptr, ss->outermost->values, copy.len * sizeof(typed_value)));
  let result = interpret(nullptr, ss, ss->outermost);
  test_assert(*(bool *)result.v.value);
}

// Execute generated C: interpreter-only checks cannot detect dropped stores.
static void check_generated_scope(allocfn arena, test_result *_result, superscope *ss) {
  char executable[] = "/tmp/cshl-cout-XXXXXX";
  int fd = mkstemp(executable);
  test_assert(fd >= 0);
  close(fd);
  defer { unlink(executable); };
  char command[256];
  snprintf(command, sizeof(command), "clang -std=c23 -x c -o %s -", executable);
  let pipe = popen(command, "w");
  test_assert(pipe);
  makeC(ss, (printerfunction_context){.put = fileprint, .arb = pipe, .allocator = arena});
  test_inteq(0, pclose(pipe));
  test_inteq(0, system(executable));
}
static void check_generated_c(allocfn allocator, test_result *_result, const char *filename) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let ctx = parse_source(nullptr, arena, nullFptr, filename);
  let ss = ss_init(arena, ctx);
  lower(nullptr, ss, ss->outermost, ctx.idx);
  check_generated_scope(arena, _result, ss);
}

test_fn(cout_comptime_constants_execute) {
  check_generated_c(allocator, _result, "test/cout_comptime_constants.cshl");
}

// stdout is part of the contract: a @cout must run where it is written, after
// the statements before it and only when its branch runs.
static void check_cout_output(allocfn allocator, test_result *_result, const char *filename, fptr expected) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  char executable[] = "/tmp/cshl-cout-order-XXXXXX";
  int fd = mkstemp(executable);
  test_assert(fd >= 0);
  close(fd);
  char output[] = "/tmp/cshl-cout-order-out-XXXXXX";
  int ofd = mkstemp(output);
  test_assert(ofd >= 0);
  close(ofd);
  defer {
    unlink(executable);
    unlink(output);
  };
  char command[256];
  snprintf(command, sizeof(command), "clang -std=c23 -x c -o %s -", executable);
  let pipe = popen(command, "w");
  test_assert(pipe);
  let ctx = parse_source(nullptr, arena, nullFptr, filename);
  let ss = ss_init(arena, ctx);
  lower(nullptr, ss, ss->outermost, ctx.idx);
  makeC(ss, (printerfunction_context){.put = fileprint, .arb = pipe, .allocator = arena});
  test_inteq(0, pclose(pipe));
  snprintf(command, sizeof(command), "%s > %s", executable, output);
  test_inteq(0, system(command));
  char buffer[128] = {};
  FILE *file = fopen(output, "r");
  test_assert(file);
  usize len = fread(buffer, 1, sizeof(buffer) - 1, file);
  fclose(file);
  test_fpeq(((fptr){len, (u8 *)buffer}), expected);
}

test_fn(cout_emits_in_source_order) {
  check_cout_output(allocator, _result, "test/cout_order.cshl", fp("body1\ncond\nafter\n"));
}

// Short-circuit is observable through @cout: the operand that is skipped must
// never reach the output, while the one that has to run still does.
test_fn(cout_and_or_short_circuit) {
  check_cout_output(allocator, _result, "test/cout_short_circuit.cshl", fp("or ran\ndone\n"));
}

test_fn(anonymous_conversion_execute) {
  check_generated_c(allocator, _result, "test/anonymous_conversion.cshl");
}

test_fn(cout_recursive_const_execute) {
  check_generated_c(allocator, _result, "test/cout_recursive_const.cshl");
}

test_fn(cout_type_dispatch_layout) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let ctx = parse_source(nullptr, arena, fp(";"), nullptr);
  let ss = ss_init(arena, ctx);
  let a_idx = ss_type_reserve(ss, 0);
  let b_idx = ss_type_reserve(ss, 0);
  Type a = T(tu_of(r_tag, {a_idx, ss_srcpos_of_text(ss, fp("A"))}));
  Type b = T(tu_of(r_tag, {b_idx, ss_srcpos_of_text(ss, fp("B"))}));
  let a_members = (r_struct){.start = ss_member_reserve(ss, 1)};
  let b_members = (r_struct){.start = ss_member_reserve(ss, 1)};
  sglist_get(ss->tlist, a_idx - 1) = T(tu_of(r_struct, a_members));
  sglist_get(ss->tlist, b_idx - 1) = T(tu_of(r_struct, b_members));
  // A points to B; B embeds A by value. Emitting B while declaring A's pointer
  // would try to embed an incomplete A.
  *type_struct_item(ss_typeenv(ss), a_members, 0) = (r_struct_item){
      .t = P$(b, ($.ptr_level = 1, $)),
      .name = ss_srcpos_of_text(ss, fp("b")), .alignment = alignof(void *),
  };
  *type_struct_item(ss_typeenv(ss), b_members, 0) = (r_struct_item){
      .t = a, .name = ss_srcpos_of_text(ss, fp("a")), .alignment = alignof(void *),
  };
  let frozen = P$(a, ($.isconst = true, $));
  ss_type_intern(ss, frozen);
  let pipe = popen("clang -std=c23 -x c -fsyntax-only -", "w");
  test_assert(pipe);
  printerfunction_context pctx = {.put = fileprint, .arb = pipe, .allocator = arena};
  out_runtime(pctx);
  let names = makeTypes(ss, pctx);
  defer { freeTypeNames(arena, names); };
  test_inteq(0, pclose(pipe));
  test_assert(out_typeName(ss, names, a) == out_typeName(ss, names, frozen));
}

test_fn(cout_dispatch_existing_programs) {
  const char *files[] = {
      "test/cout_loop_control.cshl", "test/cout_signed_shifts.cshl",
      "test/cout_packed_return.cshl", "test/cout_non_power_alignment.cshl",
      "test/cout_union_bool.cshl",
  };
  foreach (let file, span(files, countof(files))) {
    check_generated_c(allocator, _result, *file);
    if (_result->result) return;
  }
}

test_fn(result_catch_execute) {
  check_generated_c(allocator, _result, "test/result_catch.cshl");
}

// Drive instructions with an already-typed destination. Source lowering usually
// supplies `any`, which used to hide handlers overwriting the destination type.
test_fn(coerce_operations_use_destination_assignment) {
  foreach (let kind, range(0, 5)) {
    let arena = arena_new_ext(allocator, 1 << 16);
    defer { arena_cleanup(arena); };
    let ctx = parse_source(nullptr, arena, fp(
        "i32 ~~ @sint(32); type ~~ @type();\n"
        "S ~~ @struct { Self ~~ @this(); v ~i32;\n"
        " @id(\"=\") ~~ @fn (self ~@refT(Self), value ~i32), @void(), { self.v = value; }; };\n"
        "dest ~S; x ~i32 = 37; p ~= @where x;\n"
        "box ~@struct { v ~i32; }; box.v = 37;\n"
        "a ~@arr(2, i32); a[0] = 37;\n"
        "f ~~ @fn (), i32, { return 37; };\n"), nullptr);
    let ss = ss_init(arena, ctx);
    let s = ss->outermost;
    let statements = ast_block_children(ss, ctx.idx);
    foreach (let i, range(0, statements.len)) lower(nullptr, ss, s, statements.start + i);
    let dest = ss_get_named_failable_noref(ss, s, fp("dest"));
    let x = ss_get_named_failable_noref(ss, s, fp("x"));
    let expected_type = scope_find(ss, s, dest)->t;
    ir instruction;
    switch (kind) {
      case 0: instruction = (ir)tu_of(ir_putptr, {.l = ss_get_named_failable_noref(ss, s, fp("p")), .d = dest}); break;
      case 1: instruction = (ir)tu_of(ir_field, {.l = ss_get_named_failable_noref(ss, s, fp("box")), .d = dest, .i = 0}); break;
      case 2: instruction = (ir)tu_of(ir_array, {.l = ss_get_named_failable_noref(ss, s, fp("a")), .d = dest,
          .r = scope_add_value(ss, s, TV_bigint(ss, BInt.from.i64(arena, 0)))}); break;
      case 3: instruction = (ir)tu_of(ir_rawop, {.l = x, .r = scope_add_value(ss, s, TV_bigint(ss, BInt.from.i64(arena, 0))),
          .d = dest, .o = opNumbers.plus}); break;
      default: {
        let fn = ss_get_named_failable_noref(ss, s, fp("f"));
        // Instantiate the function first, then reuse its callee with our destination.
        lower_function_call_values(nullptr, ss, s, fn, (slice(u32)){});
        ir_call call = {};
        foreach (let i, span(s->instructions, msList_len(s->instructions)))
          if (tu_is(ir_call, *i)) call = i->ir_call;
        call.d = dest;
        instruction = (ir)tu_of(ir_call, call);
      }
    }
    scope_add_inst(nullptr, ss, s, instruction);
    test_inteq(expected_type, scope_find(ss, s, dest)->t);
    let member = scope_add_value(ss, s, TV_any(ss));
    scope_add_inst(nullptr, ss, s, (ir)tu_of(ir_field, {.l = dest, .d = member, .i = 1}));
    let expected = scope_add_value(ss, s, TV_bigint(ss, BInt.from.i64(arena, 37)));
    let result_slot = scope_add_value(ss, s, TV_any(ss));
    scope_add_inst(nullptr, ss, s, (ir)tu_of(ir_rawop, {.l = member, .r = expected, .d = result_slot, .o = opNumbers.eq}));
    let result = interpret(nullptr, ss, s);
    test_assert(*(bool *)result.v.value);
    check_generated_scope(arena, _result, ss);
  }
}

test_fn(cast_overload_directions_execute) {
  check_generated_c(allocator, _result, "test/cast_overload_directions.cshl");
}

test_fn(cout_explicit_pointer_casts) {
  check_generated_c(allocator, _result, "test/explicit_pointer_casts.cshl");
}

test_fn(cout_call_overload_ast_receiver) {
  check_generated_c(allocator, _result, "test/call_overload_ast_receiver.cshl");
}

test_fn(cout_block_result_reference) {
  check_generated_c(allocator, _result, "test/block_result_reference.cshl");
}

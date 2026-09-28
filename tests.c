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
  scope_finish(ss, ss->outermost);
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
static void check_generated_c(allocfn allocator, test_result *_result, const char *filename) {
  let arena = arena_new_ext(allocator, 1 << 16);
  defer { arena_cleanup(arena); };
  let ctx = parse_source(nullptr, arena, nullFptr, filename);
  let ss = ss_init(arena, ctx);
  lower(nullptr, ss, ss->outermost, ctx.idx);
  scope_finish(ss, ss->outermost);
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

test_fn(cout_comptime_constants_execute) {
  check_generated_c(allocator, _result, "test/cout_comptime_constants.cshl");
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

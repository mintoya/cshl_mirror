#pragma once
#include "wheels/macros.h"

#include "ast.h"
#include <wheels/bigint.h>
#include <wheels/fptr.h>
#include <wheels/macros.h>
#include <wheels/mylist.h>
#include <wheels/mytypes.h>
#include <wheels/sList.h>

typedef enum : u8 {
  builtin_if,
  builtin_while,

  builtin_fn,
  builtin_ast,
  builtin_any,
  builtin_int,
  builtin_uint,
  builtin_type,
  builtin_typeof,

  builtin_return,
  builtin_break,
  builtin_qot,
  builtin_uqot,
} builtin_id;
//{type
typedef struct Type Type;
typedef struct r_struct_item r_struct_item;
typedef struct r_bitf_item r_bitf_item;
typedef mList(struct {
  fptr name;
  // not nullable
  // the type is in there
  struct typed_value *init;
}) func_value;
tu_def( // type
    (Type, u8),
    (builtin /*      */, builtin_id),
    // runtime
    (r_void /*       */, nothing_t),
    (r_uint /*       */, u16),
    (r_sint /*       */, u16),
    (r_tag /*        */, struct { fptr name;Type *type; }),
    (r_ptr /*        */, Type *),
    (r_arr /*        */, struct { Type *type; usize count; }),
    (r_struct /*     */, mList(r_struct_item)),
    (r_bitfield /*   */, mList(r_bitf_item)),
    (r_func /*       */, mList(Type)),
    // compile time
    // they could all be nothing_t's, i'd rather show here what they a re but also
    // not have them take up any storage space
    (c_int /*        */, typeof(bigint[0])),
    (c_type /*       */, typeof(Type *[0])),
    (c_ast /*        */, typeof(ast *[0])),
    (c_func /*      */, typeof(func_value[0])),
);
typedef union {
  void *value;

  arrstype(c_int) c_int;
  arrstype(c_ast) c_ast;
  arrstype(c_type) c_type;
  arrstype(c_func) c_func;
} value_store;
typedef struct typed_value {
  Type t;
  value_store v;
} typed_value;
typedef struct r_struct_item {
  fptr name;
  typed_value init; // type in here
  usize alignment, offset;
  bool isconst;
} r_struct_item;
typedef struct r_bitf_item {
  typed_value init; // type in here
  fptr name;
  usize width;
} r_bitf_item;
r_func cf_convert(
    AllocatorV allocator,
    itypeof(itypeof(typed_value, v), c_func) v
) {
  return mList_map(allocator, v, $.init->t);
}
static inline usize Type_ralig(Type t) {
  return tu_match_exp(
      t,
      (r_void, _, (usize)1),
      (r_uint, u, lineup(u, 8) / 8), // TODO
      (r_sint, u, lineup(u, 8) / 8), // TODO
      (r_ptr, p, Type_ralig((Type)tu_of(r_uint, 64))),
      (r_arr, a, assertMessage(a.type); Type_ralig(a.type[0]) * a.count),
      (r_bitfield, b, ({
         usize s = 0;
         foreach (var_ b, mList_iter(b))
           s += b.width;
         Type_ralig((Type)tu_of(r_uint, s));
       })),
      (r_struct, s, ({
         usize a = 1;
         foreach (var_ i, mList_iter(s))
           a = MAX$(i.isconst ? 1 : i.alignment, a);
         a;
       })),
      (default, (usize)0)
  );
}
static inline usize Type_rsize(Type t) {
  return tu_match_exp(
      t,
      (r_void, _, (usize)0),
      (r_uint, u, lineup(u, 8) / 8),
      (r_sint, u, lineup(u, 8) / 8),
      (r_ptr, p, Type_rsize((Type)tu_of(r_uint, 64))),
      (r_arr, a, assertMessage(a.type); Type_rsize(a.type[0]) * a.count),
      (r_bitfield, b, ({
         usize s = 0;
         foreach (var_ b, mList_iter(b))
           s += b.width;
         Type_rsize((Type)tu_of(r_uint, s));
       })),
      (r_struct, s, ({
         usize sz = 0;
         foreach (var_ i, mList_iter(s))
           sz = MAX$(i.isconst ? 0 : i.offset + Type_rsize(i.init.t), sz);
         lineup(sz, Type_ralig(t));
       })),
      (default, (usize)0)
  );
}
i8 icmp(isize a, isize b) {
  return a > b
             ? -1
         : a < b
             ? 1
             : 0;
}
i8 Type_cmp(Type a, Type b) {
  // if (!mcmp(a, b))
  //   return 0;
  if (a.tag < b.tag) return -1;
  if (a.tag > b.tag) return 1;
  tu_match_void(
      a,
      (builtin, id, {
        var_ id2 = tu_catch(builtin, b);
        if (id < id2) return -1;
        if (id > id2) return 1;
        return 0;
      }),

      (r_void, _, return 0;),
      (c_int, _, return 0;),
      (c_ast, _, return 0;),
      (c_type, _, return 0;),
      (c_func, _, return 0;),

      (r_tag, ai, return fptr_cmp(ai.name, tu_catch(r_tag, b).name);),
      (r_uint, ai, return icmp(ai, tu_catch(r_uint, b));),
      (r_sint, ai, return icmp(ai, tu_catch(r_sint, b));),
      (r_ptr, ai, {
        var_ bi = tu_catch(r_ptr, b);
        return ai == bi ? 0 : Type_cmp(ai[0], bi[0]);
      }),
      (r_arr, ai, {
        var_ bi = tu_catch(r_arr, b);
        return Type_cmp(ai.type[0], bi.type[0])
                   ?: icmp(ai.count, bi.count);
      }),
      (r_func, ai, {
        var_ bi = tu_catch(r_func, b);
        var_ la = mList_len(ai);
        var_ lb = mList_len(bi);
        if (la != lb) return icmp(la, lb);
        foreach (var_ i, range(0, la)) {
          var_ a = mList_arr(ai)[i];
          var_ b = mList_arr(bi)[i];
          var_ cmp = Type_cmp(a, b);
          if (cmp) return cmp;
        }
        return 0;
      }),
      (r_bitfield, ai, {
        var_ bi = tu_catch(r_bitfield, b);
        var_ la = mList_len(ai);
        var_ lb = mList_len(bi);
        if (la != lb) return icmp(la, lb);
        foreach (var_ i, range(0, la)) {
          var_ a = mList_arr(ai)[i];
          var_ b = mList_arr(bi)[i];
          if_decl (var_ cmp, Type_cmp(a.init.t, b.init.t))
            return cmp;
          if_decl (var_ cmp, fptr_cmp(a.name, b.name))
            return cmp;
          if_decl (var_ cmp, icmp(a.width, b.width))
            return cmp;
        }
        return 0;
      }),
      (r_struct, ai, {
        var_ bi = tu_catch(r_struct, b);
        var_ la = mList_len(ai);
        var_ lb = mList_len(bi);
        if (la != lb) return icmp(la, lb);
        foreach (var_ i, range(0, la)) {
          var_ a = mList_arr(ai)[i];
          var_ b = mList_arr(bi)[i];
          if_decl (var_ cmp, Type_cmp(a.init.t, b.init.t))
            return cmp;
          if_decl (var_ cmp, fptr_cmp(a.name, b.name))
            return cmp;
          if_decl (var_ cmp, icmp(a.isconst, b.isconst))
            return cmp;
          if_decl (var_ cmp, icmp(a.alignment, b.alignment))
            return cmp;
          if_decl (var_ cmp, icmp(a.offset, b.offset))
            return cmp;
        }
        return 0;
      }),
  );
}
i8 Type_cmp_hash(const void *a, const void *b) { return Type_cmp(*(Type *)a, *(Type *)b); }
u64 Type_hash(Type a) {
  tu_match_void(
      a,
      (builtin, b, return b + 6;),
      (r_void, _, return 0;),
      (c_int, _, return 1;),
      (c_type, _, return 2;),
      (c_ast, _, return 3;),
      (c_func, _, return 4;),
      (r_uint, v, return (u64)v ^ 0x1000;),
      (r_sint, v, return (u64)v ^ 0x2000;),
      // (r_ptr, p, return Type_hash(*p) ^ 0x3000;),
      (r_ptr, p, return 0x3000;),
      (r_tag, v, return fptr_hash(v.name) ^ 0x4000;),
      (r_arr, arr, return (Type_hash(*arr.type) * 31) ^ arr.count ^ 0x5000;),
      (r_struct, l, {
        u64 h = 0x5000;
        var_ len = mList_len(l);
        foreach (var_ i, range(0, len)) {
          var_ item = mList_arr(l)[i];
          h = (h * 31) ^ Type_hash(item.init.t);
          h = (h * 31) ^ fptr_hash(item.name);
          h = (h * 31) ^ item.isconst;
          h = (h * 31) ^ item.alignment;
          h = (h * 31) ^ item.offset;
        }
        return h;
      }),
      (r_bitfield, l, {
        u64 h = 0x6000;
        var_ len = mList_len(l);
        foreach (var_ i, range(0, len)) {
          var_ item = mList_arr(l)[i];
          h = (h * 31) ^ Type_hash(item.init.t);
          h = (h * 31) ^ fptr_hash(item.name);
          h = (h * 31) ^ item.width;
        }
        return h;
      }),
      (r_func, l, {
        u64 h = 0x7000;
        var_ len = mList_len(l);
        foreach (var_ i, range(0, len)) {
          var_ t = mList_arr(l)[i];
          h = (h * 31) ^ Type_hash(t);
        }
        return h;
      })
  );
}
u64 Type_hash_hash(const void *t) { return Type_hash(*(Type *)t); }
//}
//{value
typedef struct value {
  typed_value *lvalue;
  typed_value rvalue;
} value;
//{value macros
#define value_access(value) (*({             \
  var_ _value_ptr = &(value);                \
  _value_ptr->lvalue ?: &_value_ptr->rvalue; \
}))
#define value_value(value) ({ \
  var_ _value = (value);      \
  _value.rvalue =             \
      _value.lvalue           \
          ? *_value.lvalue    \
          : _value.rvalue;    \
  _value.lvalue = nullptr;    \
  _value;                     \
})
//}
//}

typePrinter(builtin_id) {
  switch (in) {
      // clang-format off
    case(builtin_if):PUTS("if");break;
    case(builtin_fn):PUTS("fn");break;
    case(builtin_ast):PUTS("ast");break;
    case(builtin_any):PUTS("any");break;
    case(builtin_int):PUTS("int");break;
    case(builtin_uint):PUTS("uint");break;
    case(builtin_type):PUTS("type");break;
    case(builtin_while):PUTS("while");break;
    case(builtin_return):PUTS("return");break;
    case(builtin_typeof):PUTS("typeof");break;
    case(builtin_break):PUTS("break");break;
    case(builtin_qot):PUTS("qot");break;
    case(builtin_uqot):PUTS("uqot");break;
      // clang-format on
  }
}
typePrinter(Type) {
  tu_match_void(
      in,
      (builtin, builtin, {
        PUTS("builtin:");
        USETYPEPRINTER(builtin_id, builtin);
      }),
      (r_void, _, PUTS("void");),
      (r_tag, t, USENAMEDPRINTER("slice(c8)", t.name);),
      (r_uint, bits, PUTS("u"); USETYPEPRINTER(usize, bits);),
      (r_sint, bits, PUTS("i"); USETYPEPRINTER(usize, bits);),
      (r_ptr, type, {
        PUTS("[*]");
        USETYPEPRINTER(Type, *type);
      }),
      (r_arr, arr, {
        PUTS("[");
        USETYPEPRINTER(usize, arr.count);
        PUTS("]");
        USETYPEPRINTER(Type, *arr.type);
      }),
      (r_struct, st, {
        PUTS("struct{");
        defer { PUTS("}"); }
        foreach (var_ type, span(mList_arr(st), mList_len(st))) {
          if (type != mList_arr(st)) PUTS(",");

          USENAMEDPRINTER("slice(c8)", type->name);
          PUTS("@");
          USETYPEPRINTER(usize, type->offset);
          PUTS(":");
          USETYPEPRINTER(Type, type->init.t);
        }
      }),
      (r_bitfield, st, {
        PUTS("struct{");
        defer { PUTS("}"); }
        foreach (var_ type, span(mList_arr(st), mList_len(st))) {
          if (type != mList_arr(st)) PUTS(",");

          USENAMEDPRINTER("slice(c8)", type->name);
          PUTS("|");
          USETYPEPRINTER(usize, type->width);
          PUTS(":");
          USETYPEPRINTER(Type, type->init.t);
        }
      }),
      (r_func, fn, {
        PUTS("fn((");
        foreach (var_ it, span(mList_arr(fn), mList_len(fn) - 1)) {
          if (it != mList_arr(fn)) PUTS(",");
          USETYPEPRINTER(Type, it[0]);
        }
        PUTS("),");
        if (mList_len(fn)) USETYPEPRINTER(Type, mList_last(fn));
        PUTS(")");
      }),
      (c_func, f, PUTS("fn");),
      (c_int, i, PUTS("int");),
      (c_type, t, PUTS("type");),
      (c_ast, t, PUTS("ast");),
  );
}
typePrinter(typed_value) {
  PUTS("{t:");
  USETYPEPRINTER(Type, in.t);
  PUTS(",v:");
  tu_match_void(
      in.t,
      (c_int, _, USENAMEDPRINTER("bigint", in.v.c_int);),
      (c_ast, _, USENAMEDPRINTER("ast", in.v.c_ast);),
      (c_type, _, USETYPEPRINTER(Type, *in.v.c_type);),
      (c_func, _, PUTS("fn");),
      (default, {
        if (in.v.value)
          USETYPEPRINTER(fptr, ((fptr){Type_rsize(in.t), in.v.value}));
        else PUTS("empty poiner field?");
      })
  );
  PUTS("}");
}
typePrinter(value) {
  PUTS("{");
  in.lvalue ? PUTS("lvalue:") : PUTS("rvalue:");
  USETYPEPRINTER(typed_value, value_value(in).rvalue);
  PUTS("}");
}
// { test functions
test_fn(type_ptr_equality) {
  var_ v = (Type)tu_of(
      r_struct,
      mList_init(allocator, r_struct_item)
  );
  defer { mList_deinit(tu_catch(r_struct, v)); };
  mList_push(
      tu_catch(r_struct, v),
      {
          .alignment = 8,
          .isconst = false,
          .name = fp("self"),
          .offset = 0,
          .init = (typed_value){tu_of(r_ptr, &v)},
      }
  );
  test_assert(!Type_cmp(v, v));
  test_pass();
}
test_fn(type_hash_ptr_equality) {
  var_ v = (Type)tu_of(
      r_struct,
      mList_init(allocator, r_struct_item)
  );
  defer { mList_deinit(tu_catch(r_struct, v)); };
  mList_push(
      tu_catch(r_struct, v),
      {
          .alignment = 8,
          .isconst = false,
          .name = fp("self"),
          .offset = 0,
          .init = (typed_value){tu_of(r_ptr, &v)},
      }
  );
  test_assert(Type_hash(v) == Type_hash(v));
  test_pass();
}
// r_tag is nominal: same name, different underlying type -> still "equal" per Type_cmp
// (the whole point of r_tag is that it does NOT recurse into .type)
test_fn(type_tag_nominal_equality) {
  var_ u32t = (Type)tu_of(r_uint, (u16)32);
  var_ u64t = (Type)tu_of(r_uint, (u16)64);
  var_ a = (Type)tu_of(r_tag, {.name = fp("Meters"), .type = &u32t});
  var_ b = (Type)tu_of(r_tag, {.name = fp("Meters"), .type = &u64t});
  test_assert(!Type_cmp(a, b));
  test_assert(Type_hash(a) == Type_hash(b));
  test_pass();
}

// r_tag: different name, same underlying type -> must NOT be equal
test_fn(type_tag_distinct_names) {
  var_ u32t = (Type)tu_of(r_uint, (u16)32);
  var_ a = (Type)tu_of(r_tag, {.name = fp("Meters"), .type = &u32t});
  var_ b = (Type)tu_of(r_tag, {.name = fp("Seconds"), .type = &u32t});
  test_assert(Type_cmp(a, b) != 0);
  test_pass();
}

// r_ptr: two DISTINCT pointers pointing at structurally-equal (non-cyclic) types
// must still compare equal -- this is the general case the identity check
// short-circuits only when it's the SAME pointer, so it should still recurse
// correctly for the common non-cyclic case.
test_fn(type_ptr_structural_equality_distinct_pointers) {
  var_ ta = (Type)tu_of(r_uint, (u16)32);
  var_ tb = (Type)tu_of(r_uint, (u16)32);
  var_ pa = (Type)tu_of(r_ptr, &ta);
  var_ pb = (Type)tu_of(r_ptr, &tb);
  test_assert(!Type_cmp(pa, pb));
  test_pass();
}

// r_ptr: distinct pointers to STRUCTURALLY DIFFERENT types must compare unequal
test_fn(type_ptr_structural_inequality) {
  var_ ta = (Type)tu_of(r_uint, (u16)32);
  var_ tb = (Type)tu_of(r_uint, (u16)64);
  var_ pa = (Type)tu_of(r_ptr, &ta);
  var_ pb = (Type)tu_of(r_ptr, &tb);
  test_assert(Type_cmp(pa, pb) != 0);
  test_pass();
}

// tag ordering: variants earlier in tu_def should sort before later ones
// (builtin is declared before r_void)
test_fn(type_tag_ordering) {
  var_ a = (Type)tu_of(builtin, builtin_if);
  var_ b = (Type)tu_of(r_void, (nothing_t){});
  test_assert(Type_cmp(a, b) < 0);
  test_assert(Type_cmp(b, a) > 0);
  test_pass();
}

// r_uint and r_sint must NOT hash the same even for identical bit-width,
// since they're semantically distinct (signedness matters for Type_cmp
// via the tag check) -- guards against someone "simplifying" the salts later.
test_fn(type_uint_sint_hash_distinct) {
  var_ u = (Type)tu_of(r_uint, (u16)32);
  var_ s = (Type)tu_of(r_sint, (u16)32);
  test_assert(Type_cmp(u, s) != 0);
  test_assert(Type_hash(u) != Type_hash(s));
  test_pass();
}

// hash consistency across two SEPARATELY constructed but structurally
// identical r_struct values (not the same instance) -- this is the case
// that actually matters for hash-consing/interning, as opposed to
// Type_hash(v) == Type_hash(v) which is trivially true for any function.
test_fn(type_struct_hash_consistency) {
  var_ a = (Type)tu_of(r_struct, mList_init(allocator, r_struct_item));
  var_ b = (Type)tu_of(r_struct, mList_init(allocator, r_struct_item));
  defer {
    mList_deinit(tu_catch(r_struct, a));
    mList_deinit(tu_catch(r_struct, b));
  };
  var_ u32t = (typed_value){tu_of(r_uint, (u16)32)};
  mList_push(tu_catch(r_struct, a), {
                                        .alignment = 4,
                                        .isconst = false,
                                        .name = fp("x"),
                                        .offset = 0,
                                        .init = u32t,
                                    });
  mList_push(tu_catch(r_struct, b), {
                                        .alignment = 4,
                                        .isconst = false,
                                        .name = fp("x"),
                                        .offset = 0,
                                        .init = u32t,
                                    });
  test_assert(!Type_cmp(a, b));
  test_assert(Type_hash(a) == Type_hash(b));
  test_pass();
}

// r_struct field order matters as written (Type_cmp walks index-by-index,
// not by name) -- same fields in a different order should NOT compare equal.
// Worth confirming this is the intended semantics rather than an oversight.
test_fn(type_struct_field_order_matters) {
  var_ a = (Type)tu_of(r_struct, mList_init(allocator, r_struct_item));
  var_ b = (Type)tu_of(r_struct, mList_init(allocator, r_struct_item));
  defer {
    mList_deinit(tu_catch(r_struct, a));
    mList_deinit(tu_catch(r_struct, b));
  };
  var_ u32t = (typed_value){tu_of(r_uint, (u16)32)};
  var_ u8t = (typed_value){tu_of(r_uint, (u16)8)};
  mList_push(tu_catch(r_struct, a), {.alignment = 4, .isconst = false, .name = fp("x"), .offset = 0, .init = u32t});
  mList_push(tu_catch(r_struct, a), {.alignment = 1, .isconst = false, .name = fp("y"), .offset = 4, .init = u8t});
  mList_push(tu_catch(r_struct, b), {.alignment = 1, .isconst = false, .name = fp("y"), .offset = 4, .init = u8t});
  mList_push(tu_catch(r_struct, b), {.alignment = 4, .isconst = false, .name = fp("x"), .offset = 0, .init = u32t});
  test_assert(Type_cmp(a, b) != 0);
  test_pass();
}

// r_func: same param/return types -> equal; different arity -> unequal
test_fn(type_func_equality) {
  var_ p1 = mList_init(allocator, Type);
  var_ p2 = mList_init(allocator, Type);
  defer {
    mList_deinit(p1);
    mList_deinit(p2);
  };
  mList_push(p1, (Type)tu_of(r_uint, (u16)32));
  mList_push(p1, (Type)tu_of(r_void, (nothing_t){}));
  mList_push(p2, (Type)tu_of(r_uint, (u16)32));
  mList_push(p2, (Type)tu_of(r_void, (nothing_t){}));
  var_ fa = (Type)tu_of(r_func, p1);
  var_ fb = (Type)tu_of(r_func, p2);
  test_assert(!Type_cmp(fa, fb));

  var_ p3 = mList_init(allocator, Type);
  defer { mList_deinit(p3); };
  mList_push(p3, (Type)tu_of(r_uint, (u16)32));
  var_ fc = (Type)tu_of(r_func, p3);
  test_assert(Type_cmp(fa, fc) != 0);
  test_pass();
}

// Type_ralig/Type_rsize sanity: a struct with a leading u8 then a u32
// aligned to 4 should size to 8, not 5 (padding/alignment must round up)
test_fn(type_struct_size_respects_alignment) {
  var_ s = (Type)tu_of(r_struct, mList_init(allocator, r_struct_item));
  defer { mList_deinit(tu_catch(r_struct, s)); };
  mList_push(tu_catch(r_struct, s), {
                                        .alignment = 1,
                                        .isconst = false,
                                        .name = fp("a"),
                                        .offset = 0,
                                        .init = (typed_value){tu_of(r_uint, (u16)8)},
                                    });
  mList_push(tu_catch(r_struct, s), {
                                        .alignment = 4,
                                        .isconst = false,
                                        .name = fp("b"),
                                        .offset = 4,
                                        .init = (typed_value){tu_of(r_uint, (u16)32)},
                                    });
  test_assert(Type_rsize(s) == 8);
  test_assert(Type_ralig(s) == 4);
  test_pass();
}
// }

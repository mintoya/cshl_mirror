#pragma once
#include "wheels/macros.h"

#include "ast.h"
#include "wheels/oxmap.h"
#include <wheels/bigint.h>
#include <wheels/fptr.h>
#include <wheels/hxmap.h>
#include <wheels/macros.h>
#include <wheels/mylist.h>
#include <wheels/mytypes.h>
#include <wheels/sList.h>

typedef enum : u8 {
  builtin_anyType,
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
         foreach (let b, mList_iter(b))
           s += b.width;
         Type_ralig((Type)tu_of(r_uint, s));
       })),
      (r_struct, s, ({
         usize a = 1;
         foreach (let i, mList_iter(s))
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
         foreach (let b, mList_iter(b))
           s += b.width;
         Type_rsize((Type)tu_of(r_uint, s));
       })),
      (r_struct, s, ({
         usize sz = 0;
         foreach (let i, mList_iter(s))
           sz = MAX$(i.isconst ? 0 : i.offset + Type_rsize(i.init.t), sz);
         lineup(sz, Type_ralig(t));
       })),
      (default, (usize)0)
  );
}

typedef struct {
  Type *a;
  Type *b;
} type_cmp_pair;

i8 icmp(isize a, isize b) { return a - b; }
i8 Type_cmp_ex(Type a, Type b, mList(type_cmp_pair) followed) {
  if (!mcmp(a, b)) return 0;
  else if (a.tag < b.tag) return -1;
  else if (a.tag > b.tag) return 1;
  tu_match_void(
      a,
      (builtin, id, {
        let id2 = tu_catch(builtin, b);
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
        let bi = tu_catch(r_ptr, b);
        if (ai == bi) return 0;
        if (!ai || !bi) return ai ? 1 : -1; // one null, one not: deterministic order, never equal

        // Only short-circuit when we've already started comparing THIS
        // exact pair -- i.e. both sides have returned to a point they've
        // been before, together. Tracking `ai` alone (as before) let one
        // side's cycle mask real differences on the other side.
        foreach (let p, mList_iter(followed))
          if (p.a == ai && p.b == bi) return 0;

        mList_push(followed, ((type_cmp_pair){.a = ai, .b = bi}));
        i8 r = Type_cmp_ex(ai[0], bi[0], followed);
        mList_rem(followed, mList_len(followed) - 1);
        return r;
      }),
      (r_arr, ai, {
        let bi = tu_catch(r_arr, b);
        return Type_cmp_ex(ai.type[0], bi.type[0], followed)
                   ?: icmp(ai.count, bi.count);
      }),
      (r_func, ai, {
        let bi = tu_catch(r_func, b);
        let la = mList_len(ai);
        let lb = mList_len(bi);
        if (la != lb) return icmp(la, lb);
        foreach (let i, range(0, la)) {
          let a = mList_arr(ai)[i];
          let b = mList_arr(bi)[i];
          let cmp = Type_cmp_ex(a, b, followed);
          if (cmp) return cmp;
        }
        return 0;
      }),
      (r_bitfield, ai, {
        let bi = tu_catch(r_bitfield, b);
        let la = mList_len(ai);
        let lb = mList_len(bi);
        if (la != lb) return icmp(la, lb);
        foreach (let i, range(0, la)) {
          let a = mList_arr(ai)[i];
          let b = mList_arr(bi)[i];
          if_decl (let cmp, Type_cmp_ex(a.init.t, b.init.t, followed))
            return cmp;
          if_decl (let cmp, fptr_cmp(a.name, b.name))
            return cmp;
          if_decl (let cmp, icmp(a.width, b.width))
            return cmp;
        }
        return 0;
      }),
      (r_struct, ai, {
        let bi = tu_catch(r_struct, b);
        let la = mList_len(ai);
        let lb = mList_len(bi);
        if (la != lb) return icmp(la, lb);
        foreach (let i, range(0, la)) {
          let a = mList_arr(ai)[i];
          let b = mList_arr(bi)[i];
          if_decl (let cmp, Type_cmp_ex(a.init.t, b.init.t, followed))
            return cmp;
          if_decl (let cmp, fptr_cmp(a.name, b.name))
            return cmp;
          if_decl (let cmp, icmp(a.isconst, b.isconst))
            return cmp;
          if_decl (let cmp, icmp(a.alignment, b.alignment))
            return cmp;
          if_decl (let cmp, icmp(a.offset, b.offset))
            return cmp;
        }
        return 0;
      }),
  );
}
i8 Type_cmp(Type a, Type b) {
  let f = mList_init(stdAlloc, type_cmp_pair);
  defer { mList_deinit(f); };
  return Type_cmp_ex(a, b, f);
}

i8 Type_cmp_hash(const void *a, const void *b) { return Type_cmp(*(Type *)a, *(Type *)b); }

static u64 Type_hash_ex(Type a, bool follow_ptr) {
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
      (r_ptr, p, return follow_ptr ? Type_hash_ex(*p, false) * 31 ^ 0x3000 : 0x3000;),
      (r_tag, v, return fptr_hash(v.name) ^ 0x4000;),
      (r_arr, arr, return (Type_hash_ex(*arr.type, follow_ptr) * 31) ^ arr.count ^ 0x5000;),
      (r_struct, l, {
        u64 h = 0x5000;
        foreach (let item, mList_iter(l)) {
          h = (h * 31) ^ Type_hash_ex(item.init.t, follow_ptr);
          h = (h * 31) ^ fptr_hash(item.name);
          h = (h * 31) ^ item.isconst;
          h = (h * 31) ^ item.alignment;
          h = (h * 31) ^ item.offset;
        }
        return h;
      }),
      (r_bitfield, l, {
        u64 h = 0x6000;
        foreach (let item, mList_iter(l)) {
          h = (h * 31) ^ Type_hash_ex(item.init.t, follow_ptr);
          h = (h * 31) ^ fptr_hash(item.name);
          h = (h * 31) ^ item.width;
        }
        return h;
      }),
      (r_func, l, {
        u64 h = 0x7000;
        foreach (let t, mList_iter(l))
          h = (h * 31) ^ Type_hash_ex(t, follow_ptr);
        return h;
      })
  );
}

u64 Type_hash(Type a) { return Type_hash_ex(a, true); }
u64 Type_hash_hash(const void *t) { return Type_hash(*(Type *)t); }
//}
//{value
typedef struct value {
  typed_value *lvalue;
  typed_value rvalue;
} value;
//{value macros
#define value_access(value) (*({             \
  let _value_ptr = &(value);                 \
  _value_ptr->lvalue ?: &_value_ptr->rvalue; \
}))
#define value_value(value) ({ \
  let _value = (value);       \
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
    case(builtin_anyType):PUTS("anyType");break;
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

typedef struct {
  slice(u8) str;
} id_str;
typePrinter(id_str) {
  if (fptr_eq(PRINTARGS(), "decode")) {
    assertMessage(!(in.str.len & 1));
    foreach (let cp, span(in.str.ptr, in.str.len, 2))
      PUTC((char)(((cp[0] - 'a') << 4) | (cp[1] - 'a')));
  } else {
    foreach (let c, span(in.str.ptr, in.str.len)) {
      PUTC((char)(((*c >> 4) & 0xf) + 'a'));
      PUTC((char)(((*c >> 0) & 0xf) + 'a'));
    }
  }
}

void typePrinterWorker(Type in, moxmap(Type, u32) visitedmap, printerfunction_context _ctx);
static inline void typePrinterWorker_add(Type in, moxmap(Type, u32) visitedmap) {
  moxmap_set(visitedmap, in, ((u32)((oxmap *)visitedmap)->keys->length));
}
static inline void typePrinterWorker_recurse(Type in, moxmap(Type, u32) visitedmap, printerfunction_context _ctx) {
  if_decl (let idx, moxmap_get(visitedmap, in))
    USETYPEPRINTER(usize, *idx);
  else typePrinterWorker(in, visitedmap, _ctx);
}
void typePrinterWorker(Type in, moxmap(Type, u32) visitedmap, printerfunction_context _ctx) {
  tu_match_void(
      in,
      // trivial, are themselves
      (builtin, builtin, {
        PUTS("(b ");
        USETYPEPRINTER(builtin_id, builtin);
        PUTS(")");
      }),
      (c_func, f, PUTS("cfn");),
      (c_int, i, PUTS("int");),
      (c_type, t, PUTS("type");),
      (c_ast, t, PUTS("ast");),
      (r_void, _, PUTS("void");),
      (r_tag, t, {
        PUTS("(id ");
        USETYPEPRINTER(id_str, (id_str){t.name});
        PUTS(")");
      }),
      (r_uint, bits, PUTS("u"); USETYPEPRINTER(usize, bits);),
      (r_sint, bits, PUTS("i"); USETYPEPRINTER(usize, bits);),
      // complex, require other types

      // only one type, no push
      (r_ptr, type, {
        // typePrinterWorker_add(in, visited, visitedmap);
        PUTS("(* ");
        defer { PUTS(")"); };
        if (type) typePrinterWorker_recurse(*type, visitedmap, _ctx);
        else PUTS("!");
      }),
      (r_arr, arr, {
        // typePrinterWorker_add(in, visited, visitedmap);
        PUTS("(a ");
        defer { PUTS(")"); };
        PUTS("[");
        USETYPEPRINTER(usize, arr.count);
        PUTS("] ");
        if (arr.type) typePrinterWorker_recurse(*arr.type, visitedmap, _ctx);
        else PUTS("!");
      }),
      // multiple types, push
      (r_struct, st, {
        typePrinterWorker_add(in, visitedmap);
        PUTS("(s ");
        defer { PUTS(")"); }
        foreach (let type, span(mList_arr(st), mList_len(st))) {
          if (type != mList_arr(st)) PUTS(" ");
          USETYPEPRINTER(id_str, (id_str){type->name});
          PUTS("@");
          USETYPEPRINTER(usize, type->offset);
          PUTS(":");
          typePrinterWorker_recurse(type->init.t, visitedmap, _ctx);
        }
      }),
      (r_bitfield, st, {
        typePrinterWorker_add(in, visitedmap);
        PUTS("(bf ");
        defer { PUTS(")"); }
        foreach (let type, span(mList_arr(st), mList_len(st))) {
          if (type != mList_arr(st)) PUTS(" ");

          USETYPEPRINTER(id_str, (id_str){type->name});
          PUTS("|");
          USETYPEPRINTER(usize, type->width);
          PUTS(":");
          typePrinterWorker_recurse(type->init.t, visitedmap, _ctx);
        }
      }),
      (r_func, fn, {
        typePrinterWorker_add(in, visitedmap);
        PUTS("(fn (");
        foreach (let it, span(mList_arr(fn), (mList_len(fn) ?: 1) - 1)) {
          if (it != mList_arr(fn)) PUTS(" ");
          typePrinterWorker_recurse(*it, visitedmap, _ctx);
        }
        PUTS(") ");
        if (mList_len(fn))
          typePrinterWorker_recurse(mList_last(fn), visitedmap, _ctx);
        PUTS(")");
      }),
  );
}
typePrinter(Type) {
  let visited = moxmap_init(PRINTARGS_ALLOCATOR(), Type, u32, Type_cmp_hash);
  defer { moxmap_deinit(visited); };
  typePrinterWorker(in, visited, _ctx);
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

// { test functions
test_fn(type_ptr_equality) {
  let v = (Type)tu_of(
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
}
test_fn(type_hash_ptr_equality) {
  let v = (Type)tu_of(
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
}
// r_tag is nominal: same name, different underlying type -> still "equal" per Type_cmp
// (the whole point of r_tag is that it does NOT recurse into .type)
test_fn(type_tag_nominal_equality) {
  let u32t = (Type)tu_of(r_uint, (u16)32);
  let u64t = (Type)tu_of(r_uint, (u16)64);
  let a = (Type)tu_of(r_tag, {.name = fp("Meters"), .type = &u32t});
  let b = (Type)tu_of(r_tag, {.name = fp("Meters"), .type = &u64t});
  test_assert(!Type_cmp(a, b));
  test_assert(Type_hash(a) == Type_hash(b));
}

// r_tag: different name, same underlying type -> must NOT be equal
test_fn(type_tag_distinct_names) {
  let u32t = (Type)tu_of(r_uint, (u16)32);
  let a = (Type)tu_of(r_tag, {.name = fp("Meters"), .type = &u32t});
  let b = (Type)tu_of(r_tag, {.name = fp("Seconds"), .type = &u32t});
  test_assert(Type_cmp(a, b) != 0);
}

// r_ptr: two DISTINCT pointers pointing at structurally-equal (non-cyclic) types
// must still compare equal -- this is the general case the identity check
// short-circuits only when it's the SAME pointer, so it should still recurse
// correctly for the common non-cyclic case.
test_fn(type_ptr_structural_equality_distinct_pointers) {
  let ta = (Type)tu_of(r_uint, (u16)32);
  let tb = (Type)tu_of(r_uint, (u16)32);
  let pa = (Type)tu_of(r_ptr, &ta);
  let pb = (Type)tu_of(r_ptr, &tb);
  test_assert(!Type_cmp(pa, pb));
}

// r_ptr: distinct pointers to STRUCTURALLY DIFFERENT types must compare unequal
test_fn(type_ptr_structural_inequality) {
  let ta = (Type)tu_of(r_uint, (u16)32);
  let tb = (Type)tu_of(r_uint, (u16)64);
  let pa = (Type)tu_of(r_ptr, &ta);
  let pb = (Type)tu_of(r_ptr, &tb);
  test_assert(Type_cmp(pa, pb) != 0);
}

// tag ordering: variants earlier in tu_def should sort before later ones
// (builtin is declared before r_void)
test_fn(type_tag_ordering) {
  let a = (Type)tu_of(builtin, builtin_if);
  let b = (Type)tu_of(r_void, (nothing_t){});
  test_assert(Type_cmp(a, b) < 0);
  test_assert(Type_cmp(b, a) > 0);
}

// r_uint and r_sint must NOT hash the same even for identical bit-width,
// since they're semantically distinct (signedness matters for Type_cmp
// via the tag check) -- guards against someone "simplifying" the salts later.
test_fn(type_uint_sint_hash_distinct) {
  let u = (Type)tu_of(r_uint, (u16)32);
  let s = (Type)tu_of(r_sint, (u16)32);
  test_assert(Type_cmp(u, s) != 0);
  test_assert(Type_hash(u) != Type_hash(s));
}

// hash consistency across two SEPARATELY constructed but structurally
// identical r_struct values (not the same instance) -- this is the case
// that actually matters for hash-consing/interning, as opposed to
// Type_hash(v) == Type_hash(v) which is trivially true for any function.
test_fn(type_struct_hash_consistency) {
  let a = (Type)tu_of(r_struct, mList_init(allocator, r_struct_item));
  let b = (Type)tu_of(r_struct, mList_init(allocator, r_struct_item));
  defer {
    mList_deinit(tu_catch(r_struct, a));
    mList_deinit(tu_catch(r_struct, b));
  };
  let u32t = (typed_value){tu_of(r_uint, (u16)32)};
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
}

// r_struct field order matters as written (Type_cmp walks index-by-index,
// not by name) -- same fields in a different order should NOT compare equal.
// Worth confirming this is the intended semantics rather than an oversight.
test_fn(type_struct_field_order_matters) {
  let a = (Type)tu_of(r_struct, mList_init(allocator, r_struct_item));
  let b = (Type)tu_of(r_struct, mList_init(allocator, r_struct_item));
  defer {
    mList_deinit(tu_catch(r_struct, a));
    mList_deinit(tu_catch(r_struct, b));
  };
  let u32t = (typed_value){tu_of(r_uint, (u16)32)};
  let u8t = (typed_value){tu_of(r_uint, (u16)8)};
  mList_push(tu_catch(r_struct, a), {.alignment = 4, .isconst = false, .name = fp("x"), .offset = 0, .init = u32t});
  mList_push(tu_catch(r_struct, a), {.alignment = 1, .isconst = false, .name = fp("y"), .offset = 4, .init = u8t});
  mList_push(tu_catch(r_struct, b), {.alignment = 1, .isconst = false, .name = fp("y"), .offset = 4, .init = u8t});
  mList_push(tu_catch(r_struct, b), {.alignment = 4, .isconst = false, .name = fp("x"), .offset = 0, .init = u32t});
  test_assert(Type_cmp(a, b) != 0);
}

// r_func: same param/return types -> equal; different arity -> unequal
test_fn(type_func_equality) {
  let p1 = mList_init(allocator, Type);
  let p2 = mList_init(allocator, Type);
  defer {
    mList_deinit(p1);
    mList_deinit(p2);
  };
  mList_push(p1, (Type)tu_of(r_uint, (u16)32));
  mList_push(p1, (Type)tu_of(r_void, (nothing_t){}));
  mList_push(p2, (Type)tu_of(r_uint, (u16)32));
  mList_push(p2, (Type)tu_of(r_void, (nothing_t){}));
  let fa = (Type)tu_of(r_func, p1);
  let fb = (Type)tu_of(r_func, p2);
  test_assert(!Type_cmp(fa, fb));

  let p3 = mList_init(allocator, Type);
  defer { mList_deinit(p3); };
  mList_push(p3, (Type)tu_of(r_uint, (u16)32));
  let fc = (Type)tu_of(r_func, p3);
  test_assert(Type_cmp(fa, fc) != 0);
}

// Type_ralig/Type_rsize sanity: a struct with a leading u8 then a u32
// aligned to 4 should size to 8, not 5 (padding/alignment must round up)
test_fn(type_struct_size_respects_alignment) {
  let s = (Type)tu_of(r_struct, mList_init(allocator, r_struct_item));
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
}
// Touches every Type variant in one structure:
// builtin, r_void, r_uint, r_sint, r_tag, r_ptr (self-referential),
// r_arr (of a scalar and of self-pointers), r_struct (self + nested),
// r_bitfield, r_func (full / empty / return-only), and all four
// compile-time variants (c_int, c_type, c_ast, c_func).
test_fn(type_print_complex) {
  let v = (Type)tu_of(r_struct, mList_init(allocator, r_struct_item));
  defer { mList_deinit(tu_catch(r_struct, v)); };

  // --- leaf/aux types -----------------------------------------------
  let t_u8 = (Type)tu_of(r_uint, (u16)8);
  let t_u32 = (Type)tu_of(r_uint, (u16)32);
  let t_i16 = (Type)tu_of(r_sint, (u16)16);
  let t_i8 = (Type)tu_of(r_sint, (u16)8);
  let t_void = (Type)tu_of(r_void, (nothing_t){});

  // r_ptr used as an r_arr element type -- needs its own addressable
  // storage distinct from the inline r_ptr fields below
  let t_ptr_v = (Type)tu_of(r_ptr, &v);

  // --- nested composite: a plain (non-cyclic) struct "Point" --------
  let point_items = mList_init(allocator, r_struct_item);
  defer { mList_deinit(point_items); };
  mList_push(point_items, {.alignment = 4, .isconst = false, .name = fp("x"), .offset = 0, .init = (typed_value){t_u32}});
  mList_push(point_items, {.alignment = 4, .isconst = false, .name = fp("y"), .offset = 4, .init = (typed_value){t_u32}});
  let t_point = (Type)tu_of(r_struct, point_items);

  // --- r_bitfield -----------------------------------------------------
  let bf = mList_init(allocator, r_bitf_item);
  defer { mList_deinit(bf); };
  mList_push(bf, {.name = fp("flag1"), .width = 1, .init = (typed_value){t_u8}});
  mList_push(bf, {.name = fp("flag2"), .width = 3, .init = (typed_value){t_u8}});
  let t_bitfield = (Type)tu_of(r_bitfield, bf);

  // --- r_func: params + return ----------------------------------------
  let fn_params = mList_init(allocator, Type);
  defer { mList_deinit(fn_params); };
  mList_push(fn_params, t_u32);
  mList_push(fn_params, (Type)tu_of(r_ptr, &v)); // self-ptr param
  mList_push(fn_params, t_i8);                   // last elem == return
  let t_fn_full = (Type)tu_of(r_func, fn_params);

  // --- r_func: zero params, zero return (empty list edge case) -------
  let fn_empty = mList_init(allocator, Type);
  defer { mList_deinit(fn_empty); };
  let t_fn_empty = (Type)tu_of(r_func, fn_empty);

  // --- r_func: zero params, return-only (single elem edge case) ------
  let fn_ret_only = mList_init(allocator, Type);
  defer { mList_deinit(fn_ret_only); };
  mList_push(fn_ret_only, t_void);
  let t_fn_ret_only = (Type)tu_of(r_func, fn_ret_only);

  // --- assemble v -------------------------------------------------------
  {
    let p = &tu_catch(r_struct, v);
    mList_push((*p), {.alignment = 8, .isconst = false, .name = fp("val"), .offset = 16, .init = (typed_value){tu_of(r_uint, (u16)64)}});
    mList_push((*p), {.alignment = 8, .isconst = false, .name = fp("next"), .offset = 0, .init = (typed_value){tu_of(r_ptr, &v)}});
    mList_push((*p), {.alignment = 8, .isconst = false, .name = fp("prev"), .offset = 8, .init = (typed_value){tu_of(r_ptr, &v)}});
    mList_push((*p), {.alignment = 4, .isconst = false, .name = fp("id"), .offset = 24, .init = (typed_value){tu_of(r_tag, {.name = fp("Meters"), .type = &t_u32})}});
    mList_push((*p), {.alignment = 1, .isconst = false, .name = fp("flags"), .offset = 32, .init = (typed_value){t_bitfield}});
    mList_push((*p), {.alignment = 1, .isconst = false, .name = fp("kind"), .offset = 40, .init = (typed_value){tu_of(builtin, builtin_typeof)}});
    mList_push((*p), {.alignment = 1, .isconst = false, .name = fp("pad"), .offset = 48, .init = (typed_value){t_void}});
    mList_push((*p), {.alignment = 2, .isconst = false, .name = fp("scores"), .offset = 56, .init = (typed_value){tu_of(r_arr, {.type = &t_i16, .count = 4})}});
    mList_push((*p), {.alignment = 8, .isconst = false, .name = fp("children"), .offset = 72, .init = (typed_value){tu_of(r_arr, {.type = &t_ptr_v, .count = 2})}});
    mList_push((*p), {.alignment = 4, .isconst = false, .name = fp("origin"), .offset = 88, .init = (typed_value){t_point}});
    mList_push((*p), {.alignment = 8, .isconst = true, .name = fp("callback"), .offset = 96, .init = (typed_value){t_fn_full}});
    mList_push((*p), {.alignment = 8, .isconst = true, .name = fp("noop"), .offset = 104, .init = (typed_value){t_fn_empty}});
    mList_push((*p), {.alignment = 8, .isconst = true, .name = fp("make"), .offset = 112, .init = (typed_value){t_fn_ret_only}});
    mList_push((*p), {.alignment = 1, .isconst = true, .name = fp("count"), .offset = 120, .init = (typed_value){tu_of(c_int, {})}});
    mList_push((*p), {.alignment = 1, .isconst = true, .name = fp("selfType"), .offset = 120, .init = (typed_value){tu_of(c_type, {})}});
    mList_push((*p), {.alignment = 1, .isconst = true, .name = fp("astNode"), .offset = 120, .init = (typed_value){tu_of(c_ast, {})}});
    mList_push((*p), {.alignment = 1, .isconst = true, .name = fp("ctor"), .offset = 120, .init = (typed_value){tu_of(c_func, {})}});
  }

  println("{Type}", v);

  // Expected (with the recurse-add fix applied):
  // (s val@16:u64 next@0:(* 0) prev@8:(* 0) id@24:(id engfhegfhchd)
  //    flags@32:(bf flag1|1:u8 flag2|3:u8) kind@40:(b typeof) pad@48:void
  //    scores@56:(a [4] i16) children@72:(a [2] (* 0))
  //    origin@88:(s x@0:u32 y@4:u32)
  //    callback@96:(fn (u32 (* 0)) i8) noop@104:(fn () ) make@112:(fn () void)
  //    count@120:int selfType@120:type astNode@120:ast ctor@120:cfn)
  //
  // Notes on why:
  // - `next`/`prev` both fully expand to `(* 0)` -- self-pointers to `v`
  //   (idx 0), demonstrating the recurse-add fix from before.
  // - `children` is an *array of self-pointers*, distinct from next/prev's
  //   pointer objects but structurally identical; r_arr/r_ptr are never
  //   added to visitedmap, so it re-expands too, landing on the same `0`.
  // - `origin` is a structurally distinct nested r_struct ("Point"), so it
  //   gets its own fresh index rather than colliding with anything.
  // - `noop`/`make` exercise the r_func length-0 and length-1 edge cases:
  //   zero params + zero return, and zero params + return-only.
  // - `count`/`selfType`/`astNode`/`ctor` are the four compile-time
  //   variants; they carry no runtime payload, so only `.t` matters here.
}
// }

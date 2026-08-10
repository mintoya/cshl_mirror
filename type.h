#pragma once
#include "ast.h"
#include "wheels/macros.h"
#include "wheels/oxmap.h"
#include "wheels/print/print_pre.h"
#include <wheels/bigint.h>
#include <wheels/fptr.h>
#include <wheels/hxmap.h>
#include <wheels/macros.h>
#include <wheels/mylist.h>
#include <wheels/mytypes.h>
#include <wheels/sList.h>

typedef enum : u8 {
  builtin_if,       // @if(cnd , then ,)
                    //
  builtin_while,    // @while(cnd , then) //
  builtin_fn,       // fn((decl|type...) ,decl|type) c_func of this type,
                    // then when called with expression, yields an r_func
  builtin_ast,      // @ast() type of ast
  builtin_any,      // @any() type of any
  builtin_int,      // @int() compile time bigint
  builtin_type,     // @type() the type of a type
  builtin_const,    // @const(T) constant
  builtin_volatile, // @volatile(T) volatile
  builtin_sint,     // @sint(N) integer with N bits
  builtin_uint,     // @uint(N) unsigned integer with N bits
  builtin_typeof,   // @typeof(V) type of argument
  builtin_arrof,    // @arrof(N,t) array of N t's
  builtin_ptrof,    // @ptrof(t) pionter to t
  builtin_struct,   // @struct{} struct of declarations
  builtin__this,    // @this() inside of struct, this is the result type

  builtin_return, // return x return
  builtin_break,  // @break()
  builtin_qot,    // @qot(arg) ast of arg
  builtin_uqot,   // @unqot(arg) evaluate arg
} builtin_id;
//{type
typedef struct Type Type;
struct typed_value *init;
typedef struct r_struct_item r_struct_item;
typedef struct r_bitf_item r_bitf_item;
tu_def( // type
    (Type, u8),
    (builtin /*      */, builtin_id),
    // runtime
    (r_void /*       */, struct {}),
    (r_uint /*       */, u16),
    (r_sint /*       */, u16),
    (r_tag /*        */, struct { fptr name;Type *type; }),
    (r_ptr /*        */, Type *),
    (r_arr /*        */, struct { Type *type; usize count; }),
    (r_struct /*     */, mList(r_struct_item)),
    (r_bitfield /*   */, mList(r_bitf_item)),
    (r_func /*       */, mList(Type)),
    (r_ctime /*      */, Type *),
    (r_decor /*      */, struct {Type* t ; bool constant :1;bool valatile :1; }),
    // compile time
    // they could all be nothing_t's, i'd rather show here what they a re but also
    // not have them take up any storage space
    (c_any /*        */, struct {}),
    (c_int /*        */, typeof(bigint[0])),
    (c_type /*       */, typeof(Type *[0])),
    (c_ast /*        */, typeof(ast *[0])),
);
typedef union {
  void *value;

  arrstype(c_int) c_int;
  arrstype(c_ast) c_ast;
  arrstype(c_type) c_type;
} value_store;
typedef struct typed_value {
  Type t;
  value_store v;
} typed_value;
typedef struct r_struct_item {
  fptr name;
  typed_value init; // type in here
  usize alignment, offset;
} r_struct_item;
typedef struct r_bitf_item {
  typed_value init; // type in here
  fptr name;
  usize width;
} r_bitf_item;
static inline usize Type_ralig(Type t) {
  return tu_match_exp(
      t,
      (r_tag, tag, Type_ralig(*tag.type)),
      (r_void, _, (usize)1),
      (r_uint, u, lineup(u, 8) / 8), // TODO
      (r_sint, u, lineup(u, 8) / 8), // TODO
      (r_ptr, p, Type_ralig((Type)tu_of(r_uint, 64))),
      (r_arr, a, (assertMessage(a.type), Type_ralig(a.type[0]))),
      (r_bitfield, b, ({
         usize s = 0;
         foreach (let b, mList_iter(b))
           s += b.width;
         Type_ralig((Type)tu_of(r_uint, s));
       })),
      (r_struct, s, ({
         usize a = 1;
         foreach (let i, mList_iter(s))
           a = MAX$(i.alignment, a);
         a;
       })),
      (r_func, _, (usize)1),
      (default, (usize)0)
  );
}

static inline usize Type_rsize(Type t) {
  return tu_match_exp(
      t,
      (r_tag, tag, Type_rsize(*tag.type)),
      (r_void, _, (usize)0),
      (r_uint, u, lineup(u, 8) / 8),
      (r_sint, u, lineup(u, 8) / 8),
      (r_ptr, p, Type_rsize((Type)tu_of(r_uint, 64))),
      (r_arr, a, (assertMessage(a.type), Type_rsize(a.type[0]) * a.count)),
      (r_bitfield, b, ({
         usize s = 0;
         foreach (let b, mList_iter(b))
           s += b.width;
         Type_rsize((Type)tu_of(r_uint, s));
       })),
      (r_struct, s, ({
         usize sz = 0;
         foreach (let i, mList_iter(s))
           sz = MAX$(i.offset + Type_rsize(i.init.t), sz);
         lineup(sz, Type_ralig(t));
       })),
      (r_func, _, (usize)0),
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
      (c_any, _, return 0;),
      (c_ast, _, return 0;),
      (c_type, _, return 0;),
      (r_ctime, _, return 0;),

      (r_tag, ai, return fptr_cmp(ai.name, tu_catch(r_tag, b).name);),
      (r_uint, ai, return icmp(ai, tu_catch(r_uint, b));),
      (r_sint, ai, return icmp(ai, tu_catch(r_sint, b));),

      (r_decor, ai, {
        let bi = tu_catch(r_decor, b);
        if_decl (let dif, ai.constant - bi.constant)
          return dif;
        if_decl (let dif, ai.valatile - bi.valatile)
          return dif;
        if (ai.t == bi.t) return 0;
        if (!ai.t || !bi.t) return ai.t ? 1 : -1;

        foreach (let p, mList_iter(followed))
          if (p.a == ai.t && p.b == bi.t) return 0;

        mList_push(followed, ((type_cmp_pair){.a = ai.t, .b = bi.t}));
        i8 r = Type_cmp_ex(*ai.t, *bi.t, followed);
        mList_rem(followed, mList_len(followed) - 1);
        return r;
      }),
      (r_ptr, ai, {
        let bi = tu_catch(r_ptr, b);
        if (ai == bi) return 0;
        if (!ai || !bi) return ai ? 1 : -1;

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

static u64 Type_hash_ex(Type a, bool follow) {
  tu_match_void(
      a,
      (builtin, b, return b + 6),
      (r_void, _, return 0),
      (c_int, _, return 1),
      (c_type, _, return 2),
      (c_ast, _, return 3),
      (r_ctime, _, return 4),
      (c_any, _, return 5),
      (r_uint, v, return (u64)v ^ 0x1000),
      (r_sint, v, return (u64)v ^ 0x2000),
      (r_ptr, p, return follow ? Type_hash_ex(*p, false) * 31 ^ 0x3000 : 0x3000),
      (r_decor, p, return follow ? Type_hash_ex(*p.t, false) * 31 ^ 0x3000 : 0x3000),
      (r_tag, v, return fptr_hash(v.name) ^ 0x4000),
      (r_arr, arr, return (Type_hash_ex(*arr.type, follow) * 31) ^ arr.count ^ 0x5000),
      (r_struct, l, {
        u64 h = 0x5000;
        foreach (let item, mList_iter(l)) {
          h = (h * 31) ^ Type_hash_ex(item.init.t, follow);
          h = (h * 31) ^ fptr_hash(item.name);
          h = (h * 31) ^ item.alignment;
          h = (h * 31) ^ item.offset;
        }
        return h;
      }),
      (r_bitfield, l, {
        u64 h = 0x6000;
        foreach (let item, mList_iter(l)) {
          h = (h * 31) ^ Type_hash_ex(item.init.t, follow);
          h = (h * 31) ^ fptr_hash(item.name);
          h = (h * 31) ^ item.width;
        }
        return h;
      }),
      (r_func, l, {
        u64 h = 0x7000;
        foreach (let t, mList_iter(l))
          h = (h * 31) ^ Type_hash_ex(t, follow);
        return h;
      })
  );
}

u64 Type_hash(Type a) { return Type_hash_ex(a, true); }
u64 Type_hash_hash(const void *t) { return Type_hash(*(Type *)t); }
bool Type_is_comptime(Type t) {
  return tu_match_exp(
      t,
      (r_tag, t, Type_is_comptime(*t.type)),

      (builtin, _, true),
      (c_ast, _, true),
      (c_any, _, true),
      (c_int, _, true),
      (c_type, _, true),
      (r_ctime, _, true),

      (r_decor, _, false),
      (r_func, _, false),
      (r_struct, _, false),
      (r_bitfield, _, false),
      (r_void, _, false),
      (r_uint, _, false),
      (r_sint, _, false),
      (r_ptr, _, false),
      (r_arr, _, false),
  );
}
//}

typePrinter(builtin_id) {
  switch (in) {
      // clang-format off
    case(builtin_if):PUTS("if");break;
    case(builtin_fn):PUTS("fn");break;
    case(builtin_ast):PUTS("ast");break;
    case(builtin_arrof):PUTS("arrof");break;
    case(builtin_ptrof):PUTS("ptrof");break;
    case(builtin_any):PUTS("any");break;
    case(builtin_int):PUTS("int");break;
    case(builtin_sint):PUTS("sint");break;
    case(builtin_uint):PUTS("uint");break;
    case(builtin_const):PUTS("const");break;
    case(builtin_volatile):PUTS("volatile");break;
    case(builtin_type):PUTS("type");break;
    case(builtin_struct):PUTS("struct");break;
    case(builtin__this):PUTS("this");break;
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
bool iscid(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
typePrinter(id_str) {
  if (fptr_eq(PRINTARGS(), "decode")) {
    let p = in.str.ptr;
    while (p < in.str.len + in.str.ptr) {
      if (*p == '_') {
        p++;
        while (p < in.str.len + in.str.ptr && *p != '_') {
          assertMessage(p + 1 < in.str.len + in.str.ptr);
          PUTC((char)(((*p - 'a') << 4) | (*(p + 1) - 'a')));
          p += 2;
        }
        p++;
      } else PUTC((char)*p++);
    }
  } else {
    let p = in.str.ptr;
    while (p < in.str.len + in.str.ptr) {
      while (p < in.str.len + in.str.ptr && iscid(*p))
        PUTC((char)(*(p++)));
      if (p < in.str.len + in.str.ptr && !iscid(*p)) {
        PUTC((char)'_');
        while (p < in.str.len + in.str.ptr && !iscid(*p)) {
          char c = *(p++);
          PUTC((char)((((c) >> 4) & 0xf) + 'a'));
          PUTC((char)((((c) >> 0) & 0xf) + 'a'));
        }
        PUTC((char)'_');
      }
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
      (c_int, i, PUTS("int")),
      (c_type, t, PUTS("type")),
      (c_ast, t, PUTS("ast")),
      (c_any, t, PUTS("any")),
      (r_void, _, PUTS("void")),
      (r_tag, t, {
        PUTS("(id ");
        USETYPEPRINTER(id_str, (id_str){t.name});
        PUTS(")");
      }),
      (r_uint, bits, PUTS("u"); USETYPEPRINTER(usize, bits);),
      (r_sint, bits, PUTS("s"); USETYPEPRINTER(usize, bits);),
      // complex, require other types

      // only one type, no push
      (r_ptr, type, {
        // typePrinterWorker_add(in, visited, visitedmap);
        PUTS("(* ");
        defer { PUTS(")"); };
        if (type) typePrinterWorker_recurse(*type, visitedmap, _ctx);
        else PUTS("!");
      }),
      (r_ctime, type, {
        PUTS("(comp ");
        defer { PUTS(")"); };
        if (type) typePrinterWorker_recurse(*type, visitedmap, _ctx);
      }),
      (r_decor, decor, {
        PUTS("(");
        if (decor.constant) PUTS("const");
        if (decor.valatile) PUTS("-volatile ");
        defer { PUTS(")"); };
        if (decor.t) typePrinterWorker_recurse(*decor.t, visitedmap, _ctx);
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
      (c_ast, _,
#if defined TV_EXPAND_VALUE && TV_EXPAND_VALUE == 1
       USENAMEDPRINTER("ast", in.v.c_ast)
#else
       USENAMEDPRINTER("astTag", (in.v.c_ast->tag))
#endif
      ),
      (c_type, _, in.v.c_type ? USETYPEPRINTER(Type, *in.v.c_type) : PUTS("??");),
      (default, {
        if (in.v.value)
          USETYPEPRINTER(fptr, ((fptr){Type_rsize(in.t), in.v.value}));
        else PUTS("(nil)");
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
          .name = fp("self"),
          .offset = 0,
          .init = (typed_value){tu_of(r_ptr, &v)},
      }
  );
  test_assert(!Type_cmp(v, v));
}
test_fn(type_tag_nominal_equality) {
  let u32t = (Type)tu_of(r_uint, (u16)32);
  let u64t = (Type)tu_of(r_uint, (u16)64);
  let a = (Type)tu_of(r_tag, {.name = fp("Meters"), .type = &u32t});
  let b = (Type)tu_of(r_tag, {.name = fp("Meters"), .type = &u64t});
  test_assert(!Type_cmp(a, b));
  test_assert(Type_hash(a) == Type_hash(b));
}
test_fn(type_tag_distinct_names) {
  let u32t = (Type)tu_of(r_uint, (u16)32);
  let a = (Type)tu_of(r_tag, {.name = fp("Meters"), .type = &u32t});
  let b = (Type)tu_of(r_tag, {.name = fp("Seconds"), .type = &u32t});
  test_assert(Type_cmp(a, b));
}
test_fn(type_ptr_structural_inequality) {
  let ta = (Type)tu_of(r_uint, (u16)32);
  let tb = (Type)tu_of(r_uint, (u16)64);
  let pa = (Type)tu_of(r_ptr, &ta);
  let pb = (Type)tu_of(r_ptr, &tb);
  test_assert(Type_cmp(pa, pb));
}
test_fn(type_struct_hash_consistency) {
  let a = (Type)tu_of(r_struct, mList_init(allocator, r_struct_item));
  let b = (Type)tu_of(r_struct, mList_init(allocator, r_struct_item));
  defer {
    mList_deinit(tu_catch(r_struct, a));
    mList_deinit(tu_catch(r_struct, b));
  };
  let u32t = (typed_value){tu_of(r_uint, (u16)32)};
  mList_push(
      tu_catch(r_struct, a),
      {
          .alignment = 4,
          .name = fp("x"),
          .offset = 0,
          .init = u32t,
      }
  );
  mList_push(
      tu_catch(r_struct, b),
      {
          .alignment = 4,
          .name = fp("x"),
          .offset = 0,
          .init = u32t,
      }
  );
  test_assert(!Type_cmp(a, b));
  test_assert(Type_hash(a) == Type_hash(b));
}
test_fn(type_struct_field_order_matters) {
  let a = (Type)tu_of(r_struct, mList_init(allocator, r_struct_item));
  let b = (Type)tu_of(r_struct, mList_init(allocator, r_struct_item));
  defer {
    mList_deinit(tu_catch(r_struct, a));
    mList_deinit(tu_catch(r_struct, b));
  };
  let u32t = (typed_value){tu_of(r_uint, (u16)32)};
  let u8t = (typed_value){tu_of(r_uint, (u16)8)};
  mList_push(tu_catch(r_struct, a), {.alignment = 4, .name = fp("x"), .offset = 0, .init = u32t});
  mList_push(tu_catch(r_struct, a), {.alignment = 1, .name = fp("y"), .offset = 4, .init = u8t});
  mList_push(tu_catch(r_struct, b), {.alignment = 1, .name = fp("y"), .offset = 4, .init = u8t});
  mList_push(tu_catch(r_struct, b), {.alignment = 4, .name = fp("x"), .offset = 0, .init = u32t});
  test_assert(Type_cmp(a, b));
}
test_fn(type_func_equality) {
  let p1 = mList_init(allocator, Type);
  let p2 = mList_init(allocator, Type);
  defer {
    mList_deinit(p1);
    mList_deinit(p2);
  };
  mList_push(p1, tu_of(r_uint, 32));
  mList_push(p1, tu_of(r_void, {}));
  mList_pushArr(p2, *mList_vla(p1));

  let fa = (Type)tu_of(r_func, p1);
  let fb = (Type)tu_of(r_func, p2);
  test_assert(!Type_cmp(fa, fb));

  let p3 = mList_init(allocator, Type);
  defer { mList_deinit(p3); };
  mList_push(p3, (Type)tu_of(r_uint, (u16)32));
  let fc = (Type)tu_of(r_func, p3);
  test_assert(Type_cmp(fa, fc));
}
// }

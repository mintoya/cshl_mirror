#include "ast.h"
#include <limits.h>
#include <wheels/bigint.h>
#include <wheels/fptr.h>
#include <wheels/hxmap.h>
#include <wheels/macros.h>
#include <wheels/mylist.h>
#include <wheels/mytypes.h>
#include <wheels/oxmap.h>
#include <wheels/print/print_pre.h>
#include <wheels/sList.h>

typedef enum : u8 {
  builtin_fn,       // @fn((decl|type...) ,decl|type,body) c_func of this type,
  builtin_closure,  // @closure((captures...),(decl|type...) ,decl|type,body) c_func of this type,
  builtin_if,       // @if(cnd , then ,)
  builtin_while,    // @while(cnd , then) //
  builtin_return,   // return x return
  builtin_break,    // @break()
  builtin_continue, // @continue()
                    //
                    // then when called with expression, yields an r_func
  builtin__id,      // @id(str) an id which corresponds to this string, **evaluates it**
                    //
  builtin_ast,      // @ast() type of ast
  builtin_any,      // @any() type of any
  builtin_void,     // @void() the void type
  builtin_int,      // @int() compile time bigint
  builtin_type,     // @type() the type of a type
  builtin_const,    // @const(T) constant
  builtin_volatile, // @volatile(T) volatile
  builtin_sint,     // @sint(N) integer with N bits
  builtin_uint,     // @uint(N) unsigned integer with N bits
  builtin_typeof,   // @typeof(V) type of argument
  builtin_arrof,    // @arrof(N,t) array of N t's
  builtin_ptrof,    // @ptrof(t) pionter to t
                    //
  builtin_struct,   // @struct{} struct of declarations
  builtin__this,    // @this() inside of struct, this is the result type
                    //
  builtin_qot,      // @qot(arg) ast of arg
  builtin_uqot,     // @unqot(arg) evaluate arg
                    //
  builtin_ref,      // @ref(value) produces a referance to value
  builtin_reft,     // @reft(value) produces the type of a referance to a value
  builtin_at,       // @at(arg) dereference arg
  builtin_where,    // @where(arg) referance to arg
} builtin_id;
//{type
typedef struct Type Type;
typedef struct TypeTree TypeTree;
typedef struct r_struct_item r_struct_item;
typedef struct r_bitf_item r_bitf_item;

tu_def( // type
    (TypeTree, u8),
    (builtin /*      */, builtin_id),
    // runtime
    (r_void /*       */, struct {}),
    (r_uint /*       */, u16),
    (r_sint /*       */, u16),

    (r_tag /*        */, struct { TypeTree *t/*maybe nullable in the future, like extern*/; fptr name; }),
    (r_arr /*        */, struct { TypeTree *t; usize count; }),

    (r_struct /*     */, mList(r_struct_item)),
    (r_bitfield /*   */, mList(r_bitf_item)),

    (r_func /*       */, mList(TypeTree)),
    (r_closure /*    */, struct { r_func captures, args; }),
    // compile time
    // they could all be nothing_t's, i'd rather show here what they a re but also
    // not have them take up any storage space
    (c_any /*        */, struct {}),
    (c_int /*        */, typeof(bigint[0])),
    (c_type /*       */, typeof(TypeTree *[0])),
    (c_ast /*        */, typeof(ast *[0])),
);
struct Type {
  TypeTree t;
  u32 ptr_level : 32;
  bool isref : 1;
  bool isconst : 1;
  bool iscomptime : 1; // meaningless for int,type,ast
  bool isvolatile : 1;
};
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
  Type t;
  fptr name;
  usize offset, alignment;
} r_struct_item;
typedef struct r_bitf_item {
  Type t;
  fptr name;
  usize width;
} r_bitf_item;

// size/alignment

static inline usize TypeTree_alignof(TypeTree t);
static inline usize Type_alignof(Type t);
static inline usize TypeTree_sizeof(TypeTree t);
static inline usize Type_sizeof(Type t);

static inline usize TypeTree_alignof(TypeTree t) {
  return tu_match_exp(
      t,
      (r_tag, tag, TypeTree_alignof(*tag.t)),
      (r_void, _, (usize)1),
      (r_uint, u, lineup(u, CHAR_BIT) / CHAR_BIT),
      (r_sint, u, TypeTree_alignof((TypeTree)tu_of(r_uint, u))),
      (r_arr, a, TypeTree_alignof(*a.t)),
      (r_bitfield, b, ({
         usize s = 0;
         foreach (let b, mList_iter(b))
           s += b.width;
         TypeTree_alignof((TypeTree)tu_of(r_uint, s));
       })),
      (r_struct, s, ({
         usize a = 1;
         foreach (let i, mList_iter(s)) {
           // Skip comptime fields: they don't affect runtime alignment
           if (i.t.iscomptime) continue;
           a = MAX$(i.alignment, a);
         }
         a;
       })),
      (r_func, _, (usize)1),
      (r_closure, _, (usize)1),
      (default, (usize)0)
  );
}

static inline usize Type_alignof(Type t) {
  if (t.iscomptime) return 1;
  if (t.isref || t.ptr_level)
    return TypeTree_alignof((TypeTree)tu_of(r_uint, sizeof(void *) * 8));
  return TypeTree_alignof(t.t);
}

static inline usize TypeTree_sizeof(TypeTree t) {
  return tu_match_exp(
      t,
      (r_tag, tag, TypeTree_sizeof(*tag.t)),
      (r_void, _, (usize)0),
      (r_uint, u, lineup(u, CHAR_BIT) / CHAR_BIT),
      (r_sint, u, TypeTree_sizeof((TypeTree)tu_of(r_uint, u))),
      (r_arr, a, TypeTree_sizeof(*a.t) * a.count),
      (r_bitfield, b, ({
         usize s = 0;
         foreach (let b, mList_iter(b))
           s += b.width;
         TypeTree_sizeof((TypeTree)tu_of(r_uint, s));
       })),
      (r_struct, s, ({
         usize sz = 0;
         foreach (let i, mList_iter(s))
           sz = MAX$(!i.t.iscomptime * (i.offset + Type_sizeof(i.t)), sz);
         lineup(sz, TypeTree_alignof(t));
       })),
      (r_func, _, (usize)0),
      (r_closure, _, (usize)0),
      (default, (usize)0)
  );
}

static inline usize Type_sizeof(Type t) {
  if (t.iscomptime) return 0;
  if (t.isref || t.ptr_level)
    return sizeof(void *);
  return TypeTree_sizeof(t.t);
}

// comparison

i8 icmp(isize a, isize b) { return a - b; }
typedef struct {
  TypeTree *a;
  TypeTree *b;
} type_cmp_pair;

i8 TypeTree_cmp(TypeTree a, TypeTree b);

i8 Type_cmp(Type a, Type b) {
  if_decl (let cmp, icmp(a.ptr_level, b.ptr_level)) return cmp;
  if_decl (let cmp, icmp(a.isref, b.isref)) return cmp;
  if_decl (let cmp, icmp(a.isconst, b.isconst)) return cmp;
  if_decl (let cmp, icmp(a.isvolatile, b.isvolatile)) return cmp;
  if_decl (let cmp, icmp(a.iscomptime, b.iscomptime)) return cmp;
  return TypeTree_cmp(a.t, b.t);
}

i8 TypeTree_cmp(TypeTree a, TypeTree b) {
  if (!mcmp(a, b)) return 0;
  if (a.tag != b.tag) return a.tag < b.tag ? -1 : 1;

  tu_match_void(
      a,
      (builtin, ai, return icmp(ai, tu_catch(builtin, b))),
      (r_uint, ai, return icmp(ai, tu_catch(r_uint, b))),
      (r_sint, ai, return icmp(ai, tu_catch(r_sint, b))),
      (r_tag, ai, return fptr_cmp(ai.name, tu_catch(r_tag, b).name)),

      (r_void, _, return 0),
      (c_int, _, return 0),
      (c_any, _, return 0),
      (c_ast, _, return 0),
      (c_type, _, return 0),

      (r_arr, ai, {
        let bi = tu_catch(r_arr, b);
        if_decl (let cmp, icmp(ai.count, bi.count)) return cmp;
        return TypeTree_cmp(*ai.t, *bi.t);
      }),

      (r_func, ai, {
        let bi = tu_catch(r_func, b);
        if_decl (let cmp, icmp(mList_len(ai), mList_len(bi))) return cmp;

        foreach (let i, range(0, mList_len(ai)))
          if_decl (let cmp, TypeTree_cmp(mList_arr(ai)[i], mList_arr(bi)[i]))
            return cmp;
        return 0;
      }),

      (r_closure, ai, {
        let bi = tu_catch(r_closure, b);
        if_decl (let cmp, icmp(mList_len(ai.captures), mList_len(bi.captures))) return cmp;
        if_decl (let cmp, icmp(mList_len(ai.args), mList_len(bi.args))) return cmp;

        foreach (let i, range(0, mList_len(ai.captures)))
          if_decl (let cmp, TypeTree_cmp(mList_arr(ai.captures)[i], mList_arr(bi.captures)[i]))
            return cmp;

        foreach (let i, range(0, mList_len(ai.args)))
          if_decl (let cmp, TypeTree_cmp(mList_arr(ai.args)[i], mList_arr(bi.args)[i]))
            return cmp;
        return 0;
      }),

      (r_struct, ai, {
        let bi = tu_catch(r_struct, b);
        if_decl (let cmp, icmp(mList_len(ai), mList_len(bi))) return cmp;

        foreach (let i, range(0, mList_len(ai))) {
          let a_i = mList_arr(ai)[i], b_i = mList_arr(bi)[i];
          if_decl (let cmp, fptr_cmp(a_i.name, b_i.name)) return cmp;
          if_decl (let cmp, icmp(a_i.offset, b_i.offset)) return cmp;
          if_decl (let cmp, icmp(a_i.alignment, b_i.alignment)) return cmp;
          if_decl (let cmp, Type_cmp(a_i.t, b_i.t)) return cmp;
        }
        return 0;
      }),

      (r_bitfield, ai, {
        let bi = tu_catch(r_bitfield, b);
        if_decl (let cmp, icmp(mList_len(ai), mList_len(bi))) return cmp;

        foreach (let i, range(0, mList_len(ai))) {
          let a_i = mList_arr(ai)[i], b_i = mList_arr(bi)[i];
          if_decl (let cmp, fptr_cmp(a_i.name, b_i.name)) return cmp;
          if_decl (let cmp, icmp(a_i.width, b_i.width)) return cmp;
          if_decl (let cmp, Type_cmp(a_i.t, b_i.t)) return cmp;
        }
        return 0;
      })
  );
}

i8 TypeTree_cmp_hash(const void *a, const void *b) {
  return TypeTree_cmp(*(TypeTree *)a, *(TypeTree *)b);
}

i8 Type_cmp_hash(const void *a, const void *b) {
  return Type_cmp(*(Type *)a, *(Type *)b);
}

// printer
void typeTreePrinterWorker(TypeTree in, moxmap(TypeTree, u32) visitedmap, printerfunction_context _ctx);

static inline void typeTreePrinterWorker_add(TypeTree t, moxmap(TypeTree, u32) visitedmap) {
  moxmap_set(visitedmap, t, ((u32)((oxmap *)visitedmap)->keys->length));
}

typePrinter(builtin_id) {
  switch (in) {
      // clang-format off
    case(builtin_if):PUTS("if");break;
    case(builtin__id):PUTS("id");break;
    case(builtin_fn):PUTS("fn");break;
    case(builtin_closure):PUTS("closure");break;
    case(builtin_ast):PUTS("ast");break;
    case(builtin_arrof):PUTS("arrof");break;
    case(builtin_ptrof):PUTS("ptrof");break;
    case(builtin_at):PUTS("at");break;
    case(builtin_where):PUTS("where");break;
    case(builtin_any):PUTS("any");break;
    case(builtin_void):PUTS("void");break;
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
    case(builtin_continue):PUTS("break");break;
    case(builtin_qot):PUTS("qot");break;
    case(builtin_uqot):PUTS("uqot");break;
    case(builtin_ref):PUTS("ref");break;
    case(builtin_reft):PUTS("reft");break;
      // clang-format on
  }
}
void typeTreePrinterWorker(TypeTree in, moxmap(TypeTree, u32) visitedmap, printerfunction_context _ctx) {
  if_decl (let idx, moxmap_get(visitedmap, in)) {
    USETYPEPRINTER(usize, *idx);
    return;
  }

  tu_match_void(
      in,
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
        USETYPEPRINTER(id_str, t.name);
        PUTS(")");
      }),
      (r_uint, bits, PUTS("u"); USETYPEPRINTER(usize, bits);),
      (r_sint, bits, PUTS("s"); USETYPEPRINTER(usize, bits);),

      // Complex, require recursion
      (r_arr, arr, {
        typeTreePrinterWorker_add(in, visitedmap);
        PUTS("(a ");
        defer { PUTS(")"); };
        PUTS("["), USETYPEPRINTER(usize, arr.count), PUTS("] ");
        USENAMEDPRINTER("TypeTree", *arr.t);
      }),
      (r_struct, st, {
        typeTreePrinterWorker_add(in, visitedmap);
        PUTS("(s ");
        defer { PUTS(")"); }
        foreach (let type, span(mList_arr(st), mList_len(st))) {
          if (type != mList_arr(st)) PUTS(",");
          USETYPEPRINTER(id_str, type->name);
          PUTS("@");
          USETYPEPRINTER(usize, type->offset);
          PUTS("~");
          USENAMEDPRINTER("Type", type->t);
        }
      }),
      (r_bitfield, st, {
        typeTreePrinterWorker_add(in, visitedmap);
        PUTS("(bf ");
        defer { PUTS(")"); }
        foreach (let type, span(mList_arr(st), mList_len(st))) {
          if (type != mList_arr(st)) PUTS(",");
          USETYPEPRINTER(id_str, type->name);
          PUTS(":");
          USETYPEPRINTER(usize, type->width);
          PUTS("~");
          USENAMEDPRINTER("Type", type->t);
        }
      }),
      (r_func, fn, {
        typeTreePrinterWorker_add(in, visitedmap);
        PUTS("(fn (");
        foreach (let it, mList_iter(fn)) {
          USENAMEDPRINTER("TypeTree", it);
          PUTS(",");
        }
        PUTS("),");
        if (mList_len(fn)) USENAMEDPRINTER("TypeTree", mList_last(fn));
        PUTS(")");
      }),
      (r_closure, fn, {
        typeTreePrinterWorker_add(in, visitedmap);
        PUTS("(clos (");
        foreach (let it, mList_iter(fn.captures))
          USENAMEDPRINTER("TypeTree", it), PUTS(" ");
        PUTS("),");
        USENAMEDPRINTER("TypeTree", ((TypeTree)tu_of(r_func, fn.args)));
        PUTS(")");
      })
  );
}

typePrinter(TypeTree) {
  let visited = moxmap_init(PRINTARGS_ALLOCATOR(), TypeTree, u32, TypeTree_cmp_hash);
  defer { moxmap_deinit(visited); };
  typeTreePrinterWorker(in, visited, _ctx);
}

typePrinter(Type) {
  bool has_env = in.iscomptime || in.ptr_level || in.isref || in.isconst || in.isvolatile;

  if (has_env) {
    PUTS("(");
    if (in.ptr_level > 0) PUTS("*"), USETYPEPRINTER(u32, in.ptr_level), PUTS(",");
    if (in.isref) PUTS("&,");
    if (in.isconst) PUTS("const,");
    if (in.isvolatile) PUTS("volatile,");
    if (in.iscomptime) PUTS("comptime,");
  }

  USETYPEPRINTER(TypeTree, in.t);
  if (has_env) PUTS(")");
}
// test
test_fn(type_env_qualifiers) {
  let u32t = (TypeTree)tu_of(r_uint, (u16)32);
  let t1 = (Type){.t = u32t, .isconst = false};
  let t2 = (Type){.t = u32t, .isconst = true};
  test_assert(Type_cmp(t1, t2));

  let t3 = (Type){.t = u32t, .ptr_level = 1};
  let t4 = (Type){.t = u32t, .ptr_level = 2};
  test_assert(Type_cmp(t3, t4));

  let t5 = (Type){.t = u32t, .ptr_level = 2, .isconst = true};
  let t6 = (Type){.t = u32t, .ptr_level = 2, .isconst = true};
  test_assert(!Type_cmp(t5, t6));
}

test_fn(type_tag_nominal_equality) {
  let u32t = (TypeTree)tu_of(r_uint, (u16)32);
  let u64t = (TypeTree)tu_of(r_uint, (u16)64);
  let a = (TypeTree)tu_of(r_tag, {.name = fp("Node"), .t = &u32t});
  let b = (TypeTree)tu_of(r_tag, {.name = fp("Node"), .t = &u64t});
  test_assert(!TypeTree_cmp(a, b));

  let c = (TypeTree)tu_of(r_tag, {.name = fp("Edge"), .t = &u32t});
  test_assert(TypeTree_cmp(a, c));
}

test_fn(type_struct_equality) {
  let st1 = mList_init(allocator, r_struct_item);
  let st2 = mList_init(allocator, r_struct_item);
  defer {
    mList_deinit(st1);
    mList_deinit(st2);
  };

  let field_t = (Type){.t = tu_of(r_uint, (u16)32), .isconst = true};

  mList_push(st1, ((r_struct_item){.alignment = 4, .name = fp("x"), .offset = 0, .t = field_t}));
  mList_push(st2, ((r_struct_item){.alignment = 4, .name = fp("x"), .offset = 0, .t = field_t}));

  let t1 = (Type){.t = tu_of(r_struct, st1)};
  let t2 = (Type){.t = tu_of(r_struct, st2)};
  test_assert(!Type_cmp(t1, t2));

  mList_arr(st2)[0].t.isconst = false;
  test_assert(Type_cmp(t1, t2));
}

test_fn(type_arr_equality) {
  let u32t = (TypeTree)tu_of(r_uint, (u16)32);
  let a = (TypeTree)tu_of(r_arr, {.t = &u32t, .count = 10});
  let b = (TypeTree)tu_of(r_arr, {.t = &u32t, .count = 10});
  let c = (TypeTree)tu_of(r_arr, {.t = &u32t, .count = 11});

  test_assert(!TypeTree_cmp(a, b));
  test_assert(TypeTree_cmp(a, c));
}

test_fn(type_printer_complex_sanity) {
  let u32t = (TypeTree)tu_of(r_uint, (u16)32);
  let arr = (TypeTree)tu_of(r_arr, {.t = &u32t, .count = 5});

  let complex_t = (Type){
      .t = arr,
      .ptr_level = 2,
      .isconst = true,
      .isvolatile = true,
      .isref = true
  };

  println("{TypeTree}", u32t);
  println("{TypeTree}", u32t);
}

test_fn(type_linked_list) {
  let u32t = (TypeTree)tu_of(r_uint, (u16)32);

  TypeTree node_struct;
  let node_tag = (TypeTree)tu_of(r_tag, {.name = fp("Node"), .t = &node_struct});

  let st = mList_init(allocator, r_struct_item);
  defer { mList_deinit(st); };

  mList_push(st, ((r_struct_item){.alignment = 4, .offset = 0, .name = fp("val"), .t = (Type){.t = u32t}}));
  mList_push(st, ((r_struct_item){.alignment = 8, .offset = 8, .name = fp("next"), .t = (Type){.t = node_tag, .ptr_level = 1}}));

  node_struct = (TypeTree)tu_of(r_struct, st);

  test_assert(!TypeTree_cmp(node_struct, node_struct));

  println("{TypeTree}", node_struct);
}

#pragma once
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
i8 fptr_cmp_hash(const void *a, const void *b) { return fptr_cmp(*(fptr *)a, *(fptr *)b); }
typedef enum : u8 {
  builtin_fn,       // @fn((decl|type...) ,decl|type,body) c_func of this type,
  builtin_closure,  // @closure((captures...),(decl|type...) ,decl|type,body) c_func of this type,
  builtin_if,       // @if(cnd , then ,)
  builtin_cif,      // @cif same as if, comptime, produces only one branch
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
  builtin_sizeof,   // @sizeof(T) comptime size of T in bytes
  builtin_alignof,  // @alignof(T) comptime alignment of T
  builtin_offsetof, // @offsetof(T,member) comptime offset of member in T
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

    // compile time
    // they could all be nothing_t's, i'd rather show here what they a re but also
    // not have them take up any storage space
    (c_any /*        */, struct {}),
    (c_int /*        */, typeof(bigint[0])),
    (c_type /*       */, typeof(Type *[0])),
    (c_ast /*        */, typeof(ast *[0])),
    // something else
    (c_fun /*       */, mList(Type)),
    (r_closure /*    */, struct { c_fun captures, args; }),
);
#define T(...) ((Type){.t = __VA_ARGS__})
#define TV(...) ((typed_value){__VA_ARGS__})
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
  u32 c_fun; // function index in list + 1
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

static usize TypeTree_alignof(TypeTree t);
static usize TypeTree_sizeof(TypeTree t);
static inline usize Type_alignof(Type t);
static inline usize Type_sizeof(Type t);
static inline bool Type_is_comptime(Type t) {
  return t.iscomptime ||
         tu_is(builtin, t.t) ||
         tu_is(c_fun, t.t) ||
         tu_is(r_closure, t.t) ||
         tu_is(c_int, t.t) ||
         tu_is(c_any, t.t) ||
         tu_is(c_type, t.t) ||
         tu_is(c_ast, t.t);
}
static usize TypeTree_alignof(TypeTree t) {
  return tu_match_exp(
      t,
      // alias
      (r_tag, tag, TypeTree_alignof(*tag.t)),
      // no representation
      (builtin, _, (usize)1),
      (c_fun, _, (usize)1),
      (r_closure, _, (usize)1),
      (c_int, _, (usize)1),
      (c_any, _, (usize)1),
      (c_type, _, (usize)1),
      (c_ast, _, (usize)1),
      // own reason
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
         foreach (let i, mList_iter(s))
           a = Type_is_comptime(i.t) ? a : MAX$(i.alignment, a);
         a;
       })),
  );
}
static usize TypeTree_sizeof(TypeTree t) {
  return tu_match_exp(
      t,
      // alias
      (r_tag, tag, TypeTree_sizeof(*tag.t)),
      // no runtime representation
      (builtin, _, (usize)0),
      (c_type, _, (usize)0),
      (c_ast, _, (usize)0),
      (c_int, _, (usize)0),
      (c_any, _, (usize)0),
      (c_fun, _, (usize)0),
      (r_closure, _, (usize)0),
      //  0 for it's own reason
      (r_void, _, (usize)0),
      //
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
           sz = Type_is_comptime(i.t) ? sz : MAX$((i.offset + Type_sizeof(i.t)), sz);
         lineup(sz, TypeTree_alignof(t));
       })),
  );
}
static inline usize Type_alignof(Type t) {
  if (t.isref || t.ptr_level) return alignof(void *);
  else return TypeTree_alignof(t.t);
}
static inline usize Type_sizeof(Type t) {
  if (t.isref || t.ptr_level) return sizeof(void *);
  else return TypeTree_sizeof(t.t);
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
  if_decl (let cmp, icmp(a.tag, b.tag)) return cmp;

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

      (c_fun, ai, {
        let bi = tu_catch(c_fun, b);
        if_decl (let cmp, icmp(mList_len(ai), mList_len(bi))) return cmp;

        foreach (let i, range(0, mList_len(ai)))
          if_decl (let cmp, Type_cmp(mList_arr(ai)[i], mList_arr(bi)[i]))
            return cmp;
        return 0;
      }),

      (r_closure, ai, {
        let bi = tu_catch(r_closure, b);
        if_decl (let cmp, icmp(mList_len(ai.captures), mList_len(bi.captures))) return cmp;
        if_decl (let cmp, icmp(mList_len(ai.args), mList_len(bi.args))) return cmp;

        foreach (let i, range(0, mList_len(ai.captures)))
          if_decl (let cmp, Type_cmp(mList_arr(ai.captures)[i], mList_arr(bi.captures)[i]))
            return cmp;

        foreach (let i, range(0, mList_len(ai.args)))
          if_decl (let cmp, Type_cmp(mList_arr(ai.args)[i], mList_arr(bi.args)[i]))
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
    case(builtin_cif):PUTS("cif");break;
    case(builtin__id):PUTS("id");break;
    case(builtin_fn):PUTS("fn");break;
    case(builtin_closure):PUTS("closure");break;
    case(builtin_ast):PUTS("ast");break;
    case(builtin_arrof):PUTS("arrof");break;
    case(builtin_ptrof):PUTS("ptrof");break;
    case(builtin_sizeof):PUTS("sizeof");break;
    case(builtin_alignof):PUTS("alignof");break;
    case(builtin_offsetof):PUTS("offsetof");break;
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
void typeTreePrinterWorker(TypeTree in, moxmap(TypeTree, u32) visitedmap, printerfunction_context _ctx);
void typePrinterWorker(Type in, moxmap(TypeTree, u32) visitedmap, printerfunction_context _ctx) {
  bool has_env = in.iscomptime || in.ptr_level || in.isref || in.isconst || in.isvolatile;
  if (has_env) {
    PUTS("(");
    if (in.ptr_level) PUTS("ptr "), USETYPEPRINTER(u32, in.ptr_level), PUTS(",");
    if (in.isref) PUTS("ref,");
    if (in.isconst) PUTS("const,");
    if (in.isvolatile) PUTS("volatile,");
    if (in.iscomptime) PUTS("comptime,");
  }
  typeTreePrinterWorker(in.t, visitedmap, _ctx);
  if (has_env) PUTS(")");
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
        USENAMEDPRINTER("slice(c8)", t.name);
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
        typeTreePrinterWorker(*arr.t, visitedmap, _ctx);
      }),
      (r_struct, st, {
        typeTreePrinterWorker_add(in, visitedmap);
        PUTS("(s ");
        defer { PUTS(")"); }
        foreach (let type, span(mList_arr(st), mList_len(st))) {
          if (type != mList_arr(st)) PUTS(",");
          USETYPEPRINTER(id_str, type->name);
          Type_is_comptime(type->t)
              ? PUTS("~")
              : (PUTS("@"), USETYPEPRINTER(usize, type->offset), PUTS("="));
          typePrinterWorker(type->t, visitedmap, _ctx);
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
          typePrinterWorker(type->t, visitedmap, _ctx);
        }
      }),
      (c_fun, fn, {
        typeTreePrinterWorker_add(in, visitedmap);
        PUTS("(fn (");
        foreach (let it, mList_iter(fn))
          (_foreach_._foreach_._current) ? (void)PUTS(" ") : (void)0,
              typePrinterWorker(it, visitedmap, _ctx);
        PUTS("),");
        PUTS(")");
      }),
      (r_closure, fn, {
        typeTreePrinterWorker_add(in, visitedmap);
        PUTS("(clos (");
        foreach (let it, mList_iter(fn.captures))
          typePrinterWorker(it, visitedmap, _ctx), PUTS(" ");
        PUTS("),(");
        foreach (let it, mList_iter(fn.args))
          typePrinterWorker(it, visitedmap, _ctx), PUTS(" ");
        PUTS("))");
      })
  );
}

typePrinter(TypeTree) {
  let visited = moxmap_init(PRINTARGS_ALLOCATOR(), TypeTree, u32, TypeTree_cmp_hash);
  defer { moxmap_deinit(visited); };
  typeTreePrinterWorker(in, visited, _ctx);
}

typePrinter(Type) {
  let visited = moxmap_init(PRINTARGS_ALLOCATOR(), TypeTree, u32, TypeTree_cmp_hash);
  defer { moxmap_deinit(visited); };
  typePrinterWorker(in, visited, _ctx);
}
// test

test_fn(type_linked_list) {
  let typeArena = arena_new_ext(allocator, 1024);
  defer arena_cleanup(typeArena);

  let element = (TypeTree)tu_of(r_sint, 32);
  let structItems = mList_init(typeArena, r_struct_item);
  let _struct = (TypeTree)tu_of(r_struct, structItems);
  let _final = (Type){_struct};

  let ptr = (Type){_struct, 1};
  let uint = (Type){element};

  struct analog {
    struct analog *prev, *next;
    i32 element;
  };
  c_fun signatures = mList_init(typeArena, Type);
  mList_pushArr(signatures, ((Type[]){_final, _final}));

  // clang-format off
  mList_pushArr(structItems, ((r_struct_item[]){
    {
       .name = fp("Self"),
       .t =(((Type){tu_of(c_type,{})})),
    },
    {
       .name = fp("getNext"),
       .t =(((Type){tu_of(c_fun,signatures)})),
    },
    {
       .name = fp("getPrev"),
       .t =(((Type){tu_of(c_fun,signatures)})),
    },
    {
       .alignment = alignof(itypeof(struct analog, prev)),
       .offset = offsetof(struct analog, prev),
       .name = fp("prev"),
       .t = ptr,
    },
    {
       .alignment = alignof(itypeof(struct analog, next)),
       .offset = offsetof(struct analog, next),
       .name = fp("next"),
       .t = ptr,
    },
    {
       .alignment = alignof(itypeof(struct analog, element)),
       .offset = offsetof(struct analog, element),
       .name = fp("element"),
       .t = uint,
    },
  }));
  // clang-format on
  println("{Type}", _final);
  test_inteq(alignof(struct analog), Type_alignof(_final));
  test_inteq(sizeof(struct analog), Type_sizeof(_final));
}

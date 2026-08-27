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
    (r_tag /*        */, struct { TypeTree *t; fptr name; }),
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
  bool iscomptime : 1; // meaningless for any,int,type,ast
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

i8 TypeTree_cmp_ex(TypeTree a, TypeTree b, mList(type_cmp_pair) followed);

i8 Type_cmp_ex(Type a, Type b, mList(type_cmp_pair) followed) {
  if (a.ptr_level != b.ptr_level) return a.ptr_level < b.ptr_level ? -1 : 1;

  if (a.isref != b.isref) return a.isref < b.isref ? -1 : 1;
  if (a.isconst != b.isconst) return a.isconst < b.isconst ? -1 : 1;
  if (a.isvolatile != b.isvolatile) return a.isvolatile < b.isvolatile ? -1 : 1;
  if (a.iscomptime != b.iscomptime) return a.iscomptime < b.iscomptime ? -1 : 1;

  return TypeTree_cmp_ex(a.t, b.t, followed);
}

i8 TypeTree_cmp_ex(TypeTree a, TypeTree b, mList(type_cmp_pair) followed) {
  if (!mcmp(a, b)) return 0;
  else if (a.tag < b.tag) return -1;
  else if (a.tag > b.tag) return 1;

  tu_match_void(
      a,
      (builtin, id, return icmp(id, tu_catch(builtin, b))),
      (r_uint, ai, return icmp(ai, tu_catch(r_uint, b))),
      (r_sint, ai, return icmp(ai, tu_catch(r_sint, b))),

      // nominal
      (r_tag, ai, return fptr_cmp(ai.name, tu_catch(r_tag, b).name)),
      // all the same type
      (r_void, _, return 0),
      (c_int, _, return 0),
      (c_any, _, return 0),
      (c_ast, _, return 0),
      (c_type, _, return 0),

      (r_arr, ai, {
        let bi = tu_catch(r_arr, b);
        if_decl (let cmp, icmp(ai.count, bi.count)) return cmp;

        if (ai.t == bi.t) return 0;

        foreach (let p, mList_iter(followed))
          if (p.a == ai.t && p.b == bi.t) return 0;

        mList_push(followed, ((type_cmp_pair){.a = ai.t, .b = bi.t}));
        i8 r = TypeTree_cmp_ex(*ai.t, *bi.t, followed);
        mList_rem(followed, mList_len(followed) - 1);
        return r;
      }),

      (r_func, ai, {
        let bi = tu_catch(r_func, b);
        let la = mList_len(ai);
        let lb = mList_len(bi);
        if (la != lb) return icmp(la, lb);

        foreach (let i, range(0, la))
          if_decl (let cmp, TypeTree_cmp_ex(mList_arr(ai)[i], mList_arr(bi)[i], followed))
            return cmp;

        return 0;
      }),

      (r_closure, ai, {
        let bi = tu_catch(r_closure, b);

        let la_cap = mList_len(ai.captures);
        let lb_cap = mList_len(bi.captures);
        if (la_cap != lb_cap) return icmp(la_cap, lb_cap);

        let la_args = mList_len(ai.args);
        let lb_args = mList_len(bi.args);
        if (la_args != lb_args) return icmp(la_args, lb_args);

        foreach (let i, range(0, la_cap))
          if_decl (let cmp, TypeTree_cmp_ex(mList_arr(ai.captures)[i], mList_arr(bi.captures)[i], followed))
            return cmp;

        foreach (let i, range(0, la_args))
          if_decl (let cmp, TypeTree_cmp_ex(mList_arr(ai.args)[i], mList_arr(bi.args)[i], followed))
            return cmp;

        return 0;
      }),

      (r_struct, ai, {
        let bi = tu_catch(r_struct, b);
        let la = mList_len(ai);
        let lb = mList_len(bi);
        if (la != lb) return icmp(la, lb);

        foreach (let i, range(0, la)) {
          let a_item = mList_arr(ai)[i];
          let b_item = mList_arr(bi)[i];

          if_decl (let cmp, fptr_cmp(a_item.name, b_item.name)) return cmp;
          if_decl (let cmp, icmp(a_item.offset, b_item.offset)) return cmp;
          if_decl (let cmp, icmp(a_item.alignment, b_item.alignment)) return cmp;

          if_decl (let cmp, Type_cmp_ex(a_item.t, b_item.t, followed)) return cmp;
        }
        return 0;
      }),

      (r_bitfield, ai, {
        let bi = tu_catch(r_bitfield, b);
        let la = mList_len(ai);
        let lb = mList_len(bi);
        if (la != lb) return icmp(la, lb);

        foreach (let i, range(0, la)) {
          let a_item = mList_arr(ai)[i];
          let b_item = mList_arr(bi)[i];

          if_decl (let cmp, fptr_cmp(a_item.name, b_item.name)) return cmp;
          if_decl (let cmp, icmp(a_item.width, b_item.width)) return cmp;

          if_decl (let cmp, Type_cmp_ex(a_item.t, b_item.t, followed)) return cmp;
        }
        return 0;
      })
  );
}

i8 Type_cmp(Type a, Type b) {
  let f = mList_init(stdAlloc, type_cmp_pair);
  defer { mList_deinit(f); };
  return Type_cmp_ex(a, b, f);
}

i8 TypeTree_cmp_hash(const void *a, const void *b) {
  let f = mList_init(stdAlloc, type_cmp_pair);
  defer { mList_deinit(f); };
  return TypeTree_cmp_ex(*(TypeTree *)a, *(TypeTree *)b, f);
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

#pragma once
#include "ast.h"
#include "cshl.lib.h"
#include <limits.h>
#include <wheels/bigint.h>
#include <wheels/hxmap.h>
#include <wheels/oxmap.h>

cmpres fptr_cmp_hash(void *, const void *a, const void *b) { return fptr_cmp(*(fptr *)a, *(fptr *)b); }
typedef enum : u8 {
  builtin_fn,        // @fn((decl|type...) ,decl|type,body) c_func of this type,
  builtin_if,        // @if(cnd , then ,)
  builtin_cif,       // @cif same as if, comptime, produces only one branch
  builtin_loop,      // @loop(body)
  builtin_return,    // return x return
  builtin_returnT,   // @returnT() the return type of the enclosing function
  builtin_break,     // @break()
  builtin_continue,  // @continue()

  builtin__id,       // @id(str) an id which corresponds to this string, **evaluates it**

  builtin_ast,       // @ast() type of ast
  builtin_inlineast, // @inlineast() AST result expanded at the call site
  builtin_any,       // @any() type of any
  builtin_void,      // @void() the void type
  builtin_bool,      // @bool() the bool type
  builtin_int,       // @int() compile time bigint
  builtin_type,      // @type() the type of a type
  builtin_const,     // @const(T) constant
  builtin_volatile,  // @volatile(T) volatile
  builtin_comptime,  // @comptime(T) set the compile-time decoration
  builtin_sint,      // @sint(N) integer with N bits
  builtin_uint,      // @uint(N) unsigned integer with N bits
  builtin_typeof,    // @typeof(V) type of argument
  builtin_typeinfo,  // @typeinfo(T) what a type is made of, @typeinfo() being that type
  builtin_opaque,    // @opaque("Tag") an external C tag; no C definition is emitted
  builtin_cinclude,  // @cinclude("header.h") add a C header to generated output
  builtin_include,   // alias for @import: returns a namespace
  builtin_import,
  builtin_using,
  builtin_externfn,  // @externfn("name", (args), return) declare a C function
  builtin_arrof,     // @arrof(N,t) array of N t's
  builtin_ptrof,     // @ptrof(t) pionter to t

  builtin_sizeof,    // @sizeof(T) comptime size of T in bytes
  builtin_alignof,   // @alignof(T) comptime alignment of T
  builtin_offsetof,  // @offsetof(T,member) comptime offset of member in T
  builtin_rawget,    // @rawget(v,name) member v.name itself, ignoring a overload

  builtin_struct,    // @struct{} struct of declarations
  builtin_union,     // @union{} union of declarations, all members share offset 0
  builtin_bitstruct, // @bitstruct{} bitfield of declarations, each one with a bit width
  builtin__this,     // @this() inside of struct, this is the result type

  builtin_qot,       // @qot(arg) ast of arg
  builtin_uqot,      // @unqot(arg) evaluate arg
  builtin_ast_op,    // @ast_op(sym,a,b) build the ast of "a sym b", sym a comptime string
  builtin_apply,     // @apply(f,args...) call f with evaluated args (ast params get values)
  builtin_ref,       // @ref(value) produces a referance to value
  builtin_reft,      // @reft(value) produces the type of a referance to a value
  builtin_at,        // @at(arg) dereference arg
  builtin_where,     // @where(arg) referance to arg
} builtin_id;
// One-based type index; zero is null.
typedef u32 typeidx;

typedef struct Type Type;
typedef struct TypeTree TypeTree;
typedef struct r_struct_item r_struct_item;
typedef struct r_bitf_item r_bitf_item;

sliceDef(Type);
sliceDef(typeidx);
sliceDef(u32);
typedef sglist(Type) typelist;

tu_def(
    (TypeTree, u8),
    // not valid anywhere exept terminating a list
    (r_null /*       */, struct {}),
    (builtin /*      */, builtin_id),
    // runtime
    (r_void /*       */, struct {}),
    (r_bool /*       */, struct {}),
    (r_uint /*       */, struct { u16 bits; }),
    (r_sint /*       */, struct { u16 bits; }),

    // a 1-based run in the superscope's member lists, ending at the first null member
    (r_struct /*     */, struct { typeidx start; }),
    (r_bitfield /*   */, struct { typeidx start; }),

    (r_tag /*        */, struct { typeidx t; srcpos name; }),
    (r_arr /*        */, struct { typeidx t; u32 count; }),

    (c_any /*        */, struct {}),
    (c_int /*        */, struct {}),
    (c_type /*       */, struct {}),
    (c_ast /*        */, struct {}),
    (c_typeinfo /*   */, struct {}),
    (c_namespace /*  */, struct {}),
    (c_inlineast /*  */, struct {}),
    // a run of Type slots, the return type last, terminated by a null
    (c_fun /*        */, struct { typeidx start; u32 origin; }),
);
static_assert(sizeof(TypeTree));
#define T(...) ((Type){__VA_ARGS__})
#define TV(...) ((typed_value){__VA_ARGS__})
#define VS(...) ((value_store){__VA_ARGS__})

static inline TypeTree TypeTree_uint(u16 width) {
  TypeTree t = {.tag = r_uint_enum};
  t.r_uint.bits = width;
  return t;
}
static inline TypeTree TypeTree_sint(u16 width) {
  TypeTree t = {.tag = r_sint_enum};
  t.r_sint.bits = width;
  return t;
}

typedef enum : u8 {
  ref_none = 0,
  ref_ref = 1,
  ref_dref = 2
} reference_kind;
typedef double_offset dref;

struct Type {
  TypeTree t;
  union {
    u32 decorations;
    struct {
      u32 ptr_level : 27;
      reference_kind ref_kind : 2;
      u32 isconst : 1;
      u32 iscomptime : 1; // meaningless for int,type,ast
      u32 isvolatile : 1;
    };
  };
};
static_assert(sizeof(Type));
typedef union {
  void *value;
  struct c_fun_struct {
    u32 fn, spec;
  } c_fun; // function index in list + 1
  bigint c_int;
  struct scope *c_namespace;
  astidx c_ast;   // index into the ast arena, 0 = null
  typeidx c_type; // index into the typelist, 0 = null
} value_store;
static_assert(sizeof(value_store) == sizeof(itypeof(value_store, value)));

typedef struct typed_value {
  typeidx t;
  value_store v;
} typed_value;
typedef struct r_struct_item {
  Type t;
  srcpos name;
  u32 offset, alignment;
} r_struct_item;
typedef struct r_bitf_item {
  Type t;
  srcpos name;
  u32 width;
} r_bitf_item;

typedef sglist(r_struct_item) typestructlist;
typedef sglist(r_bitf_item) typebitflist;

typedef struct typeenv {
  const typelist *tlist;
  const typestructlist *sitems;
  const typebitflist *bitems;
  const poslist *plist;
  const filelist *flist;
} typeenv;

static inline Type Type_at(typeenv e, typeidx idx) {
  if (!e.tlist || idx > e.tlist->len) return (Type){};
  return sglist_get(*e.tlist, idx - 1);
}
static inline TypeTree type_tree(typeenv e, typeidx idx) { return Type_at(e, idx).t; }
static inline fptr type_name(typeenv e, srcpos p) {
  if (!e.plist || !e.flist || !p || p > e.plist->len) return nullFptr;
  let pos = sglist_get(*e.plist, p - 1);
  if (!pos.fidx || pos.fidx > e.flist->len) return nullFptr;
  let content = sglist_get(*e.flist, pos.fidx - 1).filecontent;
  if (!content.ptr || pos.offset > content.len || pos.length > content.len - pos.offset) return nullFptr;
  return (fptr){pos.length, content.ptr + pos.offset};
}
static inline u32 type_run_len(typeenv e, typeidx start) {
  if (!start) return 0;
  u32 n = 0;
  while (!tu_is(r_null, Type_at(e, start + n).t))
    n++;
  return n;
}
static inline u32 type_struct_len(typeenv e, r_struct s) {
  if (!s.start || !e.sitems) return 0;
  u32 n = 0;
  while (s.start - 1 + n < e.sitems->len &&
         !tu_is(r_null, sglist_get(*e.sitems, s.start - 1 + n).t.t))
    n++;
  return n;
}
static inline u32 type_bitf_len(typeenv e, r_bitfield b) {
  if (!b.start || !e.bitems) return 0;
  u32 n = 0;
  while (b.start - 1 + n < e.bitems->len &&
         !tu_is(r_null, sglist_get(*e.bitems, b.start - 1 + n).t.t))
    n++;
  return n;
}
static inline const r_struct_item *type_struct_item(typeenv e, r_struct r, usize i) {
  assertMessage(e.sitems, "struct environment has no members");
  assertMessage(r.start - 1 + i < e.sitems->len, "struct member out of range");
  return &sglist_get(*e.sitems, r.start - 1 + i);
}
static inline const r_bitf_item *type_bitf_item(typeenv e, r_bitfield b, usize i) {
  assertMessage(e.bitems, "bitfield environment has no members");
  assertMessage(b.start - 1 + i < e.bitems->len, "bitfield member out of range");
  return &sglist_get(*e.bitems, b.start - 1 + i);
}
static inline typeidx type_run_at(typeenv e, typeidx start, usize i) {
  assertMessage(start, "null type run");
  return start + (typeidx)i;
}

static usize TypeTree_alignof(TypeTree t, typeenv e);
static usize TypeTree_sizeof(TypeTree t, typeenv e);
static inline usize Type_alignof(Type t, typeenv e);
static inline usize Type_sizeof(Type t, typeenv e);
static inline bool TypeTree_is_ast(TypeTree t) {
  return tu_is(c_ast, t) || tu_is(c_inlineast, t);
}
static inline bool Type_is_comptime(Type t) {
  return t.iscomptime ||
         tu_is(builtin, t.t) ||
         tu_is(c_fun, t.t) ||
         tu_is(c_int, t.t) ||
         tu_is(c_any, t.t) ||
         tu_is(c_type, t.t) ||
         tu_is(c_typeinfo, t.t) || tu_is(c_namespace, t.t) ||
         tu_is(c_ast, t.t) || tu_is(c_inlineast, t.t);
}
static usize TypeTree_alignof(TypeTree t, typeenv e) {
  return tu_match_exp(
      t,
      (r_tag, tag, TypeTree_alignof(type_tree(e, tag.t), e)),
      (r_null, _, (assertMessage(false, "whos askin"), (usize)1)),
      (builtin, _, (usize)1),
      (c_fun, _, (usize)1),
      (c_int, _, (usize)1),
      (c_any, _, (usize)1),
      (c_type, _, (usize)1),
      (c_typeinfo, _, (usize)1),
      (c_namespace, _, (usize)1),
      (c_ast, _, (usize)1),
      (c_inlineast, _, (usize)1),
      (r_void, _, (usize)1),

      (r_uint, u, lineup(u.bits, CHAR_BIT) / CHAR_BIT),
      (r_bool, _, TypeTree_alignof(TypeTree_uint(1), e)),
      (r_sint, u, TypeTree_alignof(TypeTree_uint(u.bits), e)),
      (r_arr, a, TypeTree_alignof(type_tree(e, a.t), e)),
      (r_bitfield, b, ({
         usize s = 0;
         foreach (let i, range(0, type_bitf_len(e, b)))
           s += type_bitf_item(e, b, i)->width;
         TypeTree_alignof(TypeTree_uint((u16)s), e);
       })),
      (r_struct, s, ({
         usize a = 1;
         foreach (let i, range(0, type_struct_len(e, s))) {
           let item = type_struct_item(e, s, i);
           a = Type_is_comptime(item->t) ? a : MAX$(item->alignment, a);
         }
         a;
       })),
  );
}
static usize TypeTree_sizeof(TypeTree t, typeenv e) {
  return tu_match_exp(
      t,
      (r_tag, tag, TypeTree_sizeof(type_tree(e, tag.t), e)),
      (r_null, _, (assertMessage(false, "whos askin"), (usize)1)),
      (builtin, _, (usize)0),
      (c_type, _, (usize)0),
      (c_typeinfo, _, (usize)0),
      (c_namespace, _, (usize)0),
      (c_ast, _, (usize)0),
      (c_inlineast, _, (usize)0),
      (c_int, _, (usize)0),
      (c_any, _, (usize)0),
      (c_fun, _, (usize)0),
      (r_void, _, (usize)0),
      (r_uint, u, lineup(u.bits, CHAR_BIT) / CHAR_BIT),
      (r_bool, _, TypeTree_sizeof(TypeTree_uint(1), e)),
      (r_sint, u, TypeTree_sizeof(TypeTree_uint(u.bits), e)),
      (r_arr, a, TypeTree_sizeof(type_tree(e, a.t), e) * (usize)a.count),
      (r_bitfield, b, ({
         usize s = 0;
         foreach (let i, range(0, type_bitf_len(e, b)))
           s += type_bitf_item(e, b, i)->width;
         TypeTree_sizeof(TypeTree_uint((u16)s), e);
       })),
      (r_struct, s, ({
         usize sz = 0;
         foreach (let i, range(0, type_struct_len(e, s))) {
           let item = type_struct_item(e, s, i);
           sz = Type_is_comptime(item->t) ? sz : MAX$((item->offset + Type_sizeof(item->t, e)), sz);
         }
         lineup(sz, TypeTree_alignof(t, e));
       })),
  );
}
static inline usize Type_alignof(Type t, typeenv e) {
  if (t.ref_kind == ref_dref) return alignof(dref);
  if (t.ref_kind || t.ptr_level) return alignof(void *);
  else return TypeTree_alignof(t.t, e);
}
static inline usize Type_sizeof(Type t, typeenv e) {
  if (t.ref_kind == ref_dref) return sizeof(dref);
  if (t.ref_kind || t.ptr_level) return sizeof(void *);
  else return TypeTree_sizeof(t.t, e);
}

static inline r_struct TypeTree_inner_struct(TypeTree t, typeenv e) {
  return tu_match_exp(
      t,
      (r_struct, r, r),
      (r_tag, r, TypeTree_inner_struct(type_tree(e, r.t), e)),
      (default, (r_struct){0})
  );
}
static inline r_bitfield TypeTree_inner_bitfield(TypeTree t, typeenv e) {
  return tu_match_exp(
      t,
      (r_bitfield, r, r),
      (r_tag, r, TypeTree_inner_bitfield(type_tree(e, r.t), e)),
      (default, (r_bitfield){0})
  );
}
static inline r_arr TypeTree_inner_array(TypeTree t, typeenv e) {
  return tu_match_exp(
      t,
      (r_arr, r, r),
      (r_tag, r, TypeTree_inner_array(type_tree(e, r.t), e)),
      (default, (r_arr){0})
  );
}

cmpres icmp(isize a, isize b) { return a < b ? cmp_lt : a > b ? cmp_gt
                                                              : cmp_eq; }
cmpres TypeTree_cmp(typeenv e, TypeTree a, TypeTree b);
cmpres Type_cmp(typeenv e, Type a, Type b) {
  if_decl (let cmp, icmp(a.decorations, b.decorations)) return cmp;
  return TypeTree_cmp(e, a.t, b.t);
}

cmpres TypeTree_cmp(typeenv e, TypeTree a, TypeTree b) {
  if (!mcmp(a, b)) return cmp_eq;
  if_decl (let cmp, icmp(a.tag, b.tag)) return cmp;

  tu_match_void(
      a,
      (r_null, _, return 0),
      (builtin, ai, return icmp(ai, tu_catch(builtin, b))),
      (r_uint, ai, return icmp(ai.bits, tu_catch(r_uint, b).bits)),
      (r_sint, ai, return icmp(ai.bits, tu_catch(r_sint, b).bits)),
      (r_tag, ai, return fptr_cmp(type_name(e, ai.name), type_name(e, tu_catch(r_tag, b).name))),

      (r_bool, _, return 0),
      (r_void, _, return 0),
      (c_int, _, return 0),
      (c_any, _, return 0),
      (c_ast, _, return 0),
      (c_inlineast, _, return 0),
      (c_type, _, return 0),
      (c_typeinfo, _, return 0),
      (c_namespace, _, return 0),

      (r_arr, ai, {
        let bi = tu_catch(r_arr, b);
        if_decl (let cmp, icmp(ai.count, bi.count)) return cmp;
        return TypeTree_cmp(e, type_tree(e, ai.t), type_tree(e, bi.t));
      }),

      (c_fun, ai, {
        let bi = tu_catch(c_fun, b);
        if_decl (let cmp, icmp(ai.origin, bi.origin)) return cmp;
        u32 an = type_run_len(e, ai.start), bn = type_run_len(e, bi.start);
        if_decl (let cmp, icmp(an, bn)) return cmp;

        foreach (let i, range(0, an))
          if_decl (let cmp, Type_cmp(e, Type_at(e, type_run_at(e, ai.start, i)), Type_at(e, type_run_at(e, bi.start, i))))
            return cmp;
        return 0;
      }),
      (r_struct, ai, {
        let bi = tu_catch(r_struct, b);
        u32 an = type_struct_len(e, ai), bn = type_struct_len(e, bi);
        if_decl (let cmp, icmp(an, bn)) return cmp;

        foreach (let i, range(0, an)) {
          let a_i = type_struct_item(e, ai, i);
          let b_i = type_struct_item(e, bi, i);
          if_decl (let cmp, fptr_cmp(type_name(e, a_i->name), type_name(e, b_i->name))) return cmp;
          if_decl (let cmp, icmp(a_i->offset, b_i->offset)) return cmp;
          if_decl (let cmp, icmp(a_i->alignment, b_i->alignment)) return cmp;
          if_decl (let cmp, Type_cmp(e, a_i->t, b_i->t)) return cmp;
        }
        return 0;
      }),

      (r_bitfield, ai, {
        let bi = tu_catch(r_bitfield, b);
        u32 an = type_bitf_len(e, ai), bn = type_bitf_len(e, bi);
        if_decl (let cmp, icmp(an, bn)) return cmp;

        foreach (let i, range(0, an)) {
          let a_i = type_bitf_item(e, ai, i);
          let b_i = type_bitf_item(e, bi, i);
          if_decl (let cmp, fptr_cmp(type_name(e, a_i->name), type_name(e, b_i->name))) return cmp;
          if_decl (let cmp, icmp(a_i->width, b_i->width)) return cmp;
          if_decl (let cmp, Type_cmp(e, a_i->t, b_i->t)) return cmp;
        }
        return 0;
      })
  );
}

cmpres TypeTree_cmp_hash(void *tenv, const void *a, const void *b) {
  return TypeTree_cmp(*(typeenv *)tenv, *(TypeTree *)a, *(TypeTree *)b);
}
cmpres Type_cmp_hash(void *tenv, const void *a, const void *b) {
  return Type_cmp(*(typeenv *)tenv, *(Type *)a, *(Type *)b);
}

void typeTreePrinterWorker(TypeTree in, moxmap(TypeTree, u32) visitedmap, typeenv e, printerfunction_context _ctx);

static inline void typeTreePrinterWorker_add(TypeTree t, moxmap(TypeTree, u32) visitedmap) {
  moxmap_set(visitedmap, t, ((u32)((oxmap *)visitedmap)->keys->length));
}

typePrinter(builtin_id) {
  switch_tuple(
      in,
      (builtin_fn, /*      */ PUTS("fn")),
      (builtin_at, /*      */ PUTS("at")),
      (builtin_if, /*      */ PUTS("if")),
      (builtin__id, /*     */ PUTS("id")),
      (builtin_qot, /*     */ PUTS("qot")),
      (builtin_ref, /*     */ PUTS("ref")),
      (builtin_any, /*     */ PUTS("any")),
      (builtin_int, /*     */ PUTS("int")),
      (builtin_cif, /*     */ PUTS("cif")),
      (builtin_ast, /*     */ PUTS("ast")),
      (builtin_inlineast, /**/ PUTS("inlineast")),
      (builtin_uqot, /*    */ PUTS("uqot")),
      (builtin_ast_op, /*  */ PUTS("ast_op")),
      (builtin_apply, /*   */ PUTS("apply")),
      (builtin_reft, /*    */ PUTS("reft")),
      (builtin_type, /*    */ PUTS("type")),
      (builtin_void, /*    */ PUTS("void")),
      (builtin_sint, /*    */ PUTS("sint")),
      (builtin_uint, /*    */ PUTS("uint")),
      (builtin_bool, /*    */ PUTS("bool")),
      (builtin__this, /*   */ PUTS("this")),      (builtin_where, /*    */ PUTS("where")),
      (builtin_include, /**/ PUTS("include")),
      (builtin_import, PUTS("import")),
      (builtin_using, PUTS("using")),
      (builtin_arrof, /*   */ PUTS("arrof")),
      (builtin_ptrof, /*   */ PUTS("ptrof")),
      (builtin_break, /*   */ PUTS("break")),
      (builtin_const, /*   */ PUTS("const")),
      (builtin_comptime, /**/ PUTS("comptime")),
      (builtin_loop, /*    */ PUTS("loop")),
      (builtin_sizeof, /*  */ PUTS("sizeof")),
      (builtin_struct, /*  */ PUTS("struct")),
      (builtin_union, /*   */ PUTS("union")),
      (builtin_bitstruct, /**/ PUTS("bitstruct")),
      (builtin_return, /*  */ PUTS("return")),
      (builtin_returnT, /*  */ PUTS("returnT")),
      (builtin_typeof, /*  */ PUTS("typeof")),
      (builtin_typeinfo, /**/ PUTS("typeinfo")),
      (builtin_alignof, /* */ PUTS("alignof")),
      (builtin_continue, /**/ PUTS("continue")),
      (builtin_offsetof, /**/ PUTS("offsetof")),
      (builtin_rawget, /*   */ PUTS("rawget")),
      (builtin_volatile, /**/ PUTS("volatile")),
  );
}
void typePrinterWorker(Type in, moxmap(TypeTree, u32) visitedmap, typeenv e, printerfunction_context _ctx) {
  bool has_env = in.iscomptime || in.ptr_level || in.ref_kind || in.isconst || in.isvolatile;
  if (has_env) {
    PUTS("(");
    if (in.ptr_level) PUTS("ptr "), USETYPEPRINTER(u32, in.ptr_level), PUTS(",");
    if (in.ref_kind == ref_ref) PUTS("ref,");
    if (in.ref_kind == ref_dref) PUTS("dref,");
    if (in.isconst) PUTS("const,");
    if (in.isvolatile) PUTS("volatile,");
    if (in.iscomptime) PUTS("comptime,");
  }
  typeTreePrinterWorker(in.t, visitedmap, e, _ctx);
  if (has_env) PUTS(")");
}
void typeTreePrinterWorker(TypeTree in, moxmap(TypeTree, u32) visitedmap, typeenv e, printerfunction_context _ctx) {
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
      (r_null, /**/ _, PUTS("null")),
      (r_bool, /**/ _, PUTS("bool")),
      (c_int, /* */ _, PUTS("int")),
      (c_type, /**/ _, PUTS("type")),
      (c_typeinfo, _, PUTS("typeinfo")),
      (c_namespace, _, PUTS("namespace")),
      (c_ast, /* */ _, PUTS("ast")),
      (c_inlineast, _, PUTS("inlineast")),
      (c_any, /* */ _, PUTS("any")),
      (r_void, /**/ _, PUTS("void")),
      (r_tag, t, PUTS("(id "), USENAMEDPRINTER("slice(c8)", type_name(e, t.name)), PUTS(")")),
      (r_uint, bits, PUTS("u"), USETYPEPRINTER(usize, (usize)bits.bits)),
      (r_sint, bits, PUTS("s"), USETYPEPRINTER(usize, (usize)bits.bits)),

      (r_arr, arr, {
        typeTreePrinterWorker_add(in, visitedmap);
        PUTS("(a ["), USETYPEPRINTER(usize, (usize)arr.count), PUTS("],");
        typeTreePrinterWorker(type_tree(e, arr.t), visitedmap, e, _ctx);
        PUTS(")");
      }),
      (r_struct, st, {
        typeTreePrinterWorker_add(in, visitedmap);
        PUTS("(s,");
        foreach (let i, range(0, type_struct_len(e, st))) {
          let item = type_struct_item(e, st, i);
          if (i) PUTS(",");
          USENAMEDPRINTER("slice(c8)", type_name(e, item->name));
          Type_is_comptime(item->t)
              ? PUTS("~")
              : (PUTS("@"), USETYPEPRINTER(usize, item->offset), PUTS("="));
          typePrinterWorker(item->t, visitedmap, e, _ctx);
        }
        PUTS(")");
      }),

      (r_bitfield, st, {
        typeTreePrinterWorker_add(in, visitedmap);
        PUTS("(bf,");
        foreach (let i, range(0, type_bitf_len(e, st))) {
          let item = type_bitf_item(e, st, i);
          if (i) PUTS(",");
          USENAMEDPRINTER("slice(c8)", type_name(e, item->name)), PUTS(":");
          USETYPEPRINTER(usize, item->width), PUTS("~");
          typePrinterWorker(item->t, visitedmap, e, _ctx);
        }
        PUTS(")");
      }),
      (c_fun, fn, {
        typeTreePrinterWorker_add(in, visitedmap);
        PUTS("(fn (");
        foreach (let i, range(0, type_run_len(e, fn.start))) {
          if (i) PUTS(" ");
          typePrinterWorker(Type_at(e, type_run_at(e, fn.start, i)), visitedmap, e, _ctx);
        }
        PUTS("))");
      }),
  );
}

typedef struct {
  TypeTree t;
  typeenv env;
} TypeTree_full;
typedef struct {
  Type t;
  typeenv env;
} Type_full;
typedef struct {
  typed_value v;
  typeenv env;
} typed_value_full;
typePrinter(TypeTree_full) {
  let visited = moxmap_init(PRINTARGS_ALLOCATOR(), TypeTree, u32, (oxmap_cmp){TypeTree_cmp_hash, &in.env});
  defer { moxmap_deinit(visited); };
  typeTreePrinterWorker(in.t, visited, in.env, _ctx);
}
typePrinter(Type_full) {
  let visited = moxmap_init(PRINTARGS_ALLOCATOR(), TypeTree, u32, (oxmap_cmp){TypeTree_cmp_hash, &in.env});
  defer { moxmap_deinit(visited); };
  typePrinterWorker(in.t, visited, in.env, _ctx);
}

test_fn(type_bools) {
  typeenv e = {};
  let boolean = T(tu_of(r_bool, {}));
  test_inteq(sizeof(bool), Type_sizeof(boolean, e));
  test_inteq(alignof(bool), Type_alignof(boolean, e));
}
test_fn(type_linked_list) {
  let typeArena = arena_new_ext(allocator, 1024);
  defer { arena_cleanup(typeArena); };

  struct analog {
    struct analog *prev, *next;
    i32 element;
  };

  typelist tlist = {.allocator = typeArena};
  typestructlist sitems = {.allocator = typeArena};
  typebitflist bitems = {.allocator = typeArena};

  let structIdx = (sglist_push(tlist, (Type){}), (typeidx)tlist.len);
  let elementIdx = (sglist_push(tlist, T(TypeTree_sint(32))), (typeidx)tlist.len);
  let signatureStart = (typeidx)tlist.len + 1;
  sglist_push(tlist, (Type){});
  sglist_push(tlist, (Type){});
  sglist_push(tlist, (Type){});

  let signature = T(tu_of(c_fun, {.start = signatureStart}));
  let members = (r_struct){.start = (typeidx)sitems.len + 1};
  let pointer = P$(T(tu_of(r_struct, members)), ($.ptr_level = 1, $));
  // clang-format off
  sglist_push(sitems, (r_struct_item){.t = T(tu_of(c_type, {}))}); // Self
  sglist_push(sitems, (r_struct_item){.t = signature});           // getNext
  sglist_push(sitems, (r_struct_item){.t = signature});           // getPrev
  sglist_push(sitems, (r_struct_item){
    .t = pointer,
    .alignment = alignof(itypeof(struct analog, prev)),
    .offset = offsetof(struct analog, prev),
  });
  sglist_push(sitems, (r_struct_item){
    .t = pointer,
    .alignment = alignof(itypeof(struct analog, next)),
    .offset = offsetof(struct analog, next),
  });
  sglist_push(sitems, (r_struct_item){
    .t = Type_at((typeenv){.tlist = &tlist, .sitems = &sitems, .bitems = &bitems}, elementIdx),
    .alignment = alignof(itypeof(struct analog, element)),
    .offset = offsetof(struct analog, element),
  });
  sglist_push(sitems, (r_struct_item){}); // a null type ends the member run
  // clang-format on

  let _final = T(tu_of(r_struct, members));
  sglist_get(tlist, structIdx - 1) = _final;
  typeenv e = {.tlist = &tlist, .sitems = &sitems, .bitems = &bitems};
  foreach (let i, range(0, type_run_len(e, signature.t.c_fun.start)))
    sglist_get(tlist, signatureStart - 1 + i) = _final;

  void sexplike(const char *data, void *userdata, usize len, bool flush);
  print_wf(sexplike, "{Type_full}\n", ((Type_full){_final, e}));
  test_inteq(alignof(struct analog), Type_alignof(_final, e));
  test_inteq(sizeof(struct analog), Type_sizeof(_final, e));
}

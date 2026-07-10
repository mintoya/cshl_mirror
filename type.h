#pragma once
#include "wheels/macros.h"

#include "ast.h"
#include <wheels/bigint.h>
#include <wheels/fptr.h>
#include <wheels/macros.h>
#include <wheels/mylist.h>
#include <wheels/mytypes.h>
#include <wheels/sList.h>

typedef struct Type Type;
typedef struct r_struct_item r_struct_item;
typedef struct r_bitf_item r_bitf_item;
typedef enum : u8 {
  builtin_if,
  builtin_while,

  builtin_fn,
  builtin_ast,
  builtin_int,
  builtin_uint,
  builtin_type,
  builtin_typeof,

  builtin_return,
  builtin_break,
} builtin_id;
typedef struct func_value {
  mList(struct {
    fptr name;
    // not nullable
    // the type is in there
    struct typed_value *init;
  }) params;
  ast *body;
  struct Env *es;
} func_value;
tu_def( // type
    (Type, u8),
    (builtin /*      */, builtin_id),
    // runtime
    (r_void /*       */, nothing_t),
    (r_uint /*       */, u16),
    (r_sint /*       */, u16),
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
typedef struct r_struct_item {
  fptr name;
  Type type;
  usize alignment, offset;
  struct typed_value *init;
  bool isconst;
} r_struct_item;
typedef struct r_bitf_item {
  Type type;
  fptr name;
  usize width;
  struct typed_value *init;
} r_bitf_item;

typedef struct typed_value {
  Type t;
  union {
    void *value;
    arrstype(c_int) c_int;
    arrstype(c_ast) c_ast;
    arrstype(c_type) c_type;
    arrstype(c_func) c_func;
  };
} typed_value;
typedef struct value {
  typed_value *lvalue;
  typed_value rvalue;
} value;

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
inline bool Type_isComptime(Type t) {
  return tu_match(
      t,
      (c_int, _, true),
      (c_ast, _, true),
      (c_type, _, true),
      (c_func, _, true),
      (default, false)
  );
}

inline usize Type_ralig(Type t) {
  return tu_match(
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
inline usize Type_rsize(Type t) {
  return tu_match(
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
           sz = MAX$(i.isconst ? 0 : i.offset + Type_rsize(i.type), sz);
         lineup(sz, Type_ralig(t));
       })),
      (default, (usize)0)
  );
}
typePrinter(builtin_id) {
  switch (in) {
      // clang-format off
    case(builtin_if):PUTS("if");break;
    case(builtin_fn):PUTS("fn");break;
    case(builtin_ast):PUTS("ast");break;
    case(builtin_int):PUTS("int");break;
    case(builtin_uint):PUTS("uint");break;
    case(builtin_type):PUTS("type");break;
    case(builtin_while):PUTS("while");break;
    case(builtin_return):PUTS("return");break;
    case(builtin_typeof):PUTS("typeof");break;
    case(builtin_break):PUTS("break");break;
      // clang-format on
  }
}
typePrinter(Type) {
  tu_match(
      in,
      (builtin, builtin, {
        PUTS("builtin:");
        USETYPEPRINTER(builtin_id, builtin);
      }),
      (r_void, _, PUTS("void");),
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
          USETYPEPRINTER(Type, type->type);
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
          USETYPEPRINTER(Type, type->type);
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
      (c_func, f, PUTS("fn")),
      (c_int, i, PUTS("int")),
      (c_type, t, PUTS("type")),
      (c_ast, t, PUTS("ast")),
  );
}
typePrinter(typed_value) {
  PUTS("{t:");
  USETYPEPRINTER(Type, in.t);
  PUTS(",v:");

  tu_match(
      in.t,
      (c_int, _, USENAMEDPRINTER("bigint", in.c_int)),
      (c_ast, _, USENAMEDPRINTER("ast", in.c_ast)),
      (c_type, _, USETYPEPRINTER(Type, *in.c_type)),
      (c_func, fn, {
        TODO();
        PUTS("fn @");
        // USENAMEDPRINTER("ast", &in.c_func.body);
        USENAMEDPRINTER("ptr", in.c_func.es);
      }),
      (default, {
        USETYPEPRINTER(fptr, ((fptr){Type_rsize(in.t), in.value}));
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

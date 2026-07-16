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
typedef struct Env Env;
typedef struct func_value {
  mList(struct {
    fptr name;
    // not nullable
    // the type is in there
    struct typed_value *init;
  }) params;
  Env *es;
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
  return mList_map(allocator, v.params, $.init->t);
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
  if (!mcmp(a, b))
    return 0;
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

      (r_uint, ai, { return icmp(ai, tu_catch(r_uint, b)); }),
      (r_sint, ai, { return icmp(ai, tu_catch(r_sint, b)); }),
      (r_ptr, ai, { return Type_cmp(ai[0], tu_catch(r_ptr, b)[0]); }),
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
      (r_ptr, p, return Type_hash(*p) ^ 0x3000;),
      (r_arr, arr, return (Type_hash(*arr.type) * 31) ^ arr.count ^ 0x4000;),
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
      (c_func, fn, {
        PUTS("fn @");
        USENAMEDPRINTER("ptr", in.v.c_func.es);
      }),
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

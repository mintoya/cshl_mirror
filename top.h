#pragma once
#include "ast.h"
#include "type.h"
#include "wheels/allocators/fbaAllocator.h"
#include "wheels/macros.h"
#include "wheels/mytypes.h"
#include "wheels/print.h"
#include "wheels/sList.h"
#include <string.h>
tu_def(
    (Operation, u8),
    // l : left
    // r : right
    // d : dest
    (OSt, /*  */ struct { u32 l, d; }),
    (OLd, /*  */ struct { u32 l, d; }),
    (OOp, /*  */ struct { u32 l, r, d, o; }),
    (OJmp, /* */ u32),
    (OJif, /* */ struct { u32 l; u32 to; }),
    (ODot, /* */ struct { fptr r;u32 l,d; }),
    (OCall, /**/ struct { u32 l, offset, d; }),
    (ORet, /* */ u32)
);
typePrinter(Operation) {
  tu_match(
      in,
      (OSt, o, {
        PUTS("OSt(l:");
        USETYPEPRINTER(u32, o.l);
        PUTS(",d:");
        USETYPEPRINTER(u32, o.d);
        PUTS(")");
      }),
      (OLd, o, {
        PUTS("OLd(l:");
        USETYPEPRINTER(u32, o.l);
        PUTS(",d:");
        USETYPEPRINTER(u32, o.d);
        PUTS(")");
      }),
      (OOp, o, {
        PUTS("OOp(l:");
        USETYPEPRINTER(u32, o.l);
        PUTS(",r:");
        USETYPEPRINTER(u32, o.r);
        PUTS(",d:");
        USETYPEPRINTER(u32, o.d);
        PUTS(",o:");
        USETYPEPRINTER(u32, o.o);
        PUTS(")");
      }),
      (ODot, o, {
        PUTS("ODot(l:");
        USETYPEPRINTER(u32, o.l);
        PUTS(",r:");
        USENAMEDPRINTER("slice(c8)", o.r);
        PUTS(",d:");
        USETYPEPRINTER(u32, o.d);
        PUTS(")");
      }),
      (OJmp, target, {
        PUTS("OJmp(");
        USETYPEPRINTER(u32, target);
        PUTS(")");
      }),
      (OJif, o, {
        PUTS("OJif(l:");
        USETYPEPRINTER(u32, o.l);
        PUTS(",t:");
        USETYPEPRINTER(u32, o.to);
        PUTS(")");
      }),
      (OCall, o, {
        PUTS("OCall(l:");
        USETYPEPRINTER(u32, o.l);
        PUTS(",offset:");
        USETYPEPRINTER(u32, o.offset);
        PUTS(",d:");
        USETYPEPRINTER(u32, o.d);
        PUTS(")");
      }),
      (ORet, src, {
        PUTS("ORet(");
        USETYPEPRINTER(u32, src);
        PUTS(")");
      })
  );
};
typedef struct Env {
  u32 start;
  msList(value) values;
  msList(Operation) ops;
  msxmap(u32) names;

  mxmap(Type, Type *) internal_types1;
  mxmap(fptr, fptr) internal_buffers;

  Env *parent;
} Env;
Env *Env_new(AllocatorV allocator, Env *parent) {
  u64 Type_hash_hash(const void *t);
  i8 Type_cmp_hash(const void *a, const void *b);
  return aValue(
      allocator,
      ((Env){
          parent ? parent->start + msList_len(parent->values) : 0,
          msList_init(allocator, value),
          msList_init(allocator, Operation),
          msxmap_init(allocator, u32),
          mxmap_init(allocator, Type, Type *, 32, Type_hash_hash, Type_cmp_hash),
          mxmap_init(allocator, fptr, fptr, 32, hashfptr, cmpfptr),
          parent,
      })
  );
}
void Env_free(Env *env) {
  var_ allocator = ((hxmap *)env->names)->allocator;
  msList_deInit(allocator, env->values);
  msList_deInit(allocator, env->ops);
  msxmap_deinit(env->names);
  mxmap_deinit(env->internal_types1);
  mxmap_deinit(env->internal_buffers);
  aFree(allocator, env, sizeof(*env));
}
u32 Env_find(Env *env, fptr n) {
  var_ it = env;
  while (it) {
    if_decl (var_ i, msxmap_get(it->names, n))
      return *i + it->start + 1;
    it = it->parent;
  }
  return 0;
}
value Env_findI(Env *env, u32 idx) {
  assertMessage(idx);
  var_ it = env;
  while (it && it->start > idx)
    it = it->parent;
  assertMessage(it);
  assertMessage(msList_len(it->values) > idx - it->start - 1);
  return it->values[idx - it->start - 1];
}
u32 Env_addValue(Env *env, fptr name, Type val) {
  var_ allocator = ((hxmap *)env->names)->allocator;
  TODO();
  // msList_push(allocator, env->values, val);
  if (name.len) msxmap_set(env->names, name, msList_len(env->values));
  return msList_len(env->values) + env->start + 1;
}
//{hashing types
i8 icmp(isize a, isize b) {
  return a > b
             ? -1
         : a < b
             ? 1
             : 0;
}
i8 Type_cmp(Type a, Type b) {
  if (!memcmp(&a, &b, sizeof(a)))
    return 0;
  if (a.tag < b.tag) return -1;
  if (a.tag > b.tag) return 1;
  tu_match(
      a,
      (builtin, id, {
        var_ id2 = tu_catch(builtin, b);
        if (id < id2) return -1;
        if (id > id2) return -1;
        return 0;
      }),
      (r_void, _, return 0;),
      (c_int, _, return 0;),
      (c_ast, _, return 0;),
      (c_type, _, return 0;),
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
      (c_func, _, { TODO(); }),
  );
}
i8 Type_cmp_hash(const void *a, const void *b) { return Type_cmp(*(Type *)a, *(Type *)b); }
u64 Type_hash(Type a) {
  tu_match(
      a,
      (builtin, b, return b + 6),
      (r_void, _, return 0),
      (c_int, _, return 1),
      (c_type, _, return 2),
      (c_ast, _, return 3),
      (c_func, _, return 4),
      (r_uint, v, return (u64)v ^ 0x1000),
      (r_sint, v, return (u64)v ^ 0x2000),
      (r_ptr, p, return Type_hash(*p) ^ 0x3000),
      (r_arr, arr, return (Type_hash(*arr.type) * 31) ^ arr.count ^ 0x4000),
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

#define mList_map(allocator, list, ...) ({ \
  var_ _res =                              \
      mList_init(                          \
          allocator,                       \
          typeof(({                        \
            mList_iType(list) $;           \
            __VA_ARGS__;                   \
          })),                             \
          mList_len(list)                  \
      );                                   \
  foreach (var_ $, mList_iter(list)) {     \
    mList_push(_res, __VA_ARGS__);         \
  }                                        \
  _res;                                    \
})
fptr Env_store(Env *env, fptr mem) { TODO(); }
value *Env_storeValue(Env *env, value val) { TODO(); }
Type *Type_intern(Env *env, Type t) {
  var_ allocator = ((hxmap *)env->names)->allocator;
  if_decl (var_ it, mxmap_get(env->internal_types1, t))
    return *it;
  t = tu_match(
      t,
      (builtin, b, (Type)tu_of(builtin, b)),

      (c_int, _, (Type)tu_of(c_int, {})),
      (c_ast, _, (Type)tu_of(c_ast, {})),
      (c_func, _, (Type)tu_of(c_func, {})),
      (c_type, t, (Type)tu_of(c_type, {})),

      (r_void, _, (Type)tu_of(r_void, {})),
      (r_uint, u, (Type)tu_of(r_uint, u)),
      (r_sint, s, (Type)tu_of(r_sint, s)),
      (r_ptr, p, (Type)tu_of(r_ptr, Type_intern(env, *p))),
      (r_arr, a, (Type)tu_of(r_arr, {Type_intern(env, *a.type), a.count})),
      (r_func, f, (Type)tu_of(r_func, mList_map(allocator, f, *Type_intern(env, $)))),
      (
          r_struct,
          s,
          (Type)tu_of(
              r_struct,
              mList_map(
                  allocator,
                  s,
                  (typeof($)){
                      .alignment = $.alignment,
                      .isconst = $.isconst,
                      .name = Env_store(env, $.name),
                      .offset = $.offset,
                      .init = (TODO(), $.init)
                  }
              )
          )
      ),
      (
          r_bitfield,
          s,
          (Type)tu_of(
              r_bitfield,
              mList_map(
                  allocator,
                  s,
                  (typeof($)){
                      .init = (TODO(), $.init),
                      .name = Env_store(env, $.name),
                      .width = $.width,
                  }
              )
          )
      )
  );

  var_ res = aValue(allocator, t);
  mxmap_set(env->internal_types1, t, res);
  return res;
}

bool Type_equal(Type a, Type b) {
  return !Type_cmp(a, b);
}
Type value_asType(value v) {
  typed_value tv = value_value(v).rvalue;
  return tu_match(
      tv.t,
      (c_type, _, *tv.v.c_type),
      (default, ({ assertMessage(false , "interpret non type as type"); (Type){}; }))
  );
}
Type typeof_value(value v) { return value_access(v).t; }
value Type_asValue(Type t) {
  static msxmap(Type *) map;
  return (value){
      {},
      {
          .t = (Type)tu_of(c_type, {}),
          .v = {.c_type = aValue(stdAlloc, t)}, // TODO
      },
  };
}

typePrinter(Env) {
  PUTS("Env{values:[");
  foreach (var_ v, span(in.values, msList_len(in.values))) {
    if (v != in.values) PUTS(",");
    USETYPEPRINTER(value, *v);
  }
  PUTS("],ops:[");
  foreach (var_ op, span(in.ops, msList_len(in.ops))) {
    if (op != in.ops) PUTS(",");
    USETYPEPRINTER(Operation, *op);
  }
  PUTS("]}");
}

test_fn(typeof_test) {
  var_ ints = arena_new_ext(allocator, 1024);
  defer { arena_cleanup(ints); };
  value integer = (value){
      .rvalue = {.t = (Type)tu_of(c_int, {}), .v.c_int = BInt.from.i64(ints, 0)},
  };
  value integer2 = (value){
      .rvalue = {.t = (Type)tu_of(c_int, {}), .v.c_int = BInt.from.i64(ints, 1)},
  };
  value integer3 = (value){
      .lvalue = &integer2.rvalue,
  };
  // println("{value}", integer);
  // println("{value}", integer2);
  // println("{value}", integer3);
  test_assert(Type_equal(typeof_value(integer), typeof_value(integer2)));
  test_assert(Type_equal(typeof_value(integer), typeof_value(integer3)));

  var_ at = typeof_value(Type_asValue(typeof_value(integer)));
  test_assert(!Type_equal(typeof_value(integer), at));
  var_ bt = typeof_value(Type_asValue(at));
  test_assert(Type_equal(bt, at));
  test_pass();
}

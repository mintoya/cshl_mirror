#pragma once
#include "ast.h"
#include "type.h"
#include "wheels/macros.h"
#include "wheels/mytypes.h"
#include "wheels/sList.h"
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
  Env *parent;
} Env;
Env *Env_new(AllocatorV allocator, Env *parent) {
  return aValue(
      allocator,
      ((Env){
          parent ? parent->start + msList_len(parent->values) : 0,
          msList_init(allocator, value),
          msList_init(allocator, Operation),
          msxmap_init(allocator, u32),
          parent,
      })
  );
}
void Env_free(Env *env) {
  var_ allocator = ((hxmap *)env->names)->allocator;
  msList_deInit(allocator, env->values);
  msList_deInit(allocator, env->ops);
  msxmap_deinit(env->names);
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
Type value_asType(value) {}

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

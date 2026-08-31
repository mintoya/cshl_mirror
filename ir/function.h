#pragma once
#include "../ast.h"
#include "../type.h"
#include "common.h"
#include <wheels/fptr.h>
#include <wheels/mylist.h>
#include <wheels/oxmap.h>

sliceDef(typed_value);
i8 slice_typed_value_cmp(const void *a, const void *b) {
  let av = *(const slice(typed_value) *)a;
  let bv = *(const slice(typed_value) *)b;
  if_decl (let r, icmp(av.len, bv.len)) return r;
  foreach (let i, range(0, av.len)) {
    if_decl (let cm, Type_cmp(av.ptr[i].t, bv.ptr[i].t)) return cm;
    if (Type_is_comptime(av.ptr[i].t))
      if_decl (let cm, icmp((isize)av.ptr[i].v.value, (isize)bv.ptr[i].v.value)) return cm;
  }
  return 0;
}

#include "wheels/sList.h"
static inline Type unknown_function_type(superscope *ss) {
  return T(tu_of(c_any, {}));
}

static inline Type function_item_type(superscope *ss, scope *s, ast *item) {
  if (!item) return unknown_function_type(ss);

  if (tu_is(astDecl, *item)) {
    let decl = tu_catch(astDecl, *item);
    if (decl.type) {
      let value = temporary_interpret(ss, s, decl.type);
      if (tu_is(c_type, value.t.t) && value.v.value)
        return value_asType_loc(value, ast_loc(*decl.type));
    }
    if (decl.value) {
      let value = temporary_interpret(ss, s, decl.value);
      if (!tu_is(c_any, value.t.t)) return value.t;
    }
    return unknown_function_type(ss);
  }

  let value = temporary_interpret(ss, s, item);
  return tu_is(c_any, value.t.t) ? unknown_function_type(ss) : value.t;
}

static inline u32 lower_function(superscope *ss, scope *s, astPBlock args, ast *retT, ast *body) {
  let params = slice_stat(slice(astptr), *sentList_vla(args.stmts));
  let signature = mList_init(ss->alloc, Type);

  for (usize i = 0; i < params.len; i++)
    mList_push(signature, function_item_type(ss, s, params.ptr[i]));

  Type result_type = unknown_function_type(ss);
  if (retT) {
    let value = temporary_interpret(ss, s, retT);
    if (tu_is(c_type, value.t.t) && value.v.value)
      result_type = value_asType_loc(value, ast_loc(*retT));
    else if (!tu_is(c_any, value.t.t)) result_type = value.t;
  }
  mList_push(signature, result_type);

  // clang-format off
  msList_push(ss->alloc, ss->functions, {
      .params = params,
      .retT = retT,
      .body = body,
      .defscope = s,
      .mp = moxmap_init(ss->alloc, slice(typed_value), function_instance, slice_typed_value_cmp),
  });
  // clang-format on

  let function_index = msList_len(ss->functions);
  return scope_add_value(ss, s, TV(T(tu_of(c_fun, signature)), {.c_fun = function_index}));
}

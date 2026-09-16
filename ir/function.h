#pragma once
#include "../type.h"
#include "common.h"
#include "lower.h"
#include "scope.h"
#include "wheels/macros.h"
#include "wheels/mytypes.h"
#include "wheels/sList.h"
#include <string.h>
#include <wheels/fptr.h>
#include <wheels/mylist.h>
#include <wheels/oxmap.h>

sliceDef(typed_value);
sliceDef(Type);

// cmp
static cmpres typed_value_cmp_typed(Type ta, value_store av, Type tb, value_store bv, typeenv e) {
  if_decl (let cmp, Type_cmp(e, ta, tb)) return cmp;
  assertMessage(Type_is_comptime(ta) ?: (!av.value && !bv.value), "should have nulled that out");
  if (!Type_is_comptime(ta)) return 0;
  if (av.value == bv.value) return 0;
  if (ta.isref) { // recurse on comptime known referances
    let nt = ta;
    nt.isref = false;
    assertMessage(av.value && bv.value);
    return typed_value_cmp_typed(
        nt,
        (value_store){.value = *(void **)av.value},
        nt,
        (value_store){.value = *(void **)bv.value},
        e
    );
  }
  return tu_match_exp(
      ta.t,
      (r_void, _, cmp_eq),
      (builtin, _, cmp_eq), // store no data, and cought in type_cmp
      (c_int, _, BInt.cmp(av.c_int, bv.c_int)),
      (c_fun, _, (cmpres)(icmp(av.c_fun.fn, bv.c_fun.fn) ?: icmp(av.c_fun.spec, bv.c_fun.spec))),
      (c_type, _, icmp(av.c_type, bv.c_type)),
      // asts have no size, so memcmp below would call every ast equal
      (c_ast, _, icmp(av.c_ast, bv.c_ast)),
      (default, ({
         assertMessage(!Type_is_comptime(ta));
         cmp_memcmp(av.value, bv.value, Type_sizeof(ta, e));
       })),
  );
}
cmpres typed_value_cmp(typeenv e, typed_value a, typed_value b) {
  if (a.t == b.t && !memcmp(&a.v, &b.v, sizeof(a.v))) return 0;
  return typed_value_cmp_typed(type_at(e, a.t), a.v, type_at(e, b.t), b.v, e);
}
cmpres slice_typed_value_cmp_hash(void *env, const void *a, const void *b) {
  let av = *(slice(typed_value) *)a;
  let bv = *(slice(typed_value) *)b;
  if_decl (let r, icmp(av.len, bv.len)) return r;
  foreach (let i, range(0, av.len))
    if_decl (let cmp, typed_value_cmp(ss_typeenv((superscope *)env), av.ptr[i], bv.ptr[i])) return cmp;
  return 0;
}
//

static inline Type function_item_type errs(superscope *ss, scope *s, astidx item) {
  let item_node = ast_at(ss->asts, item);
  let reg = try_err(lower, (ss, s, item), T());
  typed_value *placeholder = scope_find(ss, s, reg);
  let t = ss_type(ss, placeholder->t);
  if (Type_is_comptime(t))
    *placeholder = (tu_is(c_type, t.t))
                       ? Type_asValue(ss, T(tu_of(c_any, {})))
                       : TV_any(ss);
  return t;
}
// creates a generic function, all functions are treated as generic, all global but names are local
u32 lower_function errs(superscope *ss, scope *s, astidx args, astidx retT, astidx body) {
  let params = builtin_arg_list(ss->asts, args);
  // clang-format off
  let roots = scope_find_rootable(ss,s);
  msList_push(ss->alloc, ss->functions, {
      .params = params,
      .retT = retT,
      .body = body,
      .defscope = roots,
      .instances = msList_init(ss->alloc, function_instance),
      .mp = moxmap_init(ss->alloc, slice(typed_value), u32, (oxmap_cmp){ slice_typed_value_cmp_hash , ss}),
  });
  // clang-format on
  let arg_scope = scope_inferior(ss, roots);
  // reserve the run before evaluating the args, since filling it in can intern more types
  let signature = (c_fun){.start = ss_type_reserve(ss, params.len + 1)};
  let env = ss_typeenv(ss);
  foreach (let p, span(params.start, params.len))
    sglist_get(env.tlist, signature.start - 1 + (p - params.start)) =
        try_err(function_item_type, (ss, arg_scope, p), 0);
  sglist_get(env.tlist, signature.start + params.len - 1) =
      try_err(value_asType, (ss, try_err(temporary_interpret, (ss, arg_scope, retT), 0)), 0);
  let res = scope_add_value(ss, s, TV_with(ss, T(tu_of(c_fun, signature)), VS(.c_fun = msList_len(ss->functions))));
  return res;
}
static u32 lower_function_call_with errs(superscope *ss, scope *s, u32 fn, astidx args, u32 lead) { TODO("lower_function_call_with"); }
u32 lower_function_call errs(superscope *ss, scope *s, u32 fn, astidx args) { return try_err(lower_function_call_with, (ss, s, fn, args, 0), 0); }

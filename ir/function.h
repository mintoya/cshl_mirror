#pragma once
#include "common.h"

sliceDef(typed_value);
sliceDef(Type);

//
// cmp
//

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
//
//

u32 lower_function errs(superscope *ss, scope *s, astidx args, astidx retT, astidx body);
u32 lower_function_call errs(superscope *ss, scope *s, u32 fn, u32 ofirst, astidx args);

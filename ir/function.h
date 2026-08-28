#pragma once
#include "../ast.h"
#include "scope.h"
#include <wheels/fptr.h>
#include <wheels/mylist.h>
#include <wheels/oxmap.h>
typedef int functoin_handle; // stub
typedef struct function function;
typedef struct closure closure;

sliceDef(Type);
i8 slice_type_cmp(const void *a, const void *b) {
  let av = *(const slice(Type) *)a;
  let bv = *(const slice(Type) *)b;
  if_decl (let r, icmp(av.len, bv.len)) return r;
  foreach (let c, range(0, av.len))
    if_decl (let cm, Type_cmp(av.ptr[c], bv.ptr[c]))
      return cm;
  return 0;
}
struct function {
  slice(astptr) args;
  astptr body;
  /*using slice_type_cmp*/
  moxmap(slice(Type), functoin_handle) mp;
};
// struct closure {
//   slice(astptr) captures;
//   function fnpart;
// };

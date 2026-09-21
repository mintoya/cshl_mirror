#pragma once
#include "common.h"
#include "scope.h"
#include "wheels/macros.h"
#include "wheels/print/print_pre.h"
#include "wheels/sList.h"
extern ptrstype(outputFunction) sexplike;

typedef struct {
  itypeof(typed_value, t) * t;
  itypeof(typed_value, v) * v;
} value_typed_mut;
// typedef struct {
//   itypeof(typed_value, t) t;
//   itypeof(typed_value, v) * v;
// } value_typed_mutV;
typedef struct {
  usize len;
  value_store ptr[];
} *intframe_vs;
typedef struct {
  usize pc;
  const scope *sp;
  intframe_vs vals;
  usize framesize;
  value_typed_mut ret;
} intframe;

typedef struct {
  msList(intframe) cs;
  mxmap(const scope *, msList(intframe_vs)) vmap;
  superscope *ss;
  allocfn temp;
} *intstate;

value_typed_mut intstate_find(intstate is, u32 reg) {
  const scope *o = scope_owner(msList_last(is->cs).sp, reg);
  value_store *vs = {};
  typeidx *ti = &o->values[o->start - reg - 1].t;
  if_decl (let f, mxmap_get(is->vmap, o)) vs = &msList_last(*f)->ptr[o->start - reg - 1];
  else vs = &o->values[o->start - reg - 1].v;
  return (value_typed_mut){ti, vs};
}

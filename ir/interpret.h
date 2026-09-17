#pragma once
#include "common.h"
#include "wheels/macros.h"
#include "wheels/sList.h"
sliceDef(typed_value);
typedef struct {
  usize place;
  // corresponding scope
  const scope *s;
  // items copy,  last n items are functions arguments
  slice(typed_value) vals;
} intframe;
typedef struct {
  // stack
  msList(intframe) cs;
  // map of scope to the currently active value list
  mxmap(scope *, msList(slice(typed_value))) vmap;
  // what is written to return
  typed_value *ret;
  superscope *ss;
} *intstate;

typed_value interpret errs(superscope *ss, scope *s);

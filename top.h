#pragma once
#include "ast.h"
#include "op.h"
#include "value.h"
#include "wheels/macros.h"
#include "wheels/mylist.h"
#include "wheels/mytypes.h"
#include "wheels/sList.h"

typedef struct {
  msxmap(u32) symbols;
  mList(value) values;
  mList(struct {
    u32 fnval;
    usize fnpos;
    msList(usize) stackpos;
  }) callstack;
} EState;
EState *EState_new(AllocatorV allocator) {
  return aValue(
      allocator,
      ((EState){
          msxmap_init(allocator, msxmap_iType((itypeof(EState, symbols)){})),
          mList_init(allocator, mList_iType((itypeof(EState, values)){})),
      })
  );
}
void EState_free(EState *es) {
  var_ allocator = mList_allocator(es->callstack);
  mList_deinit(es->values);
  foreach (var_ cs, mList_iter(es->callstack))
    msList_deInit(allocator, cs.stackpos);
  mList_deinit(es->callstack);
  msxmap_deinit(es->symbols);
}

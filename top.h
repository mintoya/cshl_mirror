#pragma once
#include "ast.h"
#include "op.h"
#include "value.h"
#define todo() assertMessage(false, "todo")
typedef struct {
  msxmap(u32) symbols;
  mList(value) values;
} EState;

EState *EState_new(AllocatorV allocator) {
  return aValue(
      allocator,
      ((EState){
          msxmap_init(allocator, u32),
          mList_init(allocator, value)
      })
  );
}
void EState_free(EState *es) {
  var_ allocator = mList_allocator(es->values);
  defer { aFree(allocator, es, sizeof(*es)); };
  msxmap_deinit(es->symbols);
  mList_deinit(es->values);
}
u32 EState_newValue(Type t, ...) { todo(); }

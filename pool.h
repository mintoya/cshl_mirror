#pragma once
#include <wheels/allocator.h>
#include <wheels/macros.h>
#include <wheels/sList.h>
#include <wheels/tests.h>
typedef struct {
  AllocatorV allocator;
  usize item_size, pool_size, current;
  msList(msList(u8)) blocks;
} memory_pool;
memory_pool *memory_pool_new(
    AllocatorV allocator,
    usize item_size,
    usize pool_size
) {
  return aValue(
      allocator,
      ((memory_pool){
          .pool_size = pool_size,
          .item_size = item_size,
          .allocator = allocator,
          .blocks = msList_init(allocator, msList(u8)),
          .current = 0,
      })
  );
}
void *memory_pool_alloc_multi(memory_pool *p, usize count) {
  if (msList_len(p->blocks)) {
    for (; p->current < msList_len(p->blocks); p->current++) {
      if (msList_len(p->blocks[p->current]) + p->item_size * count < msList_cap(p->blocks[p->current])) {
        var_ res = p->blocks[p->current] + msList_len(p->blocks[p->current]);
        msList_len(p->blocks[p->current]) += p->item_size * count;
        return res;
      }
    }
  }
  var_ block = msList_init(p->allocator, u8, p->item_size * MAX$(count, p->pool_size));
  msList_pushArr(p->allocator, block, *VLAP((u8 *)nullptr, count * p->item_size));
  msList_push(p->allocator, p->blocks, block);
  return block;
}
void *memory_pool_alloc(memory_pool *p) { return memory_pool_alloc_multi(p, 1); }
void memory_pool_free(memory_pool *p) {
  var_ allocator = p->allocator;
  foreach (var_ block, vlap(msList_vla(p->blocks)))
    msList_deInit(allocator, block);
  msList_deInit(allocator, p->blocks);
  aFree(allocator, p, sizeof(*p));
}
#define mpool(t) ptrof(fnptrof((ptrof(memory_pool)), ptrof(t)))
#define mpool_init(allocator, type, count) (mpool(type)) memory_pool_new(allocator, sizeof(type), count)
#define mpool_create(pool) (typeof((*pool)((memory_pool *)nullptr)))memory_pool_alloc((memory_pool *)pool)
#define mpool_multi(pool, count)                                 \
  *(ptrstype(typeof((*pool)((memory_pool *)nullptr)))(*)[count]) \
      memory_pool_alloc_multi((memory_pool *)pool, count)
#define mpool_value(pool, ...) ({                                                            \
  var_ _r = (typeof((*pool)((memory_pool *)nullptr)))memory_pool_alloc((memory_pool *)pool); \
  *_r = (typeof(*_r))__VA_ARGS__;                                                            \
  _r;                                                                                        \
})
#define mpool_deinit(pool) memory_pool_free((memory_pool *)pool)
test_fn(memory_pool_test) {
  mpool(int) x;
  x = mpool_init(allocator, int, 100);
  defer { mpool_deinit(x); };
  foreach (var_ i, range(0, 100)) {
    var_ p = mpool_create(x);
    var_ p2 = mpool_value(x, 5);
    *p = *p2;
    var_ p3 = &mpool_multi(x, 2);
    typeof(*p3) f;

    static_assert(types_eq(int *, typeof(p)));
    static_assert(types_eq(int *, typeof(p2)));
  }
  test_pass();
}

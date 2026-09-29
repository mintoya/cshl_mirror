#pragma once
#include <wheels/allocator.h>
#include <wheels/assertMessage.h>

// Individually reclaimable allocations with one owner for bulk cleanup.
typedef struct owned_allocator owned_allocator;
typedef union owned_allocation owned_allocation;
union owned_allocation {
  myAlign alignment;
  struct {
    owned_allocation *prev, *next;
    owned_allocator *owner;
    usize size;
  } item;
};
struct owned_allocator {
  struct allocfns functions;
  allocfn backing;
  owned_allocation *first;
};

static void *owned_allocator_alloc(allocfn allocator, void *ptr, usize oldsize, usize size, const char *file, uint line) {
  let self = (owned_allocator *)allocator;
  owned_allocation *old = ptr ? (owned_allocation *)ptr - 1 : nullptr;
  assertMessage(!!ptr == !!oldsize);
  if (old) assertMessage(old->item.owner == self);
  assertMessage(size <= SIZE_MAX - sizeof(owned_allocation));

  if (!size) {
    assertMessage(old);
    if (old->item.prev) old->item.prev->item.next = old->item.next;
    else self->first = old->item.next;
    if (old->item.next) old->item.next->item.prev = old->item.prev;
    vcall(self->backing, fn, (old, sizeof(*old) + old->item.size, 0, file, line));
    return nullptr;
  }

  let prev = old ? old->item.prev : nullptr;
  let next = old ? old->item.next : self->first;
  owned_allocation *block = vcall(self->backing, fn,
      (old, old ? sizeof(*old) + old->item.size : 0, sizeof(*block) + size, file, line));
  assertMessage(block);
  block->item.prev = prev;
  block->item.next = next;
  block->item.owner = self;
  block->item.size = size;
  if (prev) prev->item.next = block;
  else self->first = block;
  if (next) next->item.prev = block;
  return block + 1;
}

static inline allocfn owned_allocator_new(allocfn backing) {
  return &avalue(backing, ((owned_allocator){
      .functions = {owned_allocator_alloc}, .backing = backing,
  }))->functions;
}

static inline void owned_allocator_cleanup(allocfn allocator) {
  let self = (owned_allocator *)allocator;
  let backing = self->backing;
  while (self->first) {
    let block = self->first;
    self->first = block->item.next;
    vcall(backing, fn, (block, sizeof(*block) + block->item.size, 0, __FILE__, __LINE__));
  }
  adestroy(backing, self);
}

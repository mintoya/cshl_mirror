#include "ast.h"
// 0 in any location is null
tu_def(
    (opcode, u8),
    // create value of tpye l
    (onew, /*  */ struct { u32 l; }),
    // set item in l to const
    (oconst, /**/ struct { u32 l; }),
    // jump to d if l
    (ojif, /*  */ struct { u32 l, d; }),
    // call l with args starting at offset o and storee in d
    (ocall, /* */ struct { u32 l, o, d; }),
    // apply o to l and r and store in d
    (oop, /*   */ struct { u32 l, r, d, o; }),
    // return thing stored in l
    (oret, /*  */ struct { u32 l; }),
    // move thing in l to r
    (omov, /*   */ struct { u32 l, r; }),
    // copy thing pointed to by in l to thing pointed to by r
    (ocpy, /*   */ struct { u32 l, r; }),
);

//
// whatever
//

#include <wheels/mylist.h>

typedef struct scope scope;
typedef struct scope *scopeptr;
sliceDef(scopeptr);

struct scope {
  msList(scopeptr) relatives;
  msxmap(u32) locals;
};

typedef struct {
  AllocatorV alloc;
  mList(opcode) code;
  u32 next_reg;
  scope *env;
} IrBuilder;

#include <wheels/mylist.h>

typedef struct scope scope;
typedef struct scope *scopeptr;
sliceDef(scopeptr);

struct scope {
  msList(scopeptr) relatives;
  msxmap(u32) locals;
};

AllocatorV scope_allocator(scope *s) { return ((hxmap *)s->locals)->allocator; }
scopeptr scope_parent(scope *s) {
  if (msList_len(s->relatives))
    return s->relatives[0];
  else return nullptr;
}
slice(scopeptr) scope_children(scope *s) {
  assertMessage(msList_len(s->relatives));
  return (slice(scopeptr)){
      msList_len(s->relatives) - 1,
      s->relatives + 1
  };
}

void env_bind(IrBuilder *b, fptr id, u32 reg) { msxmap_set(b->env->locals, id, reg); }

u32 lookup_variable_register(IrBuilder *b, fptr id) {
  for (scope *s = b->env; s; s = scope_parent(s))
    if_decl (u32 * reg_ptr, msxmap_get(s->locals, id))
      return *reg_ptr;

  error_at(id.ptr, "unknown variable");
}

// Enters a new block
void env_push(IrBuilder *b) {
  scope *new_scope = aCreate(b->alloc, scope, 1);
  new_scope->relatives = msList_create(b->alloc, scopeptr, 4);
  new_scope->locals = msxmap_create(b->alloc, u32, 16); // Tune initial capacity as needed

  // relatives[0] is the parent
  msList_push(new_scope->relatives, b->env);

  // Add this new scope to the parent's children
  if (b->env) {
    msList_push(b->env->relatives, new_scope);
  }

  b->env = new_scope;
}

// Exits a block
void env_pop(IrBuilder *b) {
  assertMessage(b->env);
  b->env = scope_parent(b->env);
}

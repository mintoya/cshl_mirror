#pragma once
#include "../type.h"
#include "ir.h"
#include "wheels/allocator.h"
#include "wheels/allocators/cballocator.h"
#include "wheels/fptr.h"
#include "wheels/macros.h"
#include "wheels/mytypes.h"
#include "wheels/oxmap.h"
#include "wheels/print/print_pre.h"
#include "wheels/sList.h"
#include "wheels/smap.h"
#include <assert.h>

typedef struct superscope superscope;

struct scope {
  scope *parent;
  msList(ir) instructions;
  msList(typed_value) values;
  moxmap(fptr, u32) named;
  u32 start;
};

struct superscope {
  allocfn alloc;

  moxmap(void *, usize) pointers;
  moxmap(TypeTree, TypeTree *) typetrees;
  moxmap(Type, Type *) types;
  moxmap(Type, msList(value_store)) tdata;
  msList(struct {
    sentList_t(Type) sig;
    scope *body;
  }) all_functions;
  scope *outermost;
  msList(Type *) thisstack;
  u32 struct_count;
};

// scope helpers
static inline typed_value *scope_find(superscope *env, scope *s, u32 idx) {
  while (s)
    if (idx > s->start && idx <= s->start + msList_len(s->values))
      return &s->values[idx - s->start - 1];
    else s = s->parent;
  assertMessage(false, "value %u not found in scope", idx);
}
static inline scope *scope_inferior(superscope *b, scope *s) {
  return avalue(
      b->alloc,
      ((scope){
          .parent = s,
          .instructions = msList_init(b->alloc, ir),
          .values = msList_init(b->alloc, typed_value),
          .named = moxmap_init(b->alloc, fptr, u32, cmpfptr),
          .start = s ? s->start + msList_len(s->values) : 0,
      })
  );
}
static inline void scope_destroy(superscope *b, scope *s) {
  msList_deInit(b->alloc, s->values);
  msList_deInit(b->alloc, s->instructions);
  moxmap_deinit(s->named);
  adestroy(b->alloc, s);
}
static inline Type *ss_getType(superscope *ss, Type t);
static inline u32 scope_add_value(superscope *env, scope *s, typed_value v) {
  msList_push(env->alloc, s->values, (typed_value){*ss_getType(env, v.t), v.v});
  return msList_len(s->values) + s->start;
}
static inline void scope_add_inst(superscope *env, scope *s, ir i) { msList_push(env->alloc, s->instructions, i); }
static inline void scope_bind(superscope *env, scope *s, u32 idx, fptr name) {
  if (moxmap_get(s->named, name)) error_print(name.ptr, "already bound {slice(c8)}", name);
  assertMessage(idx <= msList_len(s->values) + s->start, "%zu <= %zu + %zu", idx, msList_len(s->values), s->start);
  moxmap_set(s->named, name, idx);
}

static inline Type Type_ptr(Type t) {
  t.ptr_level++;
  return t;
}
static inline Type Type_ref(Type t) {
  assertMessage(!t.isref, "no referance to referance");
  t.isref = true;
  return t;
}

// superscope helpers
static inline u32 ss_get_named(superscope *env, scope *s, fptr n) {
  let limit = s->start + msList_len(s->values);
  while (s) {
    if_decl (let val, moxmap_get(s->named, n))
      if (*val <= limit) {
        let vo = scope_find(env, s, *val);
        if (vo->t.isref) return *val;
        let res = scope_add_value(env, s, (typed_value){Type_ref(vo->t)});
        scope_add_inst(env, s, (ir)tu_of(ir_getref, {.d = res, .l = *val}));
        fprintf(stderr, "[dbg] ss_get_named(%.*s) -> %u (found @ %u, isref=%d)\n", (int)n.len, n.ptr, res, *val, vo->t.isref);
        return res;
      }
    limit = s->start;
    s = s->parent;
  }
  error_print(n.ptr, "unbound identifier {slice(c8)}", n);
}

static inline TypeTree *ss_getTypeTree(superscope *ss, TypeTree t) {
  if_decl (let t, moxmap_get(ss->typetrees, t)) return *t;
  let pt = acreate(ss->alloc, TypeTree);
  moxmap_set(ss->typetrees, t, pt);
  *pt = tu_match_exp(
      t,
      (builtin, b, (TypeTree)tu_of(builtin, b)),
      //
      (r_uint, u, (TypeTree)tu_of(r_uint, u)),
      (r_sint, s, (TypeTree)tu_of(r_sint, s)),
      //
      (r_tag, tag, (TypeTree)tu_of(r_tag, {tag.t, tag.name})),
      (r_arr, arr, (TypeTree)tu_of(r_arr, {ss_getTypeTree(ss, *arr.t), arr.count})),
      //
      (r_struct, items, (TypeTree)tu_of(r_struct, items)),
      (r_bitfield, items, (TypeTree)tu_of(r_bitfield, items)),
      (r_closure, params, (TypeTree)tu_of(r_closure, params)),
      (r_func, params, (TypeTree)tu_of(r_func, params)),
      //
      (r_void, v, (TypeTree)tu_of(r_void, {})),
      (c_any, _, (TypeTree)tu_of(c_any, {})),
      (c_ast, _, (TypeTree)tu_of(c_ast, {})),
      (c_int, _, (TypeTree)tu_of(c_int, {})),
      (c_type, _, (TypeTree)tu_of(c_type, {}))
  );
  return *moxmap_set(ss->typetrees, *pt, pt);
}
static inline Type *ss_getType(superscope *ss, Type t) {
  t.t = *ss_getTypeTree(ss, t.t);
  if_decl (let t, moxmap_get(ss->types, t)) return *t;
  return *moxmap_set(ss->types, t, avalue(ss->alloc, t));
}

static inline void irb_cbb(const callbackallocatorhandle *h, void *p) {
  let _map = (itypeof(superscope, pointers))h->cbself->udata;
  if (p == h->inptr)
    return (void)moxmap_set(_map, p, h->outsize);
  moxmap_rem(_map, h->inptr);
  if (p) moxmap_set(_map, p, h->outsize);
}

static inline superscope *ss_init(allocfn allocator) {
  // clang-format off
  superscope* res = avalue(allocator, ((superscope){
    .pointers = moxmap_init(allocator, void*, usize),
    .typetrees = moxmap_init(allocator, TypeTree, TypeTree *,TypeTree_cmp_hash),
    .types = moxmap_init(allocator, Type, Type *,Type_cmp_hash),
    .all_functions = msList_init(allocator, ptrstype(res->all_functions)),
    .thisstack = msList_init(allocator , Type*),
    .tdata = moxmap_init(allocator, Type, msList(value_store),Type_cmp_hash)
  }));
  // clang-format on
  res->alloc = cba_init(allocator, nullptr, irb_cbb, res->pointers);
  res->outermost = scope_inferior(res, nullptr);
  struct {
    char *name;
    builtin_id id;
  } builtins[] = {
      {"@fn", builtin_fn},
      {"@closure", builtin_closure},
      {"@this", builtin__this},
      {"@struct", builtin_struct},
      {"@at", builtin_at},
      {"@where", builtin_where},
      {"@uqot", builtin_uqot},
      {"@qot", builtin_qot},
      {"@type", builtin_type},
      {"@ast", builtin_ast},
      {"@id", builtin__id},
      {"@any", builtin_any},
      {"@void", builtin_void},
      {"@int", builtin_int},
      {"@sint", builtin_sint},
      {"@uint", builtin_uint},
      {"@ptrof", builtin_ptrof},
      {"@ref", builtin_ref},
      {"@refT", builtin_reft},
      {"@volatile", builtin_volatile},
      {"@const", builtin_const},
      {"@typeof", builtin_typeof},
      {"@arr", builtin_arrof},
      {"break", builtin_break},
      {"continue", builtin_continue},
      {"return", builtin_return},
      {"@if", builtin_if},
      {"@while", builtin_while},
  };
  foreach (let i, vla(builtins))
    scope_bind(
        res,
        res->outermost,
        scope_add_value(res, res->outermost, (typed_value){(Type){.t = tu_of(builtin, i.id)}}),
        fp(i.name)
    );
  return res;
}

static inline void ss_deInit(superscope *ss) {
  let allocator = ((struct cballocator *)ss->alloc)->allocator;
  msList_deInit(allocator, ss->all_functions);
  msList_deInit(allocator, ss->thisstack);
  foreach (let it, moxmap_iter(ss->tdata, Type, msList(value_store)))
    msList_deInit(allocator, *it.val);
  foreach (let it, moxmap_iter(ss->pointers, void *, usize))
    adestroy(allocator, (u8(*)[*it.val])it.key);
  moxmap_deinit(ss->tdata);
  moxmap_deinit(ss->pointers);
  moxmap_deinit(ss->typetrees);
  moxmap_deinit(ss->types);
  cba_deinit(ss->alloc);
  adestroy(allocator, ss);
}

#pragma once
#include "../type.h"
#include "common.h"
#include "ir.h"
#include "wheels/macros.h"
#include "wheels/oxmap.h"
#include <assert.h>
#include <wheels/allocators/cballocator.h>
#include <wheels/sList.h>
#include <wheels/smap.h>

// scope helpers
typed_value *scope_find(superscope *env, scope *s, u32 idx) {
  while (s)
    if (idx > s->start && idx <= s->start + msList_len(s->values))
      return &s->values[idx - s->start - 1];
    else s = s->parent;
  assertMessage(false, "value %u not found in scope", idx);
}

scope *scope_inferior(superscope *b, scope *s) {
  let res = avalue(
      b->alloc,
      ((scope){
          .parent = s,
          .instructions = msList_init(b->alloc, ir),
          .values = msList_init(b->alloc, typed_value),
          .named = moxmap_init(b->alloc, fptr, u32, cmpfptr),
          .start = s ? s->start + msList_len(s->values) : 0,
      })
  );
  scope_add_value(b, res, TV(T(tu_of(r_void, {}))));
  return res;
}
// deep-copies a scope tree: child scopes embedded in instructions must point at
// the copy too, or a copied activation resolves values through the original
// scope's storage instead of its own
static scope *scope_copy_deep(superscope *ss, scope *s, scope *parent) {
  if (!s) return nullptr;
  // oxmap_copy asserts on empty maps, so mint a fresh one for those
  moxmap(fptr, u32) named = ((oxmap *)s->named)->keys->length
                                ? (moxmap(fptr, u32))oxmap_copy(ss->alloc, (oxmap *)s->named)
                                : moxmap_init(ss->alloc, fptr, u32, cmpfptr);
  scope *res = avalue(
      ss->alloc,
      ((scope){

          .parent = parent,
          .instructions = msList_initArr(ss->alloc, *msList_vla(s->instructions)),
          .values = msList_initArr(ss->alloc, *msList_vla(s->values)),
          .named = named,
          .start = s->start,
      })
  );
  foreach (let i, span(res->instructions, msList_len(res->instructions))) {
    switch (i->tag) {
      case ir_block_enum: {
        let b = tu_catch(ir_block, *i);
        *i = (ir)tu_of(ir_block, scope_copy_deep(ss, b, res));
        break;
      }
      case ir_if_enum: {
        let f = tu_catch(ir_if, *i);
        *i = (ir)tu_of(
            ir_if,
            {.cnd = scope_copy_deep(ss, f.cnd, res), .t = scope_copy_deep(ss, f.t, res), .f = scope_copy_deep(ss, f.f, res)}
        );
        break;
      }
      case ir_while_enum: {
        let w = tu_catch(ir_while, *i);
        *i = (ir)tu_of(ir_while, {.cnd = scope_copy_deep(ss, w.cnd, res), .body = scope_copy_deep(ss, w.body, res)});
        break;
      }
      default:
        break;
    }
  }
  return res;
}
scope *scope_copy(superscope *ss, scope *s) { return scope_copy_deep(ss, s, s->parent); }
void scope_destroy(superscope *b, scope *s) {
  foreach (let item, span(s->values, msList_len(s->values))) // -1?
    if (tu_is(c_int, item->t.t) && !item->t.isref)
      msList_deInit(b->alloc, item->v.c_int);
  msList_deInit(b->alloc, s->values);
  foreach (let scope, span(s->instructions, msList_len(s->instructions)))
    iflet_tu (let s, ir_block, *scope)
      scope_destroy(b, s);
  msList_deInit(b->alloc, s->instructions);
  moxmap_deinit(s->named);
  adestroy(b->alloc, s);
}
u32 scope_add_value(superscope *env, scope *s, typed_value v) {
  v.t = *ss_getType(env, v.t);
  if (tu_is(c_int, v.t.t) && v.v.c_int) // explicitly destroyed in scope_destroy
    v.v.c_int = BInt.copy(env->alloc, v.v.c_int);
  msList_push(env->alloc, s->values, v);
  return msList_len(s->values) + s->start;
}
static inline void scope_add_inst(superscope *env, scope *s, ir i) {
  // if (!try_interpret(env, s, i))
  msList_push(env->alloc, s->instructions, i);
}
static inline void scope_bind(superscope *env, scope *s, u32 idx, fptr name) {
  if (moxmap_get(s->named, name)) error_print(name.ptr, "already bound {slice(c8)}", name);
  assertMessage(idx <= msList_len(s->values) + s->start, "%zu <= %zu + %zu", idx, msList_len(s->values), s->start);
  moxmap_set(s->named, name, idx);
}
static inline void scope_yield(superscope *ss, scope *s, u32 result) {
  assertMessage(result <= s->start);
  let v = scope_find(ss, s, result);
  assertMessage(tu_is(c_any, v->t.t));
  v->t = msList_last(s->values).t;
  scope_add_inst(ss, s, (ir)tu_of(ir_set, {.l = s->start + msList_len(s->values), .d = result})); // TODO operator stuff
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
static inline u32 ss_get_named_failable_noref(superscope *env, scope *s, fptr n) {
  let o = s;
  let limit = s->start + msList_len(s->values);
  while (s) {
    if_decl (let val, moxmap_get(s->named, n))
      if (*val <= limit)
        return *val;
    limit = s->start;
    s = s->parent;
  }
  return 0;
}
static inline u32 ss_get_named(superscope *env, scope *s, fptr n) {
  let val = ss_get_named_failable_noref(env, s, n);
  if (!val) error_print(n.ptr, "unbound identifier {slice(c8)}", n);
  let vo = scope_find(env, s, val);
  if (vo->t.isref) return val;
  if (Type_is_comptime(vo->t)) return scope_add_value(env, s, *vo);
  let res = scope_add_value(env, s, TV(Type_ref(vo->t)));
  scope_add_inst(env, s, (ir)tu_of(ir_getref, {.d = res, .l = val}));
  return res;
}

TypeTree *ss_getTypeTree(superscope *ss, TypeTree t) {
  if_decl (let t, moxmap_get(ss->typetrees, t)) return *t;
  let pt = acreate(ss->alloc, TypeTree);
  moxmap_set(ss->typetrees, t, pt);
  *pt = tu_match_exp(
      t,
      (builtin, b, (TypeTree)tu_of(builtin, b)),
      //
      (r_uint, u, (TypeTree)tu_of(r_uint, u)),
      (r_sint, s, (TypeTree)tu_of(r_sint, s)),
      (r_bool, _, (TypeTree)tu_of(r_bool, {})),
      //
      (r_tag, tag, (TypeTree)tu_of(r_tag, {tag.t, tag.name})),
      (r_arr, arr, (TypeTree)tu_of(r_arr, {ss_getTypeTree(ss, *arr.t), arr.count})),
      //
      (r_struct, items, (TypeTree)tu_of(r_struct, items)),
      (r_bitfield, items, (TypeTree)tu_of(r_bitfield, items)),
      (r_closure, params, (TypeTree)tu_of(r_closure, params)),
      (c_fun, params, (TypeTree)tu_of(c_fun, params)),
      //
      (r_void, v, (TypeTree)tu_of(r_void, {})),
      (c_any, _, (TypeTree)tu_of(c_any, {})),
      (c_ast, _, (TypeTree)tu_of(c_ast, {})),
      (c_int, _, (TypeTree)tu_of(c_int, {})),
      (c_type, _, (TypeTree)tu_of(c_type, {}))
  );
  return *moxmap_set(ss->typetrees, *pt, pt);
}
Type *ss_getType(superscope *ss, Type t) {
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
    .type_info = moxmap_init(allocator, fptr, scope *, fptr_cmp_hash),
    .thisstack = msList_init(allocator , Type*),
    .functions = msList_init(allocator,function),
    .fnctx = msList_init(allocator,c_fun),
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
      {"@ptr", builtin_ptrof},
      {"@sizeof", builtin_sizeof},
      {"@alignof", builtin_alignof},
      {"@offsetof", builtin_offsetof},
      {"@ref", builtin_ref},
      {"@refT", builtin_reft},
      {"@volatile", builtin_volatile},
      {"@const", builtin_const},
      {"@typeof", builtin_typeof},
      {"@arr", builtin_arrof},
      {"break", builtin_break},
      {"continue", builtin_continue},
      {"return", builtin_return},
      {"@cif", builtin_cif},
      {"@comptime_if", builtin_cif},
      {"@if", builtin_if},
      {"@while", builtin_while},
  };
  foreach (let i, vla(builtins))
    scope_bind(
        res,
        res->outermost,
        scope_add_value(res, res->outermost, TV(T(tu_of(builtin, i.id)))),
        fp(i.name)
    );
  return res;
}

static inline void ss_deInit(superscope *ss) {
  let allocator = ((struct cballocator *)ss->alloc)->allocator;
  msList_deInit(allocator, ss->thisstack);
  foreach (let it, moxmap_iter(ss->pointers, void *, usize))
    adestroy(allocator, (u8(*)[*it.val])it.key);
  moxmap_deinit(ss->pointers);
  moxmap_deinit(ss->typetrees);
  moxmap_deinit(ss->types);
  moxmap_deinit(ss->type_info);
  cba_deinit(ss->alloc);
  adestroy(allocator, ss);
}

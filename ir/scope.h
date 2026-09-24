#pragma once
#include "common.h"
#include "wheels/macros.h"
#include "wheels/print/print_pre.h"
#include <wheels/allocators/cballocator.h>
#include <wheels/smap.h>

const scope *scope_owner(const scope *s, u32 idx) {
  while (!(idx > s->start && idx <= s->start + msList_len(s->values)))
    s = s->parent;
  return s;
}
typed_value *scope_find(superscope *env, const scope *s, u32 idx) {
  let owner = scope_owner(s, idx);
  assertMessage(owner, "value %u not found in scope", idx);
  return &owner->values[idx - owner->start - 1];
}
scope *scope_inferior(superscope *b, scope *s) {
  let named = mList_init(b->alloc, moxmap(fptr, u32));
  mList_push(named, moxmap_init(b->alloc, fptr, u32, (oxmap_cmp){cmpfptr}));
  let res = avalue(
      b->alloc,
      ((scope){
          .parent = s,
          .instructions = msList_init(b->alloc, ir),
          .values = msList_init(b->alloc, typed_value),
          .named = named,
          .using = msList_init(b->alloc, scope_use),
          .start = s ? s->start + msList_len(s->values) : 0,
      })
  );
  scope_add_value(b, res, TV_void(b));
  return res;
}
static inline void scope_push_named(superscope *b, scope *s) {
  mList_push(s->named, moxmap_init(b->alloc, fptr, u32, (oxmap_cmp){cmpfptr}));
}
static inline void scope_pop_named(scope *s) {
  while (msList_len(s->using) && msList_last(s->using).depth == mList_len(s->named))
    msList_pop(s->using);
  if (mList_len(s->named) > 1) moxmap_deinit(mList_pop(s->named));
}
// only any is 'resizable'
static inline void scope_resize_value(superscope *env, typed_value *v) {
  if (v->v.value) return;
  if_decl (let sz, Type_sizeof(ss_type(env, v->t), ss_typeenv(env)))
    v->v.value = acreate(env->alloc, u8[sz]);
}
static inline void scope_set_type(superscope *env, typed_value *v, typeidx t) {
  assertMessage(v->t == env->t_any);
  v->t = t;
  scope_resize_value(env, v);
}
u32 scope_add_value(superscope *env, scope *s, typed_value v) {
  if (tu_is(c_int, ss_type(env, v.t).t) && v.v.c_int)
    v.v.c_int = BInt.copy(env->alloc, v.v.c_int);
  else scope_resize_value(env, &v);
  msList_push(env->alloc, s->values, v);
  return s->result = msList_len(s->values) + s->start;
}

static inline Type Type_ptr errs(Type t) {
  if (t.ref_kind) return_err("POINTER_TO_REFERENCE", T());
  if (Type_is_comptime(P$(t, ($.iscomptime = false, $))))
    return_err("POINTER_TO_COMPTIME_ONLY", T());
  t.ptr_level++;
  return t;
}
static inline Type Type_arr errs(superscope *ss, Type t, u32 len) {
  if (t.ref_kind)
    return_err("ARRAY_OF_REFERENCE", T());
  if (Type_is_comptime(P$(t, ($.iscomptime = false, $))))
    return_err("ARRAY_OF_COMPTIME_ONLY", T());
  return T(tu_of(r_arr, {ss_type_intern(ss, t), len}));
}
static inline Type Type_ref errs(Type t) {
  if (t.ref_kind) return_err("REFERENCE_TO_REFERENCE", T());
  if (Type_is_comptime(P$(t, ($.iscomptime = false, $))))
    return_err("REFERENCE_TO_COMPTIME_ONLY", T());
  return P$(t, ($.ref_kind = ref_ref, $));
}
static inline Type value_asType errs(superscope *ss, typed_value t) {
  if (!tu_is(c_type, ss_type(ss, t.t).t))
    return_err("TYPE_IS_NOT_VALUE", T());
  if (!t.v.c_type) return_err("UNKNOWN_TYPE", T());
  return Type_at(ss_typeenv(ss), t.v.c_type);
}

u32 scope_last_item(superscope *, scope *s) { return s->result; }

static inline cmpres Type_cmp_ndecor(Type a, Type b, typeenv e) {
  let x = ~((Type){{}, {.isconst = true, .isvolatile = true, .iscomptime = true}}.decorations);
  return Type_cmp(
      e,
      P$(a, ($.decorations &= x, $)),
      P$(b, ($.decorations &= x, $))
  );
}
static inline void scope_emit_inst errs(superscope *env, scope *s, ir i) { msList_push(env->alloc, s->instructions, i); }

static bool Type_ncmp_nptr(Type a, Type b, typeenv e) {
  // Function provenance distinguishes reflected types, not signature compatibility.
  if (tu_is(c_fun, a.t) && tu_is(c_fun, b.t)) a.t.c_fun.origin = b.t.c_fun.origin = 0;
  return a.ptr_level == b.ptr_level && !TypeTree_cmp(e, a.t, b.t);
}
static inline bool Type_isnull(Type t) { return tu_is(r_null, t.t); }
static inline void scope_add_inst errs(superscope *env, scope *s, ir i);
static inline void scope_add_inst_typecheck errs(superscope *env, scope *s, ir i);

static inline void scope_add_inst errs(superscope *env, scope *s, ir i) {
  if (tu_is(ir_blocks, i)) scope_push_named(env, s);
  else if (tu_is(ir_blocke, i)) scope_pop_named(s);
  // Generated operand temporaries must not become the enclosing expression's result.
  u32 result = s->result;
  try_err(scope_add_inst_typecheck, (env, s, i));
  s->result = result;
}
// Compiler bindings bypass reserved-name checks.
static inline void scope_sbind errs(superscope *env, scope *s, u32 idx, fptr name) {
  if (moxmap_get(mList_last(s->named), name)) return_err("SHADOWING_SAME_SCOPE");
  assertMessage(!moxmap_get(mList_last(s->named), name));
  assertMessage(idx <= msList_len(s->values) + s->start, "%zu <= %zu + %zu", idx, msList_len(s->values), s->start);
  moxmap_set(mList_last(s->named), name, idx);
}
// _ is reserved for pipes; #_ for function self.
static inline void scope_bind errs(superscope *env, scope *s, u32 idx, fptr name) {
  if (fptr_eq(name, "_") || fptr_eq(name, "#_")) return_err("RESERVED_ID");
  try_err(scope_sbind, (env, s, idx, name));
}

// Namespace lookup never follows lexical parents: those names are not exports.
static scope_binding scope_lookup errs(superscope *env, scope *s, fptr n, u32 limit, bool parents) {
  if (s->resolving) return (scope_binding){};
  s->resolving = true;
  defer { s->resolving = false; };
  let maps = mList_len(s->named);
  foreach (let i, range(0, maps)) {
    let depth = maps - i;
    if_decl (let val, moxmap_get(mList_arr(s->named)[depth - 1], n))
      if (*val <= limit) return (scope_binding){s, *val};
    scope_binding found = {};
    foreach (let use, span(s->using, msList_len(s->using))) {
      if (use->depth != depth || use->at > limit) continue;
      let candidate = try_err(scope_lookup, (env, use->ns, n, UINT32_MAX, false), (scope_binding){});
      if (!candidate.owner) continue;
      if (found.owner && (found.owner != candidate.owner || found.idx != candidate.idx))
        return_err("AMBIGUOUS_NAME", (scope_binding){});
      found = candidate;
    }
    if (found.owner) return found;
  }
  if (parents && s->parent) {
    let found = try_err(scope_lookup, (env, s->parent, n, s->start, true), (scope_binding){});
    let current_function = enclosing_function(s);
    let binding_function = enclosing_function(found.owner);
    if (found.owner && current_function && binding_function && current_function != binding_function &&
        !Type_is_comptime(ss_type(env, scope_find(env, found.owner, found.idx)->t)))
      return (scope_binding){};
    return found;
  }
  return (scope_binding){};
}
// Keep the owner when a runtime binding lives on a different namespace branch.
static void scope_import_binding errs(superscope *ss, scope *s, scope_binding binding, u32 d) {
  let value = *scope_find(ss, binding.owner, binding.idx);
  let type = ss_type(ss, value.t);
  if (Type_is_comptime(type)) {
    *scope_find(ss, s, d) = value;
    return;
  }
  if (!type.ref_kind) type = try_err(Type_ref, (type));
  scope_find(ss, s, d)->t = ss_type_intern(ss, type);
  scope_resize_value(ss, scope_find(ss, s, d));
  try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_scoperef, {.owner = binding.owner, .l = binding.idx, .d = d})));
}
static inline u32 ss_get_named errs(superscope *env, scope *s, fptr n) {
  let found = try_err(scope_lookup, (env, s, n, s->start + msList_len(s->values), true), 0);
  if (!found.owner) return_err("UNBOUND_ID", 0);
  let val = found.idx;
  let vo = scope_find(env, found.owner, val);
  let vt = ss_type(env, vo->t);
  if (Type_is_comptime(vt)) return scope_add_value(env, s, *vo);
  scope *owner = s;
  while (owner && owner != found.owner) owner = owner->parent;
  if (!owner) {
    let res = scope_add_value(env, s, TV_any(env));
    try_err(scope_import_binding, (env, s, found, res), 0);
    return res;
  }
  if (vt.ref_kind) return val;
  let res = scope_add_value(env, s, TV_of(env, try_err(Type_ref, (vt), 0)));
  try_err(scope_add_inst, (env, s, (ir)tu_of(ir_getref, {.d = res, .l = val})), 0);
  return res;
}
static inline u32 ss_get_named_mut errs(superscope *env, scope *s, fptr n) {
  let found = try_err(scope_lookup, (env, s, n, s->start + msList_len(s->values), true), 0);
  if (!found.owner) return_err("UNBOUND_ID", 0);
  // Runtime imports refer to the original namespace storage.
  scope *owner = s;
  while (owner && owner != found.owner) owner = owner->parent;
  if (!owner) {
    if (Type_is_comptime(ss_type(env, scope_find(env, found.owner, found.idx)->t)))
      return_err("ASSIGN_IMPORTED_NAME", 0);
    let res = scope_add_value(env, s, TV_any(env));
    try_err(scope_import_binding, (env, s, found, res), 0);
    return res;
  }
  return found.idx;
}

typeidx ss_type_intern(superscope *ss, Type t) {
  if (tu_is(r_null, t.t)) return 0;
  if_decl (let found, moxmap_get(ss->typeindex, t)) return *found;
  sglist_push(ss->tlist, t);
  let idx = (typeidx)ss->tlist.len;
  moxmap_set(ss->typeindex, t, idx);
  return idx;
}

static inline void irb_cbb(const callbackallocatorhandle *h, void *p) {
  let _map = (itypeof(superscope, pointers))h->cbself->udata;
  if (p == h->inptr)
    return (void)mxmap_set(_map, p, h->outsize);
  mxmap_rem(_map, h->inptr);
  if (p) mxmap_set(_map, p, h->outsize);
}

static inline superscope *ss_init(allocfn allocator, astidx_full ctx) {
  // clang-format off
  superscope* res = avalue(allocator, ((superscope){
    .asts = ctx.alist,
    .plist = ctx.plist,
    .flist  = ctx.flist,
    .tlist = {.allocator = allocator},
    .sitems = {.allocator = allocator},
    .bitems = {.allocator = allocator},
    .pointers = mxmap_init(allocator, void*, usize),
    .thisstack = msList_init(allocator , typeidx),
    .functions = msList_init(allocator,function),
    .c_includes = msList_init(allocator, fptr),
    .imports = msList_init(allocator, scope_import),
  }));
  // clang-format on
  res->typeindex = moxmap_init(allocator, Type, typeidx, (oxmap_cmp){Type_cmp_hash_ss, res});
  res->type_info = moxmap_init(allocator, fptr, scope *, (oxmap_cmp){fptr_cmp_hash, res});
  res->t_void = ss_type_intern(res, T(tu_of(r_void, {})));
  res->t_any = ss_type_intern(res, T(tu_of(c_any, {})));
  res->t_int = ss_type_intern(res, T(tu_of(c_int, {})));
  res->t_bool = ss_type_intern(res, T(tu_of(r_bool, {})));
  res->t_ast = ss_type_intern(res, T(tu_of(c_ast, {})));
  res->t_type = ss_type_intern(res, T(tu_of(c_type, {})));
  res->t_typeinfo = ss_type_intern(res, T(tu_of(c_typeinfo, {})));
  res->alloc = cba_init(allocator, nullptr, irb_cbb, res->pointers);
  res->outermost = scope_inferior(res, nullptr);
  struct {
    char *name;
    builtin_id id;
  } builtins[] = {
      {"@fn", builtin_fn},
      {"@this", builtin__this},
      {"@struct", builtin_struct},
      {"@union", builtin_union},
      {"@bitstruct", builtin_bitstruct},
      {"@at", builtin_at},
      {"@where", builtin_where},
      {"@uqot", builtin_uqot},
      {"@qot", builtin_qot},
      {"@ast_op", builtin_ast_op},
      {"@apply", builtin_apply},
      {"@type", builtin_type},
      {"@ast", builtin_ast},
      {"@inlineast", builtin_inlineast},
      {"@id", builtin__id},
      {"@any", builtin_any},
      {"@void", builtin_void},
      {"@bool", builtin_bool},
      {"@int", builtin_int},
      {"@sint", builtin_sint},
      {"@uint", builtin_uint},
      {"@ptr", builtin_ptrof},
      {"@sizeof", builtin_sizeof},
      {"@alignof", builtin_alignof},
      {"@offsetof", builtin_offsetof},
      {"@rawget", builtin_rawget},
      {"@ref", builtin_ref},
      {"@refT", builtin_reft},
      {"@volatile", builtin_volatile},
      {"@const", builtin_const},
      {"@comptime", builtin_comptime},
      {"@typeof", builtin_typeof},
      {"@typeinfo", builtin_typeinfo},
      {"@opaque", builtin_opaque},
      {"@cinclude", builtin_cinclude},
      {"@include", builtin_include},
      {"@import", builtin_import},
      {"@using", builtin_using},
      {"@externfn", builtin_externfn},
      {"@arr", builtin_arrof},
      {"break", builtin_break},
      {"continue", builtin_continue},
      {"return", builtin_return},
      {"@returnT", builtin_returnT},
      {"@cif", builtin_cif},
      {"@comptime_if", builtin_cif},
      {"@if", builtin_if},
      {"@loop", builtin_loop},
  };
  foreach (let i, vla(builtins))
    catch_errcall(
        (scope_bind,
         (res,
          res->outermost,
          scope_add_value(res, res->outermost, TV_of(res, T(tu_of(builtin, i.id)))),
          fp(i.name))),
        e,
        (assertMessage(false), 0)
    );
  res->prelude = res->outermost;
  res->outermost = scope_inferior(res, res->prelude);
  return res;
}

static inline void ss_deInit(superscope *ss) {
  let allocator = ((struct cballocator *)ss->alloc)->allocator;
  msList_deInit(allocator, ss->thisstack);
  msList_deInit(allocator, ss->c_includes);
  msList_deInit(allocator, ss->imports);
  foreach (let it, mxmap_iter(ss->pointers, void *, usize))
    adestroy(allocator, (u8(*)[*it.val])it.key);
  mxmap_deinit(ss->pointers);
  moxmap_deinit(ss->typeindex);
  moxmap_deinit(ss->type_info);
  sglist_deinit(ss->tlist);
  sglist_deinit(ss->sitems);
  sglist_deinit(ss->bitems);
  cba_deinit(ss->alloc);
  adestroy(allocator, ss);
}

#include "coerce.h"

#pragma once
#include "../owned_allocator.h"
#include "common.h"
#include "wheels/macros.h"
#include "wheels/print/print_pre.h"
#include <wheels/allocators/arenaAllocator.h>
#include <wheels/smap.h>
let const static colon_reserved = ((fptr){2, (u8 *)"_\0"});
let const static pipe_reserved = ((fptr){1, (u8 *)"_"});
sliceDef(u32);
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
  let named = avalue(b->alloc, ((name_env){
                                   .bindings = moxmap_init(b->alloc, fptr, u32, (oxmap_cmp){cmpfptr}),
                               }));
  let res = avalue(
      b->alloc,
      ((scope){
          .parent = s,
          .instructions = msList_init(b->alloc, ir),
          .values = msList_init(b->alloc, typed_value),
          .named = named,
          .parent_named = s ? s->named : nullptr,
          .start = s ? s->start + msList_len(s->values) : 0,
      })
  );
  scope_add_value(b, res, TV_void(b));
  return res;
}
static inline void scope_push_named(superscope *b, scope *s) {
  s->named = avalue(b->alloc, ((name_env){
                                  .parent = s->named,
                                  .bindings = moxmap_init(b->alloc, fptr, u32, (oxmap_cmp){cmpfptr}),
                              }));
}
static inline void scope_pop_named(scope *s) {
  // Functions may still refer to this environment after the block exits.
  if (s->named->parent) s->named = s->named->parent;
}
// only any is 'resizable'
// TODO: wierd
static inline void scope_resize_value(superscope *env, typed_value *v) {
  if (v->v.value) return;
  if_decl (let sz, Type_sizeof(ss_type(env, v->t), ss_typeenv(env)))
    v->v.value = acreate(env->alloc, u8[sz]);
}
static inline void scope_set_type(superscope *env, typed_value *v, typeidx t) {
  assertMessage(v->t == TypesTree_c_any);
  v->t = t;
  scope_resize_value(env, v);
}
u32 scope_add_value(superscope *env, scope *s, typed_value v) {
  if (tu_is(c_int, ss_type(env, v.t).t) && v.v.c_int)
    v.v.c_int = BInt.copy(env->alloc, v.v.c_int);
  else scope_resize_value(env, &v);
  msList_push(env->alloc, s->values, v);
  return msList_len(s->values) + s->start;
}
//
//
//

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
Type lower_asType errs(superscope *ss, scope *s, astidx a) {
  let work = scope_inferior(ss, s);
  let reg = try_err(lower, (ss, work, a), T());
  return try_err(value_asType, (ss, *scope_find(ss, work, reg)), T());
}

u32 scope_last_item(superscope *, scope *s) { return s->start + msList_len(s->values); }

//
//
//

static inline cmpres Type_cmp_ndecor(Type a, Type b, typeenv e) {
  let x = ~((Type){{}, {.isconst = true, .isvolatile = true, .iscomptime = true}}.decorations);
  return Type_cmp(
      e,
      P$(a, ($.decorations &= x, $)),
      P$(b, ($.decorations &= x, $))
  );
}
bool scope_exec_comptime errs(superscope *env, scope *s, ir i);
static inline void scope_emit_inst errs(superscope *env, scope *s, ir i) {
  if (!try_err(scope_exec_comptime, (env, s, i)))
    msList_push(env->alloc, s->instructions, i);
}

static bool Type_ncmp_nptr(Type a, Type b, typeenv e) { return a.ptr_level == b.ptr_level && !TypeTree_cmp(e, a.t, b.t); }
static inline bool Type_isnull(Type t) { return tu_is(r_null, t.t); }
static inline void scope_add_inst errs(superscope *env, scope *s, ir i);
static void scope_add_inst_typecheck errs(superscope *env, scope *s, ir i);

static inline void scope_add_inst errs(superscope *env, scope *s, ir i) {
  if (tu_is(ir_blocks, i)) scope_push_named(env, s);
  else if (tu_is(ir_blocke, i)) scope_pop_named(s);
  try_err(scope_add_inst_typecheck, (env, s, i));
}
static inline void scope_bind_builtin errs(superscope *env, scope *s, u32 idx, fptr name) {
  if (moxmap_get(s->named->bindings, name)) return_err("SHADOWING_SAME_SCOPE");
  assertMessage(!moxmap_get(s->named->bindings, name));
  assertMessage(idx <= msList_len(s->values) + s->start);
  moxmap_set(s->named->bindings, name, idx);
}
static inline void scope_bind errs(superscope *env, scope *s, u32 idx, fptr name) {
  if (
      fptr_eq(name, pipe_reserved) ||
      fptr_eq(name, colon_reserved)
  ) return_err("BOUND_RESERVED_NAME");
  try_err(scope_bind_builtin, (env, s, idx, name));
}
static inline void scope_unbind errs(scope *s, fptr name) {
  if (!moxmap_get(s->named->bindings, name)) return_err("UNBOUND_ID");
  moxmap_rem(s->named->bindings, name);
}

static u32 ss_get_named_failable_noref(superscope *env, scope *s, fptr n) {
  let limit = s->start + msList_len(s->values);
  bool crossed_function = false;
  let names = s->named;
  while (s) {
    for (let map = names; map; map = map->parent) {
      if_decl (let val, moxmap_get(map->bindings, n))
        if (*val <= limit) {
          let t = ss_type(env, scope_find(env, s, *val)->t);
          if (crossed_function && s->instance.fn && !Type_is_comptime(t))
            return 0;
          return *val;
        }
    }
    limit = s->start;
    crossed_function |= !!s->instance.fn;
    names = s->parent_named;
    s = s->parent;
  }
  return 0;
}
static inline u32 ss_get_named errs(superscope *env, scope *s, fptr n) {
  let val = ss_get_named_failable_noref(env, s, n);
  if (!val) return_err("UNBOUND_ID", 0);
  let vo = scope_find(env, s, val);
  let vt = ss_type(env, vo->t);
  if (vt.ref_kind) return val;
  if (Type_is_comptime(vt)) return scope_add_value(env, s, *vo);
  let res = scope_add_value(env, s, TV_of(env, try_err(Type_ref, (vt), 0)));
  try_err(scope_add_inst, (env, s, (ir)tu_of(ir_getref, {.d = res, .l = val})), 0);
  return res;
}
static inline u32 ss_get_named_mut errs(superscope *env, scope *s, fptr n) {
  let val = ss_get_named_failable_noref(env, s, n);
  if (!val) return_err("UNBOUND_ID", 0);
  return val;
}

typeidx ss_type_intern(superscope *ss, Type t) {
  return TypesTree_get(ss->types, t);
}

static inline superscope *ss_init(allocfn allocator, astidx_full ctx) {
  // Reclaim temporary allocations and replaced container buffers immediately,
  // while retaining bulk cleanup for scopes and other compiler-owned objects.
  allocfn owned = owned_allocator_new(allocator);
  // clang-format off
  superscope* res = avalue(owned, ((superscope){
    .alloc = owned,
    .asts = ctx.alist,
    .plist = ctx.plist,
    .flist  = ctx.flist,
    .tlist = {.allocator = owned},
    .sitems = {.allocator = owned},
    .bitems = {.allocator = owned},
    .thisstack = msList_init(owned, typeidx),
    .functions = msList_init(owned,function),
    .coutput = msList_init(owned, fptr),
    .cprologue = msList_init(owned, fptr),
  }));
  // clang-format on
  res->types = TypesTree_init(owned, ss_typeenv(res));
  res->type_info = moxmap_init(owned, fptr, type_metadata, (oxmap_cmp){fptr_cmp_hash, res});
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
      {"@isref", builtin_isref},
      {"@returntype", builtin_returntype},
      {"@typeinfo", builtin_typeinfo},
      {"@arr", builtin_arrof},
      {"break", builtin_break},
      {"continue", builtin_continue},
      {"return", builtin_return},
      {"@cif", builtin_cif},
      {"@comptime_if", builtin_cif},
      {"@if", builtin_if},
      {"@loop", builtin_loop},
      {"@import", builtin_import},
      {"@using", builtin_using},
      {"@unbind", builtin_unbind},
      {"@cout", builtin_cout},
      {"@cinclude", builtin_cinclude},
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
  return res;
}

static inline void ss_deInit(superscope *ss) {
  owned_allocator_cleanup(ss->alloc);
}

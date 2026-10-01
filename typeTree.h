#pragma once
#include "srcpos.h"
#include "type.h"
#include "wheels/smap.h"

typedef moxmap(u32, typeidx) TypesTree_indexmap;
typedef struct TypesTree {
  allocfn allocator;
  typeenv typeenv;
  moxmap(u16, typeidx) uints;
  moxmap(u16, typeidx) sints;
  msList(typeidx) struct_starts;
  msList(typeidx) bitfield_starts;
  msList(typeidx) function_starts;
  msxmap(typeidx) tags;
  moxmap(TypeTree, typeidx) structs, bitfields, functions;
  msList(TypesTree_indexmap) arrays;
  msList(TypesTree_indexmap) decorated;
} TypesTree;
enum : typeidx {
  TypesTree_r_void = 1,
  TypesTree_r_bool,
  TypesTree_c_any,
  TypesTree_c_int,
  TypesTree_c_type,
  TypesTree_c_ast,
  TypesTree_c_typeinfo,
  TypesTree_c_namespace,
  TypesTree_c_inlineast,
  TypesTree_uint_start,
  TypesTree_sint_start = TypesTree_uint_start + 4,
  TypesTree_builtin_start = TypesTree_sint_start + 4,
  TypesTree_builtin_count = builtin_unbind + 1,
  TypesTree_reserved_count = TypesTree_builtin_start + TypesTree_builtin_count - 1,
};

static inline typeidx TypesTree_integer_index(u16 bits, bool is_signed) {
  typeidx start = is_signed ? TypesTree_sint_start : TypesTree_uint_start;
  switch (bits) {
    case 8: return start;
    case 16: return start + 1;
    case 32: return start + 2;
    case 64: return start + 3;
    default: return 0;
  }
}

static cmpres TypesTree_u16_cmp(void *ctx, const void *a, const void *b) {
  return icmp(*(const u16 *)a, *(const u16 *)b);
}
static cmpres TypesTree_u32_cmp(void *ctx, const void *a, const void *b) {
  return icmp(*(const u32 *)a, *(const u32 *)b);
}
static typeidx TypesTree_append(TypesTree *ts, Type t) {
  sglist_push(*ts->typeenv.tlist, t);
  return (typeidx)ts->typeenv.tlist->len;
}
static typeidx TypesTree_run(TypesTree *ts, msList(typeidx) *starts, u32 start, TypeTree t) {
  while (msList_len(*starts) <= start) msList_push(ts->allocator, *starts, 0);
  if (!(*starts)[start]) {
    let map = tu_is(r_struct, t) ? ts->structs : tu_is(r_bitfield, t) ? ts->bitfields : ts->functions;
    if_decl (let found, moxmap_get(map, t)) (*starts)[start] = *found;
    else {
      (*starts)[start] = TypesTree_append(ts, T(t));
      (void)moxmap_set(map, t, (*starts)[start]);
    }
  }
  return (*starts)[start];
}
static typeidx TypesTree_branch(TypesTree *ts, msList(TypesTree_indexmap) *branches,
                                typeidx base, u32 key, Type t) {
  while (msList_len(*branches) <= base) msList_push(ts->allocator, *branches, nullptr);
  if (!(*branches)[base])
    (*branches)[base] = moxmap_init(ts->allocator, u32, typeidx, (oxmap_cmp){TypesTree_u32_cmp});
  let map = (*branches)[base];
  if_decl (let found, moxmap_get(map, key)) return *found;
  typeidx idx = TypesTree_append(ts, t);
  (void)moxmap_set(map, key, idx);
  return idx;
}
typeidx TypesTree_getTree(TypesTree *ts, TypeTree t);
typeidx TypesTree_get(TypesTree *ts, Type t) {
  if (tu_is(r_null, t.t)) return 0;
  typeidx base = TypesTree_getTree(ts, t.t);
  if (!t.decorations) return base;
  t.t = type_tree(ts->typeenv, base);
  return TypesTree_branch(ts, &ts->decorated, base, t.decorations, t);
}

TypesTree *TypesTree_init(allocfn allocator, typeenv typeenv) {
  assertMessage(typeenv.tlist && !typeenv.tlist->len, "type list must be empty");
  Type reserved[] = {
      T(tu_of(r_void, {})), T(tu_of(r_bool, {})),
      T(tu_of(c_any, {})), T(tu_of(c_int, {})),
      T(tu_of(c_type, {})), T(tu_of(c_ast, {})),
      T(tu_of(c_typeinfo, {})), T(tu_of(c_namespace, {})),
      T(tu_of(c_inlineast, {})),
      T(TypeTree_uint(8)), T(TypeTree_uint(16)),
      T(TypeTree_uint(32)), T(TypeTree_uint(64)),
      T(TypeTree_sint(8)), T(TypeTree_sint(16)),
      T(TypeTree_sint(32)), T(TypeTree_sint(64)),
  };
  static_assert(sizeof(reserved) / sizeof(reserved[0]) == TypesTree_builtin_start - 1);
  for (usize i = 0; i < sizeof(reserved) / sizeof(reserved[0]); ++i)
    sglist_push(*typeenv.tlist, reserved[i]);
  for (u32 i = 0; i < TypesTree_builtin_count; ++i)
    sglist_push(*typeenv.tlist, T(tu_of(builtin, (builtin_id)i)));
  TypesTree *ts = avalue(allocator, ((TypesTree){
      .allocator = allocator,
      .typeenv = typeenv,
      .uints = moxmap_init(allocator, u16, typeidx, (oxmap_cmp){TypesTree_u16_cmp}),
      .sints = moxmap_init(allocator, u16, typeidx, (oxmap_cmp){TypesTree_u16_cmp}),
      .struct_starts = msList_init(allocator, typeidx),
      .bitfield_starts = msList_init(allocator, typeidx),
      .function_starts = msList_init(allocator, typeidx),
      .tags = msxmap_init(allocator, typeidx),
      .arrays = msList_init(allocator, TypesTree_indexmap),
      .decorated = msList_init(allocator, TypesTree_indexmap),
  }));
  ts->structs = moxmap_init(allocator, TypeTree, typeidx, (oxmap_cmp){TypeTree_cmp_hash, &ts->typeenv});
  ts->bitfields = moxmap_init(allocator, TypeTree, typeidx, (oxmap_cmp){TypeTree_cmp_hash, &ts->typeenv});
  ts->functions = moxmap_init(allocator, TypeTree, typeidx, (oxmap_cmp){TypeTree_cmp_hash, &ts->typeenv});
  return ts;
}
typeidx TypesTree_getTree(TypesTree *ts, TypeTree t) {
  tu_match_void(
      t,
      (r_struct, st, {
        return TypesTree_run(ts, &ts->struct_starts, st.start, t);
      }),
      (r_bitfield, bf, {
        return TypesTree_run(ts, &ts->bitfield_starts, bf.start, t);
      }),
      (r_uint, u, {
        if_decl (let idx, TypesTree_integer_index(u.bits, false)) return idx;
        if_decl (let found, moxmap_get(ts->uints, u.bits)) return *found;
        typeidx idx = TypesTree_append(ts, T(t));
        (void)moxmap_set(ts->uints, u.bits, idx);
        return idx;
      }),
      (r_sint, u, {
        if_decl (let idx, TypesTree_integer_index(u.bits, true)) return idx;
        if_decl (let found, moxmap_get(ts->sints, u.bits)) return *found;
        typeidx idx = TypesTree_append(ts, T(t));
        (void)moxmap_set(ts->sints, u.bits, idx);
        return idx;
      }),
      (c_fun, fn, return TypesTree_run(ts, &ts->function_starts, fn.start, t)),
      (r_tag, tag, {
        let name = type_name(ts->typeenv, tag.name);
        if_decl (let found, msxmap_get(ts->tags, name)) return *found;
        typeidx idx = TypesTree_append(ts, T(t));
        (void)msxmap_set(ts->tags, name, idx);
        return idx;
      }),
      (r_arr, arr, {
        assertMessage(arr.t && arr.t <= ts->typeenv.tlist->len, "invalid array element type");
        typeidx base = TypesTree_getTree(ts, type_tree(ts->typeenv, arr.t));
        t.r_arr.t = base;
        return TypesTree_branch(ts, &ts->arrays, base, arr.count, T(t));
      }),
      (r_null, _, return 0),
      (r_void, _, return TypesTree_r_void),
      (r_bool, _, return TypesTree_r_bool),
      (c_any, _, return TypesTree_c_any),
      (c_int, _, return TypesTree_c_int),
      (c_type, _, return TypesTree_c_type),
      (c_ast, _, return TypesTree_c_ast),
      (c_typeinfo, _, return TypesTree_c_typeinfo),
      (c_namespace, _, return TypesTree_c_namespace),
      (c_inlineast, _, return TypesTree_c_inlineast),
      (builtin, b, {
        assertMessage(b < TypesTree_builtin_count, "invalid builtin");
        return TypesTree_builtin_start + b;
      })
  );
  unreachable();
}

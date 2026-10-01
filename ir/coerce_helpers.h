#pragma once
#include "common.h"
#include "scope.h"
// Type utilities and member lowering shared by the coercion implementation.

static inline Type Type_stripTag(Type t, typeenv e) {
  while (tu_is(r_tag, t.t))
    t.t = type_tree(e, tu_catch(r_tag, t.t).t);
  return t;
}

struct type_named {
  scope *owner;
  u32 idx;
};
static inline struct type_named ss_tag_named(superscope *ss, TypeTree t, fptr name) {
  iflet_tu (let tag, r_tag, t)
    if_decl (let info, moxmap_get(ss->type_info, type_name(ss_typeenv(ss), tag.name)))
      if_decl (let idx, moxmap_get(info->namespace->named->bindings, name)) return (struct type_named){info->namespace, *idx};
  return (struct type_named){};
}
static inline struct type_named tag_set_member(superscope *ss, Type t) {
  if (t.ref_kind || t.ptr_level || t.isconst || t.isvolatile || t.iscomptime)
    return (struct type_named){};
  return ss_tag_named(ss, t.t, fp("="));
}

static bool Type_isint(Type t, typeenv en) {
  return !t.ptr_level &&
         tu_match_exp(
             Type_stripTag(t, en).t,
             (r_sint, _, (bool)true),
             (r_uint, _, (bool)true),
             (c_int, _, (bool)true),
             (default, false),
         );
}
static inline u32 Type_intLike_bitcount(Type t, typeenv e) {
  return tu_match_exp(
      Type_stripTag(t, e).t,
      (r_sint, s, s.bits),
      (r_uint, s, s.bits),
      (r_bool, s, (u16)1),
      (default, (u16)0),
  );
}
static bool Type_is_integer_like(Type t, typeenv e) {
  t = Type_stripTag(t, e);
  return !t.ptr_level && (tu_is(r_bool, t.t) || Type_isint(t, e));
}

static inline Type Type_weaker(Type a, Type b, typeenv e) {
  Type as = Type_stripTag(a, e), bs = Type_stripTag(b, e);
  bool ac = Type_is_comptime(as), bc = Type_is_comptime(bs);
  Type pick = {};
  if (ac != bc) pick = ac ? bs : as;
  else if (ac) pick = as;
  else if (Type_intLike_bitcount(as, e) != Type_intLike_bitcount(bs, e))
    pick = Type_intLike_bitcount(as, e) > Type_intLike_bitcount(bs, e) ? as : bs;
  else pick = tu_is(r_uint, as.t) ? as : bs;
  pick.ref_kind = ref_none;
  pick.iscomptime = false;
  pick.isconst = a.isconst && b.isconst;
  pick.isvolatile = a.isvolatile && b.isvolatile;
  return pick;
}

static inline bool scope_adopt_any(superscope *env, typed_value *dv, Type t) {
  if (!tu_is(c_any, Type_stripTag(ss_type(env, dv->t), ss_typeenv(env)).t))
    return false;
  scope_set_type(env, dv, ss_type_intern(env, t));
  return true;
}
static inline bool Type_adopt_any(Type *slot, Type t) {
  if (!tu_is(c_any, slot->t)) return false;
  t.ref_kind = ref_none;
  *slot = t;
  return true;
}

static inline typed_value Type_asValue(superscope *s, Type t) { return TV_type(s, ss_type_intern(s, t)); }

static inline bigint int_as_bigint(allocfn allocator, typed_value t, typeenv e) {
  let tt = Type_at(e, t.t);
  assertMessage(!tt.ptr_level);
  assertMessage(t.v.value, "tried to convert unknown value");
  return tu_match_exp(
      tt.t,
      (c_int, _, BInt.copy(allocator, t.v.c_int)),
      (r_sint, si, BInt.from.bits(allocator, t.v.value, si.bits, 1)),
      (r_uint, si, BInt.from.bits(allocator, t.v.value, si.bits, 0)),
      (r_bool, _, BInt.from.i64(allocator, !!*(u8 *)t.v.value)),
      (r_tag, tg, int_as_bigint(allocator, (typed_value){.t = tg.t, .v = t.v}, e)),
      (default,
       (({
          if (Type_isint(tt, e))
            assertMessage(
                false,
                "unhandled integer case , %s",
                snprint(stdAlloc, "{Type_full}", ((Type_full){tt, e})).ptr
            );
          assertMessage(
              false,
              "expeceted integer, found %s",
              snprint(stdAlloc, "{typed_value_full}", ((typed_value_full){t, e})).ptr
          );
        }),
        (bigint) nullptr)),
  );
}

static inline void bigint_into_slot(u8 *mem, usize len, bigint b) {
  foreach (let w, range(0, (lineup(len, sizeof(bigint_unit)) / sizeof(bigint_unit))))
    memcpy(mem + w * sizeof(bigint_unit), REF(bigint_get(b, w)), MIN$(len - w * sizeof(bigint_unit), sizeof(bigint_unit)));
}
static inline void bigint_into_int(superscope *ss, allocfn alloc, typed_value *t, bigint b) {
  let e = ss_typeenv(ss);
  let tt = Type_at(e, t->t);
  tu_match_void(
      tt.t,
      (c_int, _, {
        let o = &t->v.c_int;
        msList_deInit(alloc, *o);
        *o = BInt.copy(alloc, b);
      }),
      (r_sint, si, bigint_into_slot(t->v.value, Type_sizeof(tt, e), b)),
      (r_uint, si, bigint_into_slot(t->v.value, Type_sizeof(tt, e), b)),
      (r_bool, _, { *(u8 *)t->v.value = !!BInt.cmp(b, nullptr); }),
      (r_tag, tg, {
        let temp = (typed_value){.t = tg.t, .v = t->v};
        return bigint_into_int(ss, alloc, &temp, b);
      }),
      (default, {
        if (Type_isint(tt, e))
          assertMessage(
              false,
              "unhandled integer case , %s",
              snprint(stdAlloc, "{Type_full}", ((Type_full){tt, e})).ptr
          );
        assertMessage(false, "should not be called with a non-int");
      }),
  );
}

void tcheck_cast errs(superscope *ss, scope *s, ir_cast cv, bool is_explicit);
// Value-returning adapter. Conversion policy belongs to tcheck_cast.
static u32 scope_cast_value errs(superscope *ss, scope *s, u32 left, Type to, bool is_explicit) {
  if (tu_is(c_any, to.t) || !Type_cmp(ss_typeenv(ss), ss_type(ss, scope_find(ss, s, left)->t), to)) return left;
  let dest = scope_add_value(ss, s, TV_of(ss, to));
  try_err(tcheck_cast, (ss, s, (ir_cast){.l = left, .d = dest}, is_explicit), 0);
  return dest;
}
static u32 scope_convert_anonymous errs(superscope *ss, scope *s, u32 source, Type target, type_metadata info) {
  let e = ss_typeenv(ss);
  let source_members = TypeTree_inner_struct(ss_type(ss, scope_find(ss, s, source)->t).t, e);
  let target_members = TypeTree_inner_struct(target.t, e);
  let count = type_struct_len(e, source_members);
  let target_count = type_struct_len(e, target_members);
  usize runtime_count = 0;
  bool shared_offset = true;
  foreach (let i, range(0, target_count)) {
    let member = type_struct_item(e, target_members, i);
    if (Type_is_comptime(member->t)) continue;
    runtime_count++;
    shared_offset &= member->offset == 0;
  }
  // Members sharing offset zero select one initializer, regardless of type origin.
  let required_count = runtime_count && shared_offset ? 1 : runtime_count;
  if (count != required_count)
    return_err("ANONYMOUS_MEMBER_COUNT", 0);

  let used = slice_alloc(ss->alloc, u8, target_count);
  defer { slice_free(ss->alloc, used); };
  if (used.len) memset(used.ptr, 0, used.len);
  u32 result = scope_add_value(ss, s, TV_of(ss, target));
  usize next = 0;
  foreach (let i, range(0, count)) {
    let source_member = *type_struct_item(e, source_members, i);
    let name = type_name(e, source_member.name);
    usize index = target_count;
    if (info.is_positional) {
      while (next < target_count && Type_is_comptime(type_struct_item(e, target_members, next)->t))
        next++;
      index = next++;
    } else {
      foreach (let j, range(0, target_count)) {
        let member = type_struct_item(e, target_members, j);
        if (!Type_is_comptime(member->t) && !fptr_cmp(name, type_name(e, member->name))) {
          index = j;
          break;
        }
      }
    }
    if (index == target_count) return_err("ANONYMOUS_UNKNOWN_MEMBER", 0);
    if (used.ptr[index]) return_err("ANONYMOUS_DUPLICATE_MEMBER", 0);
    used.ptr[index] = true;

    u32 value;
    if (Type_is_comptime(source_member.t)) {
      let binding = moxmap_get(info.namespace->named->bindings, name);
      if (!binding) return_err("ANONYMOUS_MEMBER_NO_VALUE", 0);
      value = scope_add_value(ss, s, *scope_find(ss, info.namespace, *binding));
    } else {
      value = scope_add_value(ss, s, TV_any(ss));
      try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_field, {.l = source, .d = value, .i = (u32)i, .t = ss_type_intern(ss, P$(source_member.t, ($.ref_kind = ref_ref, $)))})), 0);
    }
    let field = scope_add_value(ss, s, TV_any(ss));
    try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_field, {.l = result, .d = field, .i = (u32)index, .t = ss_type_intern(ss, P$(type_struct_item(e, target_members, index)->t, ($.ref_kind = ref_ref, $)))})), 0);
    try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_set, {.l = value, .d = field})), 0);
  }
  return result;
}
u32 lower_condition errs(superscope *ss, scope *s, astidx a) {
  let l = try_err(lower, (ss, s, a), 0);
  return try_err(scope_cast_value, (ss, s, l, T(tu_of(r_bool, {})), false), 0);
}
static inline Type scope_reg_type(superscope *env, scope *s, u32 idx) { return ss_type(env, scope_find(env, s, idx)->t); }
static inline Type scope_reg_to_type_stripTag(superscope *env, scope *s, u32 idx) {
  return Type_stripTag(scope_reg_type(env, s, idx), ss_typeenv(env));
}

static bool Type_if_deref(typeenv e, Type t, Type *out) {
  t = Type_stripTag(t, e);
  if (t.ref_kind) {
    t.ref_kind = ref_none;
    *out = t;
    return true;
  }
  if (t.ptr_level) {
    t.ptr_level--;
    *out = t;
    return true;
  }
  return false;
}

static void lower_ast_member errs(superscope *ss, scope *s, u32 l, u32 r, u32 d) {
  let rv = scope_find(ss, s, r);
  if (!TypeTree_is_ast(ss_type(ss, rv->t).t) || !rv->v.c_ast) return_err("DOT_NEEDS_AST");
  astidx self = scope_find(ss, s, l)->v.c_ast;
  astidx res = 0;
  let name_node = ast_at(ss, rv->v.c_ast);
  iflet_tu (let m, astString, name_node) {
    let ms = ast_text(ss, m);
    if (self && tu_is(astOp, ast_at(ss, self)) && ms.len >= 2 && ms.ptr[0] == '"' && ms.ptr[ms.len - 1] == '"') {
      union {
        u32 n;
        char s[sizeof(u32)];
      } spelling = {};
      memcpy(spelling.s, ms.ptr + 1, MIN$(ms.len - 2, sizeof(u32)));
      let self_node = ast_at(ss, self);
      if (tu_catch(astOp, self_node).number_rep == spelling.n) res = self;
    }
  } else if (tu_is(astId, name_node)) {
    let ms = ast_text(ss, tu_catch(astId, name_node));
    if (self) {
      iflet_tu (let aop, astOp, ast_at(ss, self)) {
        if (fptr_eq(ms, "opleft")) res = ast_child(ss, aop.left_right);
        else if (fptr_eq(ms, "opright")) res = ast_child(ss, aop.left_right + 1);
      }
      if (!res)
        iflet_tu (let dcl, astDecl, ast_at(ss, self)) {
          if (fptr_eq(ms, "name")) res = ast_child(ss, dcl.name_type_value);
          else if (fptr_eq(ms, "type")) res = ast_child(ss, dcl.name_type_value + 1);
          else if (fptr_eq(ms, "value")) res = ast_child(ss, dcl.name_type_value + 2);
        }
      if (!res)
        res = tu_match_exp(
            ast_at(ss, self),
            (astOp, _, fptr_eq(ms, "op") ? self : (astidx)0),
            (astId, _, fptr_eq(ms, "id") ? self : (astidx)0),
            (astDecl, _, fptr_eq(ms, "decl") ? self : (astidx)0),
            (astNumber, _, fptr_eq(ms, "number") ? self : (astidx)0),
            (astString, _, fptr_eq(ms, "string") ? self : (astidx)0),
            (astPBlock, _, fptr_eq(ms, "pblock") ? self : (astidx)0),
            (astBBlock, _, fptr_eq(ms, "bblock") ? self : (astidx)0),
            (astABlock, _, fptr_eq(ms, "ablock") ? self : (astidx)0),
            (default, (astidx)0),
        );
    }
  } else return_err("DOT_NEEDS_NAME");
  let dv = scope_find(ss, s, d);
  dv->t = TypesTree_c_ast;
  dv->v = VS(.c_ast = res ? res : ast_make_empty(ss));
  return;
}

static fptr value_to_fptr(superscope *ss, typed_value v) {
  let type = ss_type(ss, v.t);
  if (tu_is(r_arr, type.t)) {
    let dec = P$(T(), ($.iscomptime = true, $)).decorations;
    if (dec != type.decorations) return nullFptr;
    let len = tu_catch(r_arr, type.t).count;
    assertMessage(v.v.value || !len, "null pointer with a length");
    return (fptr){len, (u8 *)v.v.value};
  } else if (TypeTree_is_ast(type.t) && v.v.c_ast)
    return tu_match_exp(
        ast_at(ss, v.v.c_ast),
        (astId, id, ast_text(ss, id)),
        (astString, str, ast_text(ss, str)),
        (astNumber, num, ast_text(ss, num)),
        (default, nullFptr),
    );
  return nullFptr;
}
static bool bitstruct_member_index(typeenv e, Type base, fptr name, u32 *idx) {
  let bf = TypeTree_inner_bitfield(base.t, e);
  if (!bf.start) return false;
  foreach (let k, range(0, type_bitf_len(e, bf)))
    if (!fptr_cmp(type_name(e, type_bitf_item(e, bf, k)->name), name)) {
      *idx = (u32)k;
      return true;
    }
  return false;
}
static void lower_typeinfo_fn errs(superscope *ss, scope *s, typeinfo_value *info, fptr name, u32 d) {
  let tenv = ss_typeenv(ss);
  if (!fptr_eq(name, "0") && !fptr_eq(name, "1")) return_err("NO_MEMBER");
  let identity = fptr_eq(name, "0") ? info->subject.v.c_fun.fn : info->subject.v.c_fun.spec;
  *scope_find(ss, s, d) = TV_bigint(ss, BInt.from.i64(ss->alloc, identity));
  return;
}
static void lower_typeinfo_array errs(superscope *ss, scope *s, typeinfo_value *info, fptr name, u32 d) {
  let tenv = ss_typeenv(ss);
  let arr = TypeTree_inner_array(ss_type(ss, info->subject.v.c_type).t, tenv);
  if (!arr.t) return_err("TYPE_HAS_NO_ARRAY");
  if (fptr_eq(name, "len")) {
    *scope_find(ss, s, d) = TV_bigint(ss, BInt.from.i64(ss->alloc, arr.count));
    return;
  }
  if (fptr_eq(name, "elem")) {
    *scope_find(ss, s, d) = Type_asValue(ss, ss_type(ss, arr.t));
    return;
  }
  return_err("NO_MEMBER");
}
static void lower_typeinfo_struct errs(superscope *ss, scope *s, typeinfo_value *info, fptr name, u32 d) {
  let tenv = ss_typeenv(ss);
  if (!fptr_eq(name, "namespace")) return_err("NO_MEMBER");
  let target = ss_type(ss, info->subject.v.c_type);
  scope *owner = nullptr;
  if (tu_is(r_tag, target.t)) {
    let found = moxmap_get(ss->type_info, type_name(tenv, target.t.r_tag.name));
    if (found) owner = found->namespace;
  }
  if (!owner) return_err("TYPE_HAS_NO_NAMESPACE");
  *scope_find(ss, s, d) = TV_with(ss, T(tu_of(c_namespace, {})), VS(.c_namespace = owner));
  return;
}
static void lower_typeinfo_root errs(superscope *ss, scope *s, typeinfo_value *info, fptr name, u32 d) {
  let tenv = ss_typeenv(ss);
  let subject_type = ss_type(ss, info->subject.t);
  subject_type.isconst = false;
  typeinfo_value next = *info;
  if (fptr_eq(name, "fn") && tu_is(c_fun, subject_type.t)) next.view = typeinfo_fn;
  else if (fptr_eq(name, "struct") && tu_is(c_type, subject_type.t) && TypeTree_inner_struct(ss_type(ss, info->subject.v.c_type).t, tenv).start)
    next.view = typeinfo_struct;
  else if (fptr_eq(name, "array") && tu_is(c_type, subject_type.t) && TypeTree_inner_array(ss_type(ss, info->subject.v.c_type).t, tenv).t)
    next.view = typeinfo_array;
  else if (fptr_eq(name, "fn") || fptr_eq(name, "struct") || fptr_eq(name, "array")) {
    *scope_find(ss, s, d) = TV_with(ss, T(tu_of(c_typeinfo, {})), VS(.c_typeinfo = avalue(ss->alloc, ((typeinfo_value){.view = typeinfo_invalid}))));
    return;
  } else return_err("NO_MEMBER");
  *scope_find(ss, s, d) = TV_with(ss, T(tu_of(c_typeinfo, {})), VS(.c_typeinfo = avalue(ss->alloc, next)));
  return;
}
static void lower_typeinfo_member errs(superscope *ss, scope *s, typeinfo_value *info, fptr name, u32 d) {
  if (!info) return_err("NO_MEMBER");
  switch (info->view) {
    case typeinfo_fn:
      return (void)try_err(lower_typeinfo_fn, (ss, s, info, name, d));
    case typeinfo_array:
      return (void)try_err(lower_typeinfo_array, (ss, s, info, name, d));
    case typeinfo_struct:
      return (void)try_err(lower_typeinfo_struct, (ss, s, info, name, d));
    case typeinfo_root:
      return (void)try_err(lower_typeinfo_root, (ss, s, info, name, d));
    default:
      return_err("NO_MEMBER");
  }
}

static void lower_dot_namespace errs(superscope *ss, scope *s, u32 l, fptr name, u32 d) {
  if (!fptr_eq(name, "fn")) return_err("NO_MEMBER");
  scope *owner = scope_find(ss, s, l)->v.c_namespace;
  while (owner && !owner->instance.fn)
    owner = owner->parent;
  if (!owner) return_err("NAMESPACE_HAS_NO_FUNCTION");
  let fn = owner->instance.fn;
  let subject = TV(ss->functions[fn - 1].signature, VS(.c_fun = {.fn = fn, .spec = owner->instance.index}));
  *scope_find(ss, s, d) = TV_with(ss, T(tu_of(c_typeinfo, {})), VS(.c_typeinfo = avalue(ss->alloc, ((typeinfo_value){.subject = subject, .view = typeinfo_fn}))));
  return;
}
static void lower_dot_type errs(superscope *ss, scope *s, u32 l, fptr name, u32 d) {
  let target = try_err(value_asType, (ss, *scope_find(ss, s, l)));
  let m = ss_tag_named(ss, target.t, name);
  if (!m.owner) return_err("NO_MEMBER");
  *scope_find(ss, s, d) = *scope_find(ss, m.owner, m.idx);
  return;
}
static void lower_dot_field errs(superscope *ss, scope *s, u32 l, fptr name, u32 d) {
  let tenv = ss_typeenv(ss);
  let lt = scope_reg_to_type_stripTag(ss, s, l);
  let st = TypeTree_inner_struct(lt.t, tenv);
  u32 idx = 0;
  bool found = false;
  if (st.start) {
    foreach (let k, range(0, type_struct_len(tenv, st)))
      if (!found && !fptr_cmp(type_name(tenv, type_struct_item(tenv, st, k)->name), name)) {
        idx = (u32)k;
        found = true;
      }
  }
  let in_bitfield = !found && bitstruct_member_index(tenv, lt, name, &idx);
  if (!found && !in_bitfield) return_err("NO_MEMBER");
  let member = found ? type_struct_item(tenv, st, idx)->t
                     : type_bitf_item(tenv, TypeTree_inner_bitfield(lt.t, tenv), idx)->t;
  let dt = P$(member, ($.ref_kind = (in_bitfield ? ref_dref : ref_ref), $));
  try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_field, {.l = l, .d = d, .i = idx, .t = ss_type_intern(ss, dt)})));
  return;
}
static void lower_dot_member errs(superscope *ss, scope *s, u32 l, u32 r, u32 d) {
  let tenv = ss_typeenv(ss);
  let lt = scope_reg_to_type_stripTag(ss, s, l);
  if (lt.ref_kind) lt.ref_kind = ref_none;
  let name = value_to_fptr(ss, *scope_find(ss, s, r));
  if (fptr_isNull(name)) return_err("DOT_NEEDS_NAME");

  if (tu_is(c_typeinfo, lt.t))
    return (void)try_err(lower_typeinfo_member, (ss, s, scope_find(ss, s, l)->v.c_typeinfo, name, d));

  if (tu_is(c_namespace, lt.t)) {
    return (void)try_err(lower_dot_namespace, (ss, s, l, name, d));
  }

  if (tu_is(c_type, lt.t)) {
    return (void)try_err(lower_dot_type, (ss, s, l, name, d));
  }
  if (TypeTree_is_ast(lt.t)) return (void)try_err(lower_ast_member, (ss, s, l, r, d));

  if (lt.ptr_level <= 1) {
    let m = ss_tag_named(ss, scope_reg_type(ss, s, l).t, name);
    if (m.owner) {
      *scope_find(ss, s, d) = *scope_find(ss, m.owner, m.idx);
      return;
    }
  }
  try_err(lower_dot_field, (ss, s, l, name, d));
}

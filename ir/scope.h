#pragma once
#include "../type.h"
#include "common.h"
#include "ir.h"
#include "wheels/macros.h"
#include "wheels/oxmap.h"
#include "wheels/print/print_pre.h"
#include <assert.h>
#include <wheels/allocators/cballocator.h>
#include <wheels/sList.h>
#include <wheels/smap.h>
sliceDef(u32);
static struct {
  char buf[1024];
  usize len;
} errors_buffer = {};
void errors_buffer_printer(const char *c, void *v, usize len, bool flush) {
  for (int i = 0; i < len && errors_buffer.len < countof(errors_buffer.buf) - 1; i++)
    errors_buffer.buf[errors_buffer.len++] = c[i];
  if (flush)
    errors_buffer.buf[errors_buffer.len] = 0,
    errors_buffer.len = 0;
}
char *print_errsbuffer(void *) { return (char *)errors_buffer.buf; }
#define print_ebuff(fmt, ...) ({                               \
  print_wfO(errors_buffer_printer, nullptr, fmt, __VA_ARGS__); \
  print_errsbuffer;                                            \
})

scope *scope_owner(scope *s, u32 idx) {
  while (s)
    if (idx > s->start && idx <= s->start + msList_len(s->values))
      return s;
    else s = s->parent;
  return nullptr;
}
typed_value *scope_find(superscope *env, scope *s, u32 idx) {
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
          .start = s ? s->start + msList_len(s->values) : 0,
          .rootable = s ? s->rootable : true,
      })
  );
  scope_add_value(b, res, TV_void(b));
  return res;
}
static inline void scope_push_named(superscope *b, scope *s) {
  mList_push(s->named, moxmap_init(b->alloc, fptr, u32, (oxmap_cmp){cmpfptr}));
}
static inline void scope_pop_named(scope *s) {
  if (mList_len(s->named) > 1) moxmap_deinit(mList_pop(s->named));
}
u32 scope_last_item(superscope *, scope *s) { return s->start + msList_len(s->values); }

scope *scope_find_rootable(superscope *b, scope *s) {
  while (!s->rootable)
    s = s->parent;
  assertMessage(s);
  return s;
}
void scope_destroy(superscope *b, scope *s) {
  foreach (let item, span(s->values, msList_len(s->values))) {
    let it = ss_type(b, item->t);
    if (tu_is(c_int, it.t) && !it.isref)
      msList_deInit(b->alloc, item->v.c_int);
  }
  msList_deInit(b->alloc, s->values);
  msList_deInit(b->alloc, s->instructions);
  foreach (let m, mList_iter(s->named))
    moxmap_deinit(m);
  mList_deinit(s->named);
  adestroy(b->alloc, s);
}
static inline void scope_resize_value(superscope *env, typed_value *v) {
  if (v->v.value) return;
  if_decl (let sz, Type_sizeof(ss_type(env, v->t), ss_typeenv(env)))
    v->v.value = acreate(env->alloc, u8[sz]);
}
static inline void scope_set_type(superscope *env, typed_value *v, typeidx t) {
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
static inline Type Type_ptr errs(Type t) {
  if (t.isref)
    return_err("POINTER_TO_REFERENCE", T({}));
  if (Type_is_comptime(P$(t, ($.iscomptime = false, $))))
    return_err("POINTER_TO_COMPTIME_ONLY", T({}));
  t.ptr_level++;
  return t;
}
static inline Type Type_arr errs(superscope *ss, Type t, u32 len) {
  if (t.isref)
    return_err("ARRAY_OF_REFERENCE", T({}));
  if (Type_is_comptime(P$(t, ($.iscomptime = false, $))))
    return_err("ARRAY_OF_COMPTIME_ONLY", T({}));
  return T(tu_of(r_arr, {ss_type_intern(ss, t), len}));
}
static inline Type Type_ref errs(Type t) {
  if (t.isref)
    return_err("REFERENCE_TO_REFERENCE", T({}));
  if (Type_is_comptime(P$(t, ($.iscomptime = false, $))))
    return_err("REFERENCE_TO_COMPTIME_ONLY", T({}));
  t.isref = true;
  return t;
}
struct type_named {
  scope *owner;
  u32 idx;
};
static inline struct type_named ss_tag_named(superscope *ss, TypeTree t, fptr name) {
  iflet_tu (let tag, r_tag, t)
    if_decl (let info, moxmap_get(ss->type_info, type_name(ss_typeenv(ss), tag.name)))
      if_decl (let idx, moxmap_get(mList_last((*info)->named), name))
        return (struct type_named){*info, *idx};
  return (struct type_named){};
}
static inline void scope_emit_inst errs(superscope *env, scope *s, ir i) { msList_push(env->alloc, s->instructions, i); }
static inline void scope_wrap_child errs(superscope *env, scope *s, scope *b) {
  assertMessage(b != s && b->parent == s, "b not inside s?");
  assertMessage(b->start == s->start + msList_len(s->values));
  try_err(scope_emit_inst, (env, s, (ir)tu_of(ir_blocks, {})));
  foreach (let inst, span(b->instructions, msList_len(b->instructions)))
    try_err(scope_emit_inst, (env, s, *inst));
  foreach (let v, span(b->values, msList_len(b->values)))
    msList_push(env->alloc, s->values, *v);
  try_err(scope_emit_inst, (env, s, (ir)tu_of(ir_blocke, {})));
  msList_deInit(env->alloc, b->instructions);
  msList_deInit(env->alloc, b->values);
  foreach (let m, mList_iter(b->named))
    moxmap_deinit(m);
  mList_deinit(b->named);
  adestroy(env->alloc, b);
}
static bool Type_ncmp_nptr(Type a, Type b, typeenv e) {
  return a.ptr_level == b.ptr_level && !TypeTree_cmp(e, a.t, b.t);
}
static inline cmpres Type_cmp_ndecor(Type a, Type b, typeenv e) {
  let x = ~((Type){{}, {.isconst = true, .isvolatile = true, .iscomptime = true}}.decorations);
  return Type_cmp(
      e,
      P$(a, ($.decorations &= x, $)),
      P$(b, ($.decorations &= x, $))
  );
}
static inline bool Type_if_deref(Type t, Type *out) {
  if (t.isref)
    return *out = P$(t, ($.isref = false, $)), true;
  if (t.ptr_level)
    return *out = P$(t, ($.ptr_level--, $)), true;
  return false;
}
static inline Type Type_stripTag(Type t, typeenv e) {
  while (tu_is(r_tag, t.t))
    t.t = type_tree(e, tu_catch(r_tag, t.t).t);
  return t;
}
static inline u32 Type_intLike_bitcount(Type t, typeenv e) {
  t = Type_stripTag(t, e);
  return tu_is(r_bool, t.t)   ? 1
         : tu_is(r_sint, t.t) ? tu_catch(r_sint, t.t).bits
         : tu_is(r_uint, t.t) ? tu_catch(r_uint, t.t).bits
                              : 0;
}
static inline Type Type_weaker(Type a, Type b, typeenv e) {
  Type as = Type_stripTag(a, e), bs = Type_stripTag(b, e);
  bool ac = Type_is_comptime(as), bc = Type_is_comptime(bs);
  Type pick;
  if (ac != bc) pick = ac ? bs : as;
  else if (ac) pick = as;
  else if (Type_intLike_bitcount(as, e) != Type_intLike_bitcount(bs, e))
    pick = Type_intLike_bitcount(as, e) > Type_intLike_bitcount(bs, e) ? as : bs;
  else pick = tu_is(r_uint, as.t) ? as : bs;
  pick.isref = false;
  pick.iscomptime = false;
  pick.isconst = a.isconst && b.isconst;
  pick.isvolatile = a.isvolatile && b.isvolatile;
  return pick;
}
static bool Type_is_integer_like(Type t, typeenv e) {
  t = Type_stripTag(t, e);
  return !t.ptr_level && (tu_is(r_bool, t.t) || Type_isint(t, e));
}
static inline void scope_add_inst errs(superscope *env, scope *s, ir i);
static inline ir *scope_add_inst_typecheck errs(superscope *env, scope *s, ir *i);

static bool cast_would_change(Type from, Type to, typeenv e) {
  return !(
      !Type_cmp(e, from, to) ||
      !Type_cmp(e, Type_stripTag(from, e), Type_stripTag(to, e)) ||
      !Type_cmp(e, to, T(tu_of(c_any, {})))
  );
}
static u32 cast_value errs(superscope *ss, scope *s, u32 left, Type to) {
  let e = ss_typeenv(ss);
  let ot = ss_type(ss, scope_find(ss, s, left)->t);
  if (!cast_would_change(ot, to, e)) return left;
  let d = scope_add_value(ss, s, TV_of(ss, to));
  try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_cast, {.l = left, .d = d})), 0);
  return d;
}
static inline ir *scope_add_inst_typecheck errs(superscope *env, scope *s, ir *i) {
  let tenv = ss_typeenv(env);
  tu_match_void(
      *i,
      (ir_farg, _),
      (ir_blocks, _),
      (ir_blocke, _),
      (ir_ifs, _),
      (ir_ifd, _),
      (ir_ife, _),
      (ir_loops, _),
      (ir_loope, _),
      (ir_break, _),
      (ir_cont, _),
      (ir_putptr, _, {
        let pp = &tu_catch(ir_putptr, *i);
        Type lt = ss_type(env, scope_find(env, s, pp->l)->t);
        scope *b = s;
        if (lt.isref) {
          b = scope_inferior(env, s);
          lt.isref = false;
          u32 read = scope_add_value(env, b, TV_of(env, lt));
          try_err(scope_add_inst, (env, b, (ir)tu_of(ir_set, {.l = pp->l, .d = read})), 0);
          pp->l = read;
        }
        Type pointee = {};
        if (!Type_if_deref(lt, &pointee)) return_err("AT_NOT_REF", nullptr);
        Type asref = pointee;
        asref.isref = true;
        let dt = ss_type(env, scope_find(env, s, pp->d)->t);
        if (tu_is(c_any, dt.t)) scope_set_type(env, scope_find(env, s, pp->d), ss_type_intern(env, asref));
        else if (Type_cmp(tenv, pointee, dt) && Type_cmp(tenv, asref, dt))
          return_err_extras(
              "TYPE_ERROR",
              print_ebuff("{Type_full} not ptr to {Type_full}", ss_typeF(env, lt), ss_typeF(env, dt)),
              0,
              0
          );
        if (b != s) {
          try_err(scope_emit_inst, (env, b, *i), nullptr);
          return try_err(scope_wrap_child, (env, s, b), nullptr), nullptr;
        }
        return i;
      }),
      (ir_getptr, gp, {
        let lt = ss_type(env, scope_find(env, s, gp.l)->t);
        let dt = ss_type(env, scope_find(env, s, gp.d)->t);
        Type_if_deref(lt, &lt);
        Type asp = lt;
        asp.ptr_level++;
        Type asr = lt;
        asr.isref = true;
        if (tu_is(c_any, dt.t)) {
          scope_set_type(env, scope_find(env, s, gp.d), ss_type_intern(env, try_err(Type_ptr, (lt), 0)));
          return i;
        }
        if (Type_cmp(tenv, asp, dt) && Type_cmp(tenv, asr, dt))
          return_err_extras(
              "TYPE_ERROR",
              print_ebuff("{Type_full} not ptr to {Type_full}", ss_typeF(env, dt), ss_typeF(env, lt)),
              0,
              0
          );
        return i;
      }),
      (ir_getref, pp, {
        let lt = ss_type(env, scope_find(env, s, pp.l)->t);
        let dt = ss_type(env, scope_find(env, s, pp.d)->t);
        Type referred = lt;
        if (!lt.isref) referred = try_err(Type_ref, (lt), 0);
        if (tu_is(c_any, dt.t)) {
          scope_set_type(env, scope_find(env, s, pp.d), ss_type_intern(env, referred));
          if (lt.isref) *i = (ir)tu_of(ir_putptr, {.l = pp.l, .d = pp.d});
          return i;
        }
        if (Type_cmp(tenv, referred, dt))
          return_err_extras(
              "TYPE_ERROR",
              print_ebuff("{Type_full} not ref to {Type_full}", ss_typeF(env, dt), ss_typeF(env, lt)),
              0,
              0
          );
        return i;
      }),
      (ir_rawop, _, {
        let c = &tu_catch(ir_rawop, *i);
        Type lt = ss_type(env, scope_find(env, s, c->r)->t);

        char op_str[sizeof(u32) + 1] = {};
        memcpy(op_str, &c->o, 4);

        if (lt.ptr_level <= 1) {
          let m = ss_tag_named(env, lt.t, fp((char *)op_str));
          if (m.owner) {
            scope *b = scope_inferior(env, s);
            let method = scope_add_value(env, b, *scope_find(env, m.owner, m.idx));
            u32 res = try_err(lower_function_call_values, (env, b, method, ((slice(u32)){2, (u32[2]){c->r, c->l}})), 0);
            scope_set_type(env, scope_find(env, s, c->d), scope_find(env, b, res)->t);
            try_err(scope_emit_inst, (env, b, (ir)tu_of(ir_set, {.l = res, .d = c->d})), 0);
            return try_err(scope_wrap_child, (env, s, b), nullptr), nullptr;
          }
        }

        let e = ss_typeenv(env);
        Type rt = ss_type(env, scope_find(env, s, c->l)->t);

        if (c->o == opNumbers.call) return_err("UNSUPPORTED_CALL", nullptr);

        if (strcmp(op_str, "as") == 0) {
          let to = try_err(value_asType, (env, *scope_find(env, s, c->l)), nullptr);
          scope *b = cast_would_change(lt, to, tenv) ? scope_inferior(env, s) : s;
          u32 casted = try_err(cast_value, (env, b, c->r, to), 0);
          scope_set_type(env, scope_find(env, s, c->d), ss_type_intern(env, to));
          *i = (ir)tu_of(ir_set, {.l = casted, .d = c->d});
          if (b != s) {
            try_err(scope_emit_inst, (env, b, *i), nullptr);
            return try_err(scope_wrap_child, (env, s, b), nullptr), nullptr;
          }
          return i;
        }

        if ((c->o == opNumbers.eq || c->o == opNumbers.neq) && tu_is(c_type, lt.t) && tu_is(c_type, rt.t)) {
          bool res = scope_find(env, s, c->r)->v.c_type == scope_find(env, s, c->l)->v.c_type;
          if (c->o == opNumbers.neq) res = !res;
          let d = scope_find(env, s, c->d);
          d->t = ss_type_intern(env, T(tu_of(r_bool, {})));
          d->v = VS(avalue(env->alloc, res));
          return nullptr;
        }

        if (!Type_is_integer_like(lt, e) || !Type_is_integer_like(rt, e))
          return_err("OP_UNSUPPORTED_OPERANDS", nullptr);

        bool is_bool = c->o == opNumbers.mor || c->o == opNumbers.les || c->o == opNumbers.moq ||
                       c->o == opNumbers.leq || c->o == opNumbers.eq || c->o == opNumbers.neq ||
                       c->o == opNumbers.oand || c->o == opNumbers.oor;
        if (is_bool)
          scope_set_type(env, scope_find(env, s, c->d), ss_type_intern(env, T(tu_of(r_bool, {}))));
        else {
          Type res = Type_weaker(lt, rt, e);
          scope_set_type(env, scope_find(env, s, c->d), ss_type_intern(env, res));
          scope *b = (cast_would_change(lt, res, e) || cast_would_change(rt, res, e)) ? scope_inferior(env, s) : s;
          c->l = try_err(cast_value, (env, b, c->l, res), 0);
          c->r = try_err(cast_value, (env, b, c->r, res), 0);
          if (b != s) {
            try_err(scope_emit_inst, (env, b, *i), nullptr);
            return try_err(scope_wrap_child, (env, s, b), nullptr), nullptr;
          }
        }
      }),
      (ir_set, _, {
        let c = &tu_catch(ir_set, *i);
        Type dt = ss_type(env, scope_find(env, s, c->d)->t);
        Type lt = ss_type(env, scope_find(env, s, c->l)->t);
        if (lt.isref && dt.isref && !Type_cmp(ss_typeenv(env), lt, dt)) {
          *i = (ir)tu_of(ir_putptr, {.l = c->l, .d = c->d});
          return i;
        }
        dt.isref = false;
        c->l = try_err(cast_value, (env, s, c->l, dt), 0);
      }),
      (ir_cast, _, {
        let c = &tu_catch(ir_cast, *i);
        Type lt = ss_type(env, scope_find(env, s, c->l)->t);
        Type dt = ss_type(env, scope_find(env, s, c->d)->t);
        dt.isref = false;
        let e = ss_typeenv(env);
        if (Type_ncmp_nptr(lt, dt, e)) *i = (ir)tu_of(ir_set, {.l = c->l, .d = c->d});
        else if (!(Type_is_integer_like(lt, e) && Type_is_integer_like(dt, e))) return_err("CANNOT_CAST", nullptr);
      }),
      (ir_ret, _, {
        if (env->current_ret) {
          let c = &tu_catch(ir_ret, *i);
          if (tu_is(c_any, env->current_ret->t)) {
            Type vt = ss_type(env, scope_find(env, s, *c)->t);
            vt.isref = false;
            *env->current_ret = vt;
          } else *c = try_err(cast_value, (env, s, *c, *env->current_ret), 0);
        }
      }),
      (ir_call, _, {
        let c = &tu_catch(ir_call, *i);
        u32 callee = c->l;
        if (!callee) return_err("UNSUPPORTED_CALL", nullptr);
        Type lt = ss_type(env, scope_find(env, s, callee)->t);
        struct type_named m = {};
        if (lt.ptr_level <= 1) m = ss_tag_named(env, lt.t, fp("()"));
        if (!m.owner) return_err("UNSUPPORTED_CALL", nullptr);
        scope *b = scope_inferior(env, s);
        let method = scope_add_value(env, b, *scope_find(env, m.owner, m.idx));
        usize nargs = c->e > c->r ? (usize)(c->e - c->r) : 0;
        slice(u32) vals = slice_alloc(env->alloc, u32, nargs + 1);
        vals.ptr[0] = callee;
        foreach (let j, range(0, nargs))
          vals.ptr[j + 1] = c->r + (u32)j;
        u32 res = try_err(lower_function_call_values, (env, b, method, vals), 0);
        slice_free(env->alloc, vals);
        scope_set_type(env, scope_find(env, s, c->d), scope_find(env, b, res)->t);
        try_err(scope_emit_inst, (env, b, (ir)tu_of(ir_set, {.l = res, .d = c->d})), 0);
        return try_err(scope_wrap_child, (env, s, b), nullptr), nullptr;
      }),
      (ir_array, _, {
        let pp = &tu_catch(ir_array, *i);
        let lt = ss_type(env, scope_find(env, s, pp->l)->t);
        let dt = ss_type(env, scope_find(env, s, pp->d)->t);
        let rt = ss_type(env, scope_find(env, s, pp->r)->t);
        let e = ss_typeenv(env);
        if (!Type_isint(Type_stripTag(rt, e), e)) return_err("ARRAY_INDEX_NON_INT", nullptr);
        Type arr = lt;
        Type_if_deref(lt, &arr);
        arr = Type_stripTag(arr, e);
        let inner = TypeTree_inner_array(arr.t, e);
        if (!inner.t) return_err("ARRAY_OF_NON_ARRAY", nullptr);
        Type elem = type_at(e, inner.t);
        if (tu_is(c_any, dt.t))
          scope_set_type(env, scope_find(env, s, pp->d), ss_type_intern(env, elem));
        else if (Type_cmp(e, elem, dt))
          return_err_extras(
              "TYPE_ERROR",
              print_ebuff("{Type_full} not the element {Type_full}", ss_typeF(env, dt), ss_typeF(env, elem)),
              0,
              0
          );
      }),
      (ir_field, _, {
        let pp = &tu_catch(ir_field, *i);
        let lt = ss_type(env, scope_find(env, s, pp->l)->t);
        let dt = ss_type(env, scope_find(env, s, pp->d)->t);
        let e = ss_typeenv(env);
        Type strct = lt;
        Type_if_deref(lt, &strct);
        strct = Type_stripTag(strct, e);
        let st = TypeTree_inner_struct(strct.t, e);
        let bf = TypeTree_inner_bitfield(strct.t, e);
        Type member = {};
        if (st.start && pp->i < type_struct_len(e, st)) member = type_struct_item(e, st, pp->i)->t;
        else if (bf.start && pp->i < type_bitf_len(e, bf)) member = type_bitf_item(e, bf, pp->i)->t;
        else return_err("NO_SUCH_FIELD", nullptr);
        if (Type_is_comptime(member)) return_err("NO_STORAGE", nullptr);
        if (tu_is(c_any, dt.t))
          scope_set_type(env, scope_find(env, s, pp->d), ss_type_intern(env, member));
        else if (Type_cmp(e, member, dt))
          return_err_extras(
              "TYPE_ERROR",
              print_ebuff("{Type_full} not the member {Type_full}", ss_typeF(env, dt), ss_typeF(env, member)),
              0,
              0
          );
      }),
  );
  return i;
}
static inline void scope_add_inst errs(superscope *env, scope *s, ir i) {
  if (tu_is(ir_blocks, i)) scope_push_named(env, s);
  else if (tu_is(ir_blocke, i)) scope_pop_named(s);
  ir *hooked = try_err(scope_add_inst_typecheck, (env, s, &i));
  if (hooked) try_err(scope_emit_inst, (env, s, *hooked));
}
static inline void scope_bind errs(superscope *env, scope *s, u32 idx, fptr name) {
  if (moxmap_get(mList_last(s->named), name)) return_err("SHADOWING_SAME_SCOPE");
  assertMessage(!moxmap_get(mList_last(s->named), name));
  assertMessage(idx <= msList_len(s->values) + s->start, "%zu <= %zu + %zu", idx, msList_len(s->values), s->start);
  moxmap_set(mList_last(s->named), name, idx);
}
static inline void scope_yield errs(superscope *ss, scope *s, u32 result) {
  assertMessage(result <= s->start);
  let v = scope_find(ss, s, result);
  assertMessage(tu_is(c_any, ss_type(ss, v->t).t));
  Type yielded = ss_type(ss, msList_last(s->values).t);
  yielded.isref = false;
  scope_set_type(ss, v, ss_type_intern(ss, yielded));
  try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_set, {.l = s->start + msList_len(s->values), .d = result})));
}

static inline u32 ss_get_named_failable_noref(superscope *env, scope *s, fptr n) {
  let limit = s->start + msList_len(s->values);
  while (s) {
    let maps = mList_len(s->named);
    foreach (let i, range(0, maps)) {
      let map = mList_arr(s->named)[maps - 1 - i];
      if_decl (let val, moxmap_get(map, n))
        if (*val <= limit) return *val;
    }
    limit = s->start;
    s = s->parent;
  }
  return 0;
}
static inline u32 ss_get_named errs(superscope *env, scope *s, fptr n) {
  let val = ss_get_named_failable_noref(env, s, n);
  if (!val) return_err("UNBOUND_ID", 0);
  let vo = scope_find(env, s, val);
  let vt = ss_type(env, vo->t);
  if (vt.isref) return val;
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
  }));
  // clang-format on
  res->typeindex = moxmap_init(allocator, Type, typeidx, (oxmap_cmp){Type_cmp_hash_ss, res});
  res->type_info = moxmap_init(allocator, fptr, scope *, (oxmap_cmp){fptr_cmp_hash, res});
  res->t_void = ss_type_intern(res, T(tu_of(r_void, {})));
  res->t_any = ss_type_intern(res, T(tu_of(c_any, {})));
  res->t_int = ss_type_intern(res, T(tu_of(c_int, {})));
  res->t_ast = ss_type_intern(res, T(tu_of(c_ast, {})));
  res->t_type = ss_type_intern(res, T(tu_of(c_type, {})));
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
      {"@ast_op", builtin_ast_op},
      {"@apply", builtin_apply},
      {"@type", builtin_type},
      {"@ast", builtin_ast},
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
  return res;
}

static inline void ss_deInit(superscope *ss) {
  let allocator = ((struct cballocator *)ss->alloc)->allocator;
  msList_deInit(allocator, ss->thisstack);
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

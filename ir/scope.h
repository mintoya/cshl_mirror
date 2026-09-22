#pragma once
#include "common.h"
#include "wheels/macros.h"
#include "wheels/print/print_pre.h"
#include <wheels/allocators/cballocator.h>
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

scope *scope_owner(const scope *s, u32 idx) {
  while (s)
    if (idx > s->start && idx <= s->start + msList_len(s->values))
      return s;
    else s = s->parent;
  return nullptr;
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
  if (mList_len(s->named) > 1) moxmap_deinit(mList_pop(s->named));
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
//
//
//

static inline Type Type_ptr errs(Type t) {
  if (t.isref) return_err("POINTER_TO_REFERENCE", T());
  if (Type_is_comptime(P$(t, ($.iscomptime = false, $))))
    return_err("POINTER_TO_COMPTIME_ONLY", T());
  t.ptr_level++;
  return t;
}
static inline Type Type_arr errs(superscope *ss, Type t, u32 len) {
  if (t.isref)
    return_err("ARRAY_OF_REFERENCE", T());
  if (Type_is_comptime(P$(t, ($.iscomptime = false, $))))
    return_err("ARRAY_OF_COMPTIME_ONLY", T());
  return T(tu_of(r_arr, {ss_type_intern(ss, t), len}));
}
static inline Type Type_ref errs(Type t) {
  if (t.isref) return_err("REFERENCE_TO_REFERENCE", T());
  if (Type_is_comptime(P$(t, ($.iscomptime = false, $))))
    return_err("REFERENCE_TO_COMPTIME_ONLY", T());
  return P$(t, ($.isref = true, $));
}
static inline Type value_asType errs(superscope *ss, typed_value t) {
  if (!tu_is(c_type, ss_type(ss, t.t).t))
    return_err("TYPE_IS_NOT_VALUE", T());
  if (!t.v.c_type) return_err("UNKNOWN_TYPE", T());
  return type_at(ss_typeenv(ss), t.v.c_type);
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
static inline void scope_emit_inst errs(superscope *env, scope *s, ir i) {
#if !defined NDEBUG
  tu_match_void(
      i,
      (ir_putptr, pp, {
        let lt = ss_type(env, scope_find(env, s, pp.l)->t);
        let dt = ss_type(env, scope_find(env, s, pp.d)->t);
        lt.isref = false;
        dt.isref = false;
        if (!lt.ptr_level) {
          println("[dbg:1]{Type_full}->{Type_full}", ss_typeF(env, ss_type(env, scope_find(env, s, pp.l)->t)), ss_typeF(env, ss_type(env, scope_find(env, s, pp.d)->t)));
          assertMessage(false);
        }
        lt.ptr_level--;
        if (Type_cmp_ndecor(lt, dt, ss_typeenv(env))) {
          println("[dbg:2]{Type_full}->{Type_full}", ss_typeF(env, ss_type(env, scope_find(env, s, pp.l)->t)), ss_typeF(env, ss_type(env, scope_find(env, s, pp.d)->t)));
          assertMessage(false);
        }
      }),
      (ir_getptr, pp, {
        let lt = ss_type(env, scope_find(env, s, pp.l)->t);
        let dt = ss_type(env, scope_find(env, s, pp.d)->t);
        lt.isref = false;
        lt.ptr_level++;
        if (dt.isref) {
          dt.isref = false;
          dt.ptr_level++;
        }
        if (Type_cmp_ndecor(lt, dt, ss_typeenv(env))) {
          println("[dbg:3]{Type_full}->{Type_full}", ss_typeF(env, ss_type(env, scope_find(env, s, pp.l)->t)), ss_typeF(env, ss_type(env, scope_find(env, s, pp.d)->t)));
          assertMessage(false);
        }
      }),
      (default, {}),
  );
#endif
  msList_push(env->alloc, s->instructions, i);
}

static bool Type_ncmp_nptr(Type a, Type b, typeenv e) { return a.ptr_level == b.ptr_level && !TypeTree_cmp(e, a.t, b.t); }
static inline bool Type_isnull(Type t) { return tu_is(r_null, t.t); }
static inline void scope_add_inst errs(superscope *env, scope *s, ir i);
static inline void scope_add_inst_typecheck errs(superscope *env, scope *s, ir i);

static inline void scope_add_inst errs(superscope *env, scope *s, ir i) {
  if (tu_is(ir_blocks, i)) scope_push_named(env, s);
  else if (tu_is(ir_blocke, i)) scope_pop_named(s);
  try_err(scope_add_inst_typecheck, (env, s, i));
}
static inline void scope_bind errs(superscope *env, scope *s, u32 idx, fptr name) {
  if (moxmap_get(mList_last(s->named), name)) return_err("SHADOWING_SAME_SCOPE");
  assertMessage(!moxmap_get(mList_last(s->named), name));
  assertMessage(idx <= msList_len(s->values) + s->start, "%zu <= %zu + %zu", idx, msList_len(s->values), s->start);
  moxmap_set(mList_last(s->named), name, idx);
}

static u32 ss_get_named_failable_noref(superscope *env, scope *s, fptr n) {
  let limit = s->start + msList_len(s->values);
  bool crossed_function = false;
  while (s) {
    let maps = mList_len(s->named);
    foreach (let i, range(0, maps)) {
      let map = mList_arr(s->named)[maps - 1 - i];
      if_decl (let val, moxmap_get(map, n))
        if (*val <= limit) {
          let t = ss_type(env, scope_find(env, s, *val)->t);
          if (crossed_function && s->instance.fn && !Type_is_comptime(t))
            return 0;
          return *val;
        }
    }
    limit = s->start;
    crossed_function |= !!s->instance.fn;
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
  res->t_bool = ss_type_intern(res, T(tu_of(r_bool, {})));
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
      {"@union", builtin_union},
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

#include "coerce.h"

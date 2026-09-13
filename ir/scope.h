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
          .named = moxmap_init(b->alloc, fptr, u32, (oxmap_cmp){cmpfptr}),
          .start = s ? s->start + msList_len(s->values) : 0,
          .rootable = s ? s->rootable : true,
      })
  );
  scope_add_value(b, res, TV_void(b));
  return res;
}

scope *scope_find_rootable(superscope *b, scope *s) {
  while (!s->rootable)
    s = s->parent;
  assertMessage(s);
  return s;
}
static scope *scope_copy_deep(superscope *ss, scope *s, scope *parent) {
  if (!s) return nullptr;
  scope *res = avalue(
      ss->alloc,
      ((scope){

          .parent = parent,
          .instructions = msList_initArr(ss->alloc, *msList_vla(s->instructions)),
          .values = msList_initArr(ss->alloc, *msList_vla(s->values)),
          .named = (moxmap(fptr, u32))oxmap_copy(ss->alloc, (oxmap *)s->named),
          .start = s->start,
          .rootable = s->rootable,
      })
  );
  foreach (let v, span(res->values, msList_len(res->values))) {
    let vt = ss_type(ss, v->t);
    if (tu_is(c_int, vt.t) && !vt.isref && v->v.c_int)
      v->v.c_int = BInt.copy(ss->alloc, v->v.c_int);
  }
  let rec = scope_copy_deep;
  foreach (let i, span(res->instructions, msList_len(res->instructions))) {
    *i = tu_match_exp(
        *i,
        (ir_block, b, (ir)tu_of(ir_block, rec(ss, b, res))),
        (ir_if, f, (ir)tu_of(ir_if, {.cnd = rec(ss, f.cnd, res), .t = rec(ss, f.t, res), .f = rec(ss, f.f, res)})),
        (ir_while, w, (ir)tu_of(ir_while, {.cnd = rec(ss, w.cnd, res), .body = rec(ss, w.body, res)})),
        (default, *i)
    );
  }
  return res;
}
scope *scope_copy(superscope *ss, scope *s) { return scope_copy_deep(ss, s, s->parent); }
void scope_destroy(superscope *b, scope *s) {
  foreach (let item, span(s->values, msList_len(s->values))) { // -1?
    let it = ss_type(b, item->t);
    if (tu_is(c_int, it.t) && !it.isref)
      msList_deInit(b->alloc, item->v.c_int);
  }
  msList_deInit(b->alloc, s->values);
  foreach (let scope, span(s->instructions, msList_len(s->instructions)))
    iflet_tu (let s, ir_block, *scope)
      scope_destroy(b, s);
  msList_deInit(b->alloc, s->instructions);
  moxmap_deinit(s->named);
  adestroy(b->alloc, s);
}
u32 scope_add_value(superscope *env, scope *s, typed_value v) {
  if (tu_is(c_int, ss_type(env, v.t).t) && v.v.c_int) // explicitly destroyed in scope_destroy
    v.v.c_int = BInt.copy(env->alloc, v.v.c_int);
  msList_push(env->alloc, s->values, v);
  return msList_len(s->values) + s->start;
}
struct type_named {
  scope *owner;
  u32 idx;
};
static inline struct type_named ss_tag_named(superscope *ss, TypeTree t, fptr name) {
  iflet_tu (let tag, r_tag, t)
    if_decl (let info, moxmap_get(ss->type_info, type_name(ss_typeenv(ss), tag.name)))
      if_decl (let idx, moxmap_get((*info)->named, name))
        return (struct type_named){*info, *idx};
  return (struct type_named){};
}
static inline void scope_emit_inst errs(superscope *env, scope *s, ir i) {
  if (!try_err(try_interpret, (env, s, i))) msList_push(env->alloc, s->instructions, i);
}
static bool Type_similar(Type a, Type b, typeenv e) {
  return a.ptr_level == b.ptr_level && !TypeTree_cmp(e, a.t, b.t);
}
// a referance is one level of indirection kept in its own bit, so the level of a type is
// ptr_level + isref. dropping one level is what &(*x) and *x are built out of
static inline bool Type_deref(Type t, Type *out) {
  if (t.isref) {
    t.isref = false;
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
static bool Type_is_integer_like(Type t, typeenv e) {
  return !t.ptr_level && (tu_is(r_bool, t.t) || Type_isint(t, e));
}
static inline void scope_add_inst errs(superscope *env, scope *s, ir i);
static inline ir *scope_add_inst_typecheck errs(superscope *env, scope *s, ir *i);

static u32 cast_value errs(superscope *ss, scope *s, u32 left, Type to) {
  let e = ss_typeenv(ss);
  let ot = ss_type(ss, scope_find(ss, s, left)->t);
  if (!Type_cmp(e, ot, to)) return left;
  if (!Type_cmp(e, to, T(tu_of(c_any, {})))) return left;
  let d = scope_add_value(ss, s, TV_of(ss, to));
  try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_cast, {.l = left, .d = d})), 0);
  return d;
}
static inline ir *scope_add_inst_typecheck errs(superscope *env, scope *s, ir *i) {
  let tenv = ss_typeenv(env);
  tu_match_void(
      *i,
      (ir_putptr, _, {
        let pp = &tu_catch(ir_putptr, *i);
        let lt = ss_type(env, scope_find(env, s, pp->l)->t);
        let dt = ss_type(env, scope_find(env, s, pp->d)->t);
        // d = *l, so l has to be a pointer with something to point at. a referance is not a
        // pointer here: *ref unfolds into the referent, so this would be *(*x), not a deref.
        // d is the thing itself, and since it is mutable it may be spelled as a referance to
        // it or as a plain value holding it
        Type pointee = {};
        Type asref;
        bool matches = !lt.isref && Type_deref(lt, &pointee);
        if (matches) {
          asref = pointee;
          asref.isref = true;
          matches = !Type_cmp(tenv, pointee, dt) || !Type_cmp(tenv, asref, dt);
        }
        if (!matches)
          return_err_extras(
              "TYPE_ERROR",
              print_ebuff("{Type_full} not ptr to {Type_full}", ss_typeF(env, lt), ss_typeF(env, dt)),
              0,
              0
          );
        return i;
      }),
      (ir_getptr, _, {
        let pp = &tu_catch(ir_getptr, *i);
        let lt = ss_type(env, scope_find(env, s, pp->l)->t);
        let dt = ss_type(env, scope_find(env, s, pp->d)->t);
        // d = &(*l): the address of whatever l denotes. a plain value is addressed as itself,
        // a referance or pointer is addressed at what it denotes, and the address may be
        // spelled either as a pointer or as a referance, because they are the same address
        Type obj = lt;
        Type_deref(lt, &obj);
        Type asp = obj;
        asp.ptr_level++;
        Type asr = obj;
        asr.isref = true;
        if (Type_cmp(tenv, asp, dt) && Type_cmp(tenv, asr, dt))
          return_err_extras(
              "TYPE_ERROR",
              print_ebuff("{Type_full} not ptr to {Type_full}", ss_typeF(env, dt), ss_typeF(env, lt)),
              0,
              0
          );
        return i;
      }),
      (ir_getref, _, {
        let pp = &tu_catch(ir_getref, *i);
        let lt = ss_type(env, scope_find(env, s, pp->l)->t);
        let dt = ss_type(env, scope_find(env, s, pp->d)->t);
        if (Type_cmp(tenv, P$(lt, ($.isref = true, $)), dt))
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
        if (lt.ptr_level <= 1) {
          char op_str[sizeof(u32) + 1] = {};
          memcpy(op_str, &c->o, 4);
          let m = ss_tag_named(env, lt.t, fp((char *)op_str));
          if (m.owner) {
            let method = scope_add_value(env, s, *scope_find(env, m.owner, m.idx));
            u32 vals[2] = {c->r, c->l};
            u32 res = try_err(lower_function_call_values, (env, s, method, ((slice(u32)){2, vals})), 0);
            try_err(scope_emit_inst, (env, s, (ir)tu_of(ir_set, {.l = res, .d = c->d})), 0);
            return nullptr;
          }
        }
        let e = ss_typeenv(env);
        if (!Type_is_integer_like(lt, e) ||
            !Type_is_integer_like(ss_type(env, scope_find(env, s, c->l)->t), e))
          return_err("OP_UNSUPPORTED_OPERANDS", nullptr);
      }),
      (ir_set, _, {
        let c = &tu_catch(ir_set, *i);
        Type dt = ss_type(env, scope_find(env, s, c->d)->t);
        dt.isref = false; //
        c->l = try_err(cast_value, (env, s, c->l, dt), 0);
      }),
      (ir_cast, _, {
        let c = &tu_catch(ir_cast, *i);
        Type lt = ss_type(env, scope_find(env, s, c->l)->t);
        Type dt = ss_type(env, scope_find(env, s, c->d)->t);
        dt.isref = false;
        let e = ss_typeenv(env);
        if (Type_similar(lt, dt, e)) *i = (ir)tu_of(ir_set, {.l = c->l, .d = c->d});
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
        if (c->l.fn && c->l.fn <= msList_len(env->functions)) {
          let inst = &env->functions[c->l.fn - 1].instances[c->l.spec];
          usize key_len = inst->key_len;
          typed_value *key = inst->key;
          foreach (let a, span(c->args.ptr, c->args.len)) {
            let index = (usize)(a - c->args.ptr);
            if (index >= key_len) continue;
            Type target = ss_type(env, key[index].t);
            *a = try_err(cast_value, (env, s, *a, target), 0);
          }
        }
      }),

      (ir_array, _, {
        let pp = &tu_catch(ir_array, *i);
        let lt = ss_type(env, scope_find(env, s, pp->l)->t);
        let dt = ss_type(env, scope_find(env, s, pp->d)->t);
        let rt = ss_type(env, scope_find(env, s, pp->r)->t);
        let e = ss_typeenv(env);
        if (!Type_isint(rt, e)) return_err("ARRAY_INDEX_NON_INT", nullptr);
        // d = l[r]: the access is the element itself, so dt is the element's own type, a referance
        // when the element type is one. l is the array itself, a pointer to it or a referance to
        // it, so drop one level before asking for the element
        Type arr = lt;
        Type_deref(lt, &arr);
        let inner = TypeTree_inner_array(arr.t, e);
        if (!inner.t) return_err("ARRAY_OF_NON_ARRAY", nullptr);
        Type elem = type_at(e, inner.t);
        if (Type_cmp(e, elem, dt))
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
        // d = l.i: the access is the member itself, so dt is the member's own type, a referance
        // when the member type is one. l is the struct itself, a pointer to it or a referance to
        // it, so drop one level first
        Type strct = lt;
        Type_deref(lt, &strct);
        let st = TypeTree_inner_struct(strct.t, e);
        let bf = TypeTree_inner_bitfield(strct.t, e);
        Type member = {};
        if (st.start && pp->i < type_struct_len(e, st)) member = type_struct_item(e, st, pp->i)->t;
        else if (bf.start && pp->i < type_bitf_len(e, bf)) member = type_bitf_item(e, bf, pp->i)->t;
        else return_err("NO_SUCH_FIELD", nullptr);
        if (Type_is_comptime(member)) return_err("NO_STORAGE", nullptr);
        if (Type_cmp(e, member, dt))
          return_err_extras(
              "TYPE_ERROR",
              print_ebuff("{Type_full} not the member {Type_full}", ss_typeF(env, dt), ss_typeF(env, member)),
              0,
              0
          );
      }),
      // (default) // fallthrough
  );
  return i;
}
static inline void scope_add_inst errs(superscope *env, scope *s, ir i) {
  if (tu_is(ir_if, i))
    P$(&tu_catch(ir_if, i),
       ($->cnd ? $->cnd->rootable = false : 0, $),
       ($->f ? $->f->rootable = false : 0, $),
       ($->t ? $->t->rootable = false : 0, $));
  else if (tu_is(ir_while, i))
    P$(&tu_catch(ir_while, i),
       ($->cnd ? $->cnd->rootable = false : 0, $),
       ($->body ? $->body->rootable = false : 0, $));
  ir *hooked = try_err(scope_add_inst_typecheck, (env, s, &i));
  if (hooked) try_err(scope_emit_inst, (env, s, *hooked));
}
static inline void scope_bind errs(superscope *env, scope *s, u32 idx, fptr name) {
  if (moxmap_get(s->named, name)) return_err("SHADOWING_SAME_SCOPE");
  assertMessage(!moxmap_get(s->named, name));
  assertMessage(idx <= msList_len(s->values) + s->start, "%zu <= %zu + %zu", idx, msList_len(s->values), s->start);
  moxmap_set(s->named, name, idx);
}
static inline void scope_yield errs(superscope *ss, scope *s, u32 result) {
  assertMessage(result <= s->start);
  let v = scope_find(ss, s, result);
  assertMessage(tu_is(c_any, ss_type(ss, v->t).t));
  // the block yields the value, not the referance the last expression left behind, so that the
  // set below copies the thing into the result instead of writing through an address
  Type yielded = ss_type(ss, msList_last(s->values).t);
  yielded.isref = false;
  v->t = ss_type_intern(ss, yielded);
  try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_set, {.l = s->start + msList_len(s->values), .d = result}))); // TODO operator stuff
}

static inline Type Type_ptr errs(Type t) {
  if (t.isref)
    return_err("POINTER_TO_REFERENCE", T({}));
  if (Type_is_comptime(P$(t, ($.iscomptime = false, $))))
    return_err("POINTER_TO_COMPTIME_ONLY", T({}));
  t.ptr_level++;
  return t;
}

static inline Type Type_ref errs(Type t) {
  if (t.isref)
    return_err("REFERENCE_TO_REFERENCE", T({}));
  if (Type_is_comptime(P$(t, ($.iscomptime = false, $))))
    return_err("REFERENCE_TO_COMPTIME_ONLY", T({}));
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

// pushing dedupes, so equal types share an index and index equality is type equality
typeidx ss_type_intern(superscope *ss, Type t) {
  // the null type is index 0 by definition, never a real entry
  if (tu_is(r_null, t.t)) return 0;
  if_decl (let found, moxmap_get(ss->typeindex, t)) return *found;
  sglist_push(ss->tlist, t);
  let idx = (typeidx)ss->tlist.len; // 1 based
  moxmap_set(ss->typeindex, t, idx);
  return idx;
}

static inline void irb_cbb(const callbackallocatorhandle *h, void *p) {
  let _map = (itypeof(superscope, pointers))h->cbself->udata;
  if (p == h->inptr)
    return (void)moxmap_set(_map, p, h->outsize);
  moxmap_rem(_map, h->inptr);
  if (p) moxmap_set(_map, p, h->outsize);
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
    .pointers = moxmap_init(allocator, void*, usize),
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
      {"@this", builtin__this},
      {"@struct", builtin_struct},
      {"@at", builtin_at},
      {"@where", builtin_where},
      {"@uqot", builtin_uqot},
      {"@qot", builtin_qot},
      {"@splice", builtin_splice},
      {"@ast_len", builtin_ast_len},
      {"@ast_at", builtin_ast_at},
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
      {"@while", builtin_while},
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
  foreach (let it, moxmap_iter(ss->pointers, void *, usize))
    adestroy(allocator, (u8(*)[*it.val])it.key);
  moxmap_deinit(ss->pointers);
  moxmap_deinit(ss->typeindex);
  moxmap_deinit(ss->type_info);
  sglist_deinit(ss->tlist);
  sglist_deinit(ss->sitems);
  sglist_deinit(ss->bitems);
  cba_deinit(ss->alloc);
  adestroy(allocator, ss);
}

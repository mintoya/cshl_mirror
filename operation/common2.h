#pragma once
#include "../type.h"
#include "wheels/macros.h"
#include "wheels/mytypes.h"
#include "wheels/print/print_pre.h"
#include "wheels/sList.h"
#include "wheels/smap.h"
#include <assert.h>

#define assert_type(tag, val)              \
  assertMessage(                           \
      tu_is(tag, val),                     \
      "expected " #tag " but got %s",      \
      snprint(stdAlloc, "{Type}", val).ptr \
  )
typedef ast *astptr;
sliceDef(astptr);
sliceDef(u32);
typedef struct ir ir;
typedef struct scope scope;
typedef struct function function;
typedef struct superscope superscope;

tu_def(
    (ir, u8),
    // expression
    (ir_op /*      */, struct { u32 l, r, d, op; }),
    (ir_call /*    */, struct { u32 l, d; slice(u32)args; }),

    // control
    (ir_ret /*     */, u32),
    (ir_break /*   */, struct {}),
    (ir_continue /**/, struct {}),

    // scope
    (ir_if /*      */, struct { u32 cnd; typeof(scope*) t, f; }),
    (ir_while /*   */, struct { u32 cnd; typeof(scope*) t; }),
    (ir_block /*   */, scope *),
);
int i = sizeof(ir);

typedef struct {
  Type t;
  union {
    itypeof(typed_value, v) v;
    function *func;
  };
} typed_full;
static_assert(sizeof(typed_full) == sizeof(typed_value));
static_assert(offsetof(typed_full, v) == offsetof(typed_value, v));
struct scope {
  scope *parent;
  msList(ir) instructions;
  struct {
    msList(typed_full) values;
    moxmap(fptr, u32) named;
    u32 start;
  };
};
struct superscope {
  allocfn alloc;
  moxmap(void *, usize) pointers;
  moxmap(r_tag, msList(struct { u32 fn ; fptr name; })) tfunc;
  mxmap(Type, Type *) types;
  msList(function) all_functions;
  scope *outermost;
};

//{function stuff
// wrapper for slice(astptr)
struct function {
  slice(astptr) _;
};
static inline slice(astptr) function_args(function in) {
  assertMessage(in._.len >= 2);
  return slice_split(in._, (0, in._.len - 2))[0];
}
static inline astptr *function_return(function in) {
  assertMessage(in._.len >= 2);
  return &in._.ptr[in._.len - 2];
}
static inline astptr *function_body(function in) {
  assertMessage(in._.len >= 2);
  return &in._.ptr[in._.len - 1];
}
//}

typed_full *scope_find(superscope *env, scope *s, u32 idx) {
  while (s) {
    if (idx > s->start && idx <= s->start + msList_len(s->values)) {
      return &s->values[idx - s->start - 1];
    }
    s = s->parent;
  }
  assertMessage(false, "value %u not found in scope", idx);
  return nullptr;
}
scope *scope_inferior(superscope *b, scope *s) {
  return avalue(
      b->alloc,
      ((scope){
          .parent = s,
          .instructions = msList_init(b->alloc, ir),
          .values = msList_init(b->alloc, typed_full),
          .named = moxmap_init(b->alloc, fptr, u32, cmpfptr),
          .start = s ? s->start + msList_len(s->values) : 0,

      })
  );
}
void scope_destroy(superscope *b, scope *s) {
  msList_deInit(b->alloc, s->values);
  msList_deInit(b->alloc, s->instructions);
  moxmap_deinit(s->named);
  adestroy(b->alloc, s);
}

u32 scope_add_value(superscope *env, scope *s, typed_full v) {
  let res = msList_len(s->values) + s->start + 1;
  msList_push(env->alloc, s->values, v);
  return res;
}

void scope_bind(superscope *env, scope *s, u32 idx, fptr name) {
  let variable = scope_find(env, s, idx);
  println("binding {typed_value} to {slice(c8)}", *variable, name);
  moxmap_set(s->named, name, idx);
}

#include "wheels/allocators/cballocator.h"
void irb_cbb(const callbackallocatorhandle *h, void *p) {
  let _map = (itypeof(superscope, pointers))h->cbself->udata;
  moxmap_rem(_map, h->inptr);
  if (p) moxmap_set(_map, p, h->outsize);
}
superscope *ss_init(allocfn allocator) {
  // clang-format off
  superscope* res = avalue(allocator, ((superscope){
    .all_functions = msList_init(allocator, function),
    .pointers = moxmap_init(allocator, void*, usize),
    .tfunc = moxmap_init(allocator, r_tag, moxmap_vt(res->tfunc)),
    .types = mxmap_init(allocator, Type, Type *,3,Type_hash_hash,Type_cmp_hash),
  }));
  // clang-format on
  res->alloc = cba_init(allocator, nullptr, irb_cbb, res->pointers);
  res->outermost = scope_inferior(res, nullptr);
  struct {
    char *name;
    builtin_id id;
  } builtins[] = {
      {"fn", builtin_fn},
      {"@this", builtin__this},
      {"struct", builtin_struct},
      {"@at", builtin_at},
      {"@uqot", builtin_uqot},
      {"@qot", builtin_qot},
      {"@type", builtin_type},
      {"@ast", builtin_ast},
      {"@any", builtin_any},
      {"@int", builtin_int},
      {"@sint", builtin_sint},
      {"@uint", builtin_uint},
      {"@ptrof", builtin_ptrof},
      {"@volatile", builtin_volatile},
      {"@const", builtin_const},
      {"@typeof", builtin_typeof},
      {"@arrof", builtin_arrof},
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
        scope_add_value(res, res->outermost, (typed_full){tu_of(builtin, i.id)}),
        fp(i.name)
    );
  return res;
}
void ss_deInit(superscope *ss) {
  let allocator = ((struct cballocator *)ss->alloc)->allocator;
  msList_deInit(allocator, ss->all_functions);
  foreach (let it, moxmap_iter(ss->pointers, void *, usize))
    adestroy(allocator, (u8(*)[*it.val])it.key);
  moxmap_deinit(ss->pointers);
  moxmap_deinit(ss->tfunc);
  mxmap_deinit(ss->types);
  adestroy(allocator, ss);
}
u32 ss_get_named(superscope *env, scope *s, fptr n) {
  while (s) {
    if_decl (let val, moxmap_get(s->named, n))
      return *val;
    s = s->parent;
  }
  assertMessage(false, "ir could'nt find %s", snprint(stdAlloc, "{slice(c8)}", n).ptr);
}
Type *ss_getType(superscope *b, Type t) {
  if_decl (let t, mxmap_get(b->types, t)) return *t;
  let allocator = (b->alloc);
  let pt = acreate(allocator, Type);
  mxmap_set(b->types, t, pt);
  *pt = tu_match_exp(
      t,
      (builtin, b, (Type)tu_of(builtin, b)),
      //
      (r_uint, u, (Type)tu_of(r_uint, u)),
      (r_sint, s, (Type)tu_of(r_sint, s)),
      //
      (r_ptr, p, (Type)tu_of(r_ptr, ss_getType(b, *p))),
      (r_ctime, rct, (Type)tu_of(r_ctime, ss_getType(b, *rct))),
      (r_tag, tag, (Type)tu_of(r_tag, {ss_getType(b, *tag.type), tag.name})),
      (r_arr, arr, (Type)tu_of(r_arr, {ss_getType(b, *arr.type), arr.count})),
      (r_decor, dec, (Type)tu_of(r_decor, {ss_getType(b, *dec.t), dec.constant, dec.valatile})),
      //
      (r_struct, items, (Type)tu_of(r_struct, mList_map(allocator, items, ($.init.t = *ss_getType(b, $.init.t), $)))),
      (r_bitfield, items, (Type)tu_of(r_bitfield, mList_map(allocator, items, ($.init.t = *ss_getType(b, $.init.t), $)))),
      (r_func, params, (Type)tu_of(r_func, mList_map(allocator, params, (*ss_getType(b, $))))),
      //
      (r_void, v, (Type)tu_of(r_void, {})),
      (c_any, _, (Type)tu_of(c_any, {})),
      (c_ast, _, (Type)tu_of(c_ast, {})),
      (c_int, _, (Type)tu_of(c_int, {})),
      (c_type, _, (Type)tu_of(c_type, {})),
  );
  return *mxmap_set(b->types, *pt, pt);
}
bool Type_isint(Type t) {
  return tu_match_exp(
      t,
      (c_int, _, true),
      (r_uint, _, true),
      (r_sint, _, true),
      (r_tag, tg, Type_isint(*tg.type)),
      (r_decor, dec, Type_isint(*dec.t)),
      (default, false),

  );
}
usize Type_int_size(Type t) {
  return tu_match_exp(
      t,
      (c_int, _, (usize)SIZE_MAX),
      (r_uint, u, (usize)u),
      (r_sint, s, (usize)s),
      (r_tag, tg, Type_int_size(*tg.type)),
      (r_decor, dec, Type_int_size(*dec.t)),
      (default, (usize)0),

  );
}
Type value_asType(typed_value t) {
  assert_type(c_type, t.t);
  assertMessage(t.v.c_type, "type is unknown");
  return t.v.c_type[0];
}

u32 ss_compileInt(superscope *b, scope *s, fptr in) {
  let front = slice_split(in, (0, 2))[0];
  bigint result = nullptr;
  if (fptr_eq(front, "0x") || fptr_eq(front, "0b"))
    result = BInt.from.fptr(b->alloc, fptr_eq(front, "0x") ? 16 : 2, slice_split(in, (2, -1))[0]);
  else result = BInt.from.fptr(b->alloc, 10, in);
  assertMessage(result);
  return scope_add_value(b, s, (typed_full){tu_of(c_int, {}), {.v.c_int = result}});
}
u32 ss_compileString(superscope *b, scope *s, fptr in) {
  assertMessage(in.len > 1 && in.ptr[0] == '"' && in.ptr[in.len - 1] == '"');
  let result = mList_init(b->alloc, char, in.len - 2);
  defer { mList_deinit(result); };

  usize i = 1;
  usize end = in.len - 1;
  while (i < end) {
    char c = in.ptr[i];
    if (c == '\\' && i + 1 < end) {
      char out;
      switch (in.ptr[i + 1]) {
          // clang-format off
        case 'n': out = '\n'; break;
        case 't': out = '\t'; break;
        case 'r': out = '\r'; break;
        case 'a': out = '\a'; break;
        case 'b': out = '\b'; break;
        case 'f': out = '\f'; break;
        case 'v': out = '\v'; break;
        case '0': out = '\0'; break;
        case '\\': out = '\\'; break;
        case '\'': out = '\''; break;
        case '"': out = '"'; break;
        default: assertMessage(false , "unknown escape %c%c",in.ptr[i] , in.ptr[i+1]);
          // clang-format on
      }
      mList_push(result, out);
      i += 2;
    } else {
      mList_push(result, c);
      i += 1;
    }
  }

  return scope_add_value(
      b,
      s,
      (typed_full){
          *ss_getType(
              b,
              (Type)tu_of(
                  r_arr,
                  {
                      ss_getType(b, (Type)tu_of(r_sint, 8)),
                      mList_len(result),
                  }
              )
          ),
          mList_toOwned(b->alloc, result),
      }
  );
}
static inline u32 ss_find_tag_method(superscope *env, r_tag tag, fptr name) {
  if_decl (let methods, moxmap_get(env->tfunc, tag)) {
    for (usize i = 0; i < msList_len(*methods); i++) {
      if (fptr_eq((*methods)[i].name, name)) {
        return (*methods)[i].fn;
      }
    }
  }
  return 0;
}

u32 lower(superscope *, scope *, astptr);
typed_value interpret(superscope *, scope *);
void interpret_op(superscope *, scope *, ir_op);
void interpret_call(superscope *, scope *, ir_call);

astptr ast_dot(astptr node, astptr name);

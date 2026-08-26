#pragma once
#include "../type.h"
#define Typed_value(type, value) ((typed_value){(Type)type, value})
#include "wheels/allocators/cballocator.h"
#include "wheels/fptr.h"
#include "wheels/macros.h"
#include "wheels/mytypes.h"
#include "wheels/print/print_pre.h"
#include "wheels/sList.h"
#include "wheels/smap.h"
#include <assert.h>
typedef enum enum_op {
  enop_call,
  enop_dot,
  enop_colon,
  enop_mul,
  enop_div,
  enop_mod,
  enop_plus,
  enop_minus,
  enop_oand,
  enop_oor,
  enop_oband,
  enop_obor,
  enop_shr,
  enop_shl,
  enop_mor,
  enop_les,
  enop_moq,
  enop_leq,
  enop_eq,
  enop_neq,
  enop_pipe,
  enop_set,
  enop_iexpand,
} enum_op;
typePrinter(enum_op) {
  switch (in) {
      // clang-format off
    case  enop_call:PUTS("call");break;
    case  enop_dot:PUTS("dot");break;
    case  enop_colon:PUTS("colon");break;
    case  enop_mul:PUTS("mul");break;
    case  enop_div:PUTS("div");break;
    case  enop_mod:PUTS("mod");break;
    case  enop_plus:PUTS("plus");break;
    case  enop_minus:PUTS("minus");break;
    case  enop_oand:PUTS("oand");break;
    case  enop_oor:PUTS("oor");break;
    case  enop_oband:PUTS("oband");break;
    case  enop_obor:PUTS("obor");break;
    case  enop_shr:PUTS("shr");break;
    case  enop_shl:PUTS("shl");break;
    case  enop_mor:PUTS("mor");break;
    case  enop_les:PUTS("les");break;
    case  enop_moq:PUTS("moq");break;
    case  enop_leq:PUTS("leq");break;
    case  enop_eq:PUTS("eq");break;
    case  enop_neq:PUTS("neq");break;
    case  enop_pipe:PUTS("pipe");break;
    case  enop_set:PUTS("set");break;
     default:PUTS("unknown!!!");break;
      // clang-format on
  }
}
typedef enum enum_boolean_op {
  bop_oand = enop_oand,
  bop_oor = enop_oor,
  bop_mor = enop_mor,
  bop_les = enop_les,
  bop_moq = enop_moq,
  bop_leq = enop_leq,
  bop_eq = enop_eq,
  bop_neq = enop_neq,
} enum_boolean_op;
enum_op op_as_enum(u32 opn) {
  if (opn == opNumbers.call) return enop_call;
  if (opn == opNumbers.dot) return enop_dot;
  if (opn == opNumbers.colon) return enop_colon;
  if (opn == opNumbers.mul) return enop_mul;
  if (opn == opNumbers.div) return enop_div;
  if (opn == opNumbers.mod) return enop_mod;
  if (opn == opNumbers.plus) return enop_plus;
  if (opn == opNumbers.minus) return enop_minus;
  if (opn == opNumbers.oand) return enop_oand;
  if (opn == opNumbers.oor) return enop_oor;
  if (opn == opNumbers.oband) return enop_oband;
  if (opn == opNumbers.obor) return enop_obor;
  if (opn == opNumbers.shr) return enop_shr;
  if (opn == opNumbers.shl) return enop_shl;
  if (opn == opNumbers.mor) return enop_mor;
  if (opn == opNumbers.les) return enop_les;
  if (opn == opNumbers.moq) return enop_moq;
  if (opn == opNumbers.leq) return enop_leq;
  if (opn == opNumbers.eq) return enop_eq;
  if (opn == opNumbers.neq) return enop_neq;
  if (opn == opNumbers.pipe) return enop_pipe;
  if (opn == opNumbers.set) return enop_set;
  if (opn == opNumbers.iexpand) return enop_iexpand;
  assertMessage(false, "Unknown operator %x", opn);
}

typedef ast *astptr;
sliceDef(astptr);
sliceDef(u32);
typedef struct ir ir;
typedef struct scope scope;
typedef struct function function;
typedef struct superscope superscope;

extern fptr *filectx;

tu_def(
    (ir, u8),
    // expression
    (ir_op /*      */, struct { u32 l, r, d, op; }),
    (ir_call /*    */, struct { u32 l, d; slice(u32)args; }),

    // control
    (ir_ret /*     */, u32),
    (ir_break /*   */, nothing_t),
    (ir_continue /**/, nothing_t),

    // scope
    (ir_if /*      */, struct { u32 cnd; typeof(scope *) t, f; }),
    (ir_while /*   */, struct { u32 cnd; typeof(scope *) c, t; }),
    (ir_block /*   */, scope *),

    (ir_takeRef /*   */, struct { u32 d, s; }), // takes a referance to something

    (ir_putptr /*      */, struct { u32 d, s; }),
    (ir_getptr /*     */, struct { u32 d, s; }),
);

struct scope {
  scope *parent;
  msList(ir) instructions;
  msList(typed_value) values;
  moxmap(fptr, u32) named;
  u32 start;
};
typePrinter(scope) {
  PUTS("(scope start:");
  USETYPEPRINTER(u32, in.start);
  PUTS(",(instructions,");
  foreach (let i, span(in.instructions, msList_len(in.instructions))) {
    tu_match_void(
        *i,
        (ir_call, call, {
          PUTS("(ir_call ");
          USETYPEPRINTER(u32, call.l);
          PUTS("(");
          foreach (let c, span(call.args.ptr, call.args.len))
            USETYPEPRINTER(u32, *c), PUTS(" ");
          PUTS(")");
          USETYPEPRINTER(u32, call.d);
          PUTS(")");
        }),
        (ir_op, op, {
          PUTS("(ir_op ");
          USETYPEPRINTER(u32, op.l);
          PUTS(",");
          USETYPEPRINTER(enum_op, op.op);
          PUTS(",");
          USETYPEPRINTER(u32, op.r);
          PUTS(")");
        }),
        (ir_if, ifv, {
          PUTS("(ir_if ");
          USETYPEPRINTER(u32, ifv.cnd);
          PUTS(",");
          if (ifv.t) USETYPEPRINTER(scope, *ifv.t);
          else PUTS("(null)");
          PUTS(",");
          if (ifv.f) USETYPEPRINTER(scope, *ifv.f);
          else PUTS("(null)");
          PUTS(")");
        }),
        (ir_while, ifv, {
          PUTS("(ir_while ");
          USETYPEPRINTER(u32, ifv.cnd);
          PUTS(",");
          if (ifv.c) USETYPEPRINTER(scope, *ifv.c);
          else PUTS("(null)");
          PUTS(",");
          if (ifv.t) USETYPEPRINTER(scope, *ifv.t);
          else PUTS("(null)");
          PUTS(")");
        }),
        (ir_block, block, {
          PUTS("(ir_block ");
          USETYPEPRINTER(scope, *block);
          PUTS(")");
        }),
        (ir_ret, _, PUTS("(ir_ret )")),
        (ir_break, _, PUTS("(ir_break )")),
        (ir_continue, _, PUTS("(ir_continue )")),
        (ir_putptr, iv, {
          PUTS("(ir_putptr ");
          USETYPEPRINTER(u32, iv.d), PUTS(" "), USETYPEPRINTER(u32, iv.s);
          PUTS(")");
        }),
        (ir_getptr, iv, {
          PUTS("(ir_getptr ");
          USETYPEPRINTER(u32, iv.d), PUTS(" "), USETYPEPRINTER(u32, iv.s);
          PUTS(")");
        }),
        (ir_takeRef, irf, {
          PUTS("(ir_takeRef ");
          USETYPEPRINTER(u32, irf.d), PUTS(" "), USETYPEPRINTER(u32, irf.s);
          PUTS(")");
        }),
    );
    PUTS(",");
  }
  PUTS("),");
  PUTS("(values,");
  foreach (let v, span(in.values, msList_len(in.values))) {
    if (v != in.values)
      PUTS(",");
    PUTS("(r"),
        USETYPEPRINTER(u32, in.start + (v - in.values) + 1),
        USETYPEPRINTER(typed_value, *v),
        PUTS(")");
  }
  PUTS("),");
  PUTS("(named,");
  foreach (let v, moxmap_iter(in.named, fptr, u32)) {
    PUTS("('"), USENAMEDPRINTER("slice(c8)", v.key), PUTS("' ");
    USETYPEPRINTER(u32, *v.val);
    PUTS(")");
    PUTS(",");
  }
  PUTS(")");
  PUTS(")");
}
struct superscope {
  allocfn alloc;

  moxmap(void *, usize) pointers;
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

//{function stuff
// wrapper for slice(astptr)
struct function {
  slice(astptr) _;
  moxmap(sentList_t(Type), u32) specs;
};
i8 cmpstl_type(const void *a, const void *b) {
  let ap = *(sentList_t(Type) *)a;
  let bp = *(sentList_t(Type) *)b;
  let al = sentList_len(ap);
  if_decl (let dif, (i64)al - (i64)sentList_len(bp)) return dif;
  foreach (let i, range(0, al))
    if_decl (let dif, Type_cmp(ap[i], bp[i]))
      return dif;
  return 0;
}
static inline itypeof(function, specs) function_initSpecs(allocfn alloc, function *f) {
  return f->specs = f->specs ?: moxmap_init(alloc, sentList_t(Type), moxmap_vt(f->specs), cmpstl_type);
}
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

typed_value *scope_find(superscope *env, scope *s, u32 idx) {
  while (s)
    if (idx > s->start && idx <= s->start + msList_len(s->values))
      return &s->values[idx - s->start - 1];
    else s = s->parent;
  assertMessage(false, "value %u not found in scope", idx);
}
scope *scope_inferior(superscope *b, scope *s) {
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
void scope_destroy(superscope *b, scope *s) {
  msList_deInit(b->alloc, s->values);
  msList_deInit(b->alloc, s->instructions);
  moxmap_deinit(s->named);
  adestroy(b->alloc, s);
}

u32 scope_add_value(superscope *env, scope *s, typed_value v) {
  let res = msList_len(s->values) + s->start + 1;
  msList_push(env->alloc, s->values, v);
  return res;
}
void scope_add_inst(superscope *env, scope *s, ir i) { msList_push(env->alloc, s->instructions, i); }
void scope_bind(superscope *env, scope *s, u32 idx, fptr name) {
  let variable = scope_find(env, s, idx);
  // println("binding {u32} to {slice(c8)}", idx, name);
  assertMessage(!moxmap_get(s->named, name), "already bound %s", snprint(stdAlloc, "{slice(c8)}", name).ptr);
  assertMessage(idx <= msList_len(s->values) + s->start, "%zu <= %zu + %zu", idx, msList_len(s->values), s->start);
  moxmap_set(s->named, name, idx);
}

void irb_cbb(const callbackallocatorhandle *h, void *p) {
  let _map = (itypeof(superscope, pointers))h->cbself->udata;
  if (p == h->inptr)
    return (void)moxmap_set(_map, p, h->outsize);
  moxmap_rem(_map, h->inptr);
  if (p) moxmap_set(_map, p, h->outsize);
}
superscope *ss_init(allocfn allocator) {
  // clang-format off
  superscope* res = avalue(allocator, ((superscope){
    .all_functions = msList_init(allocator, ptrstype(res->all_functions)),
    .pointers = moxmap_init(allocator, void*, usize),
    .types = moxmap_init(allocator, Type, Type *,Type_cmp_hash),
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
      {"@construct", builtin_construct},
  };
  foreach (let i, vla(builtins))
    scope_bind(
        res,
        res->outermost,
        scope_add_value(res, res->outermost, (typed_value){tu_of(builtin, i.id)}),
        fp(i.name)
    );
  return res;
}
void ss_deInit(superscope *ss) {
  let allocator = ((struct cballocator *)ss->alloc)->allocator;
  msList_deInit(allocator, ss->all_functions);
  msList_deInit(allocator, ss->thisstack);
  foreach (let it, moxmap_iter(ss->tdata, Type, msList(value_store)))
    msList_deInit(allocator, *it.val);
  foreach (let it, moxmap_iter(ss->pointers, void *, usize))
    adestroy(allocator, (u8(*)[*it.val])it.key);
  moxmap_deinit(ss->tdata);
  moxmap_deinit(ss->pointers);
  moxmap_deinit(ss->types);
  cba_deinit(ss->alloc);
  adestroy(allocator, ss);
}
u32 ss_get_named(superscope *env, scope *s, fptr n) {
  let limit = s->start + msList_len(s->values);
  while (s) {
    if_decl (let val, moxmap_get(s->named, n))
      if (*val <= limit) {
        let vo = scope_find(env, s, *val);
        return tu_match_exp(
            vo->t,
            (r_ref, ref, *val),
            (default,
             scope_add_value(
                 env, s, Typed_value(tu_of(r_ref, &vo->t), avalue(env->alloc, vo->v))
             ))
        );
      }
    limit = s->start;
    s = s->parent;
  }
  error_print(n.ptr, "unbound identifier {slice(c8)}", n);
}
typed_value *scope_find_orig(superscope *ss, scope *s, u32 u) {
  let v = scope_find(ss, s, u);
  let vt = Type_peel(v->t, false, true);

  while (tu_is(r_ref, vt.t)) {
    vt.t = *tu_catch(r_ref, vt.t);
    assertMessage(v->v.value, "reference pointer is null");
    v = (typed_value *)v->v.value;
    vt = Type_peel(v->t, false, true);
  }

  return v;
}
Type *ss_getType(superscope *b, Type t) {
  if_decl (let t, moxmap_get(b->types, t)) return *t;
  let allocator = (b->alloc);
  let pt = acreate(allocator, Type);
  moxmap_set(b->types, t, pt);

  let p = Type_peel(t, false, true);
  if (p.is_comptime || p.is_const || p.is_volatile) {
    Type *inner_pt = ss_getType(b, p.t);

    if (p.is_const || p.is_volatile) {
      Type dec_type = (Type)tu_of(r_decor, {inner_pt, p.is_const, p.is_volatile});
      if (p.is_comptime) {
        Type *dec_pt = acreate(allocator, Type);
        *dec_pt = dec_type;
        *pt = (Type)tu_of(r_ctime, dec_pt);
      } else *pt = dec_type;
    } else *pt = (Type)tu_of(r_ctime, inner_pt);
    return *moxmap_set(b->types, *pt, pt);
  }

  // If there were no modifiers, fall through to process the base type as normal
  *pt = tu_match_exp(
      p.t,
      (builtin, b, (Type)tu_of(builtin, b)),
      //
      (r_uint, u, (Type)tu_of(r_uint, u)),
      (r_sint, s, (Type)tu_of(r_sint, s)),
      //
      (r_ptr, p, (Type)tu_of(r_ptr, ss_getType(b, *p))),
      (r_ref, p, (Type)tu_of(r_ptr, ss_getType(b, *p))),
      (r_ctime, rct, (Type)tu_of(r_ctime, ss_getType(b, *rct))),
      (r_tag, tag, (Type)tu_of(r_tag, {ss_getType(b, *tag.t), tag.name})),
      (r_arr, arr, (Type)tu_of(r_arr, {ss_getType(b, *arr.t), arr.count})),
      (r_decor, dec, (Type)tu_of(r_decor, {ss_getType(b, *dec.t), dec.constant, dec.valatile})),
      //
      (r_struct, items, (Type)tu_of(r_struct, items)),
      (r_bitfield, items, (Type)tu_of(r_bitfield, items)),
      (r_closure, params, (Type)tu_of(r_closure, params)),
      (r_func, params, (Type)tu_of(r_func, params)),
      //
      (r_void, v, (Type)tu_of(r_void, {})),
      (c_any, _, (Type)tu_of(c_any, {})),
      (c_ast, _, (Type)tu_of(c_ast, {})),
      (c_int, _, (Type)tu_of(c_int, {})),
      (c_type, _, (Type)tu_of(c_type, {}))
  );
  return *moxmap_set(b->types, *pt, pt);
}
bool Type_isint(Type t) {
  return tu_match_exp(
      t,
      (c_int, _, true),
      (r_uint, _, true),
      (r_sint, _, true),
      (r_ref, tg, Type_isint(*tg)),
      (r_tag, tg, Type_isint(*tg.t)),
      (r_decor, dec, Type_isint(*dec.t)),
      (r_ctime, ct, Type_isint(*ct)),
      (default, false),
  );
}
Type value_asType_loc(typed_value t, u8 *location) {
  Type current_type = Type_peel(t.t, false, true).t;
  if (!tu_is(c_type, current_type)) error_print(location, "tried to get type from {Type}", t.t);
  if (!t.v.c_type) error_print(location, "unknown type");
  return *t.v.c_type;
}
typed_value Type_asValue(superscope *s, Type t) { return (typed_value){tu_of(c_type, {}), ss_getType(s, t)}; }

u32 ss_compileInt(superscope *b, scope *s, fptr in) {
  let front = slice_split(in, (0, 2))[0];
  bigint result = nullptr;
  if (fptr_eq(front, "0x") || fptr_eq(front, "0b"))
    result = BInt.from.fptr(b->alloc, fptr_eq(front, "0x") ? 16 : 2, slice_split(in, (2, -1))[0]);
  else result = BInt.from.fptr(b->alloc, 10, in);
  assertMessage(result);
  return scope_add_value(b, s, (typed_value){tu_of(c_int, {}), result});
}
fptr string_escape(allocfn allocator, fptr in) {
  assertMessage(in.len > 1 && in.ptr[0] == '"' && in.ptr[in.len - 1] == '"');
  let result = mList_init(allocator, u8, in.len - 2);
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

  let l = mList_len(result);
  return (fptr){l, mList_toOwned(allocator, result)};
}
u32 ss_compileString(superscope *b, scope *s, fptr in) {
  assertMessage(in.len > 1 && in.ptr[0] == '"' && in.ptr[in.len - 1] == '"');
  in = string_escape(b->alloc, in);
  return scope_add_value(
      b,
      s,
      (typed_value){
          *ss_getType(
              b,
              (Type)tu_of(r_arr, {ss_getType(b, (Type)tu_of(r_sint, 8)), in.len})
          ),
          in.ptr
      }
  );
}

u32 lower(superscope *, scope *, astptr);
typed_value interpret(superscope *, scope *, u8 *);
typed_value interpret_call(superscope *, scope *, u8 *location, ir_call);
typed_value interpret_op(superscope *ss, scope *s, ir_op c);

typed_value temporary_interpret_ast(superscope *ss, scope *s, ast *a) {
  let si = scope_inferior(ss, s);
  defer { scope_destroy(ss, si); };

  u32 val_idx = lower(ss, si, a);
  interpret(ss, si, ast_loc(*a));

  if (val_idx)
    return *scope_find(ss, si, val_idx);
  assertMessage(false, "");
}
static bool interp_tv_truthy(typed_value *tv) {
  assertMessage(tv);
  Type t = strip_decor(tv->t);
  if (tu_is(c_int, t)) return (bool)bigint_cmp(tv->v.c_int, nullptr);
  if (tu_is(c_ast, t)) return tv->v.c_ast && !tu_is(astEmptyStatement, *tv->v.c_ast);
  if (tu_is(r_uint, t) || tu_is(r_sint, t)) {
    assertMessage(tv->v.value, "cant tell if value is truthy");
    return !fptr_isEmpty((fptr){Type_rsize(t), (u8 *)tv->v.value});
  }
  if (tu_is(r_void, t)) return false;
  assertMessage(false, "unhandled value");
}

static u32 function_trySpec(superscope *ss, scope *s, u8 *loc, function *fn, sentList_t(Type) typeList, slice(u32) args) {
  bool requires_comptime = false;
  foreach (let i, vlap(sentList_vla(typeList)))
    requires_comptime |= (Type_is_comptime(strip_decor(i)));
  if (!requires_comptime)
    if_decl (let m, moxmap_get(function_initSpecs(ss->alloc, fn), typeList)) return *m;

  let l = sentList_vla(typeList);
  let returnT = (Type)tu_of(c_any, {});
  if (countof * l != fn->_.len - 2)
    error_print(loc, "function called with {} args instead of {}", countof * l, fn->_.len - 2);
  else {
    let tpe_evaluated = msList_init(ss->alloc, Type);
    defer { msList_deInit(ss->alloc, tpe_evaluated); };
    let temp = scope_inferior(ss, s);
    defer { scope_destroy(ss, temp); };

    foreach (let idx, range(0, countof * l)) {
      ast *inner = fn->_.ptr[idx];
      Type arg_type = (*l)[idx];
      if (tu_is(c_any, arg_type))
        error_print(nullptr, "any is not allowed as an argument inside of a call");

      iflet_tu (let _, astDecl, *inner) {
        Type *param_type = &scope_find(ss, temp, lower(ss, temp, inner))->t;
        if (tu_is(c_any, *param_type))
          msList_push(ss->alloc, tpe_evaluated, (*param_type = arg_type));
        else msList_push(ss->alloc, tpe_evaluated, (*param_type));
      } else {
        Type param_type = temporary_interpret_ast(ss, temp, inner).t;
        if (tu_is(c_any, param_type))
          msList_push(ss->alloc, tpe_evaluated, arg_type);
        else msList_push(ss->alloc, tpe_evaluated, param_type);
      }
    }

    astptr ret_ast = *function_return(*fn);
    iflet_tu (let _, astDecl, *ret_ast) returnT = scope_find(ss, temp, lower(ss, temp, ret_ast))->t;
    else returnT = value_asType_loc(temporary_interpret_ast(ss, temp, ret_ast), nullptr);

    msList_push(ss->alloc, tpe_evaluated, {});
    if (cmpstl_type(&tpe_evaluated, &typeList))
      error_print(loc, "function could not be resolved");
  }
  let body = fn->_.ptr[fn->_.len - 1];

  let real_body = scope_inferior(ss, s);

  foreach (let idx, range(0, countof * l)) {
    astptr inner = fn->_.ptr[idx];
    u32 param_idx = lower(ss, real_body, inner);
    scope_find(ss, real_body, param_idx)->t = (*l)[idx];
    *scope_find(ss, real_body, param_idx) = *scope_find(ss, s, args.ptr[idx]);
  }

  astptr ret_ast = *function_return(*fn);
  u32 ret_idx = 0;
  iflet_tu (let _, astDecl, *ret_ast) {
    ret_idx = lower(ss, real_body, ret_ast);
    scope_find(ss, real_body, ret_idx)->t = returnT;
  }

  lower(ss, real_body, body);
  scope_add_inst(ss, real_body, (ir)tu_of(ir_ret, ret_idx));
  let full_sig = msList_init(ss->alloc, Type);
  foreach (let idx, range(0, countof * l))
    msList_push(ss->alloc, full_sig, (*l)[idx]);
  msList_push(ss->alloc, full_sig, returnT);
  msList_push(ss->alloc, full_sig, {});

  let spec_idx = msList_len(ss->all_functions);
  msList_push(ss->alloc, ss->all_functions, {.sig = full_sig, .body = real_body});
  return *moxmap_set(function_initSpecs(ss->alloc, fn), typeList, (u32)spec_idx);
}
bigint int_as_bigint(superscope *ss, typed_value t) {
  assertMessage(t.v.value, "cannot convert unknown value");
  return tu_match_exp(
      strip_decor(t.t),
      (c_int, _, BInt.copy(ss->alloc, t.v.c_int)),
      (r_sint, si, BInt.from.bits(ss->alloc, t.v.value, si, 1)),
      (r_uint, si, BInt.from.bits(ss->alloc, t.v.value, si, 0)),
      (default, (bigint)(assertMessage(false, "should not be called with a non-int"), nullptr)),
  );
}
void bigint_into_int(superscope *ss, typed_value *t, bigint b) {
  assertMessage(t->v.value, "cannot convert unknown value");
  tu_match_void(
      strip_decor(t->t),
      (c_int, _, {
        let o = &t->v.c_int;
        msList_deInit(ss->alloc, *o);
        *o = BInt.copy(ss->alloc, b);
      }),
      (r_sint, si, {
        let allocated_len = Type_rsize(t->t);
        memcpy(t->v.value, b, allocated_len);
      }),
      (r_uint, si, {
        let allocated_len = Type_rsize(t->t);
        memcpy(t->v.value, b, allocated_len);
      }),
      (r_tag, tg, {
        let temp = (typed_value){*tg.t, t->v};
        return bigint_into_int(ss, &temp, b);
      }),
      (r_ref, tg, {
        assertMessage(t->v.value);
        let temp = (typed_value){*tg, (assertMessage(false, "what's a ref?"), t->v)};
        return bigint_into_int(ss, &temp, b);
      }),
      (default, assertMessage(false, "should not be called with a non-int")),
  );
}

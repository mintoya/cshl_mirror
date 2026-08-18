#pragma once
#include "../type.h"
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
  assertMessage(false, "Unknown operator %x", opn);
}

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

// the source buffer, defined in main.c / tests.c; used to resolve call-site
// locations for @dbg
extern fptr *filectx;

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
    (ir_if /*      */, struct { u32 cnd; typeof(scope *) t, f; }),
    (ir_while /*   */, struct { u32 cnd; typeof(scope *) c, t; }),
    (ir_block /*   */, scope *),

    // debug
    (ir_dbg /*     */, struct { u32 l; u32 line, col; }),
);

tu_def((value, u8), (rvalue, typed_value), (lvalue, void *));
struct scope {
  scope *parent;
  msList(ir) instructions;
  msList(value) values;
  moxmap(fptr, u32) named;
  u32 start;
};
typePrinter(scope) {
  PUTS("(scope,");
  PUTS("(instructions,");
  foreach (let i, span(in.instructions, msList_len(in.instructions))) {
    tu_match_void(
        *i,
        (ir_call, call, {
          PUTS("(ir_call ");
          USETYPEPRINTER(u32, call.l);
          PUTS("(");
          foreach (let c, span(call.args.ptr, call.args.len))
            USETYPEPRINTER(u32, *c), PUTS(",");
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
        (ir_block, ifv, {
          PUTS("(ir_block,");
          USETYPEPRINTER(scope, *ifv);
          PUTS(")");
        }),
        (ir_ret, _, PUTS("(ir_ret )")),
        (ir_break, _, PUTS("(ir_break )")),
        (ir_continue, _, PUTS("(ir_continue )")),
        (ir_dbg, dbgv, {
          PUTS("(ir_dbg ");
          USETYPEPRINTER(u32, dbgv.l);
          PUTS(",");
          USETYPEPRINTER(u32, dbgv.line);
          PUTS(":");
          USETYPEPRINTER(u32, dbgv.col);
          PUTS(")");
        }),
    );
    PUTS(",");
  }
  PUTS("),");
  PUTS("(values,");
  foreach (let v, span(in.values, msList_len(in.values)))
    tu_match(
        *v,
        (lvalue, l, PUTS("(l"), USETYPEPRINTER(u32, in.start + v - in.values + 1), PUTS(","), USENAMEDPRINTER("ptr", l), PUTS(")")),
        (rvalue, r, PUTS("(r"), USETYPEPRINTER(u32, in.start + v - in.values + 1), PUTS(","), USETYPEPRINTER(typed_value, r), PUTS(")")),
    ),
        PUTS(",");
  PUTS("),");
  PUTS("(named,");
  foreach (let v, moxmap_iter(in.named, fptr, u32)) {
    PUTS("('");
    USENAMEDPRINTER("slice(c8)", v.key);
    PUTS("' ");
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
  let bl = sentList_len(bp);
  if_decl (let dif, (int)al - (int)bl) return dif;
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
static inline bool Type_compatible(superscope *ss, Type a, Type b) {
  // const stripping and such
  Type *ss_getType(superscope * b, Type t);
  return !Type_cmp(*ss_getType(ss, a), *ss_getType(ss, b));
}
//}

value *scope_find(superscope *env, scope *s, u32 idx) {
  while (s) {
    if (idx > s->start && idx <= s->start + msList_len(s->values))
      return &s->values[idx - s->start - 1];
    s = s->parent;
  }
  assertMessage(false, "value %u not found in scope", idx);
  return nullptr;
}
typed_value *scope_find_origional(superscope *env, scope *s, u32 idx) {
  let i = scope_find(env, s, idx);
  while (tu_is(lvalue, *i))
    i = (value *)tu_catch(lvalue, *i);
  return &tu_catch(rvalue, *i);
}
scope *scope_inferior(superscope *b, scope *s) {
  return avalue(
      b->alloc,
      ((scope){
          .parent = s,
          .instructions = msList_init(b->alloc, ir),
          .values = msList_init(b->alloc, value),
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

u32 scope_add_rvalue(superscope *env, scope *s, typed_value v) {
  let res = msList_len(s->values) + s->start + 1;
  msList_push(env->alloc, s->values, (value)tu_of(rvalue, v));
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
      {"fn", builtin_fn},
      {"@this", builtin__this},
      {"struct", builtin_struct},
      {"@struct", builtin_struct},
      {"@at", builtin_at},
      {"@uqot", builtin_uqot},
      {"@qot", builtin_qot},
      {"@type", builtin_type},
      {"type", builtin_type},
      {"@ast", builtin_ast},
      {"ast", builtin_ast},
      {"@any", builtin_any},
      {"any", builtin_any},
      {"@void", builtin_void},
      {"void", builtin_void},
      {"@int", builtin_int},
      {"int", builtin_int},
      {"@sint", builtin_sint},
      {"@uint", builtin_uint},
      {"@ptrof", builtin_ptrof},
      {"@volatile", builtin_volatile},
      {"@const", builtin_const},
      {"@typeof", builtin_typeof},
      {"typeof", builtin_typeof},
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
        scope_add_rvalue(res, res->outermost, (typed_value){tu_of(builtin, i.id)}),
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
  moxmap_deinit(ss->tdata);
  foreach (let it, moxmap_iter(ss->pointers, void *, usize))
    adestroy(allocator, (u8(*)[*it.val])it.key);
  moxmap_deinit(ss->pointers);
  moxmap_deinit(ss->types);
  cba_deinit(ss->alloc);
  adestroy(allocator, ss);
}
u32 ss_get_named(superscope *env, scope *s, fptr n) {
  if_decl (let val, moxmap_get(s->named, n))
    return *val;
  let limit = s->start;
  s = s->parent;
  while (s) {
    if_decl (let val, moxmap_get(s->named, n))
      if (*val <= limit)
        return *val;
    limit = s->start;
    s = s->parent;
  }
  assertMessage(false, "ir could'nt find %s", snprint(stdAlloc, "{slice(c8)}", n).ptr);
}
Type decor_bubble(superscope *b, Type t, bool *iconst, bool *ivolatile) {
  Type *ss_getType(superscope * b, Type t);
  assertMessage(!iconst == !ivolatile);
  iflet_tu (let dec, r_decor, t) {
    bool icn = dec.constant;
    bool ivl = dec.valatile;
    if (!iconst) iconst = &icn, ivolatile = &ivl;
    else {
      *iconst |= icn, *ivolatile |= ivl;
      return decor_bubble(b, *dec.t, iconst, ivolatile);
    }
    return (Type)tu_of(r_decor, {ss_getType(b, decor_bubble(b, *dec.t, iconst, ivolatile)), *iconst, *ivolatile});
  }
  return t;
}
Type *ss_getType(superscope *b, Type t) {
  t = decor_bubble(b, t, nullptr, nullptr);
  if_decl (let t, moxmap_get(b->types, t)) return *t;
  let allocator = (b->alloc);
  let pt = acreate(allocator, Type);
  moxmap_set(b->types, t, pt);
  *pt = tu_match_exp(
      t,
      (builtin, b, (Type)tu_of(builtin, b)),
      //
      (r_uint, u, (Type)tu_of(r_uint, u)),
      (r_sint, s, (Type)tu_of(r_sint, s)),
      //
      (r_ptr, p, (Type)tu_of(r_ptr, ss_getType(b, *p))),
      (r_ctime, rct, (Type)tu_of(r_ctime, ss_getType(b, *rct))),
      (r_tag, tag, (Type)tu_of(r_tag, {ss_getType(b, *tag.t), tag.name})),
      (r_arr, arr, (Type)tu_of(r_arr, {ss_getType(b, *arr.t), arr.count})),
      (r_decor, dec, (Type)tu_of(r_decor, {ss_getType(b, *dec.t), dec.constant, dec.valatile})),
      //
      (r_struct, items, (Type)tu_of(r_struct, mList_map(allocator, items, ($.t = *ss_getType(b, $.t), $)))),
      (r_bitfield, items, (Type)tu_of(r_bitfield, mList_map(allocator, items, ($.t = *ss_getType(b, $.t), $)))),
      (r_func, params, (Type)tu_of(r_func, mList_map(allocator, params, (*ss_getType(b, $))))),
      //
      (r_void, v, (Type)tu_of(r_void, {})),
      (c_any, _, (Type)tu_of(c_any, {})),
      (c_ast, _, (Type)tu_of(c_ast, {})),
      (c_int, _, (Type)tu_of(c_int, {})),
      (c_type, _, (Type)tu_of(c_type, {})),
  );
  return *moxmap_set(b->types, *pt, pt);
}
bool Type_isint(Type t) {
  return tu_match_exp(
      t,
      (c_int, _, true),
      (r_uint, _, true),
      (r_sint, _, true),
      (r_tag, tg, Type_isint(*tg.t)),
      (r_decor, dec, Type_isint(*dec.t)),
      (r_ctime, ct, Type_isint(*ct)),
      (default, false),

  );
}
usize Type_int_size(Type t) {
  return tu_match_exp(
      t,
      (c_int, _, ~(usize)0),
      (r_uint, u, (usize)u),
      (r_sint, s, (usize)s),
      (r_ctime, ct, Type_int_size(*ct)),
      (r_tag, tg, Type_int_size(*tg.t)),
      (r_decor, dec, Type_int_size(*dec.t)),
      (default, (usize)0),

  );
}
Type value_asType(typed_value t) {
  Type current_type = strip_decor(t.t);
  if (tu_is(builtin, current_type)) {
    // a bare type name (`void`, `ast`, ...) references the uncalled builtin;
    // in a type position it denotes the type itself
    switch (tu_catch(builtin, current_type)) {
      case builtin_ast:
        return (Type)tu_of(c_ast, {});
      case builtin_any:
        return (Type)tu_of(c_any, {});
      case builtin_void:
        return (Type)tu_of(r_void, {});
      case builtin_int:
        return (Type)tu_of(c_int, {});
      case builtin_type:
        return (Type)tu_of(c_type, {});
      default:
        assertMessage(false, "builtin is not a type");
    }
  }
  assert_type(c_type, current_type);
  assertMessage(t.v.c_type, "type is unknown");
  return t.v.c_type[0];
}
typed_value Type_asValue(superscope *s, Type t) { return (typed_value){tu_of(c_type, {}), ss_getType(s, t)}; }

u32 ss_compileInt(superscope *b, scope *s, fptr in) {
  let front = slice_split(in, (0, 2))[0];
  bigint result = nullptr;
  if (fptr_eq(front, "0x") || fptr_eq(front, "0b"))
    result = BInt.from.fptr(b->alloc, fptr_eq(front, "0x") ? 16 : 2, slice_split(in, (2, -1))[0]);
  else result = BInt.from.fptr(b->alloc, 10, in);
  assertMessage(result);
  return scope_add_rvalue(b, s, (typed_value){tu_of(c_int, {}), result});
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

  return scope_add_rvalue(
      b,
      s,
      (typed_value){
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

u32 lower(superscope *, scope *, astptr);
typed_value interpret(superscope *, scope *);

typed_value temporary_interpret_ast(superscope *ss, scope *s, ast *a) {
  let si = scope_inferior(ss, s);
  defer { scope_destroy(ss, si); };

  u32 val_idx = lower(ss, si, a);
  interpret(ss, si); // Execute any instructions (if there are any)

  if (val_idx) {
    return *scope_find_origional(ss, si, val_idx);
  }

  return (typed_value){tu_of(r_void, {})};
}
static bool interp_tv_truthy(typed_value *tv) {
  assertMessage(tv);
  Type t = strip_decor(tv->t);
  if (tu_is(c_int, t)) return (bool)bigint_cmp(tv->v.c_int, nullptr);
  if (tu_is(c_ast, t)) return tv->v.c_ast && !tu_is(astEmptyStatement, *tv->v.c_ast);
  if (tu_is(r_uint, t) || tu_is(r_sint, t)) {
    // The condition of a loop is often itself an ir_op whose evaluation
    // happens inside the loop body: before the first store it has no
    // storage, which means "not computed yet" and reads as true here.
    if (!tv->v.value) return true; // TODO tf?
    foreach (u8 *b, span((u8 *)tv->v.value, Type_rsize(t)))
      if (*b) return true;
    return false;
  }
  if (tu_is(r_void, t)) return false;
  return tv->v.value != nullptr;
}

static u32 function_trySpec(superscope *ss, scope *s, function *fn, sentList_t(Type) typeList, slice(u32) args) {
  if_decl (let m, moxmap_get(function_initSpecs(ss->alloc, fn), typeList)) return *m;
  let l = sentList_vla(typeList);
  let returnT = (Type)tu_of(c_any, {});
  if (countof * l != fn->_.len - 2) error_print(nullptr, "function called with too many args");
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
        Type *param_type = &scope_find_origional(ss, temp, lower(ss, temp, inner))->t;
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
    iflet_tu (let _, astDecl, *ret_ast) returnT = scope_find_origional(ss, temp, lower(ss, temp, ret_ast))->t;
    else returnT = value_asType(temporary_interpret_ast(ss, temp, ret_ast));

    msList_push(ss->alloc, tpe_evaluated, {});
    if (cmpstl_type(&tpe_evaluated, &typeList))
      error_print(nullptr, "function could not be resolved");
  }
  let body = fn->_.ptr[fn->_.len - 1];

  let real_body = scope_inferior(ss, s);

  foreach (let idx, range(0, countof * l)) {
    astptr inner = fn->_.ptr[idx];
    u32 param_idx = lower(ss, real_body, inner);
    scope_find_origional(ss, real_body, param_idx)->t = (*l)[idx];
    // bind the argument value before lowering the body: macros quote/unquote
    // their params at lower time, so they must already hold the actual values
    *scope_find_origional(ss, real_body, param_idx) = *scope_find_origional(ss, s, args.ptr[idx]);
  }

  astptr ret_ast = *function_return(*fn);
  u32 ret_idx = 0;
  iflet_tu (let _, astDecl, *ret_ast) {
    ret_idx = lower(ss, real_body, ret_ast);
    scope_find_origional(ss, real_body, ret_idx)->t = returnT;
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
  return *moxmap_set(fn->specs, typeList, (u32)spec_idx);
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
  // TODO
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
      (default, assertMessage(false, "should not be called with a non-int")),
  );
}

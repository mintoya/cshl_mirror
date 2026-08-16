#pragma once
#include "../type.h"
#include "wheels/allocators/cballocator.h"
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
          USETYPEPRINTER(scope, ifv.t);
          PUTS(",");
          USETYPEPRINTER(scope, ifv.f);
          PUTS(")");
        }),
        (ir_while, ifv, {
          PUTS("(ir_while ");
          USETYPEPRINTER(u32, ifv.cnd);
          PUTS(",");
          USETYPEPRINTER(scope, ifv.t);
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
    );
    PUTS(",");
  }
  PUTS("),");
  PUTS("(values,");
  foreach (let v, span(in.values, msList_len(in.values)))
    tu_match(
        *v,
        (lvalue, l, PUTS("(l :"), USETYPEPRINTER(u32, in.start + v - in.values + 1), USENAMEDPRINTER("ptr", l), PUTS(")")),
        (rvalue, r, PUTS("(r :"), USETYPEPRINTER(u32, in.start + v - in.values + 1), USETYPEPRINTER(Type, r.t), PUTS(")")),
    ),
        PUTS(",");
  PUTS("),");
  PUTS("(named,");
  foreach (let v, moxmap_iter(in.named, fptr, u32)) {
    PUTS("('");
    USETYPEPRINTER(id_str, v.key);
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
  mxmap(Type, Type *) types;
  msList(function) all_functions;
  scope *outermost;
  msList(Type *) thisstack;
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
  println("binding {u32} to {slice(c8)}", idx, name);
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
    .all_functions = msList_init(allocator, function),
    .pointers = moxmap_init(allocator, void*, usize),
    .types = mxmap_init(allocator, Type, Type *,3,Type_hash_hash,Type_cmp_hash),
    .thisstack = msList_init(allocator , Type*),
  }));
  // clang-format on
  res->alloc = cba_init(allocator, nullptr, irb_cbb, res->pointers);
  res->outermost = scope_inferior(res, nullptr);
  struct {
    char *name;
    builtin_id id;
  } builtins[] = {
      {"@fn", builtin_fn},
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
        scope_add_rvalue(res, res->outermost, (typed_value){tu_of(builtin, i.id)}),
        fp(i.name)
    );
  return res;
}
void ss_deInit(superscope *ss) {
  let allocator = ((struct cballocator *)ss->alloc)->allocator;
  msList_deInit(allocator, ss->all_functions);
  msList_deInit(allocator, ss->thisstack);
  foreach (let it, moxmap_iter(ss->pointers, void *, usize))
    adestroy(allocator, (u8(*)[*it.val])it.key);
  moxmap_deinit(ss->pointers);
  mxmap_deinit(ss->types);
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
      (r_tag, tag, (Type)tu_of(r_tag, {ss_getType(b, *tag.t), tag.name})),
      (r_arr, arr, (Type)tu_of(r_arr, {ss_getType(b, *arr.t), arr.count})),
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
  assert_type(c_type, t.t);
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
void interpret_op(superscope *, scope *, ir_op);
void interpret_call(superscope *, scope *, ir_call);
astptr ast_dot(astptr node, astptr name);
typed_value temporary_interpret(superscope *, scope *, ir);
typed_value temporary_interpret_ast(superscope *ss, scope *s, ast *a) {
  let si = scope_inferior(ss, s);
  defer { scope_destroy(ss, si); };
  lower(ss, si, a);
  return temporary_interpret(ss, s, (ir)tu_of(ir_block, si));
}

typed_value temporary_interpret(superscope *, scope *, ir) { TODO(); }
typed_value interpret(superscope *, scope *) { TODO(); }

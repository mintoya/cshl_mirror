#pragma once
#include "../type.h"
#include "ir.h"
#include <string.h>
#include <wheels/bigint.h>
#include <wheels/macros.h>
#include <wheels/print/print_pre.h>

typedef struct superscope superscope;
typedef struct scope scope;
struct scope {
  scope *parent;
  msList(ir) instructions;
  msList(typed_value) values;
  moxmap(fptr, u32) named;
  u32 start;
};
sliceDef(scope);

typedef struct {
  Type ret;
  scope *spec;
} function_instance;

typedef struct function {
  slice(astptr) params;
  ast *retT;
  ast *body;
  msList(function_instance) instances;
  moxmap(sliceDef(Type), u32) mp;
} function;
struct superscope {
  allocfn alloc;

  moxmap(void *, usize) pointers;
  moxmap(TypeTree, TypeTree *) typetrees;
  moxmap(Type, Type *) types;
  msList(Type *) thisstack;
  moxmap(fptr, scope *) type_info;
  msList(function) functions;
  msList(c_fun) fnctx;
  scope *outermost;
};
scope *scope_inferior(superscope *b, scope *s);
void scope_destroy(superscope *b, scope *s);
u32 scope_add_value(superscope *env, scope *s, typed_value v);
Type *ss_getType(superscope *ss, Type t);
TypeTree *ss_getTypeTree(superscope *ss, TypeTree t);
typed_value *scope_find(superscope *env, scope *s, u32 idx);
scope *scope_copy(superscope *ss, scope *s);
static inline void scope_bind(superscope *env, scope *s, u32 idx, fptr name);
#define T(...) ((Type){.t = __VA_ARGS__})
#define TV(...) ((typed_value){__VA_ARGS__})

// lowers an ast and gives you the resulting value's index. all instructions go into scope s
u32 lower /*                      */ (superscope *ss, scope *s, ast *a);
// interprets a scope and yields the result
typed_value interpret /*          */ (superscope *ss, scope *s);
// interprets a scope with a return slot; yields the value written by ir_ret, or falls
// back to the scope's last value if the body never returns
typed_value interpret_sig /*      */ (superscope *ss, scope *s, typed_value *ret);
// casts the value at c.l to the integer type of reg(c.d), storing the result at c.d
typed_value temporary_interpret /**/ (superscope *ss, scope *s, ast *a) {
  let work = scope_inferior(ss, s);
  defer { scope_destroy(ss, work); };
  lower(ss, work, a);
  return interpret(ss, work);
}

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

typePrinter(enum_op) {
  switch_exp(
      in,
      (enop_call, (PUTS("call"), 0)),
      (enop_dot, (PUTS("dot"), 0)),
      (enop_colon, (PUTS("colon"), 0)),
      (enop_mul, (PUTS("mul"), 0)),
      (enop_div, (PUTS("div"), 0)),
      (enop_mod, (PUTS("mod"), 0)),
      (enop_plus, (PUTS("plus"), 0)),
      (enop_minus, (PUTS("minus"), 0)),
      (enop_oand, (PUTS("oand"), 0)),
      (enop_oor, (PUTS("oor"), 0)),
      (enop_oband, (PUTS("oband"), 0)),
      (enop_obor, (PUTS("obor"), 0)),
      (enop_shr, (PUTS("shr"), 0)),
      (enop_shl, (PUTS("shl"), 0)),
      (enop_mor, (PUTS("mor"), 0)),
      (enop_les, (PUTS("les"), 0)),
      (enop_moq, (PUTS("moq"), 0)),
      (enop_leq, (PUTS("leq"), 0)),
      (enop_eq, (PUTS("eq"), 0)),
      (enop_neq, (PUTS("neq"), 0)),
      (enop_pipe, (PUTS("pipe"), 0)),
      (enop_set, (PUTS("set"), 0)),
      (enop_iexpand, (PUTS("iexpand"), 0)),
      (default, (PUTS("unknown!!!"), 0)),
  );
}

[[gnu::const]] typed_value TV_bigint(bigint b) { return TV(T(tu_of(c_int, {})), b); }
[[gnu::const]] typed_value TV_type(Type *t) { return TV(T(tu_of(c_type, {})), t); }
[[gnu::const]] typed_value TV_ast(ast *ats) { return TV(T(tu_of(c_ast, {})), ats); }
[[gnu::const]] typed_value TV_any() { return TV(T(tu_of(c_any, {}))); }

static inline enum_op op_as_enum(u32 opn) {
  return opn == opNumbers.call      ? enop_call
         : opn == opNumbers.dot     ? enop_dot
         : opn == opNumbers.colon   ? enop_colon
         : opn == opNumbers.mul     ? enop_mul
         : opn == opNumbers.div     ? enop_div
         : opn == opNumbers.mod     ? enop_mod
         : opn == opNumbers.plus    ? enop_plus
         : opn == opNumbers.minus   ? enop_minus
         : opn == opNumbers.oand    ? enop_oand
         : opn == opNumbers.oor     ? enop_oor
         : opn == opNumbers.oband   ? enop_oband
         : opn == opNumbers.obor    ? enop_obor
         : opn == opNumbers.shr     ? enop_shr
         : opn == opNumbers.shl     ? enop_shl
         : opn == opNumbers.mor     ? enop_mor
         : opn == opNumbers.les     ? enop_les
         : opn == opNumbers.moq     ? enop_moq
         : opn == opNumbers.leq     ? enop_leq
         : opn == opNumbers.eq      ? enop_eq
         : opn == opNumbers.neq     ? enop_neq
         : opn == opNumbers.pipe    ? enop_pipe
         : opn == opNumbers.set     ? enop_set
         : opn == opNumbers.iexpand ? enop_iexpand
                                    : (assertMessage(false, "Unknown operator %x", opn), 0);
}

static inline bool Type_isint(Type t) {
  return !t.ptr_level &&
         tu_match_exp(
             t.t,
             (c_int, _, true),
             (r_uint, _, true),
             (r_sint, _, true),
             (r_tag, tg, Type_isint((Type){.t = *tg.t})),
             (default, false),
         );
}

static inline Type value_asType_loc(typed_value t, u8 *location) {
  if (!tu_is(c_type, t.t.t)) error_print(location, "tried to get type from {Type}", t.t);
  if (!t.v.value) error_print(location, "use of unknown type");
  return *(Type *)t.v.value;
}
static inline typed_value Type_asValue(superscope *s, Type t) { return TV_type(ss_getType(s, t)); }
static inline fptr string_escape(allocfn allocator, fptr in) {
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
        default: assertMessage(false, "unknown escape %c%c", in.ptr[i], in.ptr[i + 1]);
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

static inline u32 ss_compileInt(superscope *b, scope *s, fptr in) {
  let front = slice_split(in, (0, 2))[0];
  bigint result = nullptr;
  if (fptr_eq(front, "0x"))
    result = BInt.from.fptr(b->alloc, 16, slice_split(in, (2, -1))[0]);
  else if (fptr_eq(front, "0x"))
    result = BInt.from.fptr(b->alloc, 2, slice_split(in, (2, -1))[0]);
  else result = BInt.from.fptr(b->alloc, 10, in);
  return scope_add_value(b, s, TV_bigint(result));
}

static inline u32 ss_compileString(superscope *b, scope *s, fptr in) {
  assertMessage(in.len > 1 && in.ptr[0] == '"' && in.ptr[in.len - 1] == '"');
  in = string_escape(b->alloc, in);
  let t = T(tu_of(r_arr, {ss_getTypeTree(b, (TypeTree)tu_of(r_sint, 8)), in.len}));
  t.iscomptime = true;
  return scope_add_value(b, s, TV(*ss_getType(b, t), in.ptr));
}
static inline bigint int_as_bigint(allocfn allocator, typed_value t) {
  assertMessage(!t.t.ptr_level);
  assertMessage(!t.t.isref, "todo");
  assertMessage(t.v.value, "tried to convert unknown value");
  return tu_match_exp(
      t.t.t,
      (c_int, _, BInt.copy(allocator, t.v.c_int)),
      (r_sint, si, BInt.from.bits(allocator, t.v.value, si, 1)),
      (r_uint, si, BInt.from.bits(allocator, t.v.value, si, 0)),
      (r_bool, _, BInt.from.i64(allocator, !!*(u8 *)t.v.value)),
      (r_tag, tg, int_as_bigint(allocator, TV(T(*tg.t), t.v))),
      (default,
       (({
          if (Type_isint(t.t))
            assertMessage(
                false,
                "unhandled integer case , %s",
                snprint(stdAlloc, "{Type}", t.t).ptr
            );
          assertMessage(
              false,
              "expeceted integer, found %s",
              snprint(stdAlloc, "{typed_value}", t).ptr
          );
        }),
        (bigint) nullptr)),
  );
}
static inline void bigint_into_int(superscope *ss, typed_value *t, bigint b) {
  assertMessage(t->v.value, "cannot convert unknown value");
  tu_match_void(
      t->t.t,
      (c_int, _, {
        let o = &t->v.c_int;
        msList_deInit(ss->alloc, *o);
        *o = BInt.copy(ss->alloc, b);
      }),
      (r_sint, si, {
        let allocated_len = Type_sizeof(t->t);
        memcpy(t->v.value, b, allocated_len);
      }),
      (r_uint, si, {
        let allocated_len = Type_sizeof(t->t);
        memcpy(t->v.value, b, allocated_len);
      }),
      (r_bool, _, { *(u8 *)t->v.value = !!BInt.cmp(b, nullptr); }),
      (r_tag, tg, {
        let temp = (typed_value){.t = T(*tg.t), .v = t->v};
        return bigint_into_int(ss, &temp, b);
      }),
      (default, {
        if (Type_isint(t->t))
          assertMessage(
              false,
              "unhandled integer case , %s",
              snprint(stdAlloc, "{Type}", t->t).ptr
          );
        assertMessage(false, "should not be called with a non-int");
      }),
  );
}

static inline slice(astptr) builtin_arg_list(ast **args) {
  iflet_tu (let block, astPBlock, **args)
    return slice_stat(slice(astptr), *sentList_vla(block.stmts));
  return (slice(astptr)){1, args};
}
static inline fptr id_from_ast(superscope *ss, scope *s, ast *name) {
  iflet_tu (let id, astId, *name) return id;
  let tv = temporary_interpret(ss, s, name);
  if (!tu_is(c_ast, tv.t.t) || !tv.v.c_ast || !tu_is(astId, *tv.v.c_ast))
    error_print(name, "expected ast identifier");
  return tu_catch(astId, *tv.v.c_ast);
}

constexpr typed_value void_value = TV(T(tu_of(r_void, {})));

// printers
typePrinter(typed_value) {
  PUTS("(tv ");
  USETYPEPRINTER(Type, in.t), PUTS(",");
  tu_match_void(
      in.t.t,
      (c_int, _, in.v.c_int ? USENAMEDPRINTER("bigint", in.v.c_int) : PUTS("??");),
      (c_ast, _, in.v.c_ast ? USENAMEDPRINTER("astTag", in.v.c_ast->tag) : PUTS("??");),
      (c_type, _, in.v.value ? USETYPEPRINTER(Type, *(Type *)in.v.value) : PUTS("??");),
      (c_fun, _, in.v.value ? PUTS("("), USETYPEPRINTER(u32, in.v.c_fun.fn), USETYPEPRINTER(u32, in.v.c_fun.spec), PUTS(")") : PUTS("??");),
      (default, {
        if (in.v.value)
          USETYPEPRINTER(fptr, ((fptr){Type_sizeof(in.t), in.v.value}));
        else PUTS("(nil)");
      })
  );
  PUTS(")");
}

TYPEPRINTERDCL(scope);
typePrinter(scopeptr) { in ? USETYPEPRINTER(scope, *in) : PUTS("(null)"); };
typePrinter(scope) {
  PUTS("(scope,");
  PUTS("(instructions,");
  foreach (let i, span(in.instructions, msList_len(in.instructions))) {
    tu_match_void(
        *i,
        (ir_call, call, {
          PUTS("(call ");
          USETYPEPRINTER(u32, call.l.fn), PUTS(" ");
          USETYPEPRINTER(u32, call.l.spec);
          PUTS(" (");
          foreach (let c, span(call.args.ptr, call.args.len))
            USETYPEPRINTER(u32, *c), c == call.args.ptr ? PUTS(" ") : 0;
          PUTS("),");
          USETYPEPRINTER(u32, call.d);
          PUTS(")");
        }),
        (ir_array, f, {
          PUTS("(arr ");
          USETYPEPRINTER(u32, f.l), PUTS(" ");
          USETYPEPRINTER(u32, f.r), PUTS(" ");
          PUTS(" -> "), USETYPEPRINTER(u32, f.d);
          PUTS(")");
        }),
        (ir_field, f, {
          PUTS("(dot ");
          USETYPEPRINTER(u32, f.l), PUTS(" ");
          USETYPEPRINTER(u32, f.i), PUTS(" ");
          PUTS(" -> "), USETYPEPRINTER(u32, f.d);
          PUTS(")");
        }),
        (ir_rawop, op, {
          PUTS("(rawop ");
          USETYPEPRINTER(u32, op.l), PUTS(" ");
          USETYPEPRINTER(enum_op, op_as_enum(op.o)), PUTS(" ");
          USETYPEPRINTER(u32, op.r);
          PUTS(" -> "), USETYPEPRINTER(u32, op.d);
          PUTS(")");
        }),
        (ir_farg, op, {
          PUTS("(rawop ");
          USETYPEPRINTER(u32, op.l), PUTS(",");
          PUTS(" -> "), USETYPEPRINTER(u32, op.d);
          PUTS(")");
        }),
        (ir_getref, iv, {
          PUTS("(getref ");
          USETYPEPRINTER(u32, iv.l), PUTS(" -> "), USETYPEPRINTER(u32, iv.d);
          PUTS(")");
        }),
        (ir_getptr, iv, {
          PUTS("(getptr ");
          USETYPEPRINTER(u32, iv.l), PUTS(" -> "), USETYPEPRINTER(u32, iv.d);
          PUTS(")");
        }),
        (ir_putptr, iv, {
          PUTS("(putptr ");
          USETYPEPRINTER(u32, iv.l), PUTS(" -> "), USETYPEPRINTER(u32, iv.d);
          PUTS(")");
        }),
        (ir_cast, cv, {
          PUTS("(cast ");
          USETYPEPRINTER(u32, cv.l), PUTS(" -> "), USETYPEPRINTER(u32, cv.d);
          PUTS(")");
        }),
        (ir_set, sv, {
          PUTS("(set ");
          USETYPEPRINTER(u32, sv.l), PUTS(" -> "), USETYPEPRINTER(u32, sv.d);
          PUTS(")");
        }),
        (ir_if, ifv, {
          PUTS("(if,");
          USETYPEPRINTER(scopeptr, ifv.cnd), PUTS(",");
          USETYPEPRINTER(scopeptr, ifv.t), PUTS(",");
          USETYPEPRINTER(scopeptr, ifv.f);
          PUTS(")");
        }),
        (ir_while, wv, {
          PUTS("(while,");
          USETYPEPRINTER(scopeptr, wv.cnd), PUTS(",");
          USETYPEPRINTER(scopeptr, wv.body);
          PUTS(")");
        }),
        (ir_block, block, PUTS("(block "), USETYPEPRINTER(scopeptr, block), PUTS(")")),
        (ir_ret, v, PUTS("(return "), USETYPEPRINTER(u32, v), PUTS(")")),
        (ir_break, _, PUTS("(break)")),
        (ir_cont, _, PUTS("(continue)")),
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
};

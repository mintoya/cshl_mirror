#pragma once
#include "scope.h"
#include "wheels/macros.h"
#include "wheels/print/print_pre.h"
#include <string.h>
#include <wheels/bigint.h>

typedef ast *astptr;
sliceDef(astptr);

#define T(...) ((Type){.t = __VA_ARGS__})

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
  switch (in) {
      // clang-format off
    case enop_call: PUTS("call"); break;
    case enop_dot: PUTS("dot"); break;
    case enop_colon: PUTS("colon"); break;
    case enop_mul: PUTS("mul"); break;
    case enop_div: PUTS("div"); break;
    case enop_mod: PUTS("mod"); break;
    case enop_plus: PUTS("plus"); break;
    case enop_minus: PUTS("minus"); break;
    case enop_oand: PUTS("oand"); break;
    case enop_oor: PUTS("oor"); break;
    case enop_oband: PUTS("oband"); break;
    case enop_obor: PUTS("obor"); break;
    case enop_shr: PUTS("shr"); break;
    case enop_shl: PUTS("shl"); break;
    case enop_mor: PUTS("mor"); break;
    case enop_les: PUTS("les"); break;
    case enop_moq: PUTS("moq"); break;
    case enop_leq: PUTS("leq"); break;
    case enop_eq: PUTS("eq"); break;
    case enop_neq: PUTS("neq"); break;
    case enop_pipe: PUTS("pipe"); break;
    case enop_set: PUTS("set"); break;
    case enop_iexpand: PUTS("iexpand"); break;
    default: PUTS("unknown!!!"); break;
      // clang-format on
  }
}

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

static inline bool Type_is_comptime(Type t) { return t.iscomptime || tu_is(c_int, t.t) || tu_is(c_type, t.t) || tu_is(c_ast, t.t); }

static inline bool Type_isint(Type t) {
  return !t.ptr_level &&
         tu_match_exp(
             t.t,
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
static inline typed_value Type_asValue(superscope *s, Type t) {
  return (typed_value){T(tu_of(c_type, {})), ss_getType(s, t)};
}
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
  if (fptr_eq(front, "0x") || fptr_eq(front, "0b"))
    result = BInt.from.fptr(b->alloc, fptr_eq(front, "0x") ? 16 : 2, slice_split(in, (2, -1))[0]);
  else result = BInt.from.fptr(b->alloc, 10, in);
  assertMessage(result);
  return scope_add_value(b, s, (typed_value){T(tu_of(c_int, {})), result});
}

static inline u32 ss_compileString(superscope *b, scope *s, fptr in) {
  assertMessage(in.len > 1 && in.ptr[0] == '"' && in.ptr[in.len - 1] == '"');
  in = string_escape(b->alloc, in);
  return scope_add_value(
      b,
      s,
      (typed_value){
          *ss_getType(b, T(tu_of(r_arr, {ss_getTypeTree(b, (TypeTree)tu_of(r_sint, 8)), in.len}))),
          in.ptr
      }
  );
}
typed_value TV_strip_ref(typed_value);
static inline bigint int_as_bigint(superscope *ss, typed_value t) {
  t = TV_strip_ref(t);
  return tu_match_exp(
      t.t.t,
      (c_int, _, BInt.copy(ss->alloc, t.v.c_int)),
      (r_sint, si, BInt.from.bits(ss->alloc, t.v.value, si, 1)),
      (r_uint, si, BInt.from.bits(ss->alloc, t.v.value, si, 0)),
      (r_tag, tg, int_as_bigint(ss, (typed_value){.t = T(*tg.t), .v = t.v})),
      (default,
       (({
          if (Type_isint(t.t))
            assertMessage(
                false,
                "unhandled integer case , %s",
                snprint(stdAlloc, "{Type}", t.t).ptr
            );
          assertMessage(false, "should not be called with a non-int");
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

typed_value temporary_interpret_ast(superscope *ss, scope *s, ast *a);
static inline typed_value interpret_op(superscope *ss, scope *s, ir_rawop c);
static inline typed_value interpret_call(superscope *ss, scope *s, u8 *location, ir_call c);

typedef struct {
  slice(astptr) _;        // args decls + return + body
  slice(astptr) captures; // closure capture exprs (closures only)
  moxmap(sentList_t(Type), u32) specs;
} function;
static inline i8 cmpstl_type(const void *a, const void *b) {
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
static inline usize function_capture_count(function f) { return f.captures.len; }

// defined in ir/lower.h; static so every TU including the headers gets its own copy
u32 lower(superscope *, scope *, astptr);

static inline u32 function_trySpec(superscope *ss, scope *s, u8 *loc, function *fn, sentList_t(Type) typeList) { TODO(); }

// ---- call site helpers ----
static inline slice(astptr) builtin_arg_list(ast **args) {
  iflet_tu (let block, astPBlock, **args)
    return slice_stat(slice(astptr), *sentList_vla(block.stmts));
  return (slice(astptr)){1, args};
}
static inline fptr id_from_ast(superscope *ss, scope *s, ast *name) {
  iflet_tu (let id, astId, *name) return id;
  let tv = temporary_interpret_ast(ss, s, name);
  if (!tu_is(c_ast, tv.t.t) || !tv.v.c_ast || !tu_is(astId, *tv.v.c_ast))
    error_print(name, "expected ast identifier");
  return tu_catch(astId, *tv.v.c_ast);
}

static const typed_value void_value = (typed_value){T(tu_of(r_void, {}))};

// ---- printers (debugging) ----
typePrinter(typed_value) {
  PUTS("(tv ");
  USETYPEPRINTER(Type, in.t);
  PUTS(",");
  tu_match_void(
      in.t.t,
      (c_int, _, in.v.c_int ? USENAMEDPRINTER("bigint", in.v.c_int) : PUTS("??");),
      (c_ast, _, if (in.v.c_ast) USENAMEDPRINTER("astTag", in.v.c_ast->tag);
       else PUTS("??");),
      (c_type, _, in.v.value ? USETYPEPRINTER(Type, *(Type *)in.v.value) : PUTS("??");),
      (default, {
        if (in.v.value)
          USETYPEPRINTER(fptr, ((fptr){Type_sizeof(in.t), in.v.value}));
        else PUTS("(nil)");
      })
  );
  PUTS(")");
}

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
        (ir_rawop, op, {
          PUTS("(ir_rawop ");
          USETYPEPRINTER(u32, op.l);
          PUTS(",");
          USETYPEPRINTER(enum_op, op_as_enum(op.o));
          PUTS(",");
          USETYPEPRINTER(u32, op.r);
          PUTS(" -> ");
          USETYPEPRINTER(u32, op.d);
          PUTS(")");
        }),
        (ir_getref, iv, {
          PUTS("(ir_getref ");
          USETYPEPRINTER(u32, iv.l), PUTS(" -> "), USETYPEPRINTER(u32, iv.d);
          PUTS(")");
        }),
        (ir_getptr, iv, {
          PUTS("(ir_getptr ");
          USETYPEPRINTER(u32, iv.l), PUTS(" -> "), USETYPEPRINTER(u32, iv.d);
          PUTS(")");
        }),
        (ir_putptr, iv, {
          PUTS("(ir_putptr ");
          USETYPEPRINTER(u32, iv.l), PUTS(" -> "), USETYPEPRINTER(u32, iv.d);
          PUTS(")");
        }),
        (ir_field, iv, {
          PUTS("(ir_field ");
          USETYPEPRINTER(u32, iv.base);
          PUTS(".");
          USETYPEPRINTER(u32, iv.member);
          PUTS(" -> ");
          USETYPEPRINTER(u32, iv.d);
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
        (ir_while, wv, {
          PUTS("(ir_while ");
          if (wv.cnd) USETYPEPRINTER(scope, *wv.cnd);
          else PUTS("(null)");
          PUTS(",");
          if (wv.body) USETYPEPRINTER(scope, *wv.body);
          else PUTS("(null)");
          PUTS(")");
        }),
        (ir_block, block, {
          PUTS("(ir_block ");
          USETYPEPRINTER(scope, *block);
          PUTS(")");
        }),
        (ir_ret, v, {
          PUTS("(ir_ret ");
          USETYPEPRINTER(u32, v);
          PUTS(")");
        }),
        (ir_break, _, PUTS("(ir_break )")),
        (ir_cont, _, PUTS("(ir_cont )")),
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

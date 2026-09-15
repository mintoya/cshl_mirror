#pragma once
#include "../type.h"
#include "ir.h"
#include "wheels/errable.h"
#include "wheels/sList.h"
#include <string.h>
#include <wheels/bigint.h>
#include <wheels/macros.h>
#include <wheels/print/print_pre.h>

typedef struct {
  astidx start;
  u32 len;
} astidx_span;
typedef struct superscope superscope;
typedef struct scope scope;
struct scope {
  scope *parent;
  msList(ir) instructions;
  msList(typed_value) values;
  moxmap(fptr, u32) named;
  u32 start;
  bool rootable;
};
sliceDef(scope);

typedef struct {
  Type ret;
  scope *spec;
  typed_value *key;
  usize key_len;
} function_instance;

typedef struct function {
  astidx_span params;
  astidx retT;
  astidx body;
  scope *defscope;
  msList(function_instance) instances;
  moxmap(sliceDef(typed_value), u32) mp;
} function;
struct superscope {
  allocfn alloc;

  struct {
    astlist asts;
    poslist plist;
    filelist flist;
  };

  typelist tlist;
  typestructlist sitems;
  typebitflist bitems;

  moxmap(Type, typeidx) typeindex;
  typeidx t_void, t_any, t_int, t_ast, t_type;
  mxmap(void *, usize) pointers;
  msList(typeidx) thisstack;
  moxmap(fptr, scope *) type_info;
  msList(function) functions;
  Type *current_ret;
  scope *outermost;
  u32 foldempty;
};
static inline typeenv ss_typeenv(superscope *ss) {
  return (typeenv){
      .tlist = ss->tlist,
      .sitems = ss->sitems,
      .bitems = ss->bitems,
      .plist = ss->plist,
      .flist = ss->flist,
  };
}
static inline TypeTree_full ss_ttf(superscope *ss, TypeTree t) { return (TypeTree_full){t, ss_typeenv(ss)}; }
static inline Type_full ss_tf(superscope *ss, Type t) { return (Type_full){t, ss_typeenv(ss)}; }

static inline cmpres Type_cmp_hash_ss(void *env, const void *a, const void *b) {
  return Type_cmp(ss_typeenv((superscope *)env), *(Type *)a, *(Type *)b);
}
static inline cmpres TypeTree_cmp_hash_ss(void *env, const void *a, const void *b) {
  return TypeTree_cmp(ss_typeenv((superscope *)env), *(TypeTree *)a, *(TypeTree *)b);
}
static inline typeidx ss_type_reserve(superscope *ss, usize count) {
  foreach (let i, range(0, count + 1))
    sglist_push(ss->tlist, {});
  return ss->tlist.len - count;
}
static inline typeidx ss_member_reserve(superscope *ss, usize count) {
  typeidx start = (typeidx)ss->sitems.len + 1;
  foreach (let i, range(0, count + 1))
    sglist_push(ss->sitems, (r_struct_item){});
  return start;
}
static inline ast ast_at(astlist al, astidx idx) {
  return (!idx || idx > al.len) ? (ast)tu_of(astNull, {}) : sglist_get(al, idx - 1);
}
static inline astidx ast_child(astlist al, astidx slot) { return tu_is(astNull, ast_at(al, slot)) ? 0 : slot; }
static inline fptr ast_text(superscope *ss, srcpos p) {
  return fp_from_srcpos((srcpos_full){.pidx = p, .plist = ss->plist, .flist = ss->flist});
}
static inline usize ast_loc(superscope *ss, astidx idx) {
  let p = srcpos_ast(ast_at(ss->asts, idx), ss->asts);
  return (!p || p > ss->plist.len) ? (usize)-1 : sglist_get(ss->plist, p - 1).offset;
}
static inline srcpos ss_srcpos_of_text(superscope *ss, fptr text) {
  sglist_push(ss->flist, (fileItem){.filename = nullptr, .filecontent = text, .fileptr = nullptr});
  u32 fidx = (u32)ss->flist.len; // 1-based
  sglist_push(ss->plist, (posItem){.fidx = fidx, .offset = 0, .length = (u32)text.len});
  return (srcpos)ss->plist.len;
}
static inline astidx ast_make_id(superscope *ss, fptr text) {
  sglist_push(ss->asts, (ast)tu_of(astId, ss_srcpos_of_text(ss, text)));
  return (astidx)ss->asts.len;
}
static inline astidx ast_make_empty(superscope *ss) {
  if (ss->foldempty) return ss->foldempty;
  else return ss->foldempty = (sglist_push(ss->asts, tu_of(astEmpty, 0)), ss->asts.len);
}

scope *scope_inferior(superscope *b, scope *s);
scope *scope_find_rootable(superscope *b, scope *s);
void scope_destroy(superscope *b, scope *s);
u32 scope_add_value(superscope *env, scope *s, typed_value v);
typeidx ss_type_intern(superscope *ss, Type t);
static inline Type *ss_getType(superscope *ss, Type t) {
  return &sglist_get(ss->tlist, ss_type_intern(ss, t) - 1);
}
static inline Type_full ss_typeF(superscope *ss, Type t) { return (Type_full){t, ss_typeenv(ss)}; }
static inline Type ss_type(superscope *ss, typeidx idx) { return type_at(ss_typeenv(ss), idx); }
static inline typed_value TV_of(superscope *ss, Type t) { return (typed_value){.t = ss_type_intern(ss, t)}; }
static inline typed_value TV_with(superscope *ss, Type t, value_store vs) {
  return TV(ss_type_intern(ss, t), vs);
}
static inline typed_value TV_void /*  */ (superscope *ss) { return (typed_value){.t = ss->t_void}; }
static inline typed_value TV_any /*   */ (superscope *ss) { return (typed_value){.t = ss->t_any}; }
static inline typed_value TV_bigint /**/ (superscope *ss, bigint b) { return (typed_value){.t = ss->t_int, .v = {.c_int = b}}; }
static inline typed_value TV_ast /*   */ (superscope *ss, astidx a) { return (typed_value){.t = ss->t_ast, .v = {.c_ast = a}}; }
static inline typed_value TV_type /*  */ (superscope *ss, typeidx t) { return (typed_value){.t = ss->t_type, .v = {.c_type = t}}; }
static inline Type ss_ast_type(superscope *ss) { return ss_type(ss, ss->t_ast); }
typed_value *scope_find(superscope *env, scope *s, u32 idx);
static inline void scope_bind errs(superscope *env, scope *s, u32 idx, fptr name);

u32 lower errs /*                      */ (superscope *ss, scope *s, astidx a);
typed_value interpret errs /*          */ (superscope *ss, scope *s);
bool try_interpret errs /*             */ (superscope *ss, scope *s, ir inst);
static u32 lower_function_call_values errs(superscope *ss, scope *s, u32 fn, slice(u32) vals);

typed_value temporary_interpret errs(superscope *ss, scope *s, astidx a) {
  let work = P$(scope_inferior(ss, s), ($->rootable = false, $));
  defer { scope_destroy(ss, work); };
  try_err(lower, (ss, work, a), (typed_value){});
  let result = try_err(interpret, (ss, work), (typed_value){});
  if (tu_is(c_int, ss_type(ss, result.t).t) && result.v.c_int)
    result.v.c_int = BInt.copy(ss->alloc, result.v.c_int);
  return result;
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
  enop_as,
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
      (enop_as, (PUTS("as"), 0)),
      (default, (PUTS("unknown!!!"), 0)),
  );
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
         : opn == opNumbers.as      ? enop_as
                                    : (assertMessage(false, "Unknown operator %x", opn), 0);
}
static inline u32 enum_as_op(enum_op en) {
  return switch_exp(
      en,
      (enop_dot /*    */, opNumbers.dot),
      (enop_colon /*  */, opNumbers.colon),
      (enop_mul /*    */, opNumbers.mul),
      (enop_div /*    */, opNumbers.div),
      (enop_mod /*    */, opNumbers.mod),
      (enop_plus /*   */, opNumbers.plus),
      (enop_minus /*  */, opNumbers.minus),
      (enop_oand /*   */, opNumbers.oand),
      (enop_oor /*    */, opNumbers.oor),
      (enop_oband /*  */, opNumbers.oband),
      (enop_obor /*   */, opNumbers.obor),
      (enop_shr /*    */, opNumbers.shr),
      (enop_shl /*    */, opNumbers.shl),
      (enop_mor /*    */, opNumbers.mor),
      (enop_les /*    */, opNumbers.les),
      (enop_moq /*    */, opNumbers.moq),
      (enop_leq /*    */, opNumbers.leq),
      (enop_eq /*     */, opNumbers.eq),
      (enop_neq /*    */, opNumbers.neq),
      (enop_pipe /*   */, opNumbers.pipe),
      (enop_set /*    */, opNumbers.set),
      (enop_iexpand /**/, opNumbers.iexpand),
      (enop_as /*     */, opNumbers.as),
      (enop_call /*   */, opNumbers.call)
  );
}

static inline bool Type_isint(Type t, typeenv e) {
  return !t.ptr_level &&
         tu_match_exp(
             t.t,
             (c_int, _, true),
             (r_uint, _, true),
             (r_sint, _, true),
             (r_tag, tg, Type_isint(type_at(e, tg.t), e)),
             (default, false),
         );
}

static inline Type value_asType errs(superscope *ss, typed_value t) {
  if (!tu_is(c_type, ss_type(ss, t.t).t))
    return_err("TYPE_IS_NOT_VALUE", T());
  if (!t.v.c_type) return_err("UNKNOWN_TYPE", T());
  return type_at(ss_typeenv(ss), t.v.c_type);
}
static inline typed_value Type_asValue(superscope *s, Type t) { return TV_type(s, ss_type_intern(s, t)); }
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
  defer { msList_deInit(b->alloc, result); };
  if (fptr_eq(front, "0x"))
    result = BInt.from.fptr(b->alloc, 16, slice_split(in, (2, -1))[0]);
  else if (fptr_eq(front, "0b"))
    result = BInt.from.fptr(b->alloc, 2, slice_split(in, (2, -1))[0]);
  else result = BInt.from.fptr(b->alloc, 10, in);
  return scope_add_value(b, s, TV_bigint(b, result));
}
static inline u32 ss_compileString(superscope *b, scope *s, fptr in) {
  assertMessage(in.len > 1 && in.ptr[0] == '"' && in.ptr[in.len - 1] == '"');
  in = string_escape(b->alloc, in);
  let t = T(tu_of(r_arr, {ss_type_intern(b, T(TypeTree_sint(8))), in.len}));
  t.iscomptime = true;
  return scope_add_value(b, s, TV_with(b, t, VS(in.ptr)));
}
static inline bigint int_as_bigint(allocfn allocator, typed_value t, typeenv e) {
  let tt = type_at(e, t.t);
  assertMessage(!tt.ptr_level);
  assertMessage(!tt.isref, "todo");
  assertMessage(t.v.value, "tried to convert unknown value");
  return tu_match_exp(
      tt.t,
      (c_int, _, BInt.copy(allocator, t.v.c_int)),
      (r_sint, si, BInt.from.bits(allocator, t.v.value, si.bits, 1)),
      (r_uint, si, BInt.from.bits(allocator, t.v.value, si.bits, 0)),
      (r_bool, _, BInt.from.i64(allocator, !!*(u8 *)t.v.value)),
      // the tag's child index is already interned, so no lookup is needed here
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

static inline void bigint_into_slot(allocfn alloc, void *slot, usize len, bigint b) {
  let limbs = (len + sizeof(bigint_unit) - 1) / sizeof(bigint_unit);
  if (b && msList_len(b) < limbs) bigint_expand(alloc, &b, limbs);
  memcpy(slot, b, len);
}
static inline void bigint_into_int(superscope *ss, allocfn alloc, typed_value *t, bigint b) {
  let e = ss_typeenv(ss);
  let tt = type_at(e, t->t);
  tu_match_void(
      tt.t,
      (c_int, _, {
        let o = &t->v.c_int;
        msList_deInit(alloc, *o);
        *o = BInt.copy(alloc, b);
      }),
      (r_sint, si, bigint_into_slot(alloc, t->v.value, Type_sizeof(tt, e), b)),
      (r_uint, si, bigint_into_slot(alloc, t->v.value, Type_sizeof(tt, e), b)),
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
static inline astidx_span ast_block_children(astlist al, astidx args) {
  return tu_match_exp(
      ast_at(al, args),
      (astABlock, b, ({
         astidx start = b.start, end = start;
         while (!tu_is(astNull, ast_at(al, end)))
           end++;
         (astidx_span){start, end - start};
       })),
      (astBBlock, b, ({
         astidx start = b.start, end = start;
         while (!tu_is(astNull, ast_at(al, end)))
           end++;
         (astidx_span){start, end - start};
       })),
      (astPBlock, b, ({
         astidx start = b.start, end = start;
         while (!tu_is(astNull, ast_at(al, end)))
           end++;
         (astidx_span){start, end - start};
       })),
      (default, (assertMessage(false, "unreachable"), (astidx_span){})),
  );
}
static inline astidx_span builtin_arg_list(astlist al, astidx args) {
  iflet_tu (let block, astPBlock, ast_at(al, args)) {
    astidx start = block.start, end = start;
    while (!tu_is(astNull, ast_at(al, end)))
      end++;
    return (astidx_span){start, end - start};
  }
  return (astidx_span){args, 1};
}
static inline astidx_full ss_ast_full(superscope *ss, astidx idx) {
  return (astidx_full){.idx = idx, .alist = ss->asts, .plist = ss->plist, .flist = ss->flist};
}

static inline fptr id_from_ast errs(superscope *ss, scope *s, astidx name) {
  iflet_tu (let id, astId, ast_at(ss->asts, name)) return ast_text(ss, id);
  let tv = try_err(temporary_interpret, (ss, s, name), nullFptr);
  if (!tu_is(c_ast, ss_type(ss, tv.t).t) || !tv.v.c_ast) return_err("ID_REQUIRES_ASTID", nullFptr);
  let inner = ast_at(ss->asts, tv.v.c_ast);
  if (!tu_is(astId, inner)) return_err("ID_REQUIRES_ASTID", nullFptr);
  return ast_text(ss, tu_catch(astId, inner));
}
static inline srcpos id_srcpos(superscope *ss, astidx name, fptr text) {
  iflet_tu (let id, astId, ast_at(ss->asts, name)) return id;
  return ss_srcpos_of_text(ss, text);
}

typePrinter(typed_value_full) {
  let t = type_at(in.env, in.v.t);
  PUTS("(tv ");
  USETYPEPRINTER(Type_full, ((Type_full){t, in.env}));
  PUTS(",");
  tu_match_void(
      t.t,
      (c_int, _, {
        if (in.v.v.c_int) {
          USENAMEDPRINTER("bigint", in.v.v.c_int);
        } else {
          PUTS("??");
        }
      }),
      (c_ast, _, {
        if (in.v.v.c_ast) {
          PUTS("(ast ");
          USETYPEPRINTER(u32, in.v.v.c_ast);
          PUTS(")");
        } else {
          PUTS("??");
        }
      }),
      (c_type, _, {
        if (in.v.v.c_type) {
          USETYPEPRINTER(Type_full, ((Type_full){type_at(in.env, in.v.v.c_type), in.env}));
        } else {
          PUTS("??");
        }
      }),
      (c_fun, _, {
        if (in.v.v.value) {
          PUTS("(");
          USETYPEPRINTER(u32, in.v.v.c_fun.fn);
          PUTS(" ");
          USETYPEPRINTER(u32, in.v.v.c_fun.spec);
          PUTS(")");
        } else {
          PUTS("??");
        }
      }),
      (default, {
        if (in.v.v.value) {
          USETYPEPRINTER(fptr, ((fptr){Type_sizeof(t, in.env), in.v.v.value}));
        } else {
          PUTS("(nil)");
        }
      })
  );
  PUTS(")");
}
TYPEPRINTERDCL(scope_full);
typedef struct {
  scope *s;
  superscope *ss;
} scope_full;
typePrinter(scope_full) {
  let sc = *in.s;
  PUTS("(scope,");
  PUTS("(instructions,");
  foreach (let i, span(sc.instructions, msList_len(sc.instructions))) {
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
          PUTS("(farg ");
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
          USETYPEPRINTER(scope_full, ((scope_full){ifv.cnd, in.ss})), PUTS(",");
          USETYPEPRINTER(scope_full, ((scope_full){ifv.t, in.ss})), PUTS(",");
          USETYPEPRINTER(scope_full, ((scope_full){ifv.f, in.ss}));
          PUTS(")");
        }),
        (ir_while, wv, {
          PUTS("(while,");

          USETYPEPRINTER(scope_full, ((scope_full){wv.cnd, in.ss})), PUTS(",");
          USETYPEPRINTER(scope_full, ((scope_full){wv.body, in.ss}));
          PUTS(")");
        }),
        (ir_block, block, PUTS("(block "), USETYPEPRINTER(scope_full, ((scope_full){block, in.ss})), PUTS(")")),
        (ir_ret, v, PUTS("(return "), USETYPEPRINTER(u32, v), PUTS(")")),
        (ir_break, _, PUTS("(break)")),
        (ir_cont, _, PUTS("(continue)")),
    );
    PUTS(",");
  }
  PUTS("),");
  PUTS("(values,");
  foreach (let v, span(sc.values, msList_len(sc.values))) {
    if (v != sc.values)
      PUTS(",");
    PUTS("(r"),
        USETYPEPRINTER(u32, sc.start + (v - sc.values) + 1),
        USETYPEPRINTER(typed_value_full, ((typed_value_full){*v, ss_typeenv(in.ss)})),
        PUTS(")");
  }
  PUTS("),");
  PUTS("(named,");
  foreach (let v, moxmap_iter(sc.named, fptr, u32)) {
    PUTS("('"), USENAMEDPRINTER("slice(c8)", v.key), PUTS("' ");
    USETYPEPRINTER(u32, *v.val);
    PUTS(")");
    PUTS(",");
  }
  PUTS(")");
  PUTS(")");
};

#pragma once
#include "../type.h"
#include "ir.h"
sliceDef(fptr);
sliceDef(typed_value);

typedef struct {
  astidx start;
  u32 len;
} astidx_span;
typedef struct typeinfo_value {
  typed_value subject;
  enum { typeinfo_root, typeinfo_struct, typeinfo_fn, typeinfo_array } view;
} typeinfo_value;
typedef struct superscope superscope;
typedef struct scope scope;
typedef struct name_env name_env;
struct name_env {
  name_env *parent;
  moxmap(fptr, u32) bindings;
};
struct scope {
  scope *parent;
  msList(ir) instructions;
  msList(typed_value) values;
  name_env *named;
  name_env *parent_named;
  u32 start;
  struct {
    u32 fn, index;
  } instance;
};
typedef struct {
  Type ret;
  scope *spec;
} function_instance;

typedef struct function {
  astidx_span params;
  astidx retT;
  astidx body;
  scope *defscope;
  name_env *defnames;
  typeidx signature;
  msList(function_instance) instances;
  moxmap(sliceDef(typed_value), u32) mp;
} function;
struct superscope {
  allocfn alloc;

  struct { // TODO: maybe store referances
    astlist asts;
    poslist plist;
    filelist flist;
    typelist tlist;
    typestructlist sitems;
    typebitflist bitems;
  };

  moxmap(Type, typeidx) typeindex;
  mxmap(void *, usize) pointers;
  msList(typeidx) thisstack;
  moxmap(fptr, scope *) type_info;
  msList(function) functions;
  scope *outermost;

  typeidx t_void, t_any, t_int, t_bool, t_ast, t_type;
  u32 foldempty;
};
typedef struct {
  scope *s;
  superscope *ss;
} scope_full;
static inline typeenv ss_typeenv(superscope *ss) {
  return (typeenv){
      .tlist = &ss->tlist,
      .sitems = &ss->sitems,
      .bitems = &ss->bitems,
      .plist = &ss->plist,
      .flist = &ss->flist,
  };
}
static inline Type_full ss_tf(superscope *ss, Type t) { return (Type_full){t, ss_typeenv(ss)}; }

static cmpres Type_cmp_hash_ss(void *env, const void *a, const void *b) {
  return Type_cmp(ss_typeenv((superscope *)env), *(Type *)a, *(Type *)b);
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
static inline typeidx ss_bitf_reserve(superscope *ss, usize count) {
  typeidx start = (typeidx)ss->bitems.len + 1;
  foreach (let i, range(0, count + 1))
    sglist_push(ss->bitems, (r_bitf_item){});
  return start;
}
static inline ast ast_at_list(astlist al, astidx idx) {
  return (!idx || idx > al.len) ? (ast)tu_of(astNull, {}) : sglist_get(al, idx - 1);
}
static inline ast ast_at(superscope *ss, astidx idx) { return ast_at_list(ss->asts, idx); }
static inline astidx ast_child(superscope *ss, astidx slot) { return tu_is(astNull, ast_at(ss, slot)) ? 0 : slot; }
static inline fptr ast_text(superscope *ss, srcpos p) {
  return fp_from_srcpos((srcpos_full){.pidx = p, .plist = &ss->plist, .flist = &ss->flist});
}
static inline usize ast_loc(superscope *ss, astidx idx) {
  let p = srcpos_ast(ast_at(ss, idx), ss->asts);
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

u32 scope_last_item(superscope *ss, scope *s);
scope *scope_inferior(superscope *b, scope *s);
u32 scope_add_value(superscope *env, scope *s, typed_value v);
const scope *scope_owner(const scope *s, u32 idx);

typeidx ss_type_intern(superscope *ss, Type t);
static inline Type *ss_getType(superscope *ss, Type t) {
  return &sglist_get(ss->tlist, ss_type_intern(ss, t) - 1);
}
static inline Type_full ss_typeF(superscope *ss, Type t) { return (Type_full){t, ss_typeenv(ss)}; }
static inline Type ss_type(superscope *ss, typeidx idx) { return Type_at(ss_typeenv(ss), idx); }
static inline typed_value TV_of(superscope *ss, Type t) { return (typed_value){.t = ss_type_intern(ss, t)}; }
static inline typed_value TV_with(superscope *ss, Type t, value_store vs) {
  return TV(ss_type_intern(ss, t), vs);
}
static inline typed_value TV_void /*  */ (superscope *ss) { return (typed_value){.t = ss->t_void}; }
static inline typed_value TV_any /*   */ (superscope *ss) { return (typed_value){.t = ss->t_any}; }
static inline typed_value TV_bigint /**/ (superscope *ss, bigint b) { return (typed_value){.t = ss->t_int, .v = {.c_int = b}}; }
static inline typed_value TV_ast /*   */ (superscope *ss, astidx a) { return (typed_value){.t = ss->t_ast, .v = {.c_ast = a}}; }
static inline typed_value TV_type /*  */ (superscope *ss, typeidx t) { return (typed_value){.t = ss->t_type, .v = {.c_type = t}}; }
static inline typed_value TV_bool /*  */ (superscope *ss, bool v) { return (typed_value){.t = ss->t_bool, .v = VS(avalue(ss->alloc, v))}; }
static inline Type ss_ast_type(superscope *ss) { return ss_type(ss, ss->t_ast); }
typed_value *scope_find(superscope *env, const scope *s, u32 idx);
static inline void scope_bind errs(superscope *env, scope *s, u32 idx, fptr name);

u32 lower errs /*                      */ (superscope *ss, scope *s, astidx a);
typed_value interpret errs /*          */ (superscope *ss, scope *s);
static u32 lower_function_call_values errs(superscope *ss, scope *s, u32 fn, slice(u32) vals);

typed_value_full ss_tvf(superscope *ss, typed_value tv) {
  return (typed_value_full){
      .env = ss_typeenv(ss),
      .v = tv,
  };
}
typed_value temporary_interpret errs(superscope *ss, scope *s, astidx a) {
  let work = scope_inferior(ss, s);
  // let afull = ((astidx_full){a, ss->asts, ss->plist, ss->flist});
  // let sfulll = ((scope_full){s, ss});
  // println("[LOG:TMP] {astidx_full}", );
  try_err(lower, (ss, work, a), (typed_value){});
  // println("[LOG:TMP] {scope_full}", );
  let res = try_err(interpret, (ss, work), (typed_value){});
  // println("[LOG:TMP] {typed_value_full}", ss_tvf(ss, res));
  return res;
}

typedef enum enum_op {
  // The arithmetic members take their values from the runtime's cshl_int_op
  // (cshl.lib.c), which is the source of truth: a wide operation passes
  // through to the runtime with one explicit cast and no name lookup.
  enop_mul = CSHL_INT_MUL,
  enop_div = CSHL_INT_DIV,
  enop_mod = CSHL_INT_MOD,
  enop_plus = CSHL_INT_ADD,
  enop_minus = CSHL_INT_SUB,
  enop_oband = CSHL_INT_AND,
  enop_obor = CSHL_INT_OR,
  enop_bxor = CSHL_INT_XOR,
  enop_shr = CSHL_INT_SHR,
  enop_shl = CSHL_INT_SHL,
  enop_oand,
  enop_oor,
  enop_mor,
  enop_les,
  enop_moq,
  enop_leq,
  enop_eq,
  enop_neq,
  enop_call,
  enop_dot,
  enop_colon,
  enop_pipe,
  enop_set,
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
  switch_tuple(
      in,
      (enop_call, PUTS("call")),
      (enop_dot, PUTS("dot")),
      (enop_colon, PUTS("colon")),
      (enop_mul, PUTS("mul")),
      (enop_div, PUTS("div")),
      (enop_mod, PUTS("mod")),
      (enop_plus, PUTS("plus")),
      (enop_minus, PUTS("minus")),
      (enop_oand, PUTS("oand")),
      (enop_oor, PUTS("oor")),
      (enop_oband, PUTS("oband")),
      (enop_obor, PUTS("obor")),
      (enop_bxor, PUTS("bxor")),
      (enop_shr, PUTS("shr")),
      (enop_shl, PUTS("shl")),
      (enop_mor, PUTS("mor")),
      (enop_les, PUTS("les")),
      (enop_moq, PUTS("moq")),
      (enop_leq, PUTS("leq")),
      (enop_eq, PUTS("eq")),
      (enop_neq, PUTS("neq")),
      (enop_pipe, PUTS("pipe")),
      (enop_set, PUTS("set")),
      (enop_as, PUTS("as")),
      (default, PUTS("unknown!!!"))
  );
}

static inline bool enum_op_is_math(enum_op op) { return op >= enop_mul && op <= enop_neq; }

static inline enum_op op_as_enum(u32 opn) {
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
  if (opn == opNumbers.bxor) return enop_bxor;
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
  if (opn == opNumbers.as) return enop_as;
  assertMessage(false, "Unknown operator %x", opn);
}
static inline u32 enum_as_op(enum_op en) {
  return switch_exp(
      en,
      (enop_call, opNumbers.call),
      (enop_dot, opNumbers.dot),
      (enop_colon, opNumbers.colon),
      (enop_mul, opNumbers.mul),
      (enop_div, opNumbers.div),
      (enop_mod, opNumbers.mod),
      (enop_plus, opNumbers.plus),
      (enop_minus, opNumbers.minus),
      (enop_oand, opNumbers.oand),
      (enop_oor, opNumbers.oor),
      (enop_oband, opNumbers.oband),
      (enop_obor, opNumbers.obor),
      (enop_bxor, opNumbers.bxor),
      (enop_shr, opNumbers.shr),
      (enop_shl, opNumbers.shl),
      (enop_mor, opNumbers.mor),
      (enop_les, opNumbers.les),
      (enop_moq, opNumbers.moq),
      (enop_leq, opNumbers.leq),
      (enop_eq, opNumbers.eq),
      (enop_neq, opNumbers.neq),
      (enop_pipe, opNumbers.pipe),
      (enop_set, opNumbers.set),
      (enop_as, opNumbers.as),
      (default, assertMessage(false, "Unknown operator %u", en); (u32)0)
  );
  return 0;
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
// the children of a block sit consecutively in the ast list, terminated by the null node
static inline astidx_span ast_run(superscope *ss, astidx start) {
  astidx end = start;
  while (!tu_is(astNull, ast_at(ss, end)))
    end++;
  return (astidx_span){start, end - start};
}
static inline astidx_span ast_block_children(superscope *ss, astidx args) {
  return tu_match_exp(
      ast_at(ss, args),
      (astABlock, b, ast_run(ss, b.start)),
      (astBBlock, b, ast_run(ss, b.start)),
      (astPBlock, b, ast_run(ss, b.start)),
      (default, (assertMessage(false, "unreachable"), (astidx_span){})),
  );
}
static inline astidx_span builtin_arg_list(superscope *ss, astidx args) {
  iflet_tu (let block, astPBlock, ast_at(ss, args)) return ast_run(ss, block.start);
  return (astidx_span){args, 1};
}
#define builtin_args(ss, args, ...)                                          \
  ({                                                                         \
    let list = builtin_arg_list(ss, args);                                   \
    if (!EQUAL_ANY(list.len, __VA_ARGS__)) return_err("WRONG_ARG_COUNT", 0); \
    list;                                                                    \
  })
#define builtin_no_args(ss, args) \
  if (builtin_arg_list(ss, args).len) return_err("WRONG_ARG_COUNT", 0)
static inline astidx_full ss_ast_full(superscope *ss, astidx idx) {
  return (astidx_full){.idx = idx, .alist = ss->asts, .plist = ss->plist, .flist = ss->flist};
}

static inline fptr id_from_ast errs(superscope *ss, scope *s, astidx name) {
  iflet_tu (let id, astId, ast_at(ss, name)) return ast_text(ss, id);
  let tv = try_err(temporary_interpret, (ss, s, name), nullFptr);
  if (!TypeTree_is_ast(ss_type(ss, tv.t).t) || !tv.v.c_ast) return_err("ID_REQUIRES_ASTID", nullFptr);
  let inner = ast_at(ss, tv.v.c_ast);
  if (!tu_is(astId, inner)) return_err("ID_REQUIRES_ASTID", nullFptr);
  return ast_text(ss, tu_catch(astId, inner));
}
static inline srcpos id_srcpos(superscope *ss, astidx name, fptr text) {
  iflet_tu (let id, astId, ast_at(ss, name)) return id;
  return ss_srcpos_of_text(ss, text);
}

typePrinter(typed_value_full) {
  let t = Type_at(in.env, in.v.t);
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
      (c_inlineast, _, {
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
          USETYPEPRINTER(Type_full, ((Type_full){Type_at(in.env, in.v.v.c_type), in.env}));
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
typePrinter(scope_full) {
  let sc = *in.s;
  PUTS("(scope,");
  PUTS("(instructions,");
  foreach (let i, span(sc.instructions, msList_len(sc.instructions))) {
    tu_match_void(
        *i,
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
        (ir_rawdot, op, {
          PUTS("(rawdot ");
          USETYPEPRINTER(u32, op.l), PUTS(" ");
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
        (ir_ret, v, PUTS("(return "), USETYPEPRINTER(u32, v), PUTS(")")),
        (ir_break, _, PUTS("(break)")),
        (ir_cont, _, PUTS("(continue)")),
        (ir_call, c, {
          PUTS("(call ");
          USETYPEPRINTER(u32, c.l), PUTS(" ");
          USETYPEPRINTER(u32, c.r), PUTS(" "), USETYPEPRINTER(u32, c.e);
          PUTS(" -> "), USETYPEPRINTER(u32, c.d);
          PUTS(")");
        }),
        (ir_blocks, _, PUTS("(blockstart)")),
        (ir_blocke, _, PUTS("(blockend)")),
        (ir_ifs, r, {
          PUTS("(ifs ");
          USETYPEPRINTER(u32, r);
          PUTS(")");
        }),
        (ir_ifd, _, PUTS("(ifd)")),
        (ir_ife, _, PUTS("(ife)")),
        (ir_loops, _, PUTS("(loops)")),
        (ir_loope, _, PUTS("(loope)")),
        (default, assertMessage(false)),
    );
    PUTS(",");
  }
  PUTS("),");
  PUTS("(values,");
  foreach (let v, span(sc.values, msList_len(sc.values))) {
    if (v != sc.values)
      PUTS(",");
    PUTS("(r"),
        USETYPEPRINTER(u32, (v - sc.values) + 1),
        USETYPEPRINTER(typed_value_full, ((typed_value_full){*v, ss_typeenv(in.ss)})),
        PUTS(")");
  }
  PUTS("),");
  PUTS(")");
};

#pragma once
#include "common.h"
#include "scope.h"
#include "wheels/fptr.h"
#include "wheels/macros.h"

static inline Type Type_stripTag(Type t, typeenv e) {
  while (tu_is(r_tag, t.t))
    t.t = type_tree(e, tu_catch(r_tag, t.t).t);
  return t;
}

// a member of a tagged struct: where it lives, and which register it is
struct type_named {
  scope *owner;
  u32 idx;
};
static inline struct type_named ss_tag_named(superscope *ss, TypeTree t, fptr name) {
  iflet_tu (let tag, r_tag, t)
    if_decl (let info, moxmap_get(ss->type_info, type_name(ss_typeenv(ss), tag.name)))
      if_decl (let idx, moxmap_get(mList_last((*info)->named), name)) return (struct type_named){*info, *idx};
  return (struct type_named){};
}

static bool Type_isint(Type t, typeenv en) {
  return !t.ptr_level &&
         tu_match_exp(
             Type_stripTag(t, en).t,
             (r_sint, _, (bool)true),
             (r_uint, _, (bool)true),
             (c_int, _, (bool)true),
             (default, false),
         );
}
static inline u32 Type_intLike_bitcount(Type t, typeenv e) {
  return tu_match_exp(
      Type_stripTag(t, e).t,
      (r_sint, s, s.bits),
      (r_uint, s, s.bits),
      (r_bool, s, (u16)1),
      (default, (u16)0),
  );
}
static bool Type_is_integer_like(Type t, typeenv e) {
  t = Type_stripTag(t, e);
  return !t.ptr_level && (tu_is(r_bool, t.t) || Type_isint(t, e));
}

static inline Type Type_weaker(Type a, Type b, typeenv e) {
  Type as = Type_stripTag(a, e), bs = Type_stripTag(b, e);
  bool ac = Type_is_comptime(as), bc = Type_is_comptime(bs);
  Type pick = {};
  if (ac != bc) pick = ac ? bs : as;
  else if (ac) pick = as;
  else if (Type_intLike_bitcount(as, e) != Type_intLike_bitcount(bs, e))
    pick = Type_intLike_bitcount(as, e) > Type_intLike_bitcount(bs, e) ? as : bs;
  else pick = tu_is(r_uint, as.t) ? as : bs;
  pick.isref = false;
  pick.iscomptime = false;
  pick.isconst = a.isconst && b.isconst;
  pick.isvolatile = a.isvolatile && b.isvolatile;
  return pick;
}

static inline bool scope_adopt_any(superscope *env, typed_value *dv, Type t) {
  if (!tu_is(c_any, Type_stripTag(ss_type(env, dv->t), ss_typeenv(env)).t))
    return false;
  scope_set_type(env, dv, ss_type_intern(env, t));
  return true;
}
static inline bool Type_adopt_any(Type *slot, Type t) {
  if (!tu_is(c_any, slot->t)) return false;
  t.isref = false;
  *slot = t;
  return true;
}

static inline typed_value Type_asValue(superscope *s, Type t) { return TV_type(s, ss_type_intern(s, t)); }

static inline bigint int_as_bigint(allocfn allocator, typed_value t, typeenv e) {
  let tt = type_at(e, t.t);
  assertMessage(!tt.ptr_level);
  // a reference operand's data pointer is resolved by the caller, so the ref flag is immaterial here
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

static inline void bigint_into_slot(u8 *mem, usize len, bigint b) {
  foreach (let w, range(0, (lineup(len, sizeof(bigint_unit)) / sizeof(bigint_unit))))
    memcpy(mem + w * sizeof(bigint_unit), REF(bigint_get(b, w)), MIN$(len - w * sizeof(bigint_unit), sizeof(bigint_unit)));
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
      (r_sint, si, bigint_into_slot(t->v.value, Type_sizeof(tt, e), b)),
      (r_uint, si, bigint_into_slot(t->v.value, Type_sizeof(tt, e), b)),
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

static bool scope_cast_would_change(Type from, Type to, typeenv e) {
  // TODO: volatile and const?
  return !EQUAL_ANY(
      0,
      Type_cmp(e, to, T(tu_of(c_any, {}))),
      Type_cmp(e, Type_stripTag(from, e), Type_stripTag(to, e))
  );
}
static u32 scope_cast_value errs(superscope *ss, scope *s, u32 left, Type to) {
  let e = ss_typeenv(ss);
  let ot = ss_type(ss, scope_find(ss, s, left)->t);
  if (!scope_cast_would_change(ot, to, e)) return left;
  let d = scope_add_value(ss, s, TV_of(ss, to));
  try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_cast, {.l = left, .d = d})), 0);
  return d;
}
u32 lower_condition errs(superscope *ss, scope *s, astidx a) {
  let l = try_err(lower, (ss, s, a), 0);
  return try_err(scope_cast_value, (ss, s, l, ss_bool_type(ss)), 0);
}
static inline Type scope_reg_type(superscope *env, scope *s, u32 idx) { return ss_type(env, scope_find(env, s, idx)->t); }
// the type most instructions actually look at
static inline Type scope_reg_to_type_stripTag(superscope *env, scope *s, u32 idx) {
  return Type_stripTag(scope_reg_type(env, s, idx), ss_typeenv(env));
}

static bool Type_if_deref(typeenv e, Type t, Type *out) {
  t = Type_stripTag(t, e);
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

static void lower_ast_member errs(superscope *ss, scope *s, u32 l, u32 r, u32 d) {
  let rv = scope_find(ss, s, r);
  if (!tu_is(c_ast, ss_type(ss, rv->t).t) || !rv->v.c_ast) return_err("DOT_NEEDS_AST");
  astidx self = scope_find(ss, s, l)->v.c_ast;
  astidx res = 0;
  let name_node = ast_at(ss, rv->v.c_ast);
  iflet_tu (let m, astString, name_node) {
    let ms = ast_text(ss, m);
    if (self && tu_is(astOp, ast_at(ss, self)) && ms.len >= 2 && ms.ptr[0] == '"' && ms.ptr[ms.len - 1] == '"') {
      union {
        u32 n;
        char s[sizeof(u32)];
      } spelling = {};
      memcpy(spelling.s, ms.ptr + 1, MIN$(ms.len - 2, sizeof(u32)));
      let self_node = ast_at(ss, self);
      if (tu_catch(astOp, self_node).number_rep == spelling.n) res = self;
    }
  } else if (tu_is(astId, name_node)) {
    let ms = ast_text(ss, tu_catch(astId, name_node));
    if (self) {
      iflet_tu (let aop, astOp, ast_at(ss, self)) {
        if (fptr_eq(ms, "opleft")) res = ast_child(ss, aop.left_right);
        else if (fptr_eq(ms, "opright")) res = ast_child(ss, aop.left_right + 1);
      }
      if (!res)
        iflet_tu (let dcl, astDecl, ast_at(ss, self)) {
          if (fptr_eq(ms, "name")) res = ast_child(ss, dcl.name_type_value);
          else if (fptr_eq(ms, "type")) res = ast_child(ss, dcl.name_type_value + 1);
          else if (fptr_eq(ms, "value")) res = ast_child(ss, dcl.name_type_value + 2);
        }
      if (!res)
        res = tu_match_exp(
            ast_at(ss, self),
            (astOp, _, fptr_eq(ms, "op") ? self : (astidx)0),
            (astId, _, fptr_eq(ms, "id") ? self : (astidx)0),
            (astDecl, _, fptr_eq(ms, "decl") ? self : (astidx)0),
            (astNumber, _, fptr_eq(ms, "number") ? self : (astidx)0),
            (astString, _, fptr_eq(ms, "string") ? self : (astidx)0),
            (astPBlock, _, fptr_eq(ms, "pblock") ? self : (astidx)0),
            (astBBlock, _, fptr_eq(ms, "bblock") ? self : (astidx)0),
            (astABlock, _, fptr_eq(ms, "ablock") ? self : (astidx)0),
            (default, (astidx)0),
        );
    }
  } else return_err("DOT_NEEDS_NAME");
  let dv = scope_find(ss, s, d);
  dv->t = ss->t_ast;
  dv->v = VS(.c_ast = res ? res : ast_make_empty(ss));
  return;
}

static fptr value_to_fptr(superscope *ss, typed_value v) {
  let type = ss_type(ss, v.t);
  if (tu_is(r_arr, type.t)) {
    let dec = P$(T(), ($.iscomptime = true, $)).decorations;
    if (dec != type.decorations) return nullFptr;
    let len = tu_catch(r_arr, type.t).count;
    assertMessage(v.v.value || !len, "null pointer with a length");
    return (fptr){len, (u8 *)v.v.value};
  } else if (tu_is(c_ast, type.t) && v.v.c_ast)
    return tu_match_exp(
        ast_at(ss, v.v.c_ast),
        (astId, id, ast_text(ss, id)),
        (astString, str, ast_text(ss, str)),
        (astNumber, num, ast_text(ss, num)),
        (default, nullFptr),
    );
  return nullFptr;
}
static void lower_dot_member errs(superscope *ss, scope *s, u32 l, u32 r, u32 d) {
  let tenv = ss_typeenv(ss);
  let lt = scope_reg_to_type_stripTag(ss, s, l);
  if (lt.isref) lt.isref = false;
  let name = value_to_fptr(ss, *scope_find(ss, s, r));
  if (fptr_isNull(name)) return_err("DOT_NEEDS_NAME");

  if (tu_is(c_type, lt.t)) {
    let target = try_err(value_asType, (ss, *scope_find(ss, s, l)));
    let m = ss_tag_named(ss, target.t, name);
    if (!m.owner) return_err("NO_MEMBER");
    *scope_find(ss, s, d) = *scope_find(ss, m.owner, m.idx);
    return;
  }
  if (tu_is(c_ast, lt.t)) return (void)try_err(lower_ast_member, (ss, s, l, r, d));

  if (lt.ptr_level <= 1) {
    let m = ss_tag_named(ss, lt.t, name);
    if (m.owner) {
      *scope_find(ss, s, d) = *scope_find(ss, m.owner, m.idx);
      return;
    }
  }
  let st = TypeTree_inner_struct(lt.t, tenv);
  let bf = TypeTree_inner_bitfield(lt.t, tenv);
  u32 idx = 0;
  bool found = false;
  if (st.start) {
    foreach (let k, range(0, type_struct_len(tenv, st)))
      if (!found && !fptr_cmp(type_name(tenv, type_struct_item(tenv, st, k)->name), name)) {
        idx = (u32)k;
        found = true;
      }
  } else if (bf.start) {
    foreach (let k, range(0, type_bitf_len(tenv, bf)))
      if (!found && !fptr_cmp(type_name(tenv, type_bitf_item(tenv, bf, k)->name), name)) {
        idx = (u32)k;
        found = true;
      }
  }
  if (!found) return_err("NO_MEMBER");
  try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_field, {.l = l, .d = d, .i = idx})));
  return;
}

//
//
// unfolded switch members
//
//
static void tcheck_ifs errs(superscope *env, scope *s, u32 r) {
  let tenv = ss_typeenv(env);
  let cnd = scope_find(env, s, r);
  if (Type_cmp_ndecor(ss_type(env, cnd->t), T(tu_of(r_bool, {})), tenv))
    return_err("IF_CONDITION_NOT_BOOL");
  try_err(scope_emit_inst, (env, s, (ir)tu_of(ir_ifs, r)));
}
// d = *l
static void tcheck_putptr errs(superscope *env, scope *s, ir_putptr pp) {
  let tenv = ss_typeenv(env);
  Type lt = scope_reg_to_type_stripTag(env, s, pp.l);
  if (lt.isref) {
    lt.isref = false;
    u32 read = scope_add_value(env, s, TV_of(env, lt));
    try_err(scope_emit_inst, (env, s, (ir)tu_of(ir_set, {.l = pp.l, .d = read})));
    pp.l = read;
  }
  Type pointee = {};
  if (!Type_if_deref(tenv, lt, &pointee)) return_err("AT_NOT_REF");
  Type asref = pointee;
  asref.isref = true;
  let dv = scope_find(env, s, pp.d);
  Type dt = scope_reg_to_type_stripTag(env, s, pp.d);
  if (!scope_adopt_any(env, dv, asref) && Type_cmp_ndecor(pointee, dt, tenv) && Type_cmp_ndecor(asref, dt, tenv))
    return_err("PUTPTR_TYPE");
  try_err(scope_emit_inst, (env, s, (ir)tu_of(ir_putptr, pp)));
}
static void tcheck_getptr errs(superscope *env, scope *s, ir_getptr gp) {
  let tenv = ss_typeenv(env);
  Type base = scope_reg_to_type_stripTag(env, s, gp.l);
  Type_if_deref(tenv, base, &base);
  Type asp = P$(base, ($.ptr_level++, $));
  Type asr = P$(base, ($.isref = true, $));

  let dv = scope_find(env, s, gp.d);
  Type dt = scope_reg_to_type_stripTag(env, s, gp.d);
  if (!scope_adopt_any(env, dv, asp) && Type_cmp_ndecor(asp, dt, tenv) && Type_cmp_ndecor(asr, dt, tenv))
    return_err("GETPTR_TYPE");
  try_err(scope_emit_inst, (env, s, (ir)tu_of(ir_getptr, gp)));
}
// d is a reference to l
static void tcheck_getref errs(superscope *env, scope *s, ir_getref gp) {
  let tenv = ss_typeenv(env);
  Type base = scope_reg_to_type_stripTag(env, s, gp.l);
  if (base.isref) base.isref = false;
  Type referred = base;
  referred.isref = true;
  let dv = scope_find(env, s, gp.d);
  Type dt = scope_reg_to_type_stripTag(env, s, gp.d);
  if (!scope_adopt_any(env, dv, referred) && Type_cmp_ndecor(referred, dt, tenv)) return_err("GETREF_TYPE");
  try_err(scope_emit_inst, (env, s, (ir)tu_of(ir_getref, gp)));
}
static void tcheck_rawdot errs(superscope *env, scope *s, ir_rawdot rd) {
  try_err(lower_dot_member, (env, s, rd.l, rd.r, rd.d));
}
static void tcheck_rawop errs(superscope *env, scope *s, ir_rawop op) {
  let tenv = ss_typeenv(env);
  Type lt = scope_reg_type(env, s, op.l);
  Type rt = scope_reg_type(env, s, op.r);

  if ((op.o == opNumbers.eq || op.o == opNumbers.neq) && tu_is(c_ast, lt.t)) {
    let self = scope_find(env, s, op.l)->v.c_ast;
    let other = scope_find(env, s, op.r);
    bool res = false;
    iflet_tu (let arr, r_arr, ss_type(env, other->t).t) {
      if (self) {
        fptr word = tu_match_exp(
            ast_at(env, self),
            (astId, id, ast_text(env, id)),
            (astNumber, num, ast_text(env, num)),
            (astString, str, ast_text(env, str)),
            (default, (fptr){})
        );
        // a string node's text is quoted and escaped, the way it is written
        if (tu_is(astString, ast_at(env, self))) word = string_escape(env->alloc, word);
        res = fptr_eq(word, ((fptr){arr.count, (u8 *)other->v.value}));
      }
    } else if (tu_is(c_ast, ss_type(env, other->t).t)) res = self == other->v.c_ast;
    if (op.o == opNumbers.neq) res = !res;
    *scope_find(env, s, op.d) = TV_bool(env, res);
    return;
  }
  if ((op.o == opNumbers.eq || op.o == opNumbers.neq) && tu_is(c_type, lt.t) && tu_is(c_type, rt.t)) {
    bool res = scope_find(env, s, op.r)->v.c_type == scope_find(env, s, op.l)->v.c_type;
    if (op.o == opNumbers.neq) res = !res;
    *scope_find(env, s, op.d) = TV_bool(env, res);
    return;
  }
  if (Type_stripTag(lt, tenv).ptr_level <= 1) {
    char sym[sizeof(u32) + 1] = {};
    memcpy(sym, &op.o, sizeof(u32));
    // the spelling is a string, not the whole padded array, so the length has to be measured
    let m = ss_tag_named(env, lt.t, (fptr){strlen(sym), (u8 *)sym});
    if (m.owner) {
      let method = scope_add_value(env, s, *scope_find(env, m.owner, m.idx));
      u32 res = try_err(lower_function_call_values, (env, s, method, ((slice(u32)){2, (u32[2]){op.l, op.r}})));
      scope_set_type(env, scope_find(env, s, op.d), scope_find(env, s, res)->t);
      msList_push(env->alloc, s->instructions, (ir)tu_of(ir_set, {.l = res, .d = op.d}));
      return;
    }
  }
  // `.` is overloadable like the rest, so a dot nothing claimed is the member access
  if (op.o == opNumbers.dot) return (void)try_err(lower_dot_member, (env, s, op.l, op.r, op.d));
  // `as` with no overload member names a type to coerce to
  if (op.o == opNumbers.as) {
    Type to = try_err(value_asType, (env, *scope_find(env, s, op.r)));
    Type from = Type_stripTag(lt, tenv);
    if (tu_is(c_any, from.t)) {
      scope_set_type(env, scope_find(env, s, op.d), ss_type_intern(env, to));
      try_err(scope_emit_inst, (env, s, (ir)tu_of(ir_rawop, op)));
    } else {
      u32 casted = try_err(scope_cast_value, (env, s, op.l, to));
      scope_set_type(env, scope_find(env, s, op.d), ss_type_intern(env, to));
      try_err(scope_emit_inst, (env, s, (ir)tu_of(ir_set, {.l = casted, .d = op.d})));
    }
    return;
  }
  if (!Type_is_integer_like(lt, tenv) || !Type_is_integer_like(rt, tenv)) return_err("OP_UNSUPPORTED_OPERANDS");
  bool is_bool = op.o == opNumbers.mor || op.o == opNumbers.les || op.o == opNumbers.moq ||
                 op.o == opNumbers.leq || op.o == opNumbers.eq || op.o == opNumbers.neq ||
                 op.o == opNumbers.oand || op.o == opNumbers.oor;
  Type res = is_bool ? ss_bool_type(env) : Type_weaker(lt, rt, tenv);
  scope_set_type(env, scope_find(env, s, op.d), ss_type_intern(env, res));
  try_err(scope_emit_inst, (env, s, (ir)tu_of(ir_rawop, op)));
}
static void tcheck_set errs(superscope *env, scope *s, ir_set st) {
  let tenv = ss_typeenv(env);
  Type dt = scope_reg_type(env, s, st.d);
  Type lt = scope_reg_type(env, s, st.l);
  // both sides are references: the destination has to point where the source does
  if (lt.isref && dt.isref && !Type_cmp_ndecor(lt, dt, tenv)) {
    try_err(scope_emit_inst, (env, s, (ir)tu_of(ir_putptr, {.l = st.l, .d = st.d})));
    return;
  }
  if (!tu_is(c_any, Type_stripTag(lt, tenv).t)) {
    let dv = scope_find(env, s, st.d);
    if (scope_adopt_any(env, dv, lt)) dt = ss_type(env, dv->t);
  }
  dt = Type_stripTag(dt, tenv);
  dt.isref = false;
  st.l = try_err(scope_cast_value, (env, s, st.l, dt));
  if (tu_is(c_int, dt.t) && !dt.isref && tu_is(c_int, Type_stripTag(lt, tenv).t) && !lt.isref) {
    let src = scope_find(env, s, st.l);
    if (src->v.c_int) scope_find(env, s, st.d)->v.c_int = BInt.copy(env->alloc, src->v.c_int);
  }
  try_err(scope_emit_inst, (env, s, (ir)tu_of(ir_set, st)));
}
static void tcheck_cast errs(superscope *env, scope *s, ir_cast cv) {
  let tenv = ss_typeenv(env);
  Type lt = scope_reg_type(env, s, cv.l);
  Type dt = scope_reg_type(env, s, cv.d);
  if (Type_ncmp_nptr(Type_stripTag(lt, tenv), Type_stripTag(dt, tenv), tenv))
    return (void)try_err(scope_emit_inst, (env, s, (ir)tu_of(ir_set, {.l = cv.l, .d = cv.d})));

  if (tu_is(c_ast, Type_stripTag(lt, tenv).t) && tu_is(r_bool, dt.t)) {
    bool truthy = P$(scope_find(env, s, cv.l), $->v.c_ast && !tu_is(astEmpty, ast_at(env, $->v.c_ast)));
    let src = scope_add_value(env, s, TV_bool(env, truthy));
    return (void)try_err(scope_emit_inst, (env, s, (ir)tu_of(ir_set, {.l = src, .d = cv.d})));
  }
  if (!(Type_is_integer_like(lt, tenv) && Type_is_integer_like(Type_stripTag(dt, tenv), tenv)))
    return_err("CANNOT_CAST");
  return (void)try_err(scope_emit_inst, (env, s, (ir)tu_of(ir_cast, cv)));
}
static void tcheck_ret errs(superscope *env, scope *s, u32 r) {
  function_instance *inst =
      s->instance.fn
          ? &env->functions[s->instance.fn - 1].instances[s->instance.index]
          : nullptr;
  if (inst) {
    Type vt = ss_type(env, scope_find(env, s, r)->t);
    if (!Type_adopt_any(&inst->ret, vt)) r = try_err(scope_cast_value, (env, s, r, inst->ret));
  }
  try_err(scope_emit_inst, (env, s, (ir)tu_of(ir_ret, r)));
}
// fn call and struct with a "()" function
static void tcheck_call errs(superscope *env, scope *s, ir_call call) {
  let tenv = ss_typeenv(env);
  let lv = scope_find(env, s, call.l);
  Type lt = ss_type(env, lv->t);
  if (tu_is(c_fun, lt.t)) {
    u32 fn = lv->v.c_fun.fn;
    if (!fn || fn > msList_len(env->functions)) return_err("UNSUPPORTED_CALL");
    Type ret = env->functions[fn - 1].instances[lv->v.c_fun.spec].ret;
    let dv = scope_find(env, s, call.d);
    scope_adopt_any(env, dv, ret);
    try_err(scope_emit_inst, (env, s, (ir)tu_of(ir_call, call)));
    return;
  }
  Type lts = Type_stripTag(lt, tenv);
  lts.isref = false;

  if (tu_is(c_ast, lts.t)) {
    if (call.e < call.r + 1) return_err("AST_INDEX_ONE_ARG");
    let av = scope_find(env, s, call.l);
    if (!av->v.c_ast) return_err("AST_INDEX_UNKNOWN");
    if (!tu_is(astABlock, ast_at(env, av->v.c_ast)) && !tu_is(astBBlock, ast_at(env, av->v.c_ast)) &&
        !tu_is(astPBlock, ast_at(env, av->v.c_ast)))
      println("given: {astidx_full}", ss_ast_full(env, av->v.c_ast)),
          return_err("AST_INDEX_NOT_LIST");
    let arv = scope_find(env, s, s->start + 1 + call.r);
    if (!tu_is(c_ast, ss_type(env, arv->t).t) || !arv->v.c_ast || !tu_is(astABlock, ast_at(env, arv->v.c_ast)))
      return_err("AST_INDEX_NEEDS_ABLOCK");
    let items = ast_block_children(env, arv->v.c_ast);
    if (items.len != 1) return_err("AST_INDEX_ONE_ARG");
    let itv = try_err(temporary_interpret, (env, s, items.start));
    if (!tu_is(c_int, ss_type(env, itv.t).t) || !itv.v.c_int) return_err("AST_INDEX_NOT_COMPTIME");
    bigint idxb = int_as_bigint(env->alloc, itv, tenv);
    defer { msList_deInit(env->alloc, idxb); };
    bigint_expand(env->alloc, &idxb, sizeof(usize) / sizeof(bigint_unit) + 1);
    usize idx = *(usize *)idxb;
    let children = ast_block_children(env, av->v.c_ast);
    let dv = scope_find(env, s, call.d);
    dv->t = env->t_ast;
    dv->v = VS(.c_ast = idx < children.len ? children.start + idx : ast_make_empty(env));
    return;
  }
  let inner = TypeTree_inner_array(lts.t, tenv);
  if (inner.t) {
    if (call.e < call.r + 1) return_err("ARRAY_INDEX_ONE_ARG");
    let arv = scope_find(env, s, s->start + 1 + call.r);
    if (!tu_is(c_ast, ss_type(env, arv->t).t) || !arv->v.c_ast) return_err("ARRAY_NEEDS_ABLOCK");
    let an = ast_at(env, arv->v.c_ast);
    (void)tu_catch(astABlock, an, return_err("ARRAY_NEEDS_ABLOCK"));
    let items = ast_block_children(env, arv->v.c_ast);
    if (items.len != 1) return_err("ARRAY_INDEX_ONE_ARG");
    u32 idx = try_err(lower, (env, s, items.start));
    let elem = type_at(tenv, inner.t);
    let dv = scope_find(env, s, call.d);
    scope_set_type(env, dv, ss_type_intern(env, elem));
    try_err(scope_emit_inst, (env, s, (ir)tu_of(ir_array, {.l = call.l, .d = call.d, .r = idx})));
    return;
  }
  struct type_named m = {};
  if (lts.ptr_level <= 1) m = ss_tag_named(env, lt.t, fp("()"));
  if (!m.owner) return_err("UNSUPPORTED_CALL");
  let method0 = scope_find(env, m.owner, m.idx);
  let mf = tu_catchr(c_fun, ss_type(env, method0->t).t);
  usize nparams = type_run_len(tenv, mf.start) - 1;
  usize nargs = call.e > call.r ? (usize)(call.e - call.r) : 0;
  if (nargs + 1 != nparams) return_err("WRONG_ARG_COUNT");
  slice(u32) vals = slice_alloc(env->alloc, u32, nargs + 1);
  vals.ptr[0] = call.l;
  foreach (let j, range(0, nargs)) {
    u32 arg = s->start + 1 + call.r + (u32)j;
    if (!tu_is(c_ast, type_at(tenv, type_run_at(tenv, mf.start, j + 1)).t)) {
      let av = scope_find(env, s, arg);
      if (tu_is(c_ast, ss_type(env, av->t).t) && av->v.c_ast) arg = try_err(lower, (env, s, av->v.c_ast));
    }
    vals.ptr[j + 1] = arg;
  }
  let method = scope_add_value(env, s, *method0);
  u32 res = try_err(lower_function_call_values, (env, s, method, vals));
  slice_free(env->alloc, vals);
  scope_set_type(env, scope_find(env, s, call.d), scope_find(env, s, res)->t);
  msList_push(env->alloc, s->instructions, (ir)tu_of(ir_set, {.l = res, .d = call.d}));
}
// ref to n-th arral elment
static void tcheck_array errs(superscope *env, scope *s, ir_array arr) {
  let tenv = ss_typeenv(env);
  Type base = scope_reg_to_type_stripTag(env, s, arr.l);
  if (base.isref) base.isref = false;
  let inner = TypeTree_inner_array(base.t, tenv);
  if (!inner.t) return_err("ARRAY_OF_NON_ARRAY");
  Type elem = type_at(tenv, inner.t);
  Type asref = elem;
  asref.isref = true;
  let dv = scope_find(env, s, arr.d);
  Type dt = scope_reg_to_type_stripTag(env, s, arr.d);
  if (!scope_adopt_any(env, dv, elem) && Type_cmp_ndecor(elem, dt, tenv) && Type_cmp_ndecor(asref, dt, tenv))
    return_err("ARRAY_TYPE");
  if (!Type_is_integer_like(scope_reg_type(env, s, arr.r), tenv)) return_err("ARRAY_INDEX_NON_INT");
  try_err(scope_emit_inst, (env, s, (ir)tu_of(ir_array, arr)));
}
// ref to n-th member, exept comptime
static void tcheck_field errs(superscope *env, scope *s, ir_field fld) {
  let tenv = ss_typeenv(env);
  Type base = scope_reg_to_type_stripTag(env, s, fld.l);
  if (base.isref) base.isref = false;
  let st = TypeTree_inner_struct(base.t, tenv);
  let bf = TypeTree_inner_bitfield(base.t, tenv);
  Type member = {};
  if (st.start && fld.i < type_struct_len(tenv, st)) member = type_struct_item(tenv, st, fld.i)->t;
  else if (bf.start && fld.i < type_bitf_len(tenv, bf)) member = type_bitf_item(tenv, bf, fld.i)->t;
  else return_err("NO_SUCH_FIELD");
  if (Type_is_comptime(member)) return_err("NO_STORAGE");
  Type asref = member;
  asref.isref = true;
  let dv = scope_find(env, s, fld.d);
  Type dt = scope_reg_to_type_stripTag(env, s, fld.d);
  if (!scope_adopt_any(env, dv, member) && Type_cmp_ndecor(member, dt, tenv) && Type_cmp_ndecor(asref, dt, tenv))
    return_err("FIELD_TYPE");
  try_err(scope_emit_inst, (env, s, (ir)tu_of(ir_field, fld)));
}

static inline void scope_add_inst_typecheck errs(superscope *env, scope *s, ir i) {
  tu_match_void(
      i,
      (ir_farg /*  */, _, try_err(scope_emit_inst, (env, s, i))), // checked elsewhere
      (ir_loops /* */, _, try_err(scope_emit_inst, (env, s, i))),
      (ir_loope /* */, _, try_err(scope_emit_inst, (env, s, i))),
      (ir_break /* */, _, try_err(scope_emit_inst, (env, s, i))),
      (ir_cont /*  */, _, try_err(scope_emit_inst, (env, s, i))),
      (ir_ifd /*   */, _, try_err(scope_emit_inst, (env, s, i))),
      (ir_ife /*   */, _, try_err(scope_emit_inst, (env, s, i))),
      (ir_blocks /**/, _, ), // nothing but namespace stuff
      (ir_blocke /**/, _, ),
      (ir_ifs /*   */, r, try_err(tcheck_ifs, (env, s, r))),
      // d = *l
      (ir_putptr, pp, try_err(tcheck_putptr, (env, s, pp))),
      (ir_getptr, gp, try_err(tcheck_getptr, (env, s, gp))),
      // d is a reference to l
      (ir_getref, gp, try_err(tcheck_getref, (env, s, gp))),
      (ir_rawdot, rd, try_err(tcheck_rawdot, (env, s, rd))),
      (ir_rawop, op, try_err(tcheck_rawop, (env, s, op))),
      (ir_set, st, try_err(tcheck_set, (env, s, st))),
      (ir_cast, cv, try_err(tcheck_cast, (env, s, cv))),
      (ir_ret, r, try_err(tcheck_ret, (env, s, r))),
      // fn call and struct with a "()" function
      (ir_call, call, try_err(tcheck_call, (env, s, call))),
      // ref to n-th arral elment
      (ir_array, arr, try_err(tcheck_array, (env, s, arr))),
      // ref to n-th member, exept comptime
      (ir_field, fld, try_err(tcheck_field, (env, s, fld))),
  );
}

//
//
//

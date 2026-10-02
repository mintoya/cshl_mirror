#pragma once
#include "coerce_helpers.h"
#include "common.h"
#include "wheels/macros.h"
#include "wheels/sList.h"

void tcheck_set errs(superscope *ss, scope *s, ir_set st);
void tcheck_cast errs(superscope *ss, scope *s, ir_cast cv, bool is_explicit);
void tcheck_array errs(superscope *ss, scope *s, ir_array arr);
static inline Type scope_regType(superscope *ss, scope *s, u32 reg) { return scope_reg_type(ss, s, reg); }
tu_def((Toi, u8), (TT, Type), (TI, typeidx));
#define Toi(x) match_type(x, (Type, v, (Toi)tu_of(TT, v)), (typeidx, v, (Toi)tu_of(TI, v)))
u32 tcheck_castV errs(superscope *ss, scope *s, u32 left, Toi to, bool is_explicit) {
  return try_err(scope_cast_value, (ss, s, left, tu_match_exp(to, (TT, v, v), (TI, v, ss_type(ss, v))), is_explicit), 0);
}
void tcheck_result errs(superscope *ss, scope *s, u32 value, u32 dest) {
  scope_adopt_any(ss, scope_find(ss, s, dest), scope_regType(ss, s, value));
  try_err(tcheck_set, (ss, s, (ir_set){.l = value, .d = dest}));
}
void tcheck_ifs errs(superscope *ss, scope *s, ir_ifs d) {
  let rd = try_err(tcheck_castV, (ss, s, d, Toi(TypesTree_r_bool), false));
  try_err(scope_emit_inst, (ss, s, (ir)tu_of(ir_ifs, rd)));
}
void tcheck_putptr errs(superscope *ss, scope *s, ir_putptr pp) {
  let lt = Type_stripTag(scope_regType(ss, s, pp.l), ss_typeenv(ss));
  if (!lt.ptr_level) return_type_err("AT_NOT_REF", ss_typeenv(ss), scope_regType(ss, s, pp.l), T());
  let dt = P$(lt, ($.ptr_level--, $.ref_kind = ref_ref, $));
  let temp = scope_add_value(ss, s, TV_of(ss, dt));
  try_err(scope_emit_inst, (ss, s, (ir)tu_of(ir_putptr, {.l = pp.l, .d = temp})));
  try_err(tcheck_result, (ss, s, temp, pp.d));
}
void tcheck_getptr errs(superscope *ss, scope *s, ir_getptr gp) {
  let lt = scope_regType(ss, s, gp.l);
  if (lt.ref_kind == ref_dref) return_type_err("BITFIELD_NOT_ADDRESSABLE", ss_typeenv(ss), lt, T());
  let dt = P$(lt, ($.ptr_level++, $.ref_kind = ref_none, $));
  let temp = scope_add_value(ss, s, TV_of(ss, dt));
  try_err(scope_emit_inst, (ss, s, (ir)tu_of(ir_getptr, {.l = gp.l, .d = temp})));
  try_err(tcheck_result, (ss, s, temp, gp.d));
}
void tcheck_getref errs(superscope *ss, scope *s, ir_getref gr) {
  let lt = scope_regType(ss, s, gr.l);
  let dt = P$(lt, ($.ref_kind = $.ref_kind == ref_dref ? ref_dref : ref_ref, $));
  let temp = scope_add_value(ss, s, TV_of(ss, dt));
  try_err(scope_emit_inst, (ss, s, (ir)tu_of(ir_getref, {.l = gr.l, .d = temp})));
  try_err(tcheck_result, (ss, s, temp, gr.d));
}
Type tcheck_member_type errs(superscope *ss, scope *s, ir_field fld) {
  let e = ss_typeenv(ss);
  let base = scope_regType(ss, s, fld.l);
  let st = TypeTree_inner_struct(base.t, e);
  let bf = TypeTree_inner_bitfield(base.t, e);
  Type member = {};
  if (st.start && fld.i < type_struct_len(e, st)) member = type_struct_item(e, st, fld.i)->t;
  else if (bf.start && fld.i < type_bitf_len(e, bf)) member = type_bitf_item(e, bf, fld.i)->t;
  else return_type_err("NO_SUCH_FIELD", e, base, T(), T());
  if (Type_is_comptime(member)) return_type_err("NO_STORAGE", e, member, T(), T());
  member.ref_kind = bf.start ? ref_dref : ref_ref;
  return member;
}
void tcheck_field errs(superscope *ss, scope *s, ir_field fld) {
  let dt = try_err(tcheck_member_type, (ss, s, fld));
  let dest = fld.d;
  fld.d = scope_add_value(ss, s, TV_of(ss, dt));
  fld.t = ss_type_intern(ss, dt);
  try_err(scope_emit_inst, (ss, s, (ir)tu_of(ir_field, fld)));
  try_err(tcheck_result, (ss, s, fld.d, dest));
}
void tcheck_array errs(superscope *ss, scope *s, ir_array arr) {
  let e = ss_typeenv(ss);
  let lt = scope_regType(ss, s, arr.l);
  let inner = TypeTree_inner_array(lt.t, e);
  if (!inner.t) return_type_err("ARRAY_OF_NON_ARRAY", e, lt, T());
  if (!Type_is_integer_like(scope_regType(ss, s, arr.r), e))
    return_type_err("ARRAY_INDEX_NON_INT", e, lt, scope_regType(ss, s, arr.r));
  let dt = P$(Type_at(e, inner.t), ($.ref_kind = ref_ref, $));
  let dest = arr.d;
  arr.d = scope_add_value(ss, s, TV_of(ss, dt));
  try_err(scope_emit_inst, (ss, s, (ir)tu_of(ir_array, arr)));
  try_err(tcheck_result, (ss, s, arr.d, dest));
}
void tcheck_rawdot errs(superscope *ss, scope *s, ir_rawdot d) {
  let temp = scope_add_value(ss, s, TV_any(ss));
  try_err(lower_dot_member, (ss, s, d.l, d.r, temp));
  try_err(tcheck_result, (ss, s, temp, d.d));
}
void tcheck_ast_compare errs(superscope *env, scope *s, ir_rawop op) {
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
      if (tu_is(astString, ast_at(env, self))) word = string_escape(env->alloc, word);
      res = fptr_eq(word, ((fptr){arr.count, (u8 *)other->v.value}));
    }
  } else if (TypeTree_is_ast(ss_type(env, other->t).t)) res = self == other->v.c_ast;
  if (op.o == opNumbers.neq) res = !res;
  *scope_find(env, s, op.d) = TV_bool(env, res);
  return;
}
void tcheck_type_compare errs(superscope *ss, scope *s, ir_rawop op) {
  let a = ss_type(ss, scope_find(ss, s, op.l)->v.c_type);
  let b = ss_type(ss, scope_find(ss, s, op.r)->v.c_type);
  bool same = !Type_cmp_ndecor(a, b, ss_typeenv(ss));
  *scope_find(ss, s, op.d) = TV_bool(ss, op.o == opNumbers.neq ? !same : same);
}
struct type_named tcheck_operator(superscope *ss, Type lt, u32 op) {
  if (Type_stripTag(lt, ss_typeenv(ss)).ptr_level > 1) return (struct type_named){};
  char sym[sizeof(u32) + 1] = {};
  memcpy(sym, &op, sizeof(u32));
  return ss_tag_named(ss, lt.t, (fptr){strlen(sym), (u8 *)sym});
}
void tcheck_operator_call errs(superscope *ss, scope *s, ir_rawop op, struct type_named m) {
  let method = scope_add_value(ss, s, *scope_find(ss, m.owner, m.idx));
  let res = try_err(lower_function_call_values, (ss, s, method, ((slice(u32)){2, (u32[]){op.l, op.r}})));
  try_err(tcheck_result, (ss, s, res, op.d));
}
void tcheck_as errs(superscope *ss, scope *s, ir_rawop op) {
  let to = try_err(value_asType, (ss, *scope_find(ss, s, op.r)));
  if (tu_is(c_any, Type_stripTag(scope_regType(ss, s, op.l), ss_typeenv(ss)).t)) {
    let dest = op.d;
    op.d = scope_add_value(ss, s, TV_of(ss, to));
    try_err(scope_emit_inst, (ss, s, (ir)tu_of(ir_rawop, op)));
    try_err(tcheck_result, (ss, s, op.d, dest));
    return;
  }
  let res = try_err(tcheck_castV, (ss, s, op.l, Toi(to), true));
  try_err(tcheck_result, (ss, s, res, op.d));
}
void tcheck_integer_op errs(superscope *ss, scope *s, ir_rawop op) {
  let e = ss_typeenv(ss);
  let lt = scope_regType(ss, s, op.l), rt = scope_regType(ss, s, op.r);
  if (!Type_is_integer_like(lt, e) || !Type_is_integer_like(rt, e))
    return_type_err("OP_UNSUPPORTED_OPERANDS", e, lt, rt);
  bool is_bool = op.o == opNumbers.mor || op.o == opNumbers.les || op.o == opNumbers.moq ||
                 op.o == opNumbers.leq || op.o == opNumbers.eq || op.o == opNumbers.neq ||
                 op.o == opNumbers.oand || op.o == opNumbers.oor;
  let res = is_bool ? T(tu_of(r_bool, {})) : Type_weaker(lt, rt, e);
  let dest = op.d;
  op.d = scope_add_value(ss, s, TV_of(ss, res));
  try_err(scope_emit_inst, (ss, s, (ir)tu_of(ir_rawop, op)));
  try_err(tcheck_result, (ss, s, op.d, dest));
}
void tcheck_compare errs(superscope *ss, scope *s, ir_rawop op, bool ast) {
  let dest = op.d;
  op.d = scope_add_value(ss, s, TV_any(ss));
  if (ast) try_err(tcheck_ast_compare, (ss, s, op));
  else try_err(tcheck_type_compare, (ss, s, op));
  try_err(tcheck_result, (ss, s, op.d, dest));
}
void tcheck_rawop errs(superscope *ss, scope *s, ir_rawop op) {
  let lt = scope_regType(ss, s, op.l), rt = scope_regType(ss, s, op.r);
  bool eq = op.o == opNumbers.eq || op.o == opNumbers.neq;
  if (eq && TypeTree_is_ast(lt.t)) return (void)try_err(tcheck_compare, (ss, s, op, true));
  if (eq && tu_is(c_type, lt.t) && tu_is(c_type, rt.t)) return (void)try_err(tcheck_compare, (ss, s, op, false));
  if (op.o == opNumbers.as) return (void)try_err(tcheck_as, (ss, s, op));
  let m = tcheck_operator(ss, lt, op.o);
  if (m.owner) return (void)try_err(tcheck_operator_call, (ss, s, op, m));
  if (op.o == opNumbers.dot) return (void)try_err(tcheck_rawdot, (ss, s, (ir_rawdot){op.l, op.r, op.d}));
  try_err(tcheck_integer_op, (ss, s, op));
}
bool tcheck_written(scope *s, u32 reg) {
  s = (scope *)scope_owner(s, reg);
  foreach (let inst, span(s->instructions, msList_len(s->instructions))) {
    let d = tu_match_exp(*inst, (ir_set, x, x.d), (ir_cast, x, x.d), (ir_getref, x, x.d), (ir_getptr, x, x.d), (ir_putptr, x, x.d), (ir_field, x, x.d), (ir_array, x, x.d), (ir_rawop, x, x.d), (ir_farg, x, x.d), (ir_call, x, x.d), (default, (u32)0));
    if (d == reg) return true;
  }
  return false;
}
bool tcheck_reference_field(superscope *ss, scope *s, u32 reg) {
  s = (scope *)scope_owner(s, reg);
  foreach (let inst, span(s->instructions, msList_len(s->instructions))) {
    if (!tu_is(ir_field, *inst) || inst->ir_field.d != reg) continue;
    let fld = inst->ir_field;
    let members = TypeTree_inner_struct(scope_regType(ss, s, fld.l).t, ss_typeenv(ss));
    return members.start && type_struct_item(ss_typeenv(ss), members, fld.i)->t.ref_kind;
  }
  return false;
}
bool tcheck_bind_field_place errs(superscope *ss, scope *s, ir_set st) {
  if (tcheck_written(s, st.d) || !tcheck_reference_field(ss, s, st.l)) return false;
  let owner = scope_owner(s, st.l);
  foreach (let inst, span(owner->instructions, msList_len(owner->instructions))) {
    if (!tu_is(ir_field, *inst) || inst->ir_field.d != st.l) continue;
    let fld = inst->ir_field;
    fld.d = st.d;
    try_err(scope_emit_inst, (ss, s, (ir)tu_of(ir_field, fld)), false);
    return true;
  }
  return false;
}
void tcheck_bind_reference errs(superscope *ss, scope *s, ir_set st) {
  let lt = scope_regType(ss, s, st.l), dt = scope_regType(ss, s, st.d);
  if (lt.ref_kind == ref_dref && dt.ref_kind == ref_ref)
    return_type_err("BITFIELD_NOT_ADDRESSABLE", ss_typeenv(ss), lt, dt);
  lt.ref_kind = dt.ref_kind;
  if (Type_cmp_ndecor(lt, dt, ss_typeenv(ss)))
    return_type_err("CANNOT_CAST", ss_typeenv(ss), scope_regType(ss, s, st.l), scope_regType(ss, s, st.d));
  if (try_err(tcheck_bind_field_place, (ss, s, st))) return;
  try_err(scope_emit_inst, (ss, s, (ir)tu_of(ir_getref, {.l = st.l, .d = st.d})));
}
void tcheck_set_overload errs(superscope *ss, scope *s, ir_set st, struct type_named m) {
  u32 self = st.d;
  let dt = scope_regType(ss, s, st.d);
  if (!dt.ref_kind) {
    self = scope_add_value(ss, s, TV_of(ss, P$(dt, ($.ref_kind = ref_ref, $))));
    try_err(scope_emit_inst, (ss, s, (ir)tu_of(ir_getref, {.l = st.d, .d = self})));
  }
  let method = scope_add_value(ss, s, *scope_find(ss, m.owner, m.idx));
  try_err(lower_function_call_values, (ss, s, method, ((slice(u32)){2, (u32[]){self, st.l}})));
}
static u32 tcheck_cast_from_source errs(superscope *ss, scope *s, u32 left, Type to, struct type_named m) {
  let method = scope_add_value(ss, s, *scope_find(ss, m.owner, m.idx));
  let target = scope_add_value(ss, s, Type_asValue(ss, to));
  let result = try_err(lower_function_call_values, (ss, s, method, ((slice(u32)){2, (u32[]){left, target}})), 0);
  if (Type_cmp_ndecor(scope_regType(ss, s, result), to, ss_typeenv(ss)))
    return_type_err("AS_OVERLOAD_RETURN_TYPE", ss_typeenv(ss), scope_regType(ss, s, result), to, 0);
  return result;
}
static u32 tcheck_cast_into_target errs(superscope *ss, scope *s, u32 left, Type to, struct type_named m) {
  let result = scope_add_value(ss, s, TV_of(ss, to));
  try_err(tcheck_set_overload, (ss, s, (ir_set){.l = left, .d = result}, m), 0);
  return result;
}
static bool tcheck_cast_overload errs(superscope *ss, scope *s, u32 left, Type to, u32 *result) {
  let from = scope_regType(ss, s, left);
  if (to.ref_kind || tu_is(c_any, to.t) ||
      !Type_cmp_ndecor(P$(from, ($.ref_kind = ref_none, $)), to, ss_typeenv(ss))) return false;
  let source = tcheck_operator(ss, from, opNumbers.as);
  if (source.owner) {
    *result = try_err(tcheck_cast_from_source, (ss, s, left, to, source), false);
    return true;
  }
  let target = tag_set_member(ss, to);
  if (!target.owner) return false;
  *result = try_err(tcheck_cast_into_target, (ss, s, left, to, target), false);
  return true;
}
static void tcheck_cast_copy_result errs(superscope *ss, scope *s, ir_cast cv, u32 result) {
  try_err(scope_emit_inst, (ss, s, (ir)tu_of(ir_set, {.l = result, .d = cv.d})));
}
void tcheck_set errs(superscope *ss, scope *s, ir_set st) {
  Type dt = scope_regType(ss, s, st.d), lt = scope_regType(ss, s, st.l);
  if (dt.ref_kind && (!tcheck_written(s, st.d) ||
                      (lt.ref_kind && tcheck_reference_field(ss, s, st.d))))
    return (void)try_err(tcheck_bind_reference, (ss, s, st));
  let m = tag_set_member(ss, P$(dt, ($.ref_kind = ref_none, $)));
  if (m.owner) return (void)try_err(tcheck_set_overload, (ss, s, st, m));
  if (!tu_is(c_any, Type_stripTag(lt, ss_typeenv(ss)).t))
    scope_adopt_any(ss, scope_find(ss, s, st.d), P$(lt, ($.ref_kind = ref_none, $)));
  dt = scope_regType(ss, s, st.d);
  dt.ref_kind = ref_none;
  st.l = try_err(tcheck_castV, (ss, s, st.l, Toi(dt), false));
  try_err(scope_emit_inst, (ss, s, (ir)tu_of(ir_set, st)));
}
void tcheck_cast_ast errs(superscope *ss, scope *s, ir_cast cv) {
  scope_find(ss, s, cv.d)->v = scope_find(ss, s, cv.l)->v;
  try_err(scope_emit_inst, (ss, s, (ir)tu_of(ir_set, {.l = cv.l, .d = cv.d})));
}
void tcheck_cast_truth errs(superscope *ss, scope *s, ir_cast cv) {
  let v = *scope_find(ss, s, cv.l);
  bool truth;
  if (TypeTree_is_ast(Type_stripTag(ss_type(ss, v.t), ss_typeenv(ss)).t))
    truth = v.v.c_ast && !tu_is(astEmpty, ast_at(ss, v.v.c_ast));
  else truth = v.v.c_typeinfo && v.v.c_typeinfo->view != typeinfo_invalid;
  let src = scope_add_value(ss, s, TV_bool(ss, truth));
  try_err(scope_emit_inst, (ss, s, (ir)tu_of(ir_set, {.l = src, .d = cv.d})));
}
static bool tcheck_cast_anonymous errs(superscope *ss, scope *s, ir_cast cv) {
  let e = ss_typeenv(ss);
  let from = scope_regType(ss, s, cv.l), to = scope_regType(ss, s, cv.d);
  let source = Type_stripTag(from, e), target = Type_stripTag(to, e);
  if (from.ptr_level || to.ptr_level || to.ref_kind || Type_is_comptime(to) ||
      !tu_is(r_tag, from.t) || !tu_is(r_struct, target.t) || !TypeTree_cmp(e, source.t, target.t)) return false;
  let info = moxmap_get(ss->type_info, type_name(e, from.t.r_tag.name));
  if (!info || !info->is_anonymous) return false;
  let result = try_err(scope_convert_anonymous, (ss, s, cv.l, to, *info), false);
  try_err(tcheck_cast_copy_result, (ss, s, cv, result), false);
  return true;
}
void tcheck_cast errs(superscope *ss, scope *s, ir_cast cv, bool is_explicit) {
  u32 overloaded = 0;
  if (try_err(tcheck_cast_overload, (ss, s, cv.l, scope_regType(ss, s, cv.d), &overloaded)))
    return (void)try_err(tcheck_cast_copy_result, (ss, s, cv, overloaded));
  if (try_err(tcheck_cast_anonymous, (ss, s, cv))) return;
  let e = ss_typeenv(ss);
  let lt = Type_stripTag(scope_regType(ss, s, cv.l), e);
  let dt = Type_stripTag(scope_regType(ss, s, cv.d), e);
  if (TypeTree_is_ast(lt.t) && TypeTree_is_ast(dt.t))
    return (void)try_err(tcheck_cast_ast, (ss, s, cv));
  if (dt.ref_kind) return (void)try_err(tcheck_bind_reference, (ss, s, (ir_set){cv.l, cv.d}));
  if (Type_ncmp_nptr(lt, dt, e))
    return (void)try_err(scope_emit_inst, (ss, s, (ir)tu_of(ir_set, {.l = cv.l, .d = cv.d})));
  if (is_explicit && lt.ptr_level && dt.ptr_level)
    return (void)try_err(scope_emit_inst, (ss, s, (ir)tu_of(ir_cast, cv)));
  if (tu_is(r_bool, dt.t) && (TypeTree_is_ast(lt.t) || tu_is(c_typeinfo, lt.t)))
    return (void)try_err(tcheck_cast_truth, (ss, s, cv));
  if (!Type_is_integer_like(lt, e) || !Type_is_integer_like(dt, e))
    return_type_err("CANNOT_CAST", e, scope_regType(ss, s, cv.l), scope_regType(ss, s, cv.d));
  try_err(scope_emit_inst, (ss, s, (ir)tu_of(ir_cast, cv)));
}
void tcheck_ret errs(superscope *ss, scope *s, ir_ret r) {
  if (s->instance.fn) {
    let inst = &ss->functions[s->instance.fn - 1].instances[s->instance.index];
    if (!Type_adopt_any(&inst->ret, scope_regType(ss, s, r)))
      r = try_err(tcheck_castV, (ss, s, r, Toi(inst->ret), false));
  }
  try_err(scope_emit_inst, (ss, s, (ir)tu_of(ir_ret, r)));
}
void tcheck_call_ast errs(superscope *env, scope *s, ir_call call) {
  let tenv = ss_typeenv(env);
  if (call.e < call.r + 1) return_err("AST_INDEX_ONE_ARG");
  let av_value = *scope_find(env, s, call.l);
  let av = &av_value;
  if (!av->v.c_ast) return_err("AST_INDEX_UNKNOWN");
  if (!tu_is(astABlock, ast_at(env, av->v.c_ast)) && !tu_is(astBBlock, ast_at(env, av->v.c_ast)) &&
      !tu_is(astPBlock, ast_at(env, av->v.c_ast)))
    println("given: {astidx_full}", ss_ast_full(env, av->v.c_ast)),
        return_err("AST_INDEX_NOT_LIST");
  let arv = scope_find(env, s, s->start + 1 + call.r);
  if (!TypeTree_is_ast(ss_type(env, arv->t).t) || !arv->v.c_ast || !tu_is(astABlock, ast_at(env, arv->v.c_ast)))
    return_type_err("AST_INDEX_NEEDS_ABLOCK", tenv, ss_type(env, arv->t), T());
  let items = ast_block_children(env, arv->v.c_ast);
  if (items.len != 1) return_err("AST_INDEX_ONE_ARG");
  let itv = try_err(temporary_interpret, (env, s, items.start));
  if (!tu_is(c_int, ss_type(env, itv.t).t) || !itv.v.c_int)
    return_type_err("AST_INDEX_NOT_COMPTIME", tenv, ss_type(env, itv.t), T());
  bigint idxb = int_as_bigint(env->alloc, itv, tenv);
  defer { msList_deInit(env->alloc, idxb); };
  bigint_expand(env->alloc, &idxb, sizeof(usize) / sizeof(bigint_unit) + 1);
  usize idx = *(usize *)idxb;
  let children = ast_block_children(env, av->v.c_ast);
  let dv = scope_find(env, s, call.d);
  dv->t = TypesTree_c_ast;
  dv->v = VS(.c_ast = idx < children.len ? children.start + idx : ast_make_empty(env));
  return;
}
void tcheck_call_array errs(superscope *env, scope *s, ir_call call) {
  if (call.e < call.r + 1) return_err("ARRAY_INDEX_ONE_ARG");
  let arv = scope_find(env, s, s->start + 1 + call.r);
  if (!TypeTree_is_ast(ss_type(env, arv->t).t) || !arv->v.c_ast) return_type_err("ARRAY_NEEDS_ABLOCK", ss_typeenv(env), ss_type(env, arv->t), T());
  let an = ast_at(env, arv->v.c_ast);
  (void)tu_catch(astABlock, an, return_type_err("ARRAY_NEEDS_ABLOCK", ss_typeenv(env), ss_type(env, arv->t), T()));
  let items = ast_block_children(env, arv->v.c_ast);
  if (items.len != 1) return_err("ARRAY_INDEX_ONE_ARG");
  u32 idx = try_err(lower, (env, s, items.start));
  try_err(tcheck_array, (env, s, (ir_array){.l = call.l, .d = call.d, .r = idx}));
}
void tcheck_call_overload errs(superscope *env, scope *s, ir_call call) {
  let tenv = ss_typeenv(env);
  let lt = scope_regType(env, s, call.l);
  let lts = Type_stripTag(lt, tenv);
  struct type_named m = {};
  if (lts.ptr_level <= 1) m = ss_tag_named(env, lt.t, fp("()"));
  if (!m.owner) return_type_err("UNSUPPORTED_CALL", tenv, lt, T());
  let method_value = *scope_find(env, m.owner, m.idx);
  let method0 = &method_value;
  let mf = tu_catchr(c_fun, ss_type(env, method0->t).t);
  usize nparams = type_run_len(tenv, mf.start) - 1;
  usize nargs = call.e > call.r ? (usize)(call.e - call.r) : 0;
  if (nargs + 1 != nparams)
    return_type_err("WRONG_ARG_COUNT", tenv, lt, ss_type(env, method0->t));
  slice(u32) vals = slice_alloc(env->alloc, u32, nargs + 1);
  defer { slice_free(env->alloc, vals); };
  bool receiver_named = false;
  defer {
    if (receiver_named) scope_pop_named(s);
  };
  let self_want = Type_at(tenv, type_run_at(tenv, mf.start, 0));
  if (TypeTree_is_ast(self_want.t) && !TypeTree_is_ast(lts.t)) {
    scope_push_named(env, s);
    receiver_named = true;
    try_err(scope_bind, (env, s, call.l, fp("#_")));
    vals.ptr[0] = scope_add_value(env, s, TV_ast(env, ast_make_id(env, fp("#_"))));
  } else vals.ptr[0] = call.l;
  foreach (let j, range(0, nargs)) {
    u32 arg = s->start + 1 + call.r + (u32)j;
    if (!TypeTree_is_ast(Type_at(tenv, type_run_at(tenv, mf.start, j + 1)).t)) {
      let av = scope_find(env, s, arg);
      if (TypeTree_is_ast(ss_type(env, av->t).t) && av->v.c_ast) arg = try_err(lower, (env, s, av->v.c_ast));
    }
    vals.ptr[j + 1] = arg;
  }
  let method = scope_add_value(env, s, *method0);
  u32 res = try_err(lower_function_call_values, (env, s, method, vals));

  try_err(tcheck_result, (env, s, res, call.d));
}
void tcheck_call_function errs(superscope *ss, scope *s, ir_call call) {
  let fun = scope_find(ss, s, call.l)->v.c_fun;
  if (!fun.fn || fun.fn > msList_len(ss->functions))
    return_type_err("UNSUPPORTED_CALL", ss_typeenv(ss), scope_regType(ss, s, call.l), T());
  let ret = ss->functions[fun.fn - 1].instances[fun.spec].ret;
  let dest = call.d;
  call.d = scope_add_value(ss, s, TV_of(ss, ret));
  try_err(scope_emit_inst, (ss, s, (ir)tu_of(ir_call, call)));
  try_err(tcheck_result, (ss, s, call.d, dest));
}
void tcheck_call_ast_result errs(superscope *ss, scope *s, ir_call call) {
  let dest = call.d;
  call.d = scope_add_value(ss, s, TV_any(ss));
  try_err(tcheck_call_ast, (ss, s, call));
  try_err(tcheck_result, (ss, s, call.d, dest));
}
void tcheck_call errs(superscope *ss, scope *s, ir_call call) {
  let lt = Type_stripTag(scope_regType(ss, s, call.l), ss_typeenv(ss));
  if (tu_is(c_fun, lt.t)) return (void)try_err(tcheck_call_function, (ss, s, call));
  if (TypeTree_is_ast(lt.t)) return (void)try_err(tcheck_call_ast_result, (ss, s, call));
  if (TypeTree_inner_array(lt.t, ss_typeenv(ss)).t) return (void)try_err(tcheck_call_array, (ss, s, call));
  try_err(tcheck_call_overload, (ss, s, call));
}
static bool tcheck_inside_loop(const scope *s) {
  isize depth = 1;
  for (int i = msList_len(s->instructions); i > 0 && depth; i--)
    depth +=
        tu_is(ir_loope, s->instructions[i - 1]) -
        tu_is(ir_loops, s->instructions[i - 1]);
  return !depth;
}
void tcheck_break errs(superscope *ss, scope *s) {
  if (!tcheck_inside_loop(s)) return_err("BREAK_OUTSIDE_LOOP");
  try_err(scope_emit_inst, (ss, s, (ir)tu_of(ir_break, {})));
}
void tcheck_continue errs(superscope *ss, scope *s) {
  if (!tcheck_inside_loop(s)) return_err("CONTINUE_OUTSIDE_LOOP");
  try_err(scope_emit_inst, (ss, s, (ir)tu_of(ir_cont, {})));
}
void scope_add_inst_typecheck(err_t *err, superscope *env, scope *s, ir i) {
  tu_match_void(
      i,
      (ir_blocks /**/, _, ), // nothing but namespace stuff
      (ir_blocke /**/, _, ),
      (ir_farg /*  */, _, return scope_emit_inst(err, env, s, i)), // checked elsewhere
      (ir_loops /* */, _, return scope_emit_inst(err, env, s, i)),
      (ir_loope /* */, _, return scope_emit_inst(err, env, s, i)),
      (ir_break /* */, _, return tcheck_break(err, env, s)),
      (ir_cont /*  */, _, return tcheck_continue(err, env, s)),
      (ir_cout /*  */, _, return scope_emit_inst(err, env, s, i)),
      (ir_ifd /*   */, _, return scope_emit_inst(err, env, s, i)),
      (ir_ife /*   */, _, return scope_emit_inst(err, env, s, i)),
      (ir_ifs /*   */, ck, return tcheck_ifs(err, env, s, ck)),
      (ir_putptr /**/, pp, return tcheck_putptr(err, env, s, pp)),
      (ir_getptr /**/, gp, return tcheck_getptr(err, env, s, gp)),
      (ir_getref /**/, gp, return tcheck_getref(err, env, s, gp)),
      (ir_rawdot /**/, rd, return tcheck_rawdot(err, env, s, rd)),
      (ir_rawop /* */, op, return tcheck_rawop(err, env, s, op)),
      (ir_set /*   */, st, return tcheck_set(err, env, s, st)),
      (ir_cast /*  */, cv, return tcheck_cast(err, env, s, cv, false)),
      (ir_ret /*   */, rt, return tcheck_ret(err, env, s, rt)),
      (ir_call /*  */, cl, return tcheck_call(err, env, s, cl)),
      (ir_array /* */, ar, return tcheck_array(err, env, s, ar)),
      (ir_field /* */, fl, return tcheck_field(err, env, s, fl)),
  );
}

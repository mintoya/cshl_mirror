#pragma once
#include "common.h"
#include "function.h"
#include "scope.h"
#include "wheels/macros.h"
#include "wheels/print/print_pre.h"
static const char *const ARG_COUNT_ERROR = "WRONG_ARG_COUNT";
static u32 lower_builtin_fn errs(superscope *ss, scope *s, astidx args) {
  let list = builtin_arg_list(ss->alloc, &ss->asts, args);
  if (list.len != 3) return_err(ARG_COUNT_ERROR);
  let pnode = ast2_at(&ss->asts, list.ptr[0]);
  tu_catch(astPBlock, pnode, return_err("EXPECTED_PBLOCK"));
  return try_err(lower_function, (ss, s, list.ptr[0], list.ptr[1], list.ptr[2]));
}
static u32 lower_builtin_closure(superscope *ss, scope *s, astidx args) { TODO(); }
u32 lower_truthy errs(superscope *ss, scope *s, astidx a) {
  let l = try_err(lower, (ss, s, a));
  let v = scope_find(ss, s, l);
  let t = v->t;
  iflet_tu (let tag, r_tag, t.t)
    if_decl (let info, moxmap_get(ss->type_info, tag.name))
      if_decl (let idx, moxmap_get((*info)->named, fp("@truthy")))
        TODO();
  if (Type_isint(t)) {
    if (tu_is(c_int, t.t))
      return scope_add_value(ss, s, TV(T(tu_of(r_bool, {})), avalue(ss->alloc, (bool)!!BInt.cmp(v->v.c_int, nullptr))));
    let d = scope_add_value(ss, s, TV(T(tu_of(r_bool, {}))));
    scope_add_inst(ss, s, (ir)tu_of(ir_cast, {.l = l, .d = d}));
    return d;
  } else if (tu_is(c_ast, t.t))
    return scope_add_value(ss, s, TV(T(tu_of(r_bool, {})), avalue(ss->alloc, (bool)(v->v.c_ast && !tu_is(astEmpty, ast2_at(&ss->asts, v->v.c_ast))))));
  else if (tu_is(c_type, t.t))
    return scope_add_value(ss, s, TV(T(tu_of(r_bool, {})), avalue(ss->alloc, (bool)(!v->v.c_type || !Type_cmp(T(tu_of(r_void, {})), *v->v.c_type)))));
  else if (tu_is(r_bool, t.t))
    return l;
  else return_err("NOT_TRUTHY");
}
static u32 lower_builtin_cif errs(superscope *ss, scope *s, astidx args) {
  let list = builtin_arg_list(ss->alloc, &ss->asts, args);
  if (!EQUAL_ANY(list.len, 2, 3)) return_err(ARG_COUNT_ERROR);
  let value = ({
    let ns = scope_inferior(ss, s);
    defer { scope_destroy(ss, ns); };
    try_err(lower_truthy, (ss, ns, list.ptr[0]));
    try_err(interpret, (ss, ns));
  });
  if (!tu_is(r_bool, value.t.t)) return_err("CIF_CONDITION_NOT_BOOL");
  if (!value.v.value) return_err("CIF_CONDITION_NOT_COMPTIME");

  if (!!*(bool *)value.v.value) try_err(lower, (ss, s, list.ptr[1]));
  else if (list.len == 3) try_err(lower, (ss, s, list.ptr[2]));

  return scope_add_value(ss, s, TV(T(tu_of(r_void, {}))));
}
static u32 lower_builtin_if errs(superscope *ss, scope *s, astidx args) {
  let list = builtin_arg_list(ss->alloc, &ss->asts, args);
  if (!EQUAL_ANY(list.len, 2, 3)) return_err(ARG_COUNT_ERROR);
  let cnds = scope_inferior(ss, s);
  let cnd = try_err(lower_truthy, (ss, cnds, list.ptr[0]));
  let ts = scope_inferior(ss, s);
  try_err(lower, (ss, ts, list.ptr[1]));
  let fs = list.len == 3 ? scope_inferior(ss, s) : nullptr;
  if (fs) try_err(lower, (ss, fs, list.ptr[2]));
  scope_add_inst(ss, s, (ir)tu_of(ir_if, {.cnd = cnds, .t = ts, .f = fs}));
  return scope_add_value(ss, s, TV(T(tu_of(r_void, {}))));
}
static u32 lower_builtin_while errs(superscope *ss, scope *s, astidx args) {
  let list = builtin_arg_list(ss->alloc, &ss->asts, args);
  if (!EQUAL_ANY(list.len, 1, 2)) return_err(ARG_COUNT_ERROR);
  let cnds = scope_inferior(ss, s);
  try_err(lower_truthy, (ss, cnds, list.ptr[0]));
  let cs = list.len == 2 ? scope_inferior(ss, s) : nullptr;
  if (list.len == 2) try_err(lower, (ss, cs, list.ptr[1]));
  scope_add_inst(ss, s, (ir)tu_of(ir_while, {.cnd = cnds, .body = cs}));
  return scope_add_value(ss, s, TV(T(tu_of(r_void, {}))));
}
static u32 lower_builtin_at errs(superscope *ss, scope *s, astidx args) {
  let list = builtin_arg_list(ss->alloc, &ss->asts, args);
  if (list.len != 1) return_err(ARG_COUNT_ERROR);
  let l = try_err(lower, (ss, s, list.ptr[0]));
  let t = scope_find(ss, s, l)->t;
  if (!t.ptr_level) return_err("AT_NOT_REF");
  Type inner = t;
  inner.ptr_level--;
  let d = scope_add_value(ss, s, TV(inner));
  scope_add_inst(ss, s, (ir)tu_of(ir_putptr, {.l = l, .d = d}));
  return d;
}
static u32 lower_builtin_where errs(superscope *ss, scope *s, astidx args) {
  let list = builtin_arg_list(ss->alloc, &ss->asts, args);
  if (list.len != 1) return_err(ARG_COUNT_ERROR);
  let l = try_err(lower, (ss, s, list.ptr[0]));
  let t = scope_find(ss, s, l)->t;
  let d = scope_add_value(ss, s, TV(Type_ptr(t)));
  scope_add_inst(ss, s, (ir)tu_of(ir_getptr, {.l = l, .d = d}));
  return d;
}
static u32 lower_builtin_ref errs(superscope *ss, scope *s, astidx args) {
  let list = builtin_arg_list(ss->alloc, &ss->asts, args);
  if (list.len != 1) return_err(ARG_COUNT_ERROR);
  let l = try_err(lower, (ss, s, list.ptr[0]));
  let t = scope_find(ss, s, l)->t;
  let d = scope_add_value(ss, s, TV(Type_ref(t)));
  scope_add_inst(ss, s, (ir)tu_of(ir_getref, {.l = l, .d = d}));
  return d;
}

static u32 lower_builtin_return errs(superscope *ss, scope *s, astidx args) {
  // TODO enforce return type
  let list = builtin_arg_list(ss->alloc, &ss->asts, args);
  if (list.len > 1) return_err(ARG_COUNT_ERROR);
  let val = list.len ? try_err(lower, (ss, s, list.ptr[0])) : scope_add_value(ss, s, void_value);
  scope_add_inst(ss, s, (ir)tu_of(ir_ret, val));
  return scope_add_value(ss, s, void_value);
}

static u32 lower_builtin_id errs(superscope *ss, scope *s, astidx args) {
  let str = try_err(temporary_interpret, (ss, s, args));
  let arr = tu_catchr(r_arr, str.t.t, return_err("ID_TYPE_ERROR"));
  if (tu_catchr(r_sint, arr.t[0], return_err("ID_TYPE_ERROR")) != 8)
    return_err("ID_TYPE_ERROR");
  let copy = slice_clone(ss->alloc, ((fptr){arr.count, (u8 *)str.v.value}));
  return scope_add_value(ss, s, (typed_value){.t = T(tu_of(c_ast, {})), .v.c_ast = ast2_make_id(ss, copy)});
}

static astidx qot_walk errs(superscope *ss, scope *s, astidx node) {
  // as long as asts are immutable
  if (!node) return 0;
  let nd = ast2_at(&ss->asts, node);

  iflet_tu (let op, astOp, nd) {
    astidx left = ast2_child(&ss->asts, op.left_right);
    astidx right = ast2_child(&ss->asts, op.left_right + 1);
    if (op.number_rep == opNumbers.call && tu_is(astId, ast2_at(&ss->asts, left))) {
      let left_node = ast2_at(&ss->asts, left);
      if (fptr_eq(ast2_text(ss, tu_catch(astId, left_node)), "@uqot")) {
        let list = builtin_arg_list(ss->alloc, &ss->asts, right);
        if (list.len != 1) return_err(ARG_COUNT_ERROR);
        let tv = try_err(temporary_interpret, (ss, s, list.ptr[0]));
        if (!tu_is(c_ast, tv.t.t)) return_err("UNQUOTE_TYPE_ERROR");
        if (!tv.v.c_ast) return_err("UNQUOTE_UNKNOWN");
        return tv.v.c_ast;
      }
    }
  }

  // TODO change location to something more useful
  return tu_match_exp(
      nd,
      (astNull, _, node),
      (astEmpty, _, node),
      (astId, _, node),
      (astNumber, _, node),
      (astString, _, node),
      (astDecl, decl, ({
         astidx name_idx = ast2_child(&ss->asts, decl.name_type_value);
         astidx type_idx = ast2_child(&ss->asts, decl.name_type_value + 1);
         astidx value_idx = ast2_child(&ss->asts, decl.name_type_value + 2);
         let name = try_err(qot_walk, (ss, s, name_idx));
         let type = try_err(qot_walk, (ss, s, type_idx));
         let value = try_err(qot_walk, (ss, s, value_idx));
         let res = node;
         if (name != name_idx || type != type_idx || value != value_idx) {
           astidx off = (astidx)ss->asts.len + 1;
           sglist_push(ss->asts, ast2_at(&ss->asts, name));
           sglist_push(ss->asts, ast2_at(&ss->asts, type));
           sglist_push(ss->asts, ast2_at(&ss->asts, value));
           astidx nidx = (astidx)ss->asts.len + 1;
           sglist_push(ss->asts, (ast)tu_of(astDecl, {.name_type_value = off, .isconst = decl.isconst}));
           res = nidx;
         }
         res;
       })),
      (astOp, op, ({
         astidx left_idx = ast2_child(&ss->asts, op.left_right);
         astidx right_idx = ast2_child(&ss->asts, op.left_right + 1);
         let left = try_err(qot_walk, (ss, s, left_idx));
         let right = try_err(qot_walk, (ss, s, right_idx));
         let res = node;
         if (left != left_idx || right != right_idx) {
           astidx off = (astidx)ss->asts.len + 1;
           sglist_push(ss->asts, ast2_at(&ss->asts, left));
           sglist_push(ss->asts, ast2_at(&ss->asts, right));
           astidx nidx = (astidx)ss->asts.len + 1;
           sglist_push(ss->asts, (ast)tu_of(astOp, {.left_right = off, .number_rep = op.number_rep}));
           res = nidx;
         }
         res;
       })),
      (astPBlock, block, ({
         let len = ast2_block_children(ss->alloc, &ss->asts, block.start);
         let new_stmts = *acreate(ss->alloc, astidx[len.len ?: 1]);
         bool changed = false;
         foreach (let i, range(0, len.len)) {
           new_stmts[i] = try_err(qot_walk, (ss, s, len.ptr[i]));
           changed = changed || (new_stmts[i] != len.ptr[i]);
         }
         let res = node;
         if (changed) {
           astidx start = (astidx)ss->asts.len + 1;
           foreach (let i, range(0, len.len))
             sglist_push(ss->asts, ast2_at(&ss->asts, new_stmts[i]));
           sglist_push(ss->asts, (ast)tu_of(astNull, {}));
           astidx nidx = (astidx)ss->asts.len + 1;
           sglist_push(ss->asts, (ast)tu_of(astPBlock, {.pos = block.pos, .start = start}));
           res = nidx;
         }
         res;
       })),
      (astBBlock, block, ({
         let len = ast2_block_children(ss->alloc, &ss->asts, block.start);
         let new_stmts = *acreate(ss->alloc, astidx[len.len ?: 1]);
         bool changed = false;
         foreach (let i, range(0, len.len)) {
           new_stmts[i] = try_err(qot_walk, (ss, s, len.ptr[i]));
           changed = changed || (new_stmts[i] != len.ptr[i]);
         }
         let res = node;
         if (changed) {
           astidx start = (astidx)ss->asts.len + 1;
           foreach (let i, range(0, len.len))
             sglist_push(ss->asts, ast2_at(&ss->asts, new_stmts[i]));
           sglist_push(ss->asts, (ast)tu_of(astNull, {}));
           astidx nidx = (astidx)ss->asts.len + 1;
           sglist_push(ss->asts, (ast)tu_of(astBBlock, {.pos = block.pos, .start = start}));
           res = nidx;
         }
         res;
       })),
      (astABlock, block, ({
         let len = ast2_block_children(ss->alloc, &ss->asts, block.start);
         let new_stmts = *acreate(ss->alloc, astidx[len.len ?: 1]);
         bool changed = false;
         foreach (let i, range(0, len.len)) {
           new_stmts[i] = try_err(qot_walk, (ss, s, len.ptr[i]));
           changed = changed || (new_stmts[i] != len.ptr[i]);
         }
         let res = node;
         if (changed) {
           astidx start = (astidx)ss->asts.len + 1;
           foreach (let i, range(0, len.len))
             sglist_push(ss->asts, ast2_at(&ss->asts, new_stmts[i]));
           sglist_push(ss->asts, (ast)tu_of(astNull, {}));
           astidx nidx = (astidx)ss->asts.len + 1;
           sglist_push(ss->asts, (ast)tu_of(astABlock, {.pos = block.pos, .start = start}));
           res = nidx;
         }
         res;
       }))
  );
}
static u32 lower_builtin_qot errs(superscope *ss, scope *s, astidx args) {
  astidx arg = args;
  let list = builtin_arg_list(ss->alloc, &ss->asts, args);
  if (!list.len) arg = ast2_make_empty(ss);
  else if (list.len == 1) arg = list.ptr[0];
  else return_err(ARG_COUNT_ERROR);
  assertMessage(arg);
  return scope_add_value(ss, s, (typed_value){.t = T(tu_of(c_ast, {})), .v.c_ast = try_err(qot_walk, (ss, s, arg))});
}
static u32 lower_builtin_uqot errs(superscope *ss, scope *s, astidx args) {
  let list = builtin_arg_list(ss->alloc, &ss->asts, args);
  if (list.len != 1) return_err(ARG_COUNT_ERROR);
  let tv = try_err(temporary_interpret, (ss, s, list.ptr[0]));
  if (!tu_is(c_ast, tv.t.t)) return_err("UNQUOTE_TYPE_ERROR");
  if (!tv.v.c_ast) return_err("UNQUOTE_UNKNOWN");
  return try_err(lower, (ss, s, tv.v.c_ast));
}

static u32 lower_builtin_reft errs(superscope *ss, scope *s, astidx args) {
  let list = builtin_arg_list(ss->alloc, &ss->asts, args);
  if (list.len != 1) return_err(ARG_COUNT_ERROR);
  Type inner = try_err(value_asType, (ss, try_err(temporary_interpret, (ss, s, list.ptr[0]))));
  return scope_add_value(ss, s, Type_asValue(ss, Type_ref(inner)));
}
static u32 lower_builtin_bare_type errs(superscope *ss, scope *s, astidx args, Type t) {
  let list = builtin_arg_list(ss->alloc, &ss->asts, args);
  if (list.len != 0) return_err(ARG_COUNT_ERROR);
  return scope_add_value(ss, s, Type_asValue(ss, t));
}
static u32 lower_builtin_decor errs(superscope *ss, scope *s, astidx args, bool is_const, bool is_volatile) {
  let list = builtin_arg_list(ss->alloc, &ss->asts, args);
  if (list.len != 1) return_err(ARG_COUNT_ERROR);
  Type inner = try_err(value_asType, (ss, try_err(temporary_interpret, (ss, s, list.ptr[0]))));
  inner.isconst |= is_const;
  inner.isvolatile |= is_volatile;
  return scope_add_value(ss, s, Type_asValue(ss, inner));
}
static u32 lower_builtin_intty errs(superscope *ss, scope *s, astidx args, bool is_signed) {
  let list = builtin_arg_list(ss->alloc, &ss->asts, args);
  if (list.len != 1) return_err(ARG_COUNT_ERROR);
  bigint largest_int = BInt.from.i64(ss->alloc, (u16)~0);
  defer { msList_deInit(ss->alloc, largest_int); };
  bigint bigbits = int_as_bigint(ss->alloc, try_err(temporary_interpret, (ss, s, list.ptr[0])));
  defer {
    msList_deInit(ss->alloc, bigbits);
  };
  if (bigint_cmp(bigbits, nullptr) <= 0) return_err("BAD_INT_WIDTH");
  if (bigint_cmp(bigbits, largest_int) > 0) return_err("BAD_INT_WIDTH");
  return P$(
      (is_signed ? T(tu_of(r_sint, *(u16 *)bigbits)) : T(tu_of(r_uint, *(u16 *)bigbits))),
      scope_add_value(ss, s, Type_asValue(ss, $))
  );
}
static u32 lower_builtin_typeof errs(superscope *ss, scope *s, astidx args) {
  return scope_add_value(ss, s, Type_asValue(ss, scope_find(ss, s, try_err(lower, (ss, s, args)))->t));
}
static u32 lower_builtin_arrof errs(superscope *ss, scope *s, astidx args) {
  let list = builtin_arg_list(ss->alloc, &ss->asts, args);
  if (list.len != 2) return_err(ARG_COUNT_ERROR);
  let count_tv = int_as_bigint(ss->alloc, try_err(temporary_interpret, (ss, s, list.ptr[0])));
  defer { msList_deInit(ss->alloc, count_tv); };
  bigint_expand(ss->alloc, &count_tv, sizeof(usize) / sizeof(bigint_unit) + 1);

  Type elem = try_err(value_asType, (ss, try_err(temporary_interpret, (ss, s, list.ptr[1]))));
  Type arr = T(tu_of(r_arr, {ss_getTypeTree(ss, elem.t), *(usize *)count_tv}));
  return scope_add_value(ss, s, Type_asValue(ss, arr));
}
static u32 lower_builtin_ptrof errs(superscope *ss, scope *s, astidx args) {
  let list = builtin_arg_list(ss->alloc, &ss->asts, args);
  if (list.len != 1) return_err(ARG_COUNT_ERROR);
  Type inner = try_err(value_asType, (ss, try_err(temporary_interpret, (ss, s, list.ptr[0]))));
  return scope_add_value(ss, s, Type_asValue(ss, Type_ptr(inner)));
}
static u32 lower_builtin_sizeof errs(superscope *ss, scope *s, astidx args) {
  let list = builtin_arg_list(ss->alloc, &ss->asts, args);
  if (list.len != 1) return_err(ARG_COUNT_ERROR);
  let t = try_err(value_asType, (ss, try_err(temporary_interpret, (ss, s, list.ptr[0]))));
  return scope_add_value(ss, s, TV_bigint(BInt.from.i64(ss->alloc, Type_sizeof(t))));
}
static u32 lower_builtin_alignof errs(superscope *ss, scope *s, astidx args) {
  let list = builtin_arg_list(ss->alloc, &ss->asts, args);
  if (list.len != 1) return_err(ARG_COUNT_ERROR);
  let t = try_err(value_asType, (ss, try_err(temporary_interpret, (ss, s, list.ptr[0]))));
  return scope_add_value(ss, s, TV_bigint(BInt.from.i64(ss->alloc, Type_alignof(t))));
}
static r_struct find_struct_typetree(TypeTree t) {
  return tu_match_exp(
      t,
      (r_struct, r, r),
      (r_tag, r, find_struct_typetree(*r.t)),
      (default, (r_struct) nullptr)
  );
}
static u32 lower_builtin_offsetof errs(superscope *ss, scope *s, astidx args) {
  let list = builtin_arg_list(ss->alloc, &ss->asts, args);
  if (list.len != 2) return_err(ARG_COUNT_ERROR);
  let t = try_err(value_asType, (ss, try_err(temporary_interpret, (ss, s, list.ptr[0]))));
  let name = try_err(id_from_ast, (ss, s, list.ptr[1]));
  let st = find_struct_typetree(t.t);
  if (!st) return_err("OFFSETOF_NOT_STRUCT");
  foreach (let item, mList_iter(st))
    if (fptr_eq(item.name, name))
      return scope_add_value(ss, s, TV_bigint(BInt.from.i64(ss->alloc, item.offset)));
  return_err("NO_MEMBER");
}

static fptr unique_type_name(superscope *ss) {
  static usize struct_count = 0;
  let str = snprint(ss->alloc, "$struct{usize}", struct_count++);
  str.len--;
  return fp(str);
}

static u32 lower_builtin_struct errs(superscope *ss, scope *s, astidx args) {
  let _struct = mList_init(ss->alloc, r_struct_item);
  // clang-format off
  let _this = ss_getTypeTree(ss,(TypeTree)tu_of(r_tag,{
    avalue(ss->alloc, (TypeTree)tu_of(r_struct, _struct)),
    unique_type_name(ss),
  }));
  // clang-format on
  let working_scope = scope_inferior(ss, s);
  msList_push(ss->alloc, ss->thisstack, {ss_getType(ss, T(*_this))});
  defer { msList_pop(ss->thisstack); };
  moxmap_set(ss->type_info, tu_catch(r_tag, *_this).name, working_scope);

  let list = builtin_arg_list(ss->alloc, &ss->asts, args);
  if (list.len != 1) return_err(ARG_COUNT_ERROR);
  let struct_node = ast2_at(&ss->asts, list.ptr[0]);
  let block = tu_catch(astBBlock, struct_node, return_err("STRUCT_EXPECTS_BLOCK"));
  let stmts = ast2_block_children(ss->alloc, &ss->asts, block.start);

  let offset = (usize){};
  foreach (let node, span(stmts.ptr, stmts.len)) {
    astidx stmt = *node;
    let stmt_node = ast2_at(&ss->asts, stmt);
    let dcl = tu_catch(astDecl, stmt_node, return_err("STRUCT_MEMBER_NOT_DECL"));
    astidx name_idx = ast2_child(&ss->asts, dcl.name_type_value);
    astidx type_idx = ast2_child(&ss->asts, dcl.name_type_value + 1);
    astidx value_idx = ast2_child(&ss->asts, dcl.name_type_value + 2);
    if (!name_idx) return_err("STRUCT_MEMBER_NO_NAME");
    let name = try_err(id_from_ast, (ss, working_scope, name_idx));

    typed_value bound = {};
    if (type_idx) {
      bound.t = try_err(value_asType, (ss, try_err(temporary_interpret, (ss, working_scope, type_idx))));
      if (tu_is(c_type, bound.t.t) && value_idx) {
        bound = try_err(temporary_interpret, (ss, working_scope, value_idx));
        if (!tu_is(c_type, bound.t.t))
          return_err("STRUCT_MEMBER_NOT_TYPE");
      } else bound = TV(*ss_getType(ss, bound.t));
    } else {
      if (!value_idx) return_err("STRUCT_MEMBER_NO_TYPE_OR_VALUE");
      bound = try_err(temporary_interpret, (ss, working_scope, value_idx));
      if (!Type_is_comptime(bound.t))
        return_err("STRUCT_MEMBER_RUNTIME");
    }

    if (dcl.isconst)
      scope_bind(ss, working_scope, scope_add_value(ss, working_scope, bound), name);

    r_struct_item ri = {.name = name, .t = *ss_getType(ss, bound.t)};
    if (!Type_is_comptime(ri.t)) {
      ri.offset = lineup(offset, ri.alignment = Type_alignof(ri.t));
      offset += Type_sizeof(ri.t);
    }
    mList_push(_struct, ri);
  }

  void sexplike(const char *data, void *userdata, usize len, bool flush);
  print_wf(sexplike, "[LOG] created struct : {Type}\n", T(*tu_catch(r_tag, *_this).t));
  print_wf(sexplike, "[LOG] it's scope {scope}\n", *working_scope);
  return scope_add_value(ss, s, TV_type(ss_getType(ss, T(*_this))));
}
static u32 lower_builtin_this errs(superscope *ss, scope *s, astidx args) {
  if (builtin_arg_list(ss->alloc, &ss->asts, args).len) return_err(ARG_COUNT_ERROR);
  if (!msList_len(ss->thisstack)) return_err("THIS_OUTSIDE_STRUCT");
  return scope_add_value(ss, s, (typed_value){T(tu_of(c_type, {})), msList_last(ss->thisstack)});
}
static u32 lower_builtin_break errs(superscope *ss, scope *s, astidx args) {
  if (builtin_arg_list(ss->alloc, &ss->asts, args).len) return_err(ARG_COUNT_ERROR);
  scope_add_inst(ss, s, (ir)tu_of(ir_break, {}));
  return scope_add_value(ss, s, void_value);
}
static u32 lower_builtin_continue errs(superscope *ss, scope *s, astidx args) {
  if (builtin_arg_list(ss->alloc, &ss->asts, args).len) return_err(ARG_COUNT_ERROR);
  scope_add_inst(ss, s, (ir)tu_of(ir_cont, {}));
  return scope_add_value(ss, s, void_value);
}

static u32 lower_builtin errs(superscope *ss, scope *s, builtin_id builtin, astidx args) {
  return switch_exp(
      builtin,
      (builtin_fn, /*      */ try_err(lower_builtin_fn, (ss, s, args))),
      (builtin_closure, /* */ lower_builtin_closure(ss, s, args)),
      (builtin_if, /*      */ try_err(lower_builtin_if, (ss, s, args))),
      (builtin_cif, /*     */ try_err(lower_builtin_cif, (ss, s, args))),
      (builtin_while, /*   */ try_err(lower_builtin_while, (ss, s, args))),
      (builtin_return, /*  */ try_err(lower_builtin_return, (ss, s, args))),
      (builtin_break, /*   */ try_err(lower_builtin_break, (ss, s, args))),
      (builtin_continue, /**/ try_err(lower_builtin_continue, (ss, s, args))),
      (builtin__id, /*     */ try_err(lower_builtin_id, (ss, s, args))),
      (builtin_ast, /*     */ try_err(lower_builtin_bare_type, (ss, s, args, T(tu_of(c_ast, {}))))),
      (builtin_any, /*     */ try_err(lower_builtin_bare_type, (ss, s, args, T(tu_of(c_any, {}))))),
      (builtin_bool, /*    */ try_err(lower_builtin_bare_type, (ss, s, args, T(tu_of(r_bool, {}))))),
      (builtin_void, /*    */ try_err(lower_builtin_bare_type, (ss, s, args, T(tu_of(r_void, {}))))),
      (builtin_int, /*     */ try_err(lower_builtin_bare_type, (ss, s, args, T(tu_of(c_int, {}))))),
      (builtin_type, /*    */ try_err(lower_builtin_bare_type, (ss, s, args, T(tu_of(c_type, {}))))),
      (builtin_const, /*   */ try_err(lower_builtin_decor, (ss, s, args, true, false))),
      (builtin_volatile, /**/ try_err(lower_builtin_decor, (ss, s, args, false, true))),
      (builtin_sint, /*    */ try_err(lower_builtin_intty, (ss, s, args, true))),
      (builtin_uint, /*    */ try_err(lower_builtin_intty, (ss, s, args, false))),
      (builtin_typeof, /*  */ try_err(lower_builtin_typeof, (ss, s, args))),
      (builtin_arrof, /*   */ try_err(lower_builtin_arrof, (ss, s, args))),
      (builtin_ptrof, /*   */ try_err(lower_builtin_ptrof, (ss, s, args))),
      (builtin_sizeof, /*  */ try_err(lower_builtin_sizeof, (ss, s, args))),
      (builtin_alignof, /* */ try_err(lower_builtin_alignof, (ss, s, args))),
      (builtin_offsetof, /**/ try_err(lower_builtin_offsetof, (ss, s, args))),
      (builtin_struct, /*  */ try_err(lower_builtin_struct, (ss, s, args))),
      (builtin__this, /*   */ try_err(lower_builtin_this, (ss, s, args))),
      (builtin_qot, /*     */ try_err(lower_builtin_qot, (ss, s, args))),
      (builtin_uqot, /*    */ try_err(lower_builtin_uqot, (ss, s, args))),
      (builtin_at, /*      */ try_err(lower_builtin_at, (ss, s, args))),
      (builtin_where, /*   */ try_err(lower_builtin_where, (ss, s, args))),
      (builtin_ref, /*     */ try_err(lower_builtin_ref, (ss, s, args))),
      (builtin_reft, /*    */ try_err(lower_builtin_reft, (ss, s, args))),
  );
  assertMessage(false, "unhandled builtin %u", builtin);
}

#pragma once
#include "common.h"
#include "function.h"
#include "scope.h"

static u32 lower_builtin_closure errs(superscope *ss, scope *s, astidx args) { return_err("UNIMPLEMENTED", 0); }
static u32 lower_builtin_fn errs(superscope *ss, scope *s, astidx args) {
  let list = builtin_arg_list(ss, args);
  if (!EQUAL_ANY(list.len, 2, 3)) return_err("WRONG_ARG_COUNT", 0);
  let pnode = ast_at(ss, list.start);
  tu_catch(astPBlock, pnode, return_err("EXPECTED_PBLOCK_FNARGS", 0));
  return try_err(lower_function, (ss, s, list.start, list.start + 1, list.len == 3 ? list.start + 2 : 0), 0);
}
static u32 lower_builtin_cif errs(superscope *ss, scope *s, astidx args) {
  let list = builtin_arg_list(ss, args);
  if (!EQUAL_ANY(list.len, 2, 3)) return_err("WRONG_ARG_COUNT", 0);
  let value = ({
    let ns = P$(scope_inferior(ss, s), ($->rootable = false, $));
    defer { scope_destroy(ss, ns); };
    try_err(lower_condition, (ss, ns, list.start + 0), 0);
    try_err(interpret, (ss, ns), 0);
  });
  if (!value.v.value) return_err("CIF_CONDITION_NOT_COMPTIME", 0);

  if (!!*(bool *)value.v.value) try_err(lower, (ss, s, list.start + 1), 0);
  else if (list.len == 3) try_err(lower, (ss, s, list.start + 2), 0);

  return scope_add_value(ss, s, TV_of(ss, T(tu_of(r_void, {}))));
}
static u32 lower_builtin_if errs(superscope *ss, scope *s, astidx args) {
  let list = builtin_arg_list(ss, args);
  if (!EQUAL_ANY(list.len, 2, 3)) return_err("WRONG_ARG_COUNT", 0);
  u32 cnd = try_err(lower_condition, (ss, s, list.start + 0), 0);
  try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_ifs, cnd)), 0);
  try_err(lower, (ss, s, list.start + 1), 0);
  if (list.len == 3) {
    try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_ifd, {})), 0);
    try_err(lower, (ss, s, list.start + 2), 0);
  }
  try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_ife, {})), 0);
  return scope_add_value(ss, s, TV_of(ss, T(tu_of(r_void, {}))));
}
static u32 lower_builtin_while errs(superscope *ss, scope *s, astidx args) {
  let list = builtin_arg_list(ss, args);
  if (!EQUAL_ANY(list.len, 1, 2)) return_err("WRONG_ARG_COUNT", 0);
  try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_loops, {})), 0);
  try_err(lower_condition, (ss, s, list.start + 0), 0);
  if (list.len == 2) try_err(lower, (ss, s, list.start + 1), 0);
  try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_loope, {})), 0);
  return scope_add_value(ss, s, TV_of(ss, T(tu_of(r_void, {}))));
}
static u32 lower_builtin_at errs(superscope *ss, scope *s, astidx args) {
  let list = builtin_arg_list(ss, args);
  if (list.len != 1) return_err("WRONG_ARG_COUNT", 0);
  let l = try_err(lower, (ss, s, list.start + 0), 0);
  let d = scope_add_value(ss, s, TV_any(ss));
  try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_putptr, {.l = l, .d = d})), 0);
  return d;
}
static u32 lower_builtin_where errs(superscope *ss, scope *s, astidx args) {
  let list = builtin_arg_list(ss, args);
  if (list.len != 1) return_err("WRONG_ARG_COUNT", 0);
  let l = try_err(lower, (ss, s, list.start + 0), 0);
  let d = scope_add_value(ss, s, TV_any(ss));
  try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_getptr, {.l = l, .d = d})), 0);
  return d;
}
static u32 lower_builtin_ref errs(superscope *ss, scope *s, astidx args) {
  let list = builtin_arg_list(ss, args);
  if (list.len != 1) return_err("WRONG_ARG_COUNT", 0);
  let l = try_err(lower, (ss, s, list.start + 0), 0);
  let d = scope_add_value(ss, s, TV_any(ss));
  try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_getref, {.l = l, .d = d})), 0);
  return d;
}

static u32 lower_builtin_return errs(superscope *ss, scope *s, astidx args) {
  let list = builtin_arg_list(ss, args);
  if (list.len > 1) return_err("WRONG_ARG_COUNT", 0);
  let val = list.len ? try_err(lower, (ss, s, list.start + 0), 0) : scope_add_value(ss, s, TV_void(ss));
  try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_ret, val)), 0);
  return scope_add_value(ss, s, TV_void(ss));
}

static u32 lower_builtin_id errs(superscope *ss, scope *s, astidx args) {
  let str = try_err(temporary_interpret, (ss, s, args), 0);
  let arr = tu_catchr(r_arr, ss_type(ss, str.t).t, return_err("ID_TYPE_ERROR", 0));
  if (tu_catchr(r_sint, type_tree(ss_typeenv(ss), arr.t), return_err("ID_TYPE_ERROR", 0)).bits != 8)
    return_err("ID_TYPE_ERROR", 0);
  let copy = slice_clone(ss->alloc, ((fptr){arr.count, (u8 *)str.v.value}));
  return scope_add_value(ss, s, TV_ast(ss, ast_make_id(ss, copy)));
}

// the children of a list ast: a block, or a lone node treated as a one element list
static astidx_span ast_list_span(superscope *ss, astidx a) {
  if (!a) return (astidx_span){0, 0};
  return tu_match_exp(
      ast_at(ss, a),
      (astABlock, _, ast_block_children(ss, a)),
      (astBBlock, _, ast_block_children(ss, a)),
      (astPBlock, _, ast_block_children(ss, a)),
      (default, ((astidx_span){a, 1})),
  );
}
static astidx qot_walk errs(superscope *ss, scope *s, astidx node);
static astidx qot_walk_block errs(superscope *ss, scope *s, astidx node, u8 tag, srcpos pos) {
  let children = ast_block_children(ss, node);
  let out = msList_init(ss->alloc, astidx);
  defer { msList_deInit(ss->alloc, out); };
  bool changed = false;
  foreach (let i, range(0, children.len)) {
    astidx child = children.start + i;
    bool spliced = false;
    let child_node = ast_at(ss, child);
    iflet_tu (let op, astOp, child_node) {
      astidx cleft = ast_child(ss, op.left_right);
      let cleft_node = ast_at(ss, cleft);
      if (op.number_rep == opNumbers.call && cleft && tu_is(astId, cleft_node) &&
          fptr_eq(ast_text(ss, tu_catch(astId, cleft_node)), "@splice")) {
        let list = builtin_arg_list(ss, ast_child(ss, op.left_right + 1));
        if (list.len != 1) return_err("WRONG_ARG_COUNT", 0);
        let tv = try_err(temporary_interpret, (ss, s, list.start + 0), 0);
        if (!tu_is(c_ast, ss_type(ss, tv.t).t)) return_err("SPLICE_TYPE_ERROR", 0);
        if (!tv.v.c_ast) return_err("SPLICE_UNKNOWN", 0);
        let ext = ast_list_span(ss, tv.v.c_ast);
        foreach (let j, range(0, ext.len))
          msList_push(ss->alloc, out, ext.start + j);
        spliced = true;
        changed = true;
      }
    }
    if (!spliced) {
      let walked = try_err(qot_walk, (ss, s, child), 0);
      changed = changed || (walked != child);
      msList_push(ss->alloc, out, walked);
    }
  }
  if (!changed) return node;
  astidx start = (astidx)ss->asts.len + 1;
  foreach (let i, span(out, msList_len(out)))
    sglist_push(ss->asts, ast_at(ss, *i));
  sglist_push(ss->asts, (ast)tu_of(astNull, {}));
  ast rebuilt = tag == TU_TAG(astPBlock) ? (ast)tu_of(astPBlock, {.pos = pos, .start = start})
                : tag == TU_TAG(astBBlock)
                    ? (ast)tu_of(astBBlock, {.pos = pos, .start = start})
                    : (ast)tu_of(astABlock, {.pos = pos, .start = start});
  astidx nidx = (astidx)ss->asts.len + 1;
  sglist_push(ss->asts, rebuilt);
  return nidx;
}
static astidx qot_walk errs(superscope *ss, scope *s, astidx node) {
  // as long as asts are immutable
  if (!node) return 0;
  let nd = ast_at(ss, node);

  iflet_tu (let op, astOp, nd) {
    astidx left = ast_child(ss, op.left_right);
    astidx right = ast_child(ss, op.left_right + 1);
    if (op.number_rep == opNumbers.call && tu_is(astId, ast_at(ss, left))) {
      let left_node = ast_at(ss, left);
      if (fptr_eq(ast_text(ss, tu_catch(astId, left_node)), "@uqot")) {
        let list = builtin_arg_list(ss, right);
        if (list.len != 1) return_err("WRONG_ARG_COUNT", 0);
        let tv = try_err(temporary_interpret, (ss, s, list.start + 0), 0);
        if (!tu_is(c_ast, ss_type(ss, tv.t).t)) return_err("UNQUOTE_TYPE_ERROR", 0);
        if (!tv.v.c_ast) return_err("UNQUOTE_UNKNOWN", 0);
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
         astidx name_idx = ast_child(ss, decl.name_type_value);
         astidx type_idx = ast_child(ss, decl.name_type_value + 1);
         astidx value_idx = ast_child(ss, decl.name_type_value + 2);
         let name = try_err(qot_walk, (ss, s, name_idx), 0);
         let type = try_err(qot_walk, (ss, s, type_idx), 0);
         let value = try_err(qot_walk, (ss, s, value_idx), 0);
         let res = node;
         if (name != name_idx || type != type_idx || value != value_idx) {
           astidx off = (astidx)ss->asts.len + 1;
           sglist_push(ss->asts, ast_at(ss, name));
           sglist_push(ss->asts, ast_at(ss, type));
           sglist_push(ss->asts, ast_at(ss, value));
           astidx nidx = (astidx)ss->asts.len + 1;
           sglist_push(ss->asts, (ast)tu_of(astDecl, {.name_type_value = off, .isconst = decl.isconst}));
           res = nidx;
         }
         res;
       })),
      (astOp, op, ({
         astidx left_idx = ast_child(ss, op.left_right);
         astidx right_idx = ast_child(ss, op.left_right + 1);
         let left = try_err(qot_walk, (ss, s, left_idx), 0);
         let right = try_err(qot_walk, (ss, s, right_idx), 0);
         let res = node;
         if (left != left_idx || right != right_idx) {
           astidx off = (astidx)ss->asts.len + 1;
           sglist_push(ss->asts, ast_at(ss, left));
           sglist_push(ss->asts, ast_at(ss, right));
           astidx nidx = (astidx)ss->asts.len + 1;
           sglist_push(ss->asts, (ast)tu_of(astOp, {.left_right = off, .number_rep = op.number_rep}));
           res = nidx;
         }
         res;
       })),
      (astPBlock, block, try_err(qot_walk_block, (ss, s, node, TU_TAG(astPBlock), block.pos), 0)),
      (astBBlock, block, try_err(qot_walk_block, (ss, s, node, TU_TAG(astBBlock), block.pos), 0)),
      (astABlock, block, try_err(qot_walk_block, (ss, s, node, TU_TAG(astABlock), block.pos), 0))
  );
}
static u32 lower_builtin_qot errs(superscope *ss, scope *s, astidx args) {
  astidx arg = args;
  let list = builtin_arg_list(ss, args);
  if (!list.len) arg = ast_make_empty(ss);
  else if (list.len == 1) arg = list.start + 0;
  else return_err("WRONG_ARG_COUNT", 0);
  assertMessage(arg);
  return scope_add_value(ss, s, TV_ast(ss, try_err(qot_walk, (ss, s, arg), 0)));
}
static u32 lower_builtin_uqot errs(superscope *ss, scope *s, astidx args) {
  let list = builtin_arg_list(ss, args);
  if (list.len != 1) return_err("WRONG_ARG_COUNT", 0);
  let tv = try_err(temporary_interpret, (ss, s, list.start + 0), 0);
  if (!tu_is(c_ast, ss_type(ss, tv.t).t)) return_err("UNQUOTE_TYPE_ERROR", 0);
  if (!tv.v.c_ast) return_err("UNQUOTE_UNKNOWN", 0);
  return try_err(lower, (ss, s, tv.v.c_ast), 0);
}
// build the ast of "a <sym> b", where sym is a comptime string naming the operator, so a macro
// can synthesize any operation ("+", ".") and not just the ones with a builtin of their own
static u32 lower_builtin_ast_op errs(superscope *ss, scope *s, astidx args) {
  let list = builtin_arg_list(ss, args);
  if (list.len != 3) return_err("WRONG_ARG_COUNT", 0);
  let sym = try_err(temporary_interpret, (ss, s, list.start + 0), 0);
  let a = try_err(temporary_interpret, (ss, s, list.start + 1), 0);
  let b = try_err(temporary_interpret, (ss, s, list.start + 2), 0);
  if (!tu_is(c_ast, ss_type(ss, a.t).t) || !tu_is(c_ast, ss_type(ss, b.t).t)) return_err("AST_OP_TYPE_ERROR", 0);
  if (!a.v.c_ast || !b.v.c_ast) return_err("AST_OP_UNKNOWN", 0);
  // a spelling is up to 4 bytes packed into a u32, so pack it the same way the parser does
  Type st = ss_type(ss, sym.t);
  if (!tu_is(r_arr, st.t) || st.ptr_level || st.isref || !st.iscomptime) return_err("AST_OP_SYM_NOT_STRING", 0);
  let arr = tu_catch(r_arr, st.t);
  if (tu_catchr(r_sint, type_tree(ss_typeenv(ss), arr.t), return_err("AST_OP_SYM_NOT_STRING", 0)).bits != 8)
    return_err("AST_OP_SYM_NOT_STRING", 0);
  if (!sym.v.value) return_err("AST_OP_SYM_UNKNOWN", 0);
  if (arr.count > sizeof(u32)) return_err("AST_OP_SYM_TOO_LONG", 0);
  union {
    u32 n;
    char s[sizeof(u32)];
  } spelling = {};
  memcpy(spelling.s, sym.v.value, arr.count);
  op_as_enum(spelling.n); // not an operator, so this is a mistake in the macro
  astidx off = (astidx)ss->asts.len + 1;
  sglist_push(ss->asts, ast_at(ss, a.v.c_ast));
  sglist_push(ss->asts, ast_at(ss, b.v.c_ast));
  sglist_push(ss->asts, (ast)tu_of(astOp, {.left_right = off, .number_rep = spelling.n}));
  return scope_add_value(ss, s, TV_ast(ss, (astidx)ss->asts.len));
}
// call with evaluated arguments, so an ast parameter receives an ast value rather than
// the syntax it was written as
static u32 lower_builtin_apply errs(superscope *ss, scope *s, astidx args) {
  let list = builtin_arg_list(ss, args);
  if (list.len < 1) return_err("WRONG_ARG_COUNT", 0);
  let fn = try_err(lower, (ss, s, list.start + 0), 0);
  if (!tu_is(c_fun, ss_type(ss, scope_find(ss, s, fn)->t).t)) return_err("APPLY_NOT_FUNCTION", 0);
  usize n = list.len - 1;
  let vals = n ? slice_alloc(ss->alloc, u32, n) : (slice(u32)){};
  foreach (let i, range(0, n))
    vals.ptr[i] = try_err(lower, (ss, s, list.start + 1 + i), 0);
  return try_err(lower_function_call_values, (ss, s, fn, vals), 0);
}

static u32 lower_builtin_reft errs(superscope *ss, scope *s, astidx args) {
  let list = builtin_arg_list(ss, args);
  if (list.len != 1) return_err("WRONG_ARG_COUNT", 0);
  Type inner = try_err(value_asType, (ss, try_err(temporary_interpret, (ss, s, list.start + 0), 0)), 0);
  return scope_add_value(ss, s, Type_asValue(ss, try_err(Type_ref, (inner), 0)));
}
static u32 lower_builtin_bare_type errs(superscope *ss, scope *s, astidx args, Type t) {
  let list = builtin_arg_list(ss, args);
  if (list.len != 0) return_err("WRONG_ARG_COUNT", 0);
  return scope_add_value(ss, s, Type_asValue(ss, t));
}
static u32 lower_builtin_decor errs(superscope *ss, scope *s, astidx args, bool is_const, bool is_volatile) {
  let list = builtin_arg_list(ss, args);
  if (list.len != 1) return_err("WRONG_ARG_COUNT", 0);
  Type inner = try_err(value_asType, (ss, try_err(temporary_interpret, (ss, s, list.start + 0), 0)), 0);
  inner.isconst |= is_const;
  inner.isvolatile |= is_volatile;
  return scope_add_value(ss, s, Type_asValue(ss, inner));
}
static u32 lower_builtin_intty errs(superscope *ss, scope *s, astidx args, bool is_signed) {
  let list = builtin_arg_list(ss, args);
  if (list.len != 1) return_err("WRONG_ARG_COUNT", 0);
  bigint largest_int = BInt.from.i64(ss->alloc, (u16)~0);
  defer { msList_deInit(ss->alloc, largest_int); };
  bigint bigbits = int_as_bigint(ss->alloc, try_err(temporary_interpret, (ss, s, list.start + 0), 0), ss_typeenv(ss));
  defer {
    msList_deInit(ss->alloc, bigbits);
  };
  if (bigint_cmp(bigbits, nullptr) <= 0) return_err("BAD_INT_WIDTH", 0);
  if (bigint_cmp(bigbits, largest_int) > 0) return_err("BAD_INT_WIDTH", 0);
  return P$(
      (is_signed ? T(TypeTree_sint(*(u16 *)bigbits)) : T(TypeTree_uint(*(u16 *)bigbits))),
      scope_add_value(ss, s, Type_asValue(ss, $))
  );
}
static u32 lower_builtin_typeof errs(superscope *ss, scope *s, astidx args) {
  return scope_add_value(ss, s, Type_asValue(ss, ss_type(ss, scope_find(ss, s, try_err(lower, (ss, s, args), 0))->t)));
}
static u32 lower_builtin_arrof errs(superscope *ss, scope *s, astidx args) {
  let list = builtin_arg_list(ss, args);
  if (list.len != 2) return_err("WRONG_ARG_COUNT", 0);
  let count_tv = int_as_bigint(ss->alloc, try_err(temporary_interpret, (ss, s, list.start + 0), 0), ss_typeenv(ss));
  defer { msList_deInit(ss->alloc, count_tv); };
  bigint_expand(ss->alloc, &count_tv, sizeof(usize) / sizeof(bigint_unit) + 1);
  // an array is inlined where it sits, so a count that does not fit the field is unrepresentable
  usize count = *(usize *)count_tv;
  if (count > (usize) ~(u32)0) return_err("ARRAY_TOO_LARGE", 0);
  Type elem = try_err(value_asType, (ss, try_err(temporary_interpret, (ss, s, list.start + 1), 0)), 0);
  return scope_add_value(ss, s, Type_asValue(ss, try_err(Type_arr, (ss, elem, count), 0)));
}
static u32 lower_builtin_ptrof errs(superscope *ss, scope *s, astidx args) {
  let list = builtin_arg_list(ss, args);
  if (list.len != 1) return_err("WRONG_ARG_COUNT", 0);
  Type inner = try_err(value_asType, (ss, try_err(temporary_interpret, (ss, s, list.start + 0), 0)), 0);
  return scope_add_value(ss, s, Type_asValue(ss, try_err(Type_ptr, (inner), 0)));
}
static u32 lower_builtin_sizeof errs(superscope *ss, scope *s, astidx args) {
  let list = builtin_arg_list(ss, args);
  if (list.len != 1) return_err("WRONG_ARG_COUNT", 0);
  let t = try_err(value_asType, (ss, try_err(temporary_interpret, (ss, s, list.start + 0), 0)), 0);
  return scope_add_value(ss, s, TV_bigint(ss, BInt.from.i64(ss->alloc, Type_sizeof(t, ss_typeenv(ss)))));
}
static u32 lower_builtin_alignof errs(superscope *ss, scope *s, astidx args) {
  let list = builtin_arg_list(ss, args);
  if (list.len != 1) return_err("WRONG_ARG_COUNT", 0);
  let t = try_err(value_asType, (ss, try_err(temporary_interpret, (ss, s, list.start + 0), 0)), 0);
  return scope_add_value(ss, s, TV_bigint(ss, BInt.from.i64(ss->alloc, Type_alignof(t, ss_typeenv(ss)))));
}
static u32 lower_builtin_offsetof errs(superscope *ss, scope *s, astidx args) {
  let list = builtin_arg_list(ss, args);
  if (list.len != 2) return_err("WRONG_ARG_COUNT", 0);
  let t = try_err(value_asType, (ss, try_err(temporary_interpret, (ss, s, list.start + 0), 0)), 0);
  let name = try_err(id_from_ast, (ss, s, list.start + 1), 0);
  let e = ss_typeenv(ss);
  let st = TypeTree_inner_struct(t.t, e);
  if (!st.start) return_err("OFFSETOF_NOT_STRUCT", 0);
  foreach (let i, range(0, type_struct_len(e, st))) {
    let item = type_struct_item(e, st, i);
    // member names are positions, so they only match when they spell the same thing
    if (!fptr_cmp(type_name(e, item->name), name))
      return scope_add_value(ss, s, TV_bigint(ss, BInt.from.i64(ss->alloc, item->offset)));
  }
  return_err("NO_MEMBER", 0);
}

static srcpos unique_type_name(superscope *ss) {
  static usize struct_count = 0;
  let str = snprint(ss->alloc, "$struct{usize}", struct_count++);
  str.len--;
  return ss_srcpos_of_text(ss, fp(str));
}

static u32 lower_builtin_struct errs(superscope *ss, scope *s, astidx args) {
  let list = builtin_arg_list(ss, args);
  if (list.len != 1) return_err("WRONG_ARG_COUNT", 0);
  let struct_node = ast_at(ss, list.start + 0);
  tu_catch(astBBlock, struct_node, return_err("STRUCT_EXPECTS_BLOCK", 0));
  let stmts = ast_block_children(ss, list.start);

  let members = (r_struct){.start = ss_member_reserve(ss, stmts.len)};
  let _this = T(tu_of(r_tag, {ss_type_intern(ss, T(tu_of(r_struct, members))), unique_type_name(ss)}));
  let this_idx = ss_type_intern(ss, _this);

  let working_scope = scope_inferior(ss, scope_find_rootable(ss, s));
  msList_push(ss->alloc, ss->thisstack, this_idx);
  defer { msList_pop(ss->thisstack); };
  moxmap_set(ss->type_info, type_name(ss_typeenv(ss), _this.t.r_tag.name), working_scope);

  let offset = (usize){};
  let e = ss_typeenv(ss);
  foreach (let i, range(0, stmts.len)) {
    e = ss_typeenv(ss);
    let stmt_node = ast_at(ss, stmts.start + i);
    let dcl = tu_catch(astDecl, stmt_node, return_err("STRUCT_MEMBER_NOT_DECL", 0));
    astidx name_idx = ast_child(ss, dcl.name_type_value);
    astidx type_idx = ast_child(ss, dcl.name_type_value + 1);
    astidx value_idx = ast_child(ss, dcl.name_type_value + 2);
    if (!name_idx) return_err("STRUCT_MEMBER_NO_NAME", 0);
    let name = try_err(id_from_ast, (ss, working_scope, name_idx), 0);

    typed_value bound = {};
    if (type_idx) {
      let bt = try_err(value_asType, (ss, try_err(temporary_interpret, (ss, working_scope, type_idx), 0)), 0);
      if (tu_is(c_type, bt.t) && value_idx) {
        bound = try_err(temporary_interpret, (ss, working_scope, value_idx), 0);
        if (!tu_is(c_type, ss_type(ss, bound.t).t))
          return_err("STRUCT_MEMBER_NOT_TYPE", 0);
      } else bound = TV_of(ss, *ss_getType(ss, bt));
    } else {
      if (!value_idx) return_err("STRUCT_MEMBER_NO_TYPE_OR_VALUE", 0);
      bound = try_err(temporary_interpret, (ss, working_scope, value_idx), 0);
      if (!Type_is_comptime(ss_type(ss, bound.t)))
        return_err("STRUCT_MEMBER_RUNTIME", 0);
    }

    if (dcl.isconst)
      try_err(scope_bind, (ss, working_scope, scope_add_value(ss, working_scope, bound), name), 0);

    r_struct_item *ri = type_struct_item(e, members, i);
    ri->name = id_srcpos(ss, name_idx, name);
    ri->t = ss_type(ss, bound.t);
    if (!Type_is_comptime(ri->t)) {
      ri->offset = lineup(offset, ri->alignment = Type_alignof(ri->t, e));
      offset += Type_sizeof(ri->t, e);
    }
  }

  e = ss_typeenv(ss);
  void sexplike(const char *data, void *userdata, usize len, bool flush);
  print_wf(sexplike, "[LOG] created struct : {Type_full}\n", ((Type_full){type_at(e, _this.t.r_tag.t), e}));
  print_wf(sexplike, "[LOG] it's scope {scope_full}\n", ((scope_full){working_scope, ss}));
  return scope_add_value(ss, s, Type_asValue(ss, _this));
}
static u32 lower_builtin_this errs(superscope *ss, scope *s, astidx args) {
  if (builtin_arg_list(ss, args).len) return_err("WRONG_ARG_COUNT", 0);
  if (!msList_len(ss->thisstack)) return_err("THIS_OUTSIDE_STRUCT", 0);
  return scope_add_value(ss, s, TV_type(ss, msList_last(ss->thisstack)));
}
static u32 lower_builtin_break errs(superscope *ss, scope *s, astidx args) {
  if (builtin_arg_list(ss, args).len) return_err("WRONG_ARG_COUNT", 0);
  try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_break, {})), 0);
  return scope_add_value(ss, s, TV_void(ss));
}
static u32 lower_builtin_continue errs(superscope *ss, scope *s, astidx args) {
  if (builtin_arg_list(ss, args).len) return_err("WRONG_ARG_COUNT", 0);
  try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_cont, {})), 0);
  return scope_add_value(ss, s, TV_void(ss));
}

static u32 lower_builtin errs(superscope *ss, scope *s, builtin_id builtin, astidx args) {
  return switch_exp(
      builtin,
      (builtin_fn, /*      */ try_err(lower_builtin_fn, (ss, s, args), 0)),
      (builtin_closure, /* */ try_err(lower_builtin_closure, (ss, s, args), 0)),
      (builtin_if, /*      */ try_err(lower_builtin_if, (ss, s, args), 0)),
      (builtin_cif, /*     */ try_err(lower_builtin_cif, (ss, s, args), 0)),
      (builtin_loop, /*    */ try_err(lower_builtin_while, (ss, s, args), 0)),
      (builtin_return, /*  */ try_err(lower_builtin_return, (ss, s, args), 0)),
      (builtin_break, /*   */ try_err(lower_builtin_break, (ss, s, args), 0)),
      (builtin_continue, /**/ try_err(lower_builtin_continue, (ss, s, args), 0)),
      (builtin__id, /*     */ try_err(lower_builtin_id, (ss, s, args), 0)),
      (builtin_ast, /*     */ try_err(lower_builtin_bare_type, (ss, s, args, T(tu_of(c_ast, {}))), 0)),
      (builtin_any, /*     */ try_err(lower_builtin_bare_type, (ss, s, args, T(tu_of(c_any, {}))), 0)),
      (builtin_bool, /*    */ try_err(lower_builtin_bare_type, (ss, s, args, T(tu_of(r_bool, {}))), 0)),
      (builtin_void, /*    */ try_err(lower_builtin_bare_type, (ss, s, args, T(tu_of(r_void, {}))), 0)),
      (builtin_int, /*     */ try_err(lower_builtin_bare_type, (ss, s, args, T(tu_of(c_int, {}))), 0)),
      (builtin_type, /*    */ try_err(lower_builtin_bare_type, (ss, s, args, T(tu_of(c_type, {}))), 0)),
      (builtin_const, /*   */ try_err(lower_builtin_decor, (ss, s, args, true, false), 0)),
      (builtin_volatile, /**/ try_err(lower_builtin_decor, (ss, s, args, false, true), 0)),
      (builtin_sint, /*    */ try_err(lower_builtin_intty, (ss, s, args, true), 0)),
      (builtin_uint, /*    */ try_err(lower_builtin_intty, (ss, s, args, false), 0)),
      (builtin_typeof, /*  */ try_err(lower_builtin_typeof, (ss, s, args), 0)),
      (builtin_arrof, /*   */ try_err(lower_builtin_arrof, (ss, s, args), 0)),
      (builtin_ptrof, /*   */ try_err(lower_builtin_ptrof, (ss, s, args), 0)),
      (builtin_sizeof, /*  */ try_err(lower_builtin_sizeof, (ss, s, args), 0)),
      (builtin_alignof, /* */ try_err(lower_builtin_alignof, (ss, s, args), 0)),
      (builtin_offsetof, /**/ try_err(lower_builtin_offsetof, (ss, s, args), 0)),
      (builtin_struct, /*  */ try_err(lower_builtin_struct, (ss, s, args), 0)),
      (builtin__this, /*   */ try_err(lower_builtin_this, (ss, s, args), 0)),
      (builtin_qot, /*     */ try_err(lower_builtin_qot, (ss, s, args), 0)),
      (builtin_uqot, /*    */ try_err(lower_builtin_uqot, (ss, s, args), 0)),
      (builtin_ast_op, /*  */ try_err(lower_builtin_ast_op, (ss, s, args), 0)),
      (builtin_apply, /*   */ try_err(lower_builtin_apply, (ss, s, args), 0)),
      (builtin_at, /*      */ try_err(lower_builtin_at, (ss, s, args), 0)),
      (builtin_where, /*   */ try_err(lower_builtin_where, (ss, s, args), 0)),
      (builtin_ref, /*     */ try_err(lower_builtin_ref, (ss, s, args), 0)),
      (builtin_reft, /*    */ try_err(lower_builtin_reft, (ss, s, args), 0)),
  );
  assertMessage(false, "unhandled builtin %u", builtin);
}

#pragma once
#include "common.h"
#include "scope.h"
#include "wheels/macros.h"
#include "wheels/sList.h"

static u32 lower_builtin_fn(superscope *ss, scope *s, ast *args) { TODO(); }
static u32 lower_builtin_closure(superscope *ss, scope *s, ast *args) { TODO(); }
static u32 lower_builtin_if(superscope *ss, scope *s, ast *args) { TODO(); }
static u32 lower_builtin_cif(superscope *ss, scope *s, ast *args) { TODO(); }
static u32 lower_builtin_while(superscope *ss, scope *s, ast *args) { TODO(); }
static u32 lower_builtin_at(superscope *ss, scope *s, ast *args) { TODO(); }
static u32 lower_builtin_where(superscope *ss, scope *s, ast *args) { TODO(); }
static u32 lower_builtin_ref(superscope *ss, scope *s, ast *args) { TODO(); }

static u32 lower_builtin_return(superscope *ss, scope *s, ast *args) {
  // TODO enforce return type
  let list = builtin_arg_list(&args);
  if (list.len > 1) error_print(args, "return dont take args");
  let val = list.len ? lower(ss, s, list.ptr[0]) : scope_add_value(ss, s, void_value);
  scope_add_inst(ss, s, (ir)tu_of(ir_ret, val));
  return scope_add_value(ss, s, void_value);
}

static u32 lower_builtin_id(superscope *ss, scope *s, ast *args) {
  let str = temporary_interpret(ss, s, args);
  let arr = tu_catchr(r_arr, str.t.t, error_print(args, "id takes an array of i8"));
  if (tu_catchr(r_sint, arr.t[0], error_print(args, "id takes an array of i8")) != 8)
    error_print(args, "id takes an array of i8");
  let copy = slice_clone(ss->alloc, ((fptr){arr.count, (u8 *)str.v.value}));
  return scope_add_value(ss, s, (typed_value){T(tu_of(c_ast, {})), avalue(ss->alloc, (ast)tu_of(astId, copy))});
}

static astptr qot_walk(superscope *ss, scope *s, astptr node) {
  // as long as asts are immutable
  if (!node) return avalue(ss->alloc, (ast)tu_of(astEmptyStatement, {}));
  let nd = *node;

  iflet_tu (let op, astOp, nd)
    if (op.number_rep == opNumbers.call && tu_is(astId, *op.left)) {
      if (fptr_eq(tu_catch(astId, *op.left), "@uqot")) {
        let args = op.right;
        let list = builtin_arg_list(&args);
        assertMessage(list.len == 1, "@uqot takes exactly one argument");
        let tv = temporary_interpret(ss, s, list.ptr[0]);
        if (!tu_is(c_ast, tv.t.t)) error_print(node, "tried to unquote a non-ast");
        if (!tv.v.c_ast) error_print(node, "@uqot got a null ast?");
        return tv.v.c_ast;
      }
    }

  // TODO change location to something more useful
  return tu_match_exp(
      nd,
      (astEmptyStatement, _, node),
      (astId, _, node),
      (astNumber, _, node),
      (astString, _, node),
      (astDecl, decl, ({
         let name = qot_walk(ss, s, decl.name);
         let type = qot_walk(ss, s, decl.type);
         let value = qot_walk(ss, s, decl.value);
         let res = node;
         if (name != decl.name || type != decl.type || value != decl.value)
           res = avalue(ss->alloc, (ast)tu_of(astDecl, {.name = name, .type = type, .value = value, .is_const = decl.is_const}));
         res;
       })),
      (astOp, op, ({
         let left = qot_walk(ss, s, op.left);
         let right = qot_walk(ss, s, op.right);
         let res = node;
         if (left != op.left || right != op.right)
           res = avalue(ss->alloc, (ast)tu_of(astOp, {.left = left, .right = right, .number_rep = op.number_rep}));
         res;
       })),
      (astPBlock, block, ({
         let len = sentList_len(block.stmts);
         let stmts = sentList_vla(block.stmts);
         let new_stmts = *acreate(ss->alloc, astptr[len + 1]);
         bool changed = false;
         foreach (let i, range(0, len)) {
           new_stmts[i] = qot_walk(ss, s, stmts[0][i]);
           changed = changed || (new_stmts[i] != stmts[0][i]);
         }
         let res = node;
         if (changed) {
           new_stmts[len] = nullptr;
           res = avalue(ss->alloc, (ast)tu_of(astPBlock, {.loc = block.loc, .stmts = new_stmts}));
         }
         res;
       })),
      (astBBlock, block, ({
         let len = sentList_len(block.stmts);
         let stmts = sentList_vla(block.stmts);
         let new_stmts = *acreate(ss->alloc, astptr[len + 1]);
         bool changed = false;
         foreach (let i, range(0, len)) {
           new_stmts[i] = qot_walk(ss, s, stmts[0][i]);
           changed = changed || (new_stmts[i] != stmts[0][i]);
         }
         let res = node;
         if (changed) {
           new_stmts[len] = nullptr;
           res = avalue(ss->alloc, (ast)tu_of(astBBlock, {.loc = block.loc, .stmts = new_stmts}));
         }
         res;
       })),
      (astABlock, block, ({
         let len = sentList_len(block.items);
         let items = sentList_vla(block.items);
         let new_stmts = *acreate(ss->alloc, astptr[len + 1]);
         bool changed = false;
         foreach (let i, range(0, len)) {
           new_stmts[i] = qot_walk(ss, s, items[0][i]);
           changed = changed || (new_stmts[i] != items[0][i]);
         }
         let res = node;
         if (changed) {
           new_stmts[len] = nullptr;
           res = avalue(ss->alloc, (ast)tu_of(astABlock, {.loc = block.loc, .items = new_stmts}));
         }
         res;
       }))
  );
}
static u32 lower_builtin_qot(superscope *ss, scope *s, ast *args) {
  ast *arg = args;
  let list = builtin_arg_list(&args);
  if (list.len == 0) arg = ({static ast ex = tu_of(astEmptyStatement, {}); &ex; });
  else if (list.len == 1) arg = list.ptr[0];
  else error_print(args, "qot takes at most 1 arg");
  assertMessage(arg);
  return scope_add_value(ss, s, (typed_value){T(tu_of(c_ast, {})), qot_walk(ss, s, arg)});
}
static u32 lower_builtin_uqot(superscope *ss, scope *s, ast *args) {
  let list = builtin_arg_list(&args);
  if (list.len != 1) error_print(args, "@uqot(arg) takes one argument");
  let tv = temporary_interpret(ss, s, list.ptr[0]);
  if (!tu_is(c_ast, tv.t.t)) error_print(args, "tried to unquote a non-ast");
  assertMessage(tv.v.c_ast, "@uqot argument is null?");
  return lower(ss, s, tv.v.c_ast);
}

static u32 lower_builtin_reft(superscope *ss, scope *s, ast *args) {
  let list = builtin_arg_list(&args);
  if (list.len != 1) error_print(args, "@reft(arg) takes exactly one argument");
  Type inner = value_asType_loc(temporary_interpret(ss, s, list.ptr[0]), ast_loc(*args));
  return scope_add_value(ss, s, Type_asValue(ss, Type_ref(inner)));
}
static u32 lower_builtin_bare_type(superscope *ss, scope *s, ast *args, Type t) {
  let list = builtin_arg_list(&args);
  assertMessage(list.len == 0, "@(type)() takes no arguments");
  return scope_add_value(ss, s, Type_asValue(ss, t));
}
static u32 lower_builtin_decor(superscope *ss, scope *s, ast *args, bool is_const, bool is_volatile) {
  let list = builtin_arg_list(&args);
  if (list.len != 1) error_print(args, "@const//volatile(T) takes exactly one argument");
  Type inner = value_asType_loc(temporary_interpret(ss, s, list.ptr[0]), ast_loc(*args));
  inner.isconst |= is_const;
  inner.isvolatile |= is_volatile;
  return scope_add_value(ss, s, Type_asValue(ss, inner));
}
static u32 lower_builtin_intty(superscope *ss, scope *s, ast *args, bool is_signed) {
  let list = builtin_arg_list(&args);
  if (list.len != 1) error_print(args, "@sint//uint(N) takes exactly one argument");
  bigint largest_int = BInt.from.i64(ss->alloc, (u16)~0);
  defer { msList_deInit(ss->alloc, largest_int); };
  bigint bigbits = int_as_bigint(ss, temporary_interpret(ss, s, list.ptr[0]));
  defer { msList_deInit(ss->alloc, bigbits); };
  if (bigint_cmp(bigbits, nullptr) <= 0) error_print(args, "integer width must be positive");
  if (bigint_cmp(bigbits, largest_int) > 0) error_print(args, "integer width too large");
  return P$(
      (is_signed ? T(tu_of(r_sint, *(u16 *)bigbits)) : T(tu_of(r_uint, *(u16 *)bigbits))),
      scope_add_value(ss, s, Type_asValue(ss, $))
  );
}
static u32 lower_builtin_typeof(superscope *ss, scope *s, ast *args) {
  return scope_add_value(ss, s, Type_asValue(ss, temporary_interpret(ss, s, args).t));
}
static u32 lower_builtin_arrof(superscope *ss, scope *s, ast *args) {
  let list = builtin_arg_list(&args);
  assertMessage(list.len == 2, "@arrof(N, T) takes exactly two arguments");
  let count_tv = int_as_bigint(ss, temporary_interpret(ss, s, list.ptr[0]));
  defer { msList_deInit(ss->alloc, count_tv); };
  bigint_expand(ss->alloc, &count_tv, sizeof(usize) / sizeof(bigint_unit) + 1);

  Type elem = value_asType_loc(temporary_interpret(ss, s, list.ptr[1]), ast_loc(*args));
  Type arr = T(tu_of(r_arr, {ss_getTypeTree(ss, elem.t), *(usize *)count_tv}));
  return scope_add_value(ss, s, Type_asValue(ss, arr));
}
static u32 lower_builtin_ptrof(superscope *ss, scope *s, ast *args) {
  let list = builtin_arg_list(&args);
  if (list.len != 1) error_print(args, "@ptrof takes one argument");
  Type inner = value_asType_loc(temporary_interpret(ss, s, list.ptr[0]), ast_loc(*args));
  return scope_add_value(ss, s, Type_asValue(ss, Type_ptr(inner)));
}

fptr unique_type_name(superscope *ss) {
  static usize count = 0;
  count++;
  return fp(snprint(ss->alloc, "struct{}", count));
}
static u32 lower_builtin_struct(superscope *ss, scope *s, ast *args) {
  let _struct = mList_init(ss->alloc, r_struct_item);
  // clang-format off
  let _this = ss_getTypeTree(ss,(TypeTree)tu_of(r_tag,{
    avalue(ss->alloc, (TypeTree)tu_of(r_struct, _struct)),
    unique_type_name(ss),
  }));
  // clang-format on
  msList_push(ss->alloc, ss->thisstack, {});
  defer msList_pop(ss->thisstack);
  let working_scope = scope_inferior(ss, ss->outermost); // ?
  //
  TODO();
  //
  return scope_add_value(ss, s, TV_type(ss_getType(ss, T(*_this))));
}
static u32 lower_builtin_this(superscope *ss, scope *s, ast *args) {
  if (builtin_arg_list(&args).len) error_print(args, "@this takes no arguments");
  if (!msList_len(ss->thisstack)) error_print(args, "@this() used outside of a struct definition");
  return scope_add_value(ss, s, (typed_value){T(tu_of(c_type, {})), msList_last(ss->thisstack).t});
}
static u32 lower_builtin_break(superscope *ss, scope *s, ast *args) {
  if (builtin_arg_list(&args).len) error_print(args, "break takes no args");
  scope_add_inst(ss, s, (ir)tu_of(ir_break, {}));
  return scope_add_value(ss, s, void_value);
}
static u32 lower_builtin_continue(superscope *ss, scope *s, ast *args) {
  if (builtin_arg_list(&args).len) error_print(args, "break takes no args");
  scope_add_inst(ss, s, (ir)tu_of(ir_cont, {}));
  return scope_add_value(ss, s, void_value);
}

static u32 lower_builtin(superscope *ss, scope *s, builtin_id builtin, ast *args) {
  return switch_exp(
      builtin,
      (builtin_fn, /*      */ lower_builtin_fn(ss, s, args)),
      (builtin_closure, /* */ lower_builtin_closure(ss, s, args)),
      (builtin_if, /*      */ lower_builtin_if(ss, s, args)),
      (builtin_cif, /*     */ lower_builtin_cif(ss, s, args)),
      (builtin_while, /*   */ lower_builtin_while(ss, s, args)),
      (builtin_return, /*  */ lower_builtin_return(ss, s, args)),
      (builtin_break, /*   */ lower_builtin_break(ss, s, args)),
      (builtin_continue, /**/ lower_builtin_continue(ss, s, args)),
      (builtin__id, /*     */ lower_builtin_id(ss, s, args)),
      (builtin_ast, /*     */ lower_builtin_bare_type(ss, s, args, T(tu_of(c_ast, {})))),
      (builtin_any, /*     */ lower_builtin_bare_type(ss, s, args, T(tu_of(c_any, {})))),
      (builtin_void, /*    */ lower_builtin_bare_type(ss, s, args, T(tu_of(r_void, {})))),
      (builtin_int, /*     */ lower_builtin_bare_type(ss, s, args, T(tu_of(c_int, {})))),
      (builtin_type, /*    */ lower_builtin_bare_type(ss, s, args, T(tu_of(c_type, {})))),
      (builtin_const, /*   */ lower_builtin_decor(ss, s, args, true, false)),
      (builtin_volatile, /**/ lower_builtin_decor(ss, s, args, false, true)),
      (builtin_sint, /*    */ lower_builtin_intty(ss, s, args, true)),
      (builtin_uint, /*    */ lower_builtin_intty(ss, s, args, false)),
      (builtin_typeof, /*  */ lower_builtin_typeof(ss, s, args)),
      (builtin_arrof, /*   */ lower_builtin_arrof(ss, s, args)),
      (builtin_ptrof, /*   */ lower_builtin_ptrof(ss, s, args)),
      (builtin_struct, /*  */ lower_builtin_struct(ss, s, args)),
      (builtin__this, /*   */ lower_builtin_this(ss, s, args)),
      (builtin_qot, /*     */ lower_builtin_qot(ss, s, args)),
      (builtin_uqot, /*    */ lower_builtin_uqot(ss, s, args)),
      (builtin_at, /*      */ lower_builtin_at(ss, s, args)),
      (builtin_where, /*   */ lower_builtin_where(ss, s, args)),
      (builtin_ref, /*     */ lower_builtin_ref(ss, s, args)),
      (builtin_reft, /*    */ lower_builtin_reft(ss, s, args)),
  );
  assertMessage(false, "unhandled builtin %u", builtin);
}

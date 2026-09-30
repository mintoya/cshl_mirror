#pragma once
#include "common.h"
#include "lower_op.h"

u32 lower_op_wrap errs(superscope *ss, scope *s, astidx left, astidx right, u32 number) {
  if (number == opNumbers.call && tu_or(astOp, ast_at(ss, left), {}).number_rep == opNumbers.colon) {
    let left_node = ast_at(ss, left);
    let colon = tu_catch(astOp, left_node);
    let a = try_err(lower, (ss, s, ast_child(ss, colon.left_right)), 0);
    let c = try_err(lower_op, (ss, s, a, ast_child(ss, colon.left_right + 1), opNumbers.dot), 0);
    return try_err(lower_call_extra, (ss, s, c, a, right), 0);
  }
  if (number == opNumbers.set && tu_is(astId, ast_at(ss, left))) {
    let left_node = ast_at(ss, left);
    let dst = try_err(ss_get_named_mut, (ss, s, ast_text(ss, tu_catch(astId, left_node))), 0);
    try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_set, {.d = dst, .l = try_err(lower, (ss, s, right), 0)})), 0);
    return dst;
  }
  return try_err(lower_op, (ss, s, try_err(lower, (ss, s, left), 0), right, number), 0);
}
static u32 lower_anon_struct errs(superscope *ss, scope *s, astidx a) {
  let elems = ast_block_children(ss, a);
  let n = elems.len;

  let members = (r_struct){.start = ss_member_reserve(ss, n)};
  let inner = ss_type_reserve(ss, 0);
  sglist_get(ss->tlist, inner - 1) = T(tu_of(r_struct, members));
  let _this = T(tu_of(r_tag, {inner, unique_type_name(ss)}));
  let ns = scope_inferior(ss, s);
  bool is_positional = true;
  moxmap_set(ss->type_info, type_name(ss_typeenv(ss), _this.t.r_tag.name), ((type_metadata){.namespace = ns, .is_anonymous = true, .is_positional = true}));

  let res = scope_add_value(ss, s, TV_of(ss, _this));
  usize offset = 0;
  foreach (let i, range(0, n)) {
    astidx elem = elems.start + i;
    astidx nameidx = 0, typeidx = 0, valueidx = elem;
    if (tu_is(astDecl, ast_at(ss, elem))) {
      let dcl = tu_catchr(astDecl, ast_at(ss, elem), return_err("ANONYMOUS_MEMBER_NOT_DECL", 0));
      nameidx = ast_child(ss, dcl.name_type_value);
      typeidx = ast_child(ss, dcl.name_type_value + 1);
      valueidx = ast_child(ss, dcl.name_type_value + 2);
    } else if (tu_is(astOp, ast_at(ss, elem))) {
      let op = tu_catchr(astOp, ast_at(ss, elem), return_err("ANONYMOUS_MEMBER_NOT_OP", 0));
      astidx left = ast_child(ss, op.left_right);
      if (op.number_rep == opNumbers.set && tu_is(astId, ast_at(ss, left))) {
        nameidx = left;
        valueidx = ast_child(ss, op.left_right + 1);
      }
    }

    if (!i) {
      is_positional = !nameidx;
      moxmap_get(ss->type_info, type_name(ss_typeenv(ss), _this.t.r_tag.name))->is_positional = is_positional;
    } else if (is_positional != !nameidx) return_err("ANONYMOUS_MIXED_MEMBERS", 0);
    let numbered = snprint(ss->alloc, "{usize}", (usize)i);
    numbered.len--;
    fptr name = nameidx ? try_err(id_from_ast, (ss, s, nameidx), 0) : fp(numbered);

    foreach (let j, range(0, i))
      if (!fptr_cmp(name, type_name(ss_typeenv(ss), type_struct_item(ss_typeenv(ss), members, j)->name)))
        return_err("ANONYMOUS_DUPLICATE_MEMBER", 0);

    u32 val = valueidx ? try_err(lower, (ss, s, valueidx), 0) : 0;
    Type member = {};
    if (typeidx) {
      member = try_err(lower_asType, (ss, s, typeidx), 0);
      val = val ? try_err(scope_cast_value, (ss, s, val, member, false), 0) : scope_add_value(ss, s, TV_of(ss, member));
    } else if (val) member = Type_stripRef(ss_type(ss, scope_find(ss, s, val)->t));
    else return_err("ANONYMOUS_MEMBER_NO_VALUE", 0);

    let e = ss_typeenv(ss);
    r_struct_item *ri = type_struct_item(e, members, i);
    ri->name = id_srcpos(ss, nameidx, name);
    ri->t = member;
    if (Type_is_comptime(member)) {
      let snapshot = scope_add_value(ss, s, TV_of(ss, member));
      try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_set, {.l = val, .d = snapshot})), 0);
      try_err(scope_bind, (ss, ns, scope_add_value(ss, ns, *scope_find(ss, s, snapshot)), name), 0);
      continue;
    }
    ri->offset = lineup(offset, ri->alignment = Type_alignof(member, e));
    offset = ri->offset + Type_sizeof(member, e);

    let slot = scope_add_value(ss, s, TV_any(ss));
    try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_field, {.l = res, .d = slot, .i = (u32)i})), 0);
    try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_set, {.l = val, .d = slot})), 0);
  }
  return res;
}
u32 lower_low errs(superscope *ss, scope *s, astidx a) {
  if (!a) return scope_add_value(ss, s, TV_void(ss));
  let res = tu_match_exp(
      ast_at(ss, a),
      (astNull, _, (return_err("ENCOUNTERED_NULL_AST", 0), (u32)0)),
      (astEmpty, _, scope_add_value(ss, s, TV_void(ss))),
      (astOp, op, try_err(lower_op_wrap, (ss, s, ast_child(ss, op.left_right), ast_child(ss, op.left_right + 1), op.number_rep), 0)),
      (astId, id, try_err(ss_get_named, (ss, s, ast_text(ss, id)), 0)),
      (astNumber, n, ss_compileInt(ss, s, ast_text(ss, n))),
      (astString, f, ss_compileString(ss, s, ast_text(ss, f))),
      (astPBlock, pb, ({
         let stmts = ast_block_children(ss, a);
         if (stmts.len != 1)
           return_err("TUPLE", 0);
         // (a ~= 1); -> a ~= 1;
         try_err(lower, (ss, s, stmts.start), 0);
       })),
      (astBBlock, bb, ({
         let res = scope_add_value(ss, s, TV_any(ss));
         try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_blocks, {})), 0);
         let stmts = ast_block_children(ss, a);
         u32 last = s->start + 1;
         foreach (let i, span(stmts.start, stmts.len))
           last = try_err(lower, (ss, s, i), 0);
         try_err(tcheck_result, (ss, s, last, res), 0);
         try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_blocke, {})), 0);
         res;
       })),
      (astABlock, _, try_err(lower_anon_struct, (ss, s, a), 0)),
      (astDecl, dcl, ({
         astidx nameidx = ast_child(ss, dcl.name_type_value);
         astidx typeidx = ast_child(ss, dcl.name_type_value + 1);
         astidx valueidx = ast_child(ss, dcl.name_type_value + 2);
         let type = T(tu_of(c_any, {}));
         if (typeidx) {
           let t = try_err(temporary_interpret, (ss, s, typeidx), 0);
           type = try_err(value_asType, (ss, t), 0);
         }
         u32 value = 0;
         if (valueidx) {
           value = try_err(lower, (ss, s, valueidx), 0);
           assertMessage(value);
           let temp = scope_add_value(ss, s, TV_of(ss, type));
           try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_set, {.l = value, .d = temp})), 0);
           value = temp;
         } else {
           if (!typeidx) return_err("DECL_NO_TYPE_OR_VALUE", 0);
           value = scope_add_value(ss, s, TV_of(ss, type));
         }
         if (dcl.isconst) {
           let v = scope_find(ss, s, value);
           let vt = ss_type(ss, v->t);
           vt.isconst = true;
           v->t = ss_type_intern(ss, vt);
         }
         if (nameidx)
           P$(try_err(id_from_ast, (ss, s, nameidx), 0),
              try_err(scope_bind, (ss, s, value, $), 0));
         value;
       })),
  );
  assertMessage(res);
  return res;
}
u32 lower errs(superscope *ss, scope *s, astidx a) {
  return catch_errcall(
      (lower_low, (ss, s, a)), e, ({
        static thread_local char ns[sizeof(errors_buffer.buf)] = {};

        let full = ((srcpos_full){srcpos_ast(ast_at(ss, a), ss->asts), &ss->plist, &ss->flist});
        pass_err(P$(e, ($.fn = print_ebuff("{cstr}:{srcpos_full}", strcpy(ns, errors_buffer.buf), full), $)), 0);
        0;
      })
  );
}

#pragma once
#include "common.h"
#include "function.h"
#include "lower_builtins.h"
#include "scope.h"
#include "wheels/fptr.h"
#include "wheels/macros.h"
#include "wheels/mytypes.h"
#include <string.h>
u32 lower_call_extra(superscope *ss, scope *s, u32 left, u32 first, ast *args) {
  let l = scope_find(ss, s, left);
  iflet_tu (let fn, c_fun, l->t.t)
    return lower_function_call_with(ss, s, left, args, first);
  error_print(args, "unsupported : call on {*:typed_value}", l);
}
u32 lower_call(superscope *ss, scope *s, u32 left, ast *args) {
  let l = scope_find(ss, s, left);
  iflet_tu (let builtin, builtin, l->t.t)
    return lower_builtin(ss, s, builtin, args);
  iflet_tu (let fn, c_fun, l->t.t)
    return lower_function_call(ss, s, left, args);
  error_print(args, "unsupported call on {*:typed_value}", l);
}
tu_def(
    (astLit, u8),
    (astIdLit, astId),
    (astStrLit, astString),
    (astNumLit, astNumber),
);
u32 lower_overload_possible(superscope *ss, scope *s, u32 left, ast *right, u32 opNumber) {
  let t = scope_find(ss, s, left)->t;
  if (t.ptr_level > 1) return 0;
  let result = tu_match_exp(
      t.t,
      (default, (u32)0),
      (r_arr, arr, ({ // array index
                      // no bounds checking, because there is no pointer arithmatic
         if (opNumber != opNumbers.call) error_at(*right, "array does not support this operation");
         let block = tu_catch(astABlock, *right, error_at(*right, "array does not support this call"));
         if (sentList_len(block.items) != 1) error_at(*right, "expected [N]");
         let d = scope_add_value(ss, s, TV(T(*arr.t, .isref = true)));
         let r = lower(ss, s, block.items[0]);
         if (!Type_isint(scope_find(ss, s, r)->t)) error_print(*right, "index into array with non-integer");
         scope_add_inst(ss, s, (ir)tu_of(ir_array, {.d = d, .r = r, .l = left}));
         d;
       })),
      (r_tag, t, ({
         let result = (u32)0;
         if_decl (let info, moxmap_get(ss->type_info, t.name)) {
           char op_str[5] = {};
           memcpy(op_str, &opNumber, 4);
           if_decl (let item, moxmap_get((*info)->named, fp((char *)op_str))) {
             TODO("handle operator overloading, depends on functions though");
           }
         }
         result;
       })),
      (c_ast, _, ({
         u32 ovl = 0;
         if (opNumber == opNumbers.dot) {
           ast *empty = ({
             static ast empty = (ast)tu_of(astEmptyStatement, {});
             &empty;
           });
           let self = scope_find(ss, s, left)->v.c_ast;
           iflet_tu (let m, astString, *right) {
             ast *res = empty;
             if (self && tu_is(astOp, *self) && m.len >= 2 && m.ptr[0] == '"' && m.ptr[m.len - 1] == '"') {
               let op = tu_catch(astOp, *self);
               union {
                 u32 n;
                 char s[sizeof(u32)];
               } spelling = {};
               memcpy(spelling.s, m.ptr + 1, m.len - 2);
               if (op.number_rep == spelling.n) res = self;
             }
             ovl = scope_add_value(ss, s, TV_ast(res));
           } else iflet_tu (let m, astId, *right) {
             ast *res = empty;
             if (self)
               iflet_tu (let op, astOp, *self) {
                 if (fptr_eq(m, "opleft")) res = op.left ?: empty;
                 else if (fptr_eq(m, "opright")) res = op.right ?: empty;
               }
             if (res == empty && self)
               res = tu_match_exp(
                   *self,
                   (astOp, _, /*    */ fptr_eq(m, "op") /*    */ ? self : empty),
                   (astId, _, /*    */ fptr_eq(m, "id") /*    */ ? self : empty),
                   (astDecl, _, /*  */ fptr_eq(m, "decl") /*  */ ? self : empty),
                   (astNumber, _, /**/ fptr_eq(m, "number") /**/ ? self : empty),
                   (astString, _, /**/ fptr_eq(m, "string") /**/ ? self : empty),
                   (astPBlock, _, /**/ fptr_eq(m, "pblock") /**/ ? self : empty),
                   (astBBlock, _, /**/ fptr_eq(m, "bblock") /**/ ? self : empty),
                   (astABlock, _, /**/ fptr_eq(m, "ablock") /**/ ? self : empty),
                   (astEmptyStatement, _, empty),
               );
             ovl = scope_add_value(ss, s, TV_ast(res));
           }
         } else if (opNumber == opNumbers.eq) {
           // "checking against string for id. for asts, do it like java":
           // ast == ast is identity; id/number/string nodes compare their
           // spelling when the other side is a string
           let self = scope_find(ss, s, left)->v.c_ast;
           let other = scope_find(ss, s, lower(ss, s, right));
           bool res = false;
           iflet_tu (let arr, r_arr, other->t.t) {
             if (self) {
               fptr spelling = tu_match_exp(
                   *self,
                   (astId, id, /*     */ id),
                   (astNumber, num, /*  */ num),
                   (astString, str, /*  */ str),
                   (default, (fptr){})
               );
               if (spelling.len && spelling.ptr[0] == '"') // string nodes still carry their quotes
                 spelling = string_escape(ss->alloc, spelling);
               res = fptr_eq(spelling, ((fptr){arr.count, (u8 *)other->v.value}));
             }
           } else if (tu_is(c_ast, other->t.t)) {
             res = self == other->v.c_ast; // java-style identity
           }
           ovl = scope_add_value(ss, s, TV(T(tu_of(r_bool, {})), avalue(ss->alloc, (bool)res)));
         }
         ovl;
       })),
  );
  // if (!result) println("[LOG] failed overload {opNumber} for {Type}", opNumber, t);
  return result;
}

fptr astLit_str(astLit l) { return tu_match_exp(l, (astIdLit, l, l), (astStrLit, l, l), (astNumLit, l, l)); }
r_bitfield find_inner_bitfield(TypeTree t) {
  return tu_match_exp(
      t,
      (r_bitfield, r, r),
      (r_tag, r, find_inner_bitfield(*r.t)),
      (default, (r_bitfield) nullptr)
  );
}
r_struct find_inner_struct(TypeTree t) {
  return tu_match_exp(
      t,
      (r_struct, r, r),
      (r_tag, r, find_inner_struct(*r.t)),
      (default, (r_struct) nullptr)
  );
}
u32 lower_dot(superscope *ss, scope *s, u32 left, ast *right, astLit lit) {
  let l = scope_find(ss, s, left);
  let name = astLit_str(lit);

  if (tu_is(c_type, l->t.t)) {
    let target = value_asType_loc(*l, ast_loc(*right));
    iflet_tu (let tag, r_tag, target.t)
      if_decl (let info, moxmap_get(ss->type_info, tag.name))
        if_decl (let idx, moxmap_get((*info)->named, name))
          return scope_add_value(ss, s, *scope_find(ss, *info, *idx));
    error_print(*right, "{Type} has no member {slice(c8)}", target, name);
  }

  if (l->t.ptr_level > 1) error_print(*right, "{Type} does not support dot", l->t);

  iflet_tu (let tag, r_tag, l->t.t)
    if_decl (let info, moxmap_get(ss->type_info, tag.name))
      if_decl (let idx, moxmap_get((*info)->named, name))
        return scope_add_value(ss, s, *scope_find(ss, *info, *idx));

  if_decl (let st, find_inner_struct(l->t.t)) {
    u32 i = 0;
    foreach (let item, mList_iter(st)) {
      if (fptr_eq(item.name, name)) {
        if (Type_is_comptime(item.t))
          error_print(*right, "{slice(c8)} has no storage; its type has no name to look it up by", name);
        Type rt = item.t;
        rt.isref = true;
        let d = scope_add_value(ss, s, TV(rt));
        scope_add_inst(ss, s, (ir)tu_of(ir_field, {.l = left, .d = d, .i = i}));
        return d;
      }
      i++;
    }
  } else if_decl (let bf, find_inner_bitfield(l->t.t)) {
    u32 i = 0;
    foreach (let item, mList_iter(bf)) {
      if (fptr_eq(item.name, name)) {
        Type rt = item.t;
        rt.isref = true;
        let d = scope_add_value(ss, s, TV(rt));
        scope_add_inst(ss, s, (ir)tu_of(ir_field, {.l = left, .d = d, .i = i})); // maybe it's own ir entry, if porting to c without bitfields
        return d;
      }
      i++;
    }
  }

  error_print(*right, "{Type} has no member {slice(c8)}", l->t, name);
}
typedef enum {
  math_mul = enop_mul,
  math_div = enop_div,
  math_mod = enop_mod,
  math_add = enop_plus,
  math_sub = enop_minus,
  math_shr = enop_shr,
  math_shl = enop_shl,
  math_and = enop_oand,
  math_or = enop_oor,
  math_band = enop_oband,
  math_bor = enop_obor,
  math_mor = enop_mor,
  math_les = enop_les,
  math_moq = enop_moq,
  math_leq = enop_leq,
  math_neq = enop_neq,
  math_eq = enop_eq,
} math_operation;
u32 lower_math(superscope *ss, scope *s, u32 left, u32 right, math_operation op) {
  let opNumber = enum_as_op((enum_op)op);
  let lhs = scope_find(ss, s, left);
  let rhs_value = scope_find(ss, s, right);
  let lt = lhs->t;
  Type rt;
  switch (op) {
    case math_mor:
    case math_les:
    case math_moq:
    case math_leq:
    case math_neq:
    case math_eq:
    case math_and:
    case math_or:
      rt = T(tu_of(r_uint, (u16)1));
      break;
    default:
      rt = lt;
      rt.isref = false;
      rt.isconst = false;
      rt.isvolatile = false;
      break;
  }
  let d = scope_add_value(ss, s, TV(rt));
  scope_add_inst(ss, s, (ir)tu_of(ir_rawop, {.r = left, .l = right, .d = d, .o = opNumber}));
  return d;
}
u32 lower_op(superscope *ss, scope *s, u32 left, ast *right, u32 opNumber) {
  // these are not overloadable
  // dot probably coming soon
  switch_tuple(
      op_as_enum(opNumber),
      (enop_pipe, {
        let result = scope_add_value(ss, s, TV_any());

        let pipscope = scope_inferior(ss, s);
        scope_add_inst(ss, s, (ir)tu_of(ir_block, pipscope));
        scope_bind(ss, pipscope, left, fp("_"));
        lower(ss, pipscope, right);
        scope_yield(ss, pipscope, result);

        return result;
      }),
      (enop_colon, assertMessage(false, "unreachable")),
      (enop_iexpand, ({
         // a'b
         // ->
         // @uqot a @qot b
         let argument = scope_find(ss, s, lower_builtin_qot(ss, s, right));
         if (Type_cmp(TV_ast(nullptr).t, argument->t)) error_print(right, "expected ast, got {Type}", argument->t);
         let result = scope_find(ss, s, lower_call(ss, s, left, argument->v.c_ast));
         if (Type_cmp(TV_ast(nullptr).t, result->t)) error_print(right, "expected ast, got {Type}", argument->t);
         lower_builtin_uqot(ss, s, result->v.c_ast);
       })),
      (default, ),
  );
  // mask out so that copy constructor stuff can be applied later
  switch_tuple(
      op_as_enum(opNumber),
      (enop_set, {
        scope_add_inst(ss, s, (ir)tu_of(ir_set, {.d = left, .l = lower(ss, s, right)}));
        return left;
      }),
      (default, ),
  );
  if_decl (let ovl, lower_overload_possible(ss, s, left, right, opNumber)) return ovl;
  let res = switch_exp(
      (op_as_enum(opNumber)),
      (enop_pipe /*   */, (assertMessage(false, "unreachable"), (u32)0)),
      (enop_colon /*  */, (assertMessage(false, "unreachable"), (u32)0)),
      (enop_set /*    */, (assertMessage(false, "unreachable"), (u32)0)),
      (enop_iexpand /**/, (assertMessage(false, "unreachable"), (u32)0)),
      (enop_call, lower_call(ss, s, left, right)),
      (enop_dot, ({
         P$(tu_match_exp(
                *right,
                (astId, id, (astLit)tu_of(astIdLit, id)),
                (astString, str, (astLit)tu_of(astStrLit, str)),
                (astNumber, num, (astLit)tu_of(astNumLit, num)),
                (default, (assertMessage(false, "non-overloaded dot only works on literals"), (astLit){}))
            ),
            lower_dot(ss, s, left, right, $));
       })),
      // operators are handled, so it should just be math now
      (enop_mul, /*     */ lower_math(ss, s, left, lower(ss, s, right), math_mul)),
      (enop_div, /*     */ lower_math(ss, s, left, lower(ss, s, right), math_div)),
      (enop_mod, /*     */ lower_math(ss, s, left, lower(ss, s, right), math_mod)),
      (enop_plus, /*    */ lower_math(ss, s, left, lower(ss, s, right), math_add)),
      (enop_minus, /*   */ lower_math(ss, s, left, lower(ss, s, right), math_sub)),
      (enop_shr, /*     */ lower_math(ss, s, left, lower(ss, s, right), math_shr)),
      (enop_shl, /*     */ lower_math(ss, s, left, lower(ss, s, right), math_shl)),
      (enop_oand, /*    */ lower_math(ss, s, left, lower(ss, s, right), math_and)),
      (enop_oor, /*     */ lower_math(ss, s, left, lower(ss, s, right), math_or)),
      (enop_oband, /*   */ lower_math(ss, s, left, lower(ss, s, right), math_band)),
      (enop_obor, /*    */ lower_math(ss, s, left, lower(ss, s, right), math_bor)),
      (enop_les, /*     */ lower_math(ss, s, left, lower(ss, s, right), math_les)),
      (enop_mor, /*     */ lower_math(ss, s, left, lower(ss, s, right), math_mor)),
      (enop_moq, /*     */ lower_math(ss, s, left, lower(ss, s, right), math_moq)),
      (enop_eq, /*      */ lower_math(ss, s, left, lower(ss, s, right), math_eq)),
      (enop_neq, /*     */ lower_math(ss, s, left, lower(ss, s, right), math_neq)),
      (enop_leq, /*     */ lower_math(ss, s, left, lower(ss, s, right), math_leq)),
  );
  if (!res) error_print(right, "something went wrong {} , {} , {ast}", left, opNumber, right);
  return res;
}

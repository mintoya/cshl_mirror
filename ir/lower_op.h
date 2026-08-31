#pragma once
#include "common.h"
#include "lower_builtins.h"
#include "scope.h"
#include "wheels/fptr.h"
#include "wheels/macros.h"
#include "wheels/mytypes.h"
#include "wheels/sList.h"
#include <string.h>
u32 lower_call_inner(superscope *ss, scope *s, r_func f, slice(u32) args) { TODO("actually decide what an r_func is"); }
u32 lower_call_extra(superscope *ss, scope *s, u32 left, u32 first, ast *args) {
  TODO("just a call with an extra member");
}
u32 lower_call(superscope *ss, scope *s, u32 left, ast *args) {
  let l = scope_find(ss, s, left);
  iflet_tu (let builtin, builtin, l->t.t)
    return lower_builtin(ss, s, builtin, args);

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
  return tu_match_exp(
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
         TODO(
             "opwerator overloading \
             dot :\
             strings : opwerators go through a similar process to opNumbers, then if the ast is an opNumber and it's the matching operation, return the operatoin itself. otherwise return the empty ast, which is falsy \
             literals : lower case versoin of ast is available, also leave unchanged. so .bblock,.ablock,.op... erturn the ast itself if it's that thing\
             in addition, opleft and opright supported for checking operation members\
             call :\
             array acess is a macro call with an ablock, so act accordingly for the block types"
         );
         (u32)0;
       })),
  );
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
u32 lower_dot(superscope *ss, scope *s, u32 left, astLit lit) {
  TODO();
}
typedef enum {
  math_mul,
  math_div,
  math_mod,
  math_add,
  math_sub,
  math_shr,
  math_shl,

  math_and,
  math_or,
  math_band,
  math_bor,

  math_mor,
  math_les,
  math_moq,
  math_leq,
  math_neq,
  math_eq,
} math_operation;
u32 lower_math(superscope *ss, scope *s, u32 left, u32 right, u32 opNumber, math_operation op) {
  let lt = scope_find(ss, s, left)->t;
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
  return switch_exp(
      (op_as_enum(opNumber)),
      (enop_pipe, (assertMessage(false, "unreachable"), (u32)0)),
      (enop_colon, (assertMessage(false, "unreachable"), (u32)0)),
      (enop_set, (assertMessage(false, "unreachable"), (u32)0)),
      (enop_call, lower_call(ss, s, left, right)),
      (enop_iexpand, lower_call(ss, s, left, right)),
      (enop_dot, ({
         P$(tu_match_exp(
                *right,
                (astId, id, (astLit)tu_of(astIdLit, id)),
                (astString, str, (astLit)tu_of(astStrLit, str)),
                (astNumber, num, (astLit)tu_of(astNumLit, num)),
                (default, (assertMessage(false, "non-overloaded dot only works on literals"), (astLit){}))
            ),
            lower_dot(ss, s, left, $));
       })),
      // operators are handled, so it should just be math now
      (enop_mul, /*     */ lower_math(ss, s, left, lower(ss, s, right), opNumber, math_mul)),
      (enop_div, /*     */ lower_math(ss, s, left, lower(ss, s, right), opNumber, math_div)),
      (enop_mod, /*     */ lower_math(ss, s, left, lower(ss, s, right), opNumber, math_mod)),
      (enop_plus, /*    */ lower_math(ss, s, left, lower(ss, s, right), opNumber, math_add)),
      (enop_minus, /*   */ lower_math(ss, s, left, lower(ss, s, right), opNumber, math_sub)),
      (enop_shr, /*     */ lower_math(ss, s, left, lower(ss, s, right), opNumber, math_shr)),
      (enop_shl, /*     */ lower_math(ss, s, left, lower(ss, s, right), opNumber, math_shl)),
      (enop_oand, /*    */ lower_math(ss, s, left, lower(ss, s, right), opNumber, math_and)),
      (enop_oor, /*     */ lower_math(ss, s, left, lower(ss, s, right), opNumber, math_or)),
      (enop_oband, /*   */ lower_math(ss, s, left, lower(ss, s, right), opNumber, math_band)),
      (enop_obor, /*    */ lower_math(ss, s, left, lower(ss, s, right), opNumber, math_bor)),
      (enop_les, /*     */ lower_math(ss, s, left, lower(ss, s, right), opNumber, math_les)),
      (enop_mor, /*     */ lower_math(ss, s, left, lower(ss, s, right), opNumber, math_mor)),
      (enop_moq, /*     */ lower_math(ss, s, left, lower(ss, s, right), opNumber, math_moq)),
      (enop_eq, /*      */ lower_math(ss, s, left, lower(ss, s, right), opNumber, math_eq)),
      (enop_neq, /*     */ lower_math(ss, s, left, lower(ss, s, right), opNumber, math_neq)),
      (enop_leq, /*     */ lower_math(ss, s, left, lower(ss, s, right), opNumber, math_leq)),
  );
}

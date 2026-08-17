#pragma once
#include "common2.h"
#include "wheels/bigint.h"
#include "wheels/macros.h"
#include "wheels/sList.h"
#include <string.h>

static bool Type_mutable(Type t) {
  while (1)
    tu_match_void(
        t,
        (r_ctime, ct, t = *ct;),
        (r_decor, dec, if (dec.constant) return false; t = *dec.t;),
        (r_tag, tag, t = *tag.t;),
        (default, return true)
    );
}

typedef enum {
  int_ctrl_norm,
  int_ctrl_brk,
  int_ctrl_cnt,
  int_ctrl_ret,
} interpret_ctrl;
typed_value interpret_state(superscope *ss, scope *s, interpret_ctrl *ctrl);

typed_value interpret_call(superscope *ss, scope *s, ir_call c) { TODO(); }
typed_value interpret_math(superscope *ss, scope *s, ir_op c) { TODO(); }
typed_value interpret_bool(superscope *ss, scope *s, ir_op c) { TODO(); }
typed_value interpret_op(superscope *ss, scope *s, ir_op c) {
  switch ((enum_op)(c.op)) {
    case enop_oor:
    case enop_oand:
      return interpret_bool(ss, s, c);
    case enop_obor:
    case enop_oband:
    case enop_shr:
    case enop_shl:

    case enop_mor:
    case enop_les:
    case enop_moq:
    case enop_leq:

    case enop_mul:
    case enop_div:
    case enop_mod:
    case enop_plus:
    case enop_minus:
      return interpret_math(ss, s, c);
    case enop_pipe:
    case enop_colon:
    case enop_call:
      assertMessage(false, "unhandled op %u", c.op);
  }
}

#define msList_iter_init(l) (struct {typeof(l) curr; typeof(l) end; }, ({                                                     \
      let _list = l;                                                   \
      (typeof(_foreach_._foreach_)){_list, _list + msList_len(_list)}; }))
#define msList_iter_next(it) it.curr++
#define msList_iter_cast(it) *it.curr
#define msList_iter_check(it) it.curr < it.end

#define FOREACH_msList_iter \
  (msList_iter_init, msList_iter_next, msList_iter_check, msList_iter_cast)

typed_value interpret_state(superscope *ss, scope *s, interpret_ctrl *ctrl) {
  if (!s) return (typed_value){tu_of(r_void, {})};
  foreach (let ins, msList_iter(s->instructions))
    tu_match_void(
        ins,
        (ir_op, op, ),
        (ir_break, _, assertMessage(ctrl), *ctrl = int_ctrl_brk;),
        (ir_continue, _, assertMessage(ctrl), *ctrl = int_ctrl_cnt;),
        (default, TODO()),
    );
}
typed_value interpret(superscope *ss, scope *s) {
  if (!s) return (typed_value){tu_of(r_void, {})};
  return interpret_state(ss, s, nullptr);
}

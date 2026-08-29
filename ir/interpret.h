#include "common.h"
#include "wheels/macros.h"
#include "wheels/sList.h"
typed_value interpret(superscope *ss, scope *s) {

  foreach (let ip, span(s->instructions, msList_len(s->instructions)))
    tu_match_void(
        *ip,
        (ir_if, _, TODO()),
        (ir_while, _, TODO()),
        (ir_break, _, TODO()),
        (ir_cont, _, TODO()),
        //
        (ir_getref, _, TODO()),
        (ir_getptr, _, TODO()),
        (ir_putptr, _, TODO()),
        (ir_set, _, TODO()),
        (ir_call, _, TODO()),
        (ir_rawop, _, TODO()),
        (ir_farg, _, TODO()),
        (ir_block, _, TODO()),
        (ir_ret, _, TODO()),
    );
  return msList_last(s->values);
}

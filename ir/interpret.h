#include "common.h"
#include "wheels/macros.h"
#include "wheels/sList.h"
#include <string.h>

static void interpret_set(superscope *ss, scope *s, ir_set c) {
  TODO("set");
}
typed_value interpret_cast(superscope *ss, scope *s, u32 l, u32 d) {
  TODO();
}

typed_value interpret(superscope *ss, scope *s) {

  foreach (let ip, span(s->instructions, msList_len(s->instructions)))
    tu_match_void(
        *ip,
        (ir_if, _, TODO()),
        (ir_while, _, TODO()),
        (ir_getref, iv, TODO()),
        (ir_getptr, _, TODO()),
        (ir_putptr, _, TODO()),
        (ir_cast, c, interpret_cast(ss, s, c.l, c.d)),
        (ir_set, st, interpret_set(ss, s, st);),
        (ir_call, _, TODO()),
        (ir_rawop, _, TODO()),
        (ir_block, _, TODO()),
        (ir_farg, _, assertMessage(false, "should be unreachable")),
        (ir_ret, _, assertMessage(false, "should be unreachable")),
        (ir_break, _, assertMessage(false, "should be unreachable")),
        (ir_cont, _, assertMessage(false, "should no")),
    );
  return msList_last(s->values);
}

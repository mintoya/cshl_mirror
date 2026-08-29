#include "common.h"
#include "wheels/macros.h"
#include "wheels/sList.h"
#include <string.h>

static void interpret_set(superscope *ss, scope *s, ir_set c) {
  TODO("set");
}

// casts the value at c.l to the integer type of reg(c.d); the target type
// lives on the destination value, not in the instruction
typed_value interpret_cast(superscope *ss, scope *s, u32 l, u32 d) {
  TODO();
}

typed_value interpret(superscope *ss, scope *s) {

  foreach (let ip, span(s->instructions, msList_len(s->instructions)))
    tu_match_void(
        *ip,
        (ir_if, _, TODO()),
        (ir_while, _, TODO()),
        (ir_break, _, TODO()),
        (ir_cont, _, TODO()),
        //
        (ir_getref, iv, {
          scope_find(ss, s, iv.d)->v.value = scope_find(ss, s, iv.l)->v.value;
        }),
        (ir_getptr, _, TODO()),
        (ir_putptr, _, TODO()),
        (ir_cast, c, TODO();),
        (ir_set, st, interpret_set(ss, s, st);),
        (ir_call, _, TODO()),
        (ir_rawop, _, TODO()),
        (ir_farg, _, TODO()),
        (ir_block, _, TODO()),
        (ir_ret, _, TODO()),
    );
  return msList_last(s->values);
}

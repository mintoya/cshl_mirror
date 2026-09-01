#include "common.h"
#include "wheels/macros.h"
#include "wheels/print/print_pre.h"
#include "wheels/sList.h"
#include <string.h>
typedef struct {
  u32 idx;
} idxtv;
static struct {
  superscope *ss;
  scope *s;
} scopeCtx;
typedef struct {
  typeof(scopeCtx) sc;
} setitemscope;
typePrinter(setitemscope) { scopeCtx = in.sc; }
typePrinter(idxtv) { USETYPEPRINTER(typed_value, *scope_find(scopeCtx.ss, scopeCtx.s, in.idx)); }

static void interpret_set(superscope *ss, scope *s, ir_set c) {
  println("{setitemscope}{idxtv} , set , {{idxtv}}", ((setitemscope){{ss, s}}), c.d, c.l);
  TODO("set");
}
typed_value interpret_cast(superscope *ss, scope *s, ir_cast c) {
  println("{setitemscope}{idxtv} , set , {{idxtv}}", ((setitemscope){{ss, s}}), c.d, c.l);
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
        (ir_cast, c, interpret_cast(ss, s, c)),
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

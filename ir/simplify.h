#pragma once
#include "common.h"
#include "scope.h"

static bool scope_void_type(superscope *ss, typeidx type) {
  let t = Type_stripTag(ss_type(ss, type), ss_typeenv(ss));
  return !t.ptr_level && tu_is(r_void, t.t);
}

static bool scope_void_register(superscope *ss, scope *s, u32 reg) {
  return scope_void_type(ss, scope_find(ss, s, reg)->t);
}

// Called once a scope has finished lowering. Register slots stay in place:
// child scopes use their numbers, calls use contiguous argument ranges, and
// the interpreter uses the last slot as the result of a non-function scope.
void scope_finish(superscope *ss, scope *s) {
  // TODO: remove comptime math?
  foreach (let value, span(s->values, msList_len(s->values)))
    if (scope_void_type(ss, value->t)) *value = TV_void(ss);

  u32 voidreg = s->start + 1; // scope_inferior reserves this void slot
  usize count = 0;
  foreach (let instruction, span(s->instructions, msList_len(s->instructions))) {
    ir inst = *instruction;
    u32 destination = tu_match_exp(
        inst,
        (ir_set, op, op.d),
        (ir_cast, op, op.d),
        (ir_getref, op, op.d),
        (ir_getptr, op, op.d),
        (ir_putptr, op, op.d),
        (ir_field, op, op.d),
        (ir_array, op, op.d),
        (ir_rawop, op, op.d),
        (ir_rawdot, op, op.d),
        (ir_farg, op, op.d),
        (default, (u32)0)
    );
    if (destination && scope_void_register(ss, s, destination)) continue;

    // Calls and returns still have effects even when their value is void.
    if (tu_is(ir_call, inst) && scope_void_register(ss, s, inst.ir_call.d))
      inst.ir_call.d = voidreg;
    if (tu_is(ir_ret, inst) && scope_void_register(ss, s, inst.ir_ret))
      inst.ir_ret = voidreg;
    s->instructions[count++] = inst;
  }
  msList_len(s->instructions) = count;
}

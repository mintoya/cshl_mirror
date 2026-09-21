#pragma once
#include "common.h"

// The lowered form is readable to the interpreter but noisy as C: every value
// gets its own register, and chains of temporaries stand between the value that
// is wanted and the one that is stored. Once a scope has been fully lowered this
// walks it and folds that noise back down.

// a register of void type carries no data: there is nothing to write into it,
// nothing to read back out, and no address worth taking
static bool simpl_is_void(superscope *ss, scope *s, u32 reg) {
  if (!reg) return false;
  return tu_is(r_void, ss_type(ss, scope_find(ss, s, reg)->t).t);
}

// an instruction that only moves a void value around has nothing to do, so it is
// dropped. anything with an effect of its own (a call, a return, control flow)
// is kept even when its result is void
static bool simpl_is_void_transfer(superscope *ss, scope *s, ir i) {
  bool l = false, r = false, d = false;
  tu_match_void(
      i,
      (ir_getref, c, l = simpl_is_void(ss, s, c.l), d = simpl_is_void(ss, s, c.d)),
      (ir_getptr, c, l = simpl_is_void(ss, s, c.l), d = simpl_is_void(ss, s, c.d)),
      (ir_putptr, c, l = simpl_is_void(ss, s, c.l), d = simpl_is_void(ss, s, c.d)),
      (ir_cast, /* */ c, l = simpl_is_void(ss, s, c.l), d = simpl_is_void(ss, s, c.d)),
      (ir_rawop, /**/ c, l = simpl_is_void(ss, s, c.l), r = simpl_is_void(ss, s, c.r), d = simpl_is_void(ss, s, c.d)),
      (ir_rawdot, /**/ c, l = simpl_is_void(ss, s, c.l), d = simpl_is_void(ss, s, c.d)),
      (ir_field, /* */ c, l = simpl_is_void(ss, s, c.l), d = simpl_is_void(ss, s, c.d)),
      (ir_array, /* */ c, l = simpl_is_void(ss, s, c.l), d = simpl_is_void(ss, s, c.d)),
      (ir_set, /*   */ c, l = simpl_is_void(ss, s, c.l), d = simpl_is_void(ss, s, c.d)),
      (default, ),
  );
  return l || r || d;
}

// called once a scope has finished lowering
void scope_finish(superscope *ss, scope *s) {
  msList(ir) kept = msList_init(ss->alloc, ir);
  foreach (let i, range(0, msList_len(s->instructions)))
    if (!simpl_is_void_transfer(ss, s, s->instructions[i]))
      msList_push(ss->alloc, kept, s->instructions[i]);
  msList_deInit(ss->alloc, s->instructions);
  s->instructions = kept;
}

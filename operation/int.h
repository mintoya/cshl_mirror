#pragma once
#include "common2.h"
#include "wheels/bigint.h"
#include "wheels/macros.h"
#include "wheels/print/print_pre.h"
#include "wheels/sList.h"
#include <string.h>

static bool Type_mutable(Type t) {
  return !Type_peel(t, false, true).is_const;
}

typedef enum {
  int_ctrl_norm,
  int_ctrl_brk,
  int_ctrl_cnt,
  int_ctrl_ret,
} interpret_ctrl;

static void interp_store_bigint(superscope *ss, typed_value *dest, bigint b) {
  Type t = strip_decor(dest->t);
  assertMessage(Type_isint(t));
  if (tu_is(c_int, t)) {
    if_decl (let bn, dest->v.c_int) msList_deInit(ss->alloc, bn);
    dest->v.c_int = BInt.copy(ss->alloc, b);
  } else {
    dest->v.value = dest->v.value ?: acreate(ss->alloc, u8[Type_rsize(t) ?: 1]); // safe?
    bigint_into_int(ss, dest, b);
  }
}

static bool interp_truthy(superscope *ss, scope *s, u32 idx) {
  return interp_tv_truthy(scope_find(ss, s, idx));
}

// -- instruction handlers ---------------------------------------------------

typed_value interpret_op(superscope *ss, scope *s, ir_op c);

static typed_value interpret_set(superscope *ss, scope *s, ir_op c) {
  let dst = scope_find(ss, s, c.d);
  let src = scope_find(ss, s, c.l);
  Type dt = strip_decor(dst->t);
  if (Type_isint(dt)) {
    let b = int_as_bigint(ss, *src);
    defer { msList_deInit(ss->alloc, b); };
    interp_store_bigint(ss, dst, b);
  } else {
    assertMessage(dst->v.value && src->v.value, "set on values without storage");
    let sz = Type_rsize(dt);
    assertMessage(sz == Type_rsize(strip_decor(src->t)), "set between values of different size");
    memcpy(dst->v.value, src->v.value, sz);
  }
  return *dst;
}

typed_value interpret_math(superscope *ss, scope *s, ir_op c) {
  let lv = scope_find(ss, s, c.l);
  let rv = scope_find(ss, s, c.r);

  let a = int_as_bigint(ss, *lv);
  let b = int_as_bigint(ss, *rv);

  bigint res = switch_exp(
      (enum_op)c.op,
      (enop_plus, BInt.add(ss->alloc, a, b)),
      (enop_minus, BInt.sub(ss->alloc, a, b)),
      (enop_mul, BInt.mul(ss->alloc, a, b)),
      (enop_div, P$(BInt.div(ss->alloc, a, b), (({ msList_deInit(ss->alloc, $.mod);$.div; })));),
      (enop_mod, P$(BInt.div(ss->alloc, a, b), (({ msList_deInit(ss->alloc, $.div);$.mod; })));),
      (enop_shl, bigint_bitshift(ss->alloc, a, b)),
      (enop_shr, P$(({ BInt.advanced.negate_ip(ss->alloc, &b);b; }), bigint_bitshift(ss->alloc, a, b))),
      (enop_oband, bigint_band(ss->alloc, a, b)),
      (enop_obor, bigint_bor(ss->alloc, a, b)),
      (default, (assertMessage(false, "math on not math"), nullptr)),
  );
  msList_deInit(ss->alloc, b);
  msList_deInit(ss->alloc, a);
  defer { msList_deInit(ss->alloc, res); };

  let dest = scope_find(ss, s, c.d);
  interp_store_bigint(ss, dest, res);
  return *dest;
}

typed_value interpret_bool(superscope *ss, scope *s, ir_op c) {
  let lv = scope_find(ss, s, c.l);
  let rv = scope_find(ss, s, c.r);
  bool r = switch_exp(
      (enum_op)c.op,
      (enop_oand, interp_tv_truthy(lv) && interp_tv_truthy(rv)),
      (enop_oor, interp_tv_truthy(lv) || interp_tv_truthy(rv)),
      (default, ({
         let ac = int_as_bigint(ss, *lv);
         defer { msList_deInit(ss->alloc, ac); };
         let bc = int_as_bigint(ss, *rv);
         defer { msList_deInit(ss->alloc, bc); };
         let cmp = BInt.cmp(ac, bc);
         switch_exp(
             (enum_op)c.op,
             (enop_eq, cmp == 0),
             (enop_neq, cmp != 0),
             (enop_mor, cmp > 0),
             (enop_les, cmp < 0),
             (enop_moq, cmp >= 0),
             (enop_leq, cmp <= 0),
             (default, (assertMessage(false, "interpret_bool on a non-boolean op"), false))
         );
       }));
  );

  let dest = scope_find(ss, s, c.d);
  let rb = BInt.from.i64(ss->alloc, r ? 1 : 0);
  defer { msList_deInit(ss->alloc, rb); };
  interp_store_bigint(ss, dest, rb);
  return *dest;
}

typed_value interpret_op(superscope *ss, scope *s, ir_op c) {
  switch ((enum_op)c.op) {
    // math
    case enop_plus:
    case enop_minus:
    case enop_mul:
    case enop_div:
    case enop_mod:
    case enop_shl:
    case enop_shr:
    case enop_oband:
    case enop_obor:
      return interpret_math(ss, s, c);
    // boolean / comparison
    case enop_oand:
    case enop_oor:
    case enop_eq:
    case enop_neq:
    case enop_mor:
    case enop_les:
    case enop_moq:
    case enop_leq:
      return interpret_bool(ss, s, c);
    case enop_set:
      return interpret_set(ss, s, c);
    // operators that lowering rewrites away (pipes become block+set,
    // overloaded ops become calls, dot/colon become field lookups)
    case enop_pipe:
    case enop_colon:
    case enop_call:
    case enop_dot:
      assertMessage(false, "operator %u should have been lowered away", c.op);
  }
}

typed_value interpret_state(superscope *ss, scope *s, u8 *location, interpret_ctrl *ctrl);

typed_value interpret_call(superscope *ss, scope *s, u8 *loc, ir_call c) {
  let fn_val = scope_find(ss, s, c.l);
  Type t = strip_decor(fn_val->t);
  iflet_tu (let _, r_ctime, t) t = *_;
  // a statically-known function is r_func; a dynamically-typed callable
  // (ret type `any`) is c_any and we trust that its payload is a function
  assertMessage(tu_is(r_func, t) || tu_is(c_any, t), "call of a non-function value");
  let fn = (function *)fn_val->v.value;
  assertMessage(fn, "call of a function value without a body");

  // concrete argument types -> spec (param+body) lookup
  let typeList = acreate(ss->alloc, Type[c.args.len + 1]);
  foreach (let i, range(0, c.args.len))
    typeList[0][i] = strip_decor(scope_find(ss, s, c.args.ptr[i])->t);
  typeList[0][c.args.len] = (Type){}; // sentinel

  // evaluate function_trySpec first: it may realloc ss->all_functions, so the
  // array must be re-read *after* the call (C doesn't guarantee the order
  // inside the subscript expression)
  let spec_idx = function_trySpec(ss, s, loc, fn, *typeList, c.args);
  let spec = ss->all_functions[spec_idx];
  let body = spec.body;

  // re-bind the arguments into the spec's parameter slots: on a spec cache hit
  // the slots still hold the previous call's values
  foreach (let i, range(0, c.args.len))
    *scope_find(ss, body, body->start + i + 1) = *scope_find(ss, s, c.args.ptr[i]);

  // run the body; its trailing ir_ret hands back the returned value
  interpret_ctrl ctrl = int_ctrl_norm;
  typed_value rv = interpret_state(ss, body, loc, &ctrl);
  assertMessage(ctrl == int_ctrl_ret, "function body did not return");

  let dest = scope_find(ss, s, c.d);
  *dest = rv;
  return rv;
}

// -- scope walk --------------------------------------------------------------

#define msList_iter_init(l) (struct {typeof(l) curr; typeof(l) end; }, ({                                                     \
      let _list = l;                                                   \
      (typeof(_foreach_._foreach_)){_list, _list + msList_len(_list)}; }))
#define msList_iter_next(it) it.curr++
#define msList_iter_cast(it) *it.curr
#define msList_iter_check(it) it.curr < it.end

#define FOREACH_msList_iter \
  (msList_iter_init, msList_iter_next, msList_iter_check, msList_iter_cast)

// Walk s->instructions. `ctrl` is the control-flow flag of an enclosing
// loop/function (or null at top level): break/continue/return are only
// legal when it is non-null, which is exactly how those scopes are
// guaranteed to be entered.
typed_value interpret_state(superscope *ss, scope *s, u8 *loc, interpret_ctrl *ctrl) {
  if (!s) return (typed_value){tu_of(r_void, {})};
  foreach (let ins, msList_iter(s->instructions))
    tu_match_void(
        ins,
        (ir_op, op, interpret_op(ss, s, op);),
        (ir_call, call, interpret_call(ss, s, loc, call);),
        (ir_ret, v, {
          assertMessage(ctrl, "return outside of a function");
          *ctrl = int_ctrl_ret;
          return v ? (typed_value)*scope_find(ss, s, v) : (typed_value){tu_of(r_void, {})};
        }),
        (ir_break, _, {
          assertMessage(ctrl, "break outside of a loop");
          *ctrl = int_ctrl_brk;
          return (typed_value){tu_of(r_void, {})};
        }),
        (ir_continue, _, {
          assertMessage(ctrl, "continue outside of a loop");
          *ctrl = int_ctrl_cnt;
          return (typed_value){tu_of(r_void, {})};
        }),
        (ir_if, iv, {
          let r = interp_truthy(ss, s, iv.cnd)
                      ? interpret_state(ss, iv.t, loc, ctrl)
                      : interpret_state(ss, iv.f, loc, ctrl);
          if (ctrl && *ctrl != int_ctrl_norm) return r;
        }),
        (ir_while, wv, {
          interpret_ctrl lc = int_ctrl_norm;
          typed_value rv = {tu_of(r_void, {})};
          // The condition lives in its own scope (wv.c), re-evaluated at the
          // top of every iteration; the body scope only contains the body.
          scope *cnd_scope = wv.c ? wv.c : wv.t;
          for (;;) {
            if (wv.c) {
              typed_value cv = interpret_state(ss, wv.c, loc, ctrl);
              if (ctrl && *ctrl != int_ctrl_norm) return cv;
            }
            if (!interp_truthy(ss, cnd_scope, wv.cnd)) break;
            lc = int_ctrl_norm;
            rv = interpret_state(ss, wv.t, loc, &lc);
            if (lc == int_ctrl_brk) break;
            if (lc == int_ctrl_cnt) continue;
            if (lc == int_ctrl_ret) {
              assertMessage(ctrl, "return outside of a function");
              *ctrl = lc;
              return rv;
            }
          }
        }),
        (ir_block, blk, {
          typed_value r = interpret_state(ss, blk, loc, ctrl);
          assertMessage(msList_len(blk->values), "block did not yield a value");
          let src = &blk->values[msList_len(blk->values) - 1];
          let dest = scope_find(ss, s, blk->start + 1);
          *dest = *src;
          if (ctrl && *ctrl != int_ctrl_norm) return r;
        }),
        (ir_putptr, _, assertMessage(false, "encountered dereferance in interpreter")),
        (ir_getptr, _, assertMessage(false, "encountered referance in interpreter")),
    );
  return (typed_value){tu_of(r_void, {})};
}
typed_value interpret(superscope *ss, scope *s, u8 *loc) {
  if (!s) return (typed_value){tu_of(r_void, {})};
  return interpret_state(ss, s, loc, nullptr);
}

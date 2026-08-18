#pragma once
#include "common2.h"
#include "wheels/bigint.h"
#include "wheels/macros.h"
#include "wheels/sList.h"
#include <stdio.h>
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

// -- value helpers ---------------------------------------------------------
// Store bigint `b` into the storage of `dest`, honoring dest's type.
// c_int destinations own a bigint; sized integer destinations get the low
// bytes (sign-padded first so bigint_into_int's memcpy never overreads).
static void interp_store_bigint(superscope *ss, typed_value *dest, bigint b) {
  Type t = strip_decor(dest->t);
  if (tu_is(c_int, t)) {
    // Replacing the pointer without freeing: the cba arena already owns the
    // old bigint, and intermediate values stay visible to the printer (and
    // to debug output) until the scope is torn down.
    dest->v.c_int = BInt.copy(ss->alloc, b);
  } else {
    if (!dest->v.value)
      dest->v.value = acreate(ss->alloc, u8[Type_rsize(t) ?: 1]);
    let nb = BInt.copy(ss->alloc, b);
    defer { msList_deInit(ss->alloc, nb); };
    usize limbs = Type_rsize(t) / sizeof(bigint_unit);
    if (msList_len(nb) < limbs)
      BInt.expand(ss->alloc, &nb, limbs);
    bigint_into_int(ss, dest, nb);
  }
}

static bool interp_truthy(superscope *ss, scope *s, u32 idx) {
  return interp_tv_truthy(scope_find_origional(ss, s, idx));
}

// 2^n as a bigint. bigint.h has no pow2/shift primitives, so build the bit
// pattern and let BInt.from.bits unpack it.
static bigint bigint_pow2(allocfn allocator, usize n) {
  usize limb = n / (8 * sizeof(bigint_unit));
  let buf = acreate(allocator, bigint_unit[limb + 1]);
  buf[0][limb] = (bigint_unit)1 << (n % (8 * sizeof(bigint_unit)));
  let r = BInt.from.bits(allocator, buf, n + 1, 0);
  adestroy(allocator, buf);
  return r;
}

// Shift by a bigint bit count: multiply/divide by 2^n, so sign handling
// follows bigint's own mul/div semantics.
static bigint interp_bigint_shift(superscope *ss, bigint a, bigint b, bool left) {
  assertMessage(!bigint_negetive(b), "shift by a negative amount");
  usize n = bigint_get(b, 0);
  foreach (let i, range(1, bigint_digits(b)))
    assertMessage(!bigint_get(b, i), "shift amount too large");
  let p2 = bigint_pow2(ss->alloc, n);
  defer { msList_deInit(ss->alloc, p2); };
  if (left) return BInt.mul(ss->alloc, a, p2);
  let d = BInt.div(ss->alloc, a, p2);
  defer { msList_deInit(ss->alloc, d.mod); };
  return d.div;
}

// Bitwise and/or over sign-padded, equal-length limbs (two's complement).
static bigint bigint_bitop(superscope *ss, bigint a, bigint b, bool is_or) {
  usize len = MAX$(bigint_digits(a), bigint_digits(b));
  let la = BInt.copy(ss->alloc, a);
  defer { msList_deInit(ss->alloc, la); };
  let lb = BInt.copy(ss->alloc, b);
  defer { msList_deInit(ss->alloc, lb); };
  if (msList_len(la) < len) BInt.expand(ss->alloc, &la, len);
  if (msList_len(lb) < len) BInt.expand(ss->alloc, &lb, len);

  let res = msList_init(ss->alloc, bigint_unit);
  foreach (let i, range(0, len))
    msList_push(ss->alloc, res, is_or ? (la[i] | lb[i]) : (la[i] & lb[i]));
  BInt.trim(&res);
  return res;
}

// -- instruction handlers ---------------------------------------------------

typed_value interpret_op(superscope *ss, scope *s, ir_op c);

static typed_value interpret_set(superscope *ss, scope *s, ir_op c) {
  let dst = scope_find_origional(ss, s, c.d);
  let src = scope_find_origional(ss, s, c.l);
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
  let lv = scope_find_origional(ss, s, c.l);
  let rv = scope_find_origional(ss, s, c.r);
  let a = int_as_bigint(ss, *lv);
  defer { msList_deInit(ss->alloc, a); };
  let b = int_as_bigint(ss, *rv);
  defer { msList_deInit(ss->alloc, b); };

  bigint res = nullptr;
  switch ((enum_op)c.op) {
    case enop_plus:
      res = BInt.add(ss->alloc, a, b);
      break;
    case enop_minus:
      res = BInt.sub(ss->alloc, a, b);
      break;
    case enop_mul:
      res = BInt.mul(ss->alloc, a, b);
      break;
    case enop_div: {
      let d = BInt.div(ss->alloc, a, b);
      defer { msList_deInit(ss->alloc, d.mod); };
      res = d.div;
      break;
    }
    case enop_mod: {
      let d = BInt.div(ss->alloc, a, b);
      defer { msList_deInit(ss->alloc, d.div); };
      res = d.mod;
      break;
    }
    case enop_shl:
      res = interp_bigint_shift(ss, a, b, true);
      break;
    case enop_shr:
      res = interp_bigint_shift(ss, a, b, false);
      break;
    case enop_oband:
      res = bigint_bitop(ss, a, b, false);
      break;
    case enop_obor:
      res = bigint_bitop(ss, a, b, true);
      break;
    default:
      assertMessage(false, "interpret_math on a non-math operator");
  }
  defer { msList_deInit(ss->alloc, res); };

  let dest = scope_find_origional(ss, s, c.d);
  interp_store_bigint(ss, dest, res);
  return *dest;
}

typed_value interpret_bool(superscope *ss, scope *s, ir_op c) {
  let lv = scope_find_origional(ss, s, c.l);
  let rv = scope_find_origional(ss, s, c.r);
  bool r = false;
  switch ((enum_op)c.op) {
    case enop_oand:
      r = interp_tv_truthy(lv) && interp_tv_truthy(rv);
      break;
    case enop_oor:
      r = interp_tv_truthy(lv) || interp_tv_truthy(rv);
      break;
    default: {
      let ac = int_as_bigint(ss, *lv);
      defer { msList_deInit(ss->alloc, ac); };
      let bc = int_as_bigint(ss, *rv);
      defer { msList_deInit(ss->alloc, bc); };
      let cmp = BInt.cmp(ac, bc);
      switch ((enum_op)c.op) {
        case enop_eq:
          r = cmp == 0;
          break;
        case enop_neq:
          r = cmp != 0;
          break;
        case enop_mor:
          r = cmp > 0;
          break;
        case enop_les:
          r = cmp < 0;
          break;
        case enop_moq:
          r = cmp >= 0;
          break;
        case enop_leq:
          r = cmp <= 0;
          break;
        default:
          assertMessage(false, "interpret_bool on a non-boolean op");
      }
    }
  }

  let dest = scope_find_origional(ss, s, c.d);
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

typed_value interpret_state(superscope *ss, scope *s, interpret_ctrl *ctrl);

typed_value interpret_call(superscope *ss, scope *s, ir_call c) {
  let fn_val = scope_find_origional(ss, s, c.l);
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
    typeList[0][i] = strip_decor(scope_find_origional(ss, s, c.args.ptr[i])->t);
  typeList[0][c.args.len] = (Type){}; // sentinel

  let spec = ss->all_functions[function_trySpec(ss, s, fn, *typeList, c.args)];
  let body = spec.body;

  // re-bind the arguments into the spec's parameter slots: on a spec cache hit
  // the slots still hold the previous call's values
  foreach (let i, range(0, c.args.len))
    *scope_find_origional(ss, body, body->start + i + 1) = *scope_find_origional(ss, s, c.args.ptr[i]);

  // run the body; its trailing ir_ret hands back the returned value
  interpret_ctrl ctrl = int_ctrl_norm;
  typed_value rv = interpret_state(ss, body, &ctrl);
  assertMessage(ctrl == int_ctrl_ret, "function body did not return");

  let dest = scope_find_origional(ss, s, c.d);
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
typed_value interpret_state(superscope *ss, scope *s, interpret_ctrl *ctrl) {
  if (!s) return (typed_value){tu_of(r_void, {})};
  foreach (let ins, msList_iter(s->instructions))
    tu_match_void(
        ins,
        (ir_op, op, interpret_op(ss, s, op);),
        (ir_call, call, interpret_call(ss, s, call);),
        (ir_ret, v, {
          assertMessage(ctrl, "return outside of a function");
          *ctrl = int_ctrl_ret;
          return v ? (typed_value)*scope_find_origional(ss, s, v) : (typed_value){tu_of(r_void, {})};
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
                      ? interpret_state(ss, iv.t, ctrl)
                      : interpret_state(ss, iv.f, ctrl);
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
              typed_value cv = interpret_state(ss, wv.c, ctrl);
              if (ctrl && *ctrl != int_ctrl_norm) return cv;
            }
            if (!interp_truthy(ss, cnd_scope, wv.cnd)) break;
            lc = int_ctrl_norm;
            rv = interpret_state(ss, wv.t, &lc);
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
          typed_value r = interpret_state(ss, blk, ctrl);
          if (ctrl && *ctrl != int_ctrl_norm) return r;
        }),
        (ir_dbg, dbg, {
          let pv = scope_find_origional(ss, s, dbg.l);
          let str = snprint(stdAlloc, "{*:typed_value}", pv);
          fprintf(stderr, "[dbg %zu:%zu] %.*s\n", (size_t)dbg.line, (size_t)dbg.col, (int)str.len, str.ptr);
        }),
    );
  return (typed_value){tu_of(r_void, {})};
}
typed_value interpret(superscope *ss, scope *s) {
  if (!s) return (typed_value){tu_of(r_void, {})};
  return interpret_state(ss, s, nullptr);
}

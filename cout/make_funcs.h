#pragma once
#include "../cout.h"
#include "../ir/common.h"
#include "../type.h"
#include "make_types.h"
#include "wheels/macros.h"
#include "wheels/print/print_pre.h"

static typeidx function_instance_params_start(superscope *ss, function_instance *instance) {
  function *fn = &ss->functions[instance->spec->instance.fn - 1];
  return tu_catchr(c_fun, ss_type(ss, fn->signature).t).start;
}
static usize function_instance_param_count(superscope *ss, function_instance *instance) {
  return type_run_len(ss_typeenv(ss), function_instance_params_start(ss, instance)) - 1;
}
static Type function_instance_type_at(superscope *ss, function_instance *instance, usize index) {
  assertMessage(index < function_instance_param_count(ss, instance), "function signature index out of range");
  return ss_type(ss, instance->spec->values[1 + index].t);
}
static Type function_instance_ret(superscope *ss, function_instance *instance) {
  (void)ss;
  return instance->ret;
}

static bool function_should_be_emitted(superscope *ss, function_instance *instance) {
  (void)ss;
  bool has_runtime_arg = false;
  foreach (let inst, span(instance->spec->instructions, msList_len(instance->spec->instructions)))
    has_runtime_arg |= tu_is(ir_farg, *inst);
  bool returns_runtime = !Type_is_comptime(instance->ret);
  assertMessage(!has_runtime_arg || returns_runtime, "runtime arguments require a runtime return");
  return returns_runtime;
}

static void makeFunctionType(superscope *ss, printerfunction_context _ctx, typeidx idx) {
  Type t = ss_type(ss, idx);
  if (Type_is_comptime(t)) PUTS("void_t");
  else makeTypedefName(_ctx, idx);
}

static void makeVariable(printerfunction_context _ctx, scope *s, u32 reg) {
  (void)s;
  // Registers are numbered across the whole program, so the index is unique.
  PUTS("v");
  USETYPEPRINTER(u32, reg);
}

static Type function_register_type(superscope *ss, scope *s, u32 reg) {
  return ss_type(ss, scope_find(ss, s, reg)->t);
}
static typeidx function_register_typeidx(superscope *ss, scope *s, u32 reg) {
  return scope_find(ss, s, reg)->t;
}
static void makeRegisterType(superscope *ss, printerfunction_context _ctx, scope *s, u32 reg) {
  makeTypedefName(_ctx, function_register_typeidx(ss, s, reg));
}

static bool function_is_wide(superscope *ss, Type t) {
  return out_integerIsWide(ss_typeenv(ss), t);
}

static void makeWideType(printerfunction_context _ctx, u32 bits) {
  PUTS("cshl_int_");
  USETYPEPRINTER(usize, bits);
}

static u32 function_integer_bits(superscope *ss, Type t) {
  t = Type_stripTag(t, ss_typeenv(ss));
  return tu_match_exp(
      t.t,
      (r_sint, v, (u32)v.bits),
      (r_uint, v, (u32)v.bits),
      (default, (u32)0)
  );
}

static u32 function_sign_bits(Type t, typeenv e) {
  while (tu_is(r_tag, t.t))
    t = Type_at(e, tu_catch(r_tag, t.t).t);
  return tu_is(r_sint, t.t) ? t.t.r_sint.bits : 0;
}

static bool function_is_non_int_comptime(Type t) {
  return Type_is_comptime(t) && !tu_is(c_int, t.t);
}

static bool scope_register_written(scope *s, u32 reg) {
  foreach (let instruction, span(s->instructions, msList_len(s->instructions))) {
    u32 destination = tu_match_exp(
        *instruction,
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
        (ir_call, op, op.d),
        (default, (u32)0)
    );
    if (destination == reg) return true;
  }
  return false;
}

static bool function_register_is_field_ref(superscope *ss, scope *s, u32 reg) {
  typeenv e = ss_typeenv(ss);
  foreach (let instruction, span(s->instructions, msList_len(s->instructions))) {
    if (!tu_is(ir_field, *instruction) || instruction->ir_field.d != reg) continue;
    Type base = function_register_type(ss, s, instruction->ir_field.l);
    while (tu_is(r_tag, base.t))
      base = Type_at(e, tu_catch(r_tag, base.t).t);
    r_struct strct = TypeTree_inner_struct(base.t, e);
    if (strct.start && instruction->ir_field.i < type_struct_len(e, strct))
      return !!type_struct_item(e, strct, instruction->ir_field.i)->t.ref_kind;
  }
  return false;
}

static void makeCStringLiteral(printerfunction_context _ctx, const unsigned char *text, usize len) {
  PUTS("\"");
  for (usize i = 0; i < len; i++) {
    unsigned char c = text[i];
    switch (c) {
      case '\\':
        PUTS("\\\\");
        break;
      case '"':
        PUTS("\\\"");
        break;
      case '\n':
        PUTS("\\n");
        break;
      case '\r':
        PUTS("\\r");
        break;
      case '\t':
        PUTS("\\t");
        break;
      case '\b':
        PUTS("\\b");
        break;
      case '\f':
        PUTS("\\f");
        break;
      case '\v':
        PUTS("\\v");
        break;
      case '\a':
        PUTS("\\a");
        break;
      case '\0':
        PUTS("\\0");
        break;
      default:
        PUTC((c8)c);
        break;
    }
  }
  PUTS("\\0\"");
}

static void makeRegisterPlace(superscope *ss, printerfunction_context _ctx, scope *s, u32 reg) {
  Type t = function_register_type(ss, s, reg);
  typed_value *value = scope_find(ss, s, reg);
  if (tu_is(c_int, t.t)) {
    USETYPEPRINTER(bigint, value->v.c_int);
    return;
  }
  if (Type_is_comptime(t)) {
    Type base = Type_stripTag(t, ss_typeenv(ss));
    if (tu_is(r_arr, base.t)) {
      r_arr arr = base.t.r_arr;
      Type elem = Type_at(ss_typeenv(ss), arr.t);
      if (tu_is(r_sint, elem.t) && elem.t.r_sint.bits == 8 && value->v.value) {
        makeCStringLiteral(_ctx, (const unsigned char *)value->v.value, arr.count);
        return;
      }
    }
  }
  if (!scope_register_written((scope *)scope_owner(s, reg), reg) && tu_is(r_bool, t.t) && value->v.value) {
    if (*(bool *)value->v.value) PUTS("((_Bool)1)");
    else PUTS("((_Bool)0)");
    return;
  }
  assertMessage(!Type_is_comptime(t), "unsupported comptime value in C output");
  assertMessage(t.ref_kind != ref_dref, "double-offset reference used as a C value");
  if (t.ref_kind == ref_ref && function_register_is_field_ref(ss, s, reg)) {
    PUTS("*(*(");
    makeRegisterType(ss, _ctx, s, reg);
    PUTS(" *)");
    makeVariable(_ctx, s, reg);
    PUTS(")");
    return;
  }
  if (t.ref_kind == ref_ref) PUTS("*");
  makeVariable(_ctx, s, reg);
}

static void makeDrefSignBits(superscope *ss, printerfunction_context _ctx, scope *s, u32 reg) {
  Type t = function_register_type(ss, s, reg);
  if (t.ref_kind == ref_dref && function_sign_bits(t, ss_typeenv(ss))) {
    makeVariable(_ctx, s, reg);
    PUTS(".data.width");
  } else USETYPEPRINTER(u32, function_sign_bits(t, ss_typeenv(ss)));
}

static void makeDrefValue(superscope *ss, printerfunction_context _ctx, scope *s, u32 reg) {
  Type t = function_register_type(ss, s, reg);
  t.ref_kind = 0;
  PUTS("(");
  makeType(ss, _ctx, t);
  PUTS(")");
  PUTS("cshl_load(");
  makeVariable(_ctx, s, reg);
  PUTS(".data, ");
  makeDrefSignBits(ss, _ctx, s, reg);
  PUTS(")");
}

static void makeRegisterAddress(superscope *ss, printerfunction_context _ctx, scope *s, u32 reg) {
  Type t = function_register_type(ss, s, reg);
  assertMessage(t.ref_kind != ref_dref, "double-offset reference used as an address");
  if (t.ref_kind == ref_ref) {
    PUTS("&");
    makeRegisterPlace(ss, _ctx, s, reg);
  } else if (t.ptr_level) makeRegisterPlace(ss, _ctx, s, reg);
  else {
    PUTS("&");
    makeRegisterPlace(ss, _ctx, s, reg);
  }
}

static void makeRegisterSize(superscope *ss, printerfunction_context _ctx, scope *s, u32 reg) {
  PUTS("sizeof(");
  makeRegisterPlace(ss, _ctx, s, reg);
  PUTS(")");
}

static void makeRawOperator(printerfunction_context _ctx, enum_op op) {
  switch (op) {
    case enop_mul:
      PUTS("*");
      break;
    case enop_div:
      PUTS("/");
      break;
    case enop_mod:
      PUTS("%");
      break;
    case enop_plus:
      PUTS("+");
      break;
    case enop_minus:
      PUTS("-");
      break;
    case enop_shl:
      PUTS("<<");
      break;
    case enop_shr:
      PUTS(">>");
      break;
    case enop_oband:
      PUTS("&");
      break;
    case enop_obor:
      PUTS("|");
      break;
    case enop_bxor:
      PUTS("^");
      break;
    case enop_oand:
      PUTS("&&");
      break;
    case enop_oor:
      PUTS("||");
      break;
    case enop_eq:
      PUTS("==");
      break;
    case enop_neq:
      PUTS("!=");
      break;
    case enop_les:
      PUTS("<");
      break;
    case enop_mor:
      PUTS(">");
      break;
    case enop_leq:
      PUTS("<=");
      break;
    case enop_moq:
      PUTS(">=");
      break;
    default:
      assertMessage(false, "unsupported raw operator");
  }
}

static void makeRawOperand(superscope *ss, printerfunction_context _ctx, scope *s, Type t, u32 reg) {
  if (t.ref_kind == ref_dref) makeDrefValue(ss, _ctx, s, reg);
  else makeRegisterPlace(ss, _ctx, s, reg);
}

static void makeWideDecimal(printerfunction_context _ctx, bigint value);

static void makeWideOperandAs(
    superscope *ss,
    printerfunction_context _ctx,
    scope *s,
    u32 reg,
    u32 target_bits
) {
  Type t = function_register_type(ss, s, reg);
  if (function_is_wide(ss, t) && function_integer_bits(ss, t)) {
    if (t.ref_kind == ref_ref) {
      makeVariable(_ctx, s, reg);
      PUTS("->bytes");
    } else {
      PUTS("(");
      makeVariable(_ctx, s, reg);
      PUTS(".bytes)");
    }
    return;
  }
  if (!target_bits) {
    PUTS("&(unsigned char){");
    USETYPEPRINTER(bigint, scope_find(ss, s, reg)->v.c_int);
    PUTS("}");
    return;
  }
  makeWideType(_ctx, target_bits);
  PUTS("_from_decimal(");
  makeWideDecimal(_ctx, scope_find(ss, s, reg)->v.c_int);
  PUTS(").bytes");
}

static void makeWideOperand(superscope *ss, printerfunction_context _ctx, scope *s, u32 reg) {
  u32 bits = function_integer_bits(ss, function_register_type(ss, s, reg));
  makeWideOperandAs(ss, _ctx, s, reg, bits);
}

static void makeWideDecimal(printerfunction_context _ctx, bigint value) {
  let text = snprint(stdAlloc, "{bigint}", value);
  defer { slice_free(stdAlloc, text); };
  PUTS("\"");
  for (usize i = 0; i < text.len; i++) {
    const char c = text.ptr[i];
    if (c == '-' || (c >= '0' && c <= '9')) PUTC(c);
  }
  PUTS("\"");
}

static void makeWideCastFromOperand(
    superscope *ss,
    printerfunction_context _ctx,
    scope *s,
    u32 dest_reg,
    u32 source_reg
) {
  Type dt = function_register_type(ss, s, dest_reg);
  Type lt = function_register_type(ss, s, source_reg);
  u32 bits = function_integer_bits(ss, dt);
  Type base = Type_stripTag(dt, ss_typeenv(ss));
  bool is_signed = tu_is(r_sint, base.t);
  if (tu_is(c_int, lt.t)) {
    makeWideType(_ctx, bits);
    PUTS("_from_decimal(");
    makeWideDecimal(_ctx, scope_find(ss, s, source_reg)->v.c_int);
    PUTS(")");
    return;
  }
  makeWideType(_ctx, bits);
  PUTS("_cast_value(");
  if (function_is_wide(ss, lt)) {
    makeWideOperandAs(ss, _ctx, s, source_reg, bits);
  } else {
    PUTS("(const unsigned char *)&(");
    lt.ref_kind = 0;
    makeType(ss, _ctx, lt);
    PUTS("){ ");
    makeRegisterPlace(ss, _ctx, s, source_reg);
    PUTS(" }");
  }
  lt.ref_kind = 0;
  PUTS(", sizeof(");
  makeType(ss, _ctx, lt);
  PUTS("), ");
  USETYPEPRINTER(u32, function_integer_bits(ss, lt));
  PUTS(", ");
  USETYPEPRINTER(u32, !!function_sign_bits(lt, ss_typeenv(ss)));
  PUTS(", ");
  USETYPEPRINTER(u32, is_signed);
  PUTS(")");
}

static void makeWideOperation(
    superscope *ss,
    printerfunction_context _ctx,
    scope *s,
    u32 dest_reg,
    u32 left_reg,
    u32 right_reg,
    u32 operation
) {
  Type dt = function_register_type(ss, s, dest_reg);
  u32 bits = function_integer_bits(ss, dt);
  Type base = Type_stripTag(dt, ss_typeenv(ss));
  bool is_signed = tu_is(r_sint, base.t);
  if (operation == enop_shl || operation == enop_shr) {
    makeWideType(_ctx, bits);
    PUTS("_shift_value(");
    makeWideOperandAs(ss, _ctx, s, left_reg, bits);
    PUTS(", ");
    makeRegisterPlace(ss, _ctx, s, right_reg);
    PUTS(", ");
    if (operation == enop_shl) PUTS("1");
    else PUTS("0");
    PUTS(", ");
    if (is_signed) PUTS("1");
    else PUTS("0");
    PUTS(")");
    return;
  }
  makeWideType(_ctx, bits);
  PUTS("_binary_value(");
  makeWideOperandAs(ss, _ctx, s, left_reg, bits);
  PUTS(", ");
  makeWideOperandAs(ss, _ctx, s, right_reg, bits);
  // enum_op derives its arithmetic values from cshl_int_op, so the operation
  // passes straight through as a cast.
  PUTS(", (enum cshl_int_op)");
  USETYPEPRINTER(u32, operation);
  PUTS(", ");
  if (is_signed) PUTS("1");
  else PUTS("0");
  PUTS(")");
}

static void makeWideTruth(
    superscope *ss,
    printerfunction_context _ctx,
    scope *s,
    u32 reg,
    u32 bits
) {
  PUTS("(cshl_int_compare(");
  makeWideOperandAs(ss, _ctx, s, reg, bits);
  PUTS(", ");
  makeWideType(_ctx, bits);
  PUTS("_from_decimal(\"0\").bytes");
  PUTS(", sizeof(");
  makeWideType(_ctx, bits);
  PUTS("), ");
  USETYPEPRINTER(usize, bits);
  PUTS(", 0) != 0)");
}

static void makeWideComparison(
    superscope *ss,
    printerfunction_context _ctx,
    scope *s,
    u32 left_reg,
    u32 right_reg,
    u32 operation
) {
  Type lt = function_register_type(ss, s, left_reg);
  Type rt = function_register_type(ss, s, right_reg);
  Type comparison_type = function_is_wide(ss, lt) ? lt : rt;
  u32 bits = function_integer_bits(ss, comparison_type);
  Type base = Type_stripTag(comparison_type, ss_typeenv(ss));
  bool is_signed = tu_is(r_sint, base.t);
  PUTS("(cshl_int_compare(");
  makeWideOperandAs(ss, _ctx, s, left_reg, bits);
  PUTS(", ");
  makeWideOperandAs(ss, _ctx, s, right_reg, bits);
  PUTS(", sizeof(");
  makeWideType(_ctx, bits);
  PUTS("), ");
  USETYPEPRINTER(usize, bits);
  PUTS(", ");
  if (is_signed) PUTS("1");
  else PUTS("0");
  PUTS(") ");
  makeRawOperator(_ctx, operation);
  PUTS(" 0)");
}

static void makeShiftAmount(superscope *ss, printerfunction_context _ctx, scope *s, Type rt, u32 r, bool negative) {
  if (negative) {
    PUTS("((");
    u32 bits = function_integer_bits(ss, rt);
    makeType(ss, _ctx, T(TypeTree_uint(bits ? bits : sizeof(usize) * CHAR_BIT)));
    PUTS(")(-(");
  }
  makeRawOperand(ss, _ctx, s, rt, r);
  if (negative) PUTS(" + 1)) + 1u)");
}

static void makeDirectedShift(superscope *ss, printerfunction_context _ctx, scope *s, Type dt, Type lt, Type rt, u32 l, u32 r, bool left, bool negative) {
  u32 bits = function_integer_bits(ss, dt);
  bool is_signed = !!function_sign_bits(dt, ss_typeenv(ss));
  PUTS("(");
  makeShiftAmount(ss, _ctx, s, rt, r, negative);
  PUTS(" >= ");
  USETYPEPRINTER(u32, bits);
  PUTS(" ? ");
  if (!left && is_signed) {
    PUTS("(");
    makeRawOperand(ss, _ctx, s, lt, l);
    PUTS(" < 0 ? -1 : 0)");
  } else PUTS("0");
  PUTS(" : ((");
  if (left) {
    PUTS("(");
    makeType(ss, _ctx, T(TypeTree_uint(bits)));
    PUTS(")");
  }
  makeRawOperand(ss, _ctx, s, lt, l);
  // Keep narrow left operands unsigned after C's integer promotions.
  if (left) PUTS(" + 0u");
  if (left) PUTS(") << ");
  else PUTS(") >> ");
  makeShiftAmount(ss, _ctx, s, rt, r, negative);
  PUTS("))");
}

static void makeShift(superscope *ss, printerfunction_context _ctx, scope *s, u32 l, u32 r, Type dt, Type lt, Type rt, u32 op) {
  bool left = op == enop_shl;
  bool signed_amount = function_sign_bits(rt, ss_typeenv(ss)) || tu_is(c_int, rt.t);
  if (signed_amount) {
    PUTS("(");
    makeRawOperand(ss, _ctx, s, rt, r);
    PUTS(" < 0 ? ");
    makeDirectedShift(ss, _ctx, s, dt, lt, rt, l, r, !left, true);
    PUTS(" : ");
  }
  makeDirectedShift(ss, _ctx, s, dt, lt, rt, l, r, left, false);
  if (signed_amount) PUTS(")");
}

static void makeCall(
    superscope *ss,
    printerfunction_context _ctx,
    scope *s,
    u32 callee_reg,
    u32 first_arg,
    u32 end_arg
) {
  typed_value *callee = scope_find(ss, s, callee_reg);
  assertMessage(callee->v.c_fun.fn && callee->v.c_fun.spec < msList_len(ss->functions[callee->v.c_fun.fn - 1].instances), "un-specialized function call");
  PUTS("cshl_fn");
  USETYPEPRINTER(usize, callee->v.c_fun.fn);
  PUTS("_");
  USETYPEPRINTER(usize, callee->v.c_fun.spec);
  PUTS("(");
  bool first = true;
  foreach (let i, range(first_arg, end_arg)) {
    Type t = function_register_type(ss, s, i);
    if (Type_is_comptime(t)) continue;
    if (!first) PUTS(", ");
    first = false;

    if (t.ref_kind == ref_ref) {
      makeRegisterAddress(ss, _ctx, s, i);
    } else if (t.ref_kind == ref_dref) {
      makeVariable(_ctx, s, i);
    } else {
      makeRegisterPlace(ss, _ctx, s, i);
    }
  }
  PUTS(")");
}

static void makeReference(superscope *ss, printerfunction_context _ctx, scope *s, itypeof(ir, ir_getref) op) {
  Type dt = function_register_type(ss, s, op.d);
  PUTS("  ");
  if (dt.ref_kind == ref_dref) {
    makeVariable(_ctx, s, op.d);
    PUTS(".data = ");
    makeVariable(_ctx, s, op.l);
    PUTS(".data");
  } else {
    Type lt = function_register_type(ss, s, op.l);
    bool field_ref = lt.ref_kind && function_register_is_field_ref(ss, s, op.d);
    if (field_ref) {
      PUTS("*((");
      makeRegisterType(ss, _ctx, s, op.l);
      PUTS(" *)");
      makeVariable(_ctx, s, op.d);
      PUTS(") = ");
      makeVariable(_ctx, s, op.l);
    } else {
      makeVariable(_ctx, s, op.d);
      PUTS(" = ");
      if (lt.ref_kind == ref_dref) makeVariable(_ctx, s, op.l), PUTS(".data");
      else if (lt.ref_kind) {
        if (function_register_is_field_ref(ss, s, op.l)) {
          PUTS("*(");
          makeRegisterType(ss, _ctx, s, op.l);
          PUTS(" *)");
        }
        makeVariable(_ctx, s, op.l);
      } else {
        PUTS("&");
        makeVariable(_ctx, s, op.l);
      }
    }
  }
  PUTS(";");
}

static void makeFieldReference(superscope *ss, printerfunction_context _ctx, scope *s, itypeof(ir, ir_field) op) {
  typeenv e = ss_typeenv(ss);
  Type lt = function_register_type(ss, s, op.l);
  r_bitfield bf = TypeTree_inner_bitfield(lt.t, e);
  PUTS("  ");
  makeVariable(_ctx, s, op.d);
  if (bf.start) {
    u32 offset = 0;
    foreach (let i, range(0, op.i))
      offset += type_bitf_item(e, bf, i)->width;
    PUTS(".data = (double_offset){ .ptr = (unsigned char *)");
    makeRegisterAddress(ss, _ctx, s, op.l);
    PUTS(" + ");
    USETYPEPRINTER(usize, offset / 8);
    PUTS(", .bit_offset = ");
    USETYPEPRINTER(usize, offset % 8);
    PUTS(", .width = ");
    USETYPEPRINTER(usize, type_bitf_item(e, bf, op.i)->width);
    PUTS(" }");
  } else {
    r_struct strct = TypeTree_inner_struct(lt.t, e);
    assertMessage(strct.start && op.i < type_struct_len(e, strct), "field type has no member");
    u32 offset = type_struct_item(e, strct, op.i)->offset;
    PUTS(" = (");
    makeRegisterType(ss, _ctx, s, op.d);
    PUTS(")((unsigned char *)");
    makeRegisterAddress(ss, _ctx, s, op.l);
    PUTS(" + ");
    USETYPEPRINTER(usize, offset);
    PUTS(")");
  }
  PUTS(";");
}

static void makeArrayReference(superscope *ss, printerfunction_context _ctx, scope *s, itypeof(ir, ir_array) op) {
  PUTS("  ");
  makeVariable(_ctx, s, op.d);
  PUTS(" = (");
  makeRegisterType(ss, _ctx, s, op.d);
  PUTS(")((unsigned char *)");
  makeRegisterAddress(ss, _ctx, s, op.l);
  PUTS(" + ");
  makeRegisterPlace(ss, _ctx, s, op.r);
  PUTS(" * sizeof(*");
  makeVariable(_ctx, s, op.d);
  PUTS("));");
}

static void makeCast(superscope *ss, printerfunction_context _ctx, scope *s, itypeof(ir, ir_cast) op) {
  Type dt = function_register_type(ss, s, op.d);
  Type lt = function_register_type(ss, s, op.l);
  PUTS("  ");
  if (function_is_wide(ss, dt)) {
    if (dt.ref_kind == ref_ref) PUTS("*");
    makeVariable(_ctx, s, op.d);
    PUTS(" = ");
    makeWideCastFromOperand(ss, _ctx, s, op.d, op.l);
    PUTS(";");
  } else if (function_is_wide(ss, lt)) {
    u32 source_bits = function_integer_bits(ss, lt);
    Type source_base = Type_stripTag(lt, ss_typeenv(ss));
    bool source_signed = tu_is(r_sint, source_base.t);
    if (dt.ref_kind == ref_ref) PUTS("*");
    makeVariable(_ctx, s, op.d);
    PUTS(" = (");
    makeType(ss, _ctx, dt);
    PUTS(") cshl_int_to_i64(");
    makeWideOperand(ss, _ctx, s, op.l);
    PUTS(", sizeof(");
    makeWideType(_ctx, source_bits);
    PUTS("), ");
    USETYPEPRINTER(usize, source_bits);
    PUTS(", ");
    if (source_signed) PUTS("1");
    else PUTS("0");
    PUTS(");");
  } else if (lt.ref_kind == ref_dref) {
    PUTS("cshl_read(");
    makeVariable(_ctx, s, op.l);
    PUTS(".data, (unsigned char *)&");
    if (dt.ref_kind == ref_ref) PUTS("*");
    makeVariable(_ctx, s, op.d);
    PUTS(", ");
    makeRegisterSize(ss, _ctx, s, op.d);
    PUTS(", ");
    makeRegisterSize(ss, _ctx, s, op.d);
    PUTS(", ");
    makeDrefSignBits(ss, _ctx, s, op.l);
    PUTS(");");
  } else {
    if (dt.ref_kind == ref_ref) PUTS("*");
    makeVariable(_ctx, s, op.d);
    PUTS(" = (");
    makeType(ss, _ctx, dt);
    PUTS(")");
    makeRegisterPlace(ss, _ctx, s, op.l);
    PUTS(";");
  }
}

static void makeFunctionInstruction(superscope *ss, printerfunction_context _ctx, scope *s, ir inst) {
  if (tu_is(ir_set, inst) &&
      (Type_is_comptime(function_register_type(ss, s, inst.ir_set.d)) ||
       function_is_non_int_comptime(function_register_type(ss, s, inst.ir_set.l))))
    return;
  if (tu_is(ir_cast, inst) &&
      (Type_is_comptime(function_register_type(ss, s, inst.ir_cast.d)) ||
       function_is_non_int_comptime(function_register_type(ss, s, inst.ir_cast.l))))
    return;
  if (tu_is(ir_rawop, inst) &&
      (Type_is_comptime(function_register_type(ss, s, inst.ir_rawop.d)) ||
       function_is_non_int_comptime(function_register_type(ss, s, inst.ir_rawop.l)) ||
       function_is_non_int_comptime(function_register_type(ss, s, inst.ir_rawop.r))))
    return;
  if (tu_is(ir_call, inst) && Type_is_comptime(function_register_type(ss, s, inst.ir_call.d)))
    return;
  if (tu_is(ir_ret, inst) && Type_is_comptime(function_register_type(ss, s, inst.ir_ret)))
    return;
  if (tu_is(ir_rawdot, inst)) PUTS("  // ir_rawdot");
  tu_match_void(
      inst,
      (ir_getref, op, makeReference(ss, _ctx, s, op)),
      (ir_getptr, op, {
        PUTS("  ");
        makeVariable(_ctx, s, op.d);
        PUTS(" = ");
        makeRegisterAddress(ss, _ctx, s, op.l);
        PUTS(";");
      }),
      (ir_putptr, op, {
        PUTS("  ");
        makeVariable(_ctx, s, op.d);
        PUTS(" = ");
        makeRegisterPlace(ss, _ctx, s, op.l);
        PUTS(";");
      }),
      (ir_cast, op, makeCast(ss, _ctx, s, op)),
      (ir_rawop, op, {
        Type dt = function_register_type(ss, s, op.d);
        Type lt = function_register_type(ss, s, op.l);
        Type rt = function_register_type(ss, s, op.r);
        u32 raw_op = op_as_enum(op.o);
        PUTS("  ");
        if (function_is_wide(ss, dt) && dt.ref_kind == ref_ref) PUTS("*");
        makeVariable(_ctx, s, op.d);
        PUTS(" = ");
        if (function_is_wide(ss, dt))
          makeWideOperation(ss, _ctx, s, op.d, op.l, op.r, raw_op);
        else if (function_is_wide(ss, lt) || function_is_wide(ss, rt)) {
          switch_tuple(
              raw_op,
              (enop_oand, {
                u32 truth_bits = function_is_wide(ss, lt) ? function_integer_bits(ss, lt) : function_integer_bits(ss, rt);
                makeWideTruth(ss, _ctx, s, op.l, truth_bits);
                PUTS(" && ");
                makeWideTruth(ss, _ctx, s, op.r, truth_bits);
              }),
              (enop_oor, {
                u32 truth_bits = function_is_wide(ss, lt) ? function_integer_bits(ss, lt) : function_integer_bits(ss, rt);
                makeWideTruth(ss, _ctx, s, op.l, truth_bits);
                PUTS(" || ");
                makeWideTruth(ss, _ctx, s, op.r, truth_bits);
              }),
              (enop_eq, makeWideComparison(ss, _ctx, s, op.l, op.r, enop_eq)),
              (enop_neq, makeWideComparison(ss, _ctx, s, op.l, op.r, enop_neq)),
              (enop_les, makeWideComparison(ss, _ctx, s, op.l, op.r, enop_les)),
              (enop_mor, makeWideComparison(ss, _ctx, s, op.l, op.r, enop_mor)),
              (enop_leq, makeWideComparison(ss, _ctx, s, op.l, op.r, enop_leq)),
              (enop_moq, makeWideComparison(ss, _ctx, s, op.l, op.r, enop_moq)),
              (default, assertMessage(false, "wide integer operation has a native destination"))
          );
        } else if (raw_op == enop_shl || raw_op == enop_shr) {
          makeShift(ss, _ctx, s, op.l, op.r, dt, lt, rt, raw_op);
        } else {
          makeRawOperand(ss, _ctx, s, lt, op.l);
          PUTS(" ");
          makeRawOperator(_ctx, raw_op);
          PUTS(" ");
          makeRawOperand(ss, _ctx, s, rt, op.r);
        }
        PUTS(";");
      }),
      (ir_rawdot, _, PUTS("")),
      (ir_field, op, makeFieldReference(ss, _ctx, s, op)),
      (ir_array, op, makeArrayReference(ss, _ctx, s, op)),
      (ir_call, op, {
        Type dt = function_register_type(ss, s, op.d);
        PUTS("  ");
        if (dt.ref_kind == ref_dref) {
          makeVariable(_ctx, s, op.d);
          PUTS(" = ");
          makeCall(ss, _ctx, s, op.l, op.r, op.e);
        } else {
          makeVariable(_ctx, s, op.d);
          PUTS(" = ");
          makeCall(ss, _ctx, s, op.l, op.r, op.e);
        }
        PUTS(";");
      }),
      (ir_set, op, {
        Type dt = function_register_type(ss, s, op.d);
        Type lt = function_register_type(ss, s, op.l);
        PUTS("  ");
        if (dt.ref_kind == ref_dref) {
          if (lt.ref_kind == ref_dref) {
            makeVariable(_ctx, s, op.d);
            PUTS(".data = ");
            makeVariable(_ctx, s, op.l);
            PUTS(".data;");
          } else {
            PUTS("  cshl_write(");
            makeVariable(_ctx, s, op.d);
            PUTS(".data, (const unsigned char *)&");
            makeRegisterPlace(ss, _ctx, s, op.l);
            PUTS(", ");
            makeRegisterSize(ss, _ctx, s, op.l);
            PUTS(");");
          }
        } else {
          if (lt.ref_kind == ref_dref) {
            PUTS("cshl_read(");
            makeVariable(_ctx, s, op.l);
            PUTS(".data, (unsigned char *)&");
            if (dt.ref_kind == ref_ref) PUTS("*");
            makeVariable(_ctx, s, op.d);
            PUTS(", ");
            makeRegisterSize(ss, _ctx, s, op.d);
            PUTS(", ");
            makeRegisterSize(ss, _ctx, s, op.d);
            PUTS(", ");
            makeDrefSignBits(ss, _ctx, s, op.l);
            PUTS(");");
            PUTS(";");
          } else {
            bool field_ref = dt.ref_kind == ref_ref && function_register_is_field_ref(ss, s, op.d);
            if (field_ref) {
              PUTS("*(*(");
              makeRegisterType(ss, _ctx, s, op.d);
              PUTS(" *)");
              makeVariable(_ctx, s, op.d);
              PUTS(")");
              PUTS(" = ");
            } else {
              if (dt.ref_kind == ref_ref) PUTS("*");
              makeVariable(_ctx, s, op.d);
              PUTS(" = ");
            }
            if (dt.ref_kind == ref_ref && lt.ref_kind) makeVariable(_ctx, s, op.l);
            else makeRegisterPlace(ss, _ctx, s, op.l);
            PUTS(";");
          }
        }
      }),
      (ir_blocks, _, PUTS("  {")),
      (ir_blocke, _, PUTS("  }")),
      (ir_ifs, cond, {
        PUTS("  if (");
        makeRegisterPlace(ss, _ctx, s, cond);
        PUTS(") {");
      }),
      (ir_ifd, _, PUTS("  } else {")),
      (ir_ife, _, PUTS("  }")),
      (ir_farg, op, {
        Type dt = function_register_type(ss, s, op.d);
        function *function = &ss->functions[s->instance.fn - 1];
        function_instance *instance = &function->instances[s->instance.index];
        u32 arg_index = 0;
        for (u32 i = 0; i < op.l; i++)
          arg_index += !Type_is_comptime(function_instance_type_at(ss, instance, i));
        PUTS("  ");
        if (dt.ref_kind == ref_dref) {
          makeVariable(_ctx, s, op.d);
          PUTS(".data = arg");
          USETYPEPRINTER(usize, arg_index);
          PUTS(".data");
        } else {
          makeVariable(_ctx, s, op.d);
          PUTS(" = arg");
          USETYPEPRINTER(usize, arg_index);
        }
        PUTS(";");
      }),
      (ir_ret, reg, {
        function *function = &ss->functions[s->instance.fn - 1];
        Type rt = function_instance_ret(ss, &function->instances[s->instance.index]);
        PUTS("  return ");
        if (rt.ref_kind == ref_ref) {
          makeRegisterAddress(ss, _ctx, s, reg);
        } else if (rt.ref_kind == ref_dref) {
          makeVariable(_ctx, s, reg);
        } else {
          makeRegisterPlace(ss, _ctx, s, reg);
        }
        PUTS(";");
      }),
      // (ir_ret, reg, {
      //   function *function = &ss->functions[s->instance.fn - 1];
      //   Type rt = function_instance_ret(ss, &function->instances[s->instance.index]);
      //   PUTS("  return ");
      //   if (rt.ref_kind) makeVariable(_ctx, s, reg);
      //   else makeRegisterPlace(ss, _ctx, s, reg);
      //   PUTS(";");
      // }),
      (ir_loops, _, PUTS("  while (1) {")),
      (ir_loope, _, PUTS("  }")),
      (ir_break, _, PUTS("  break;")),
      (ir_cont, _, PUTS("  continue;")),
      (default, assertMessage(false, "unhandled instruction")),
  );
  PUTS("\n");
}

// This IR keeps compile-time-known constants as runtime registers carrying a
// value, so a register must be initialized from that value; a bare {0} would
// lose it (e.g. the constant arguments of a call).
static void makeVariableInit(superscope *ss, printerfunction_context _ctx, scope *s, u32 reg) {
  typeenv e = ss_typeenv(ss);
  Type t = function_register_type(ss, s, reg);
  typed_value *value = scope_find(ss, s, reg);
  usize size = Type_sizeof(t, e);
  if (!size || !value->v.value || t.ref_kind || t.ptr_level) {
    PUTS("{0}");
    return;
  }
  Type base = Type_stripTag(t, e);
  if (tu_is(r_bool, base.t)) {
    if (*(bool *)value->v.value) PUTS("1");
    else PUTS("0");
    return;
  }
  if (out_integerIsWide(e, t)) {
    // A width C has no type for is kept as its own bytes, little endian.
    PUTS("{ .bytes = {");
    foreach (let i, range(0, size)) {
      if (i) PUTS(", ");
      USETYPEPRINTER(u32, ((const unsigned char *)value->v.value)[i]);
    }
    PUTS("} }");
    return;
  }
  if (tu_is(r_sint, base.t) || tu_is(r_uint, base.t)) {
    u64 raw = 0;
    memcpy(&raw, value->v.value, MIN$(size, sizeof(raw)));
    if (tu_is(r_sint, base.t)) {
      unsigned shift = 64 - (unsigned)(size * CHAR_BIT);
      USETYPEPRINTER(i64, (i64)(raw << shift) >> shift);
    } else USETYPEPRINTER(u64, raw);
    return;
  }
  PUTS("{0}");
}

static void makeVariables(superscope *ss, printerfunction_context _ctx, scope *s) {
  foreach (let value, span(s->values, msList_len(s->values))) {
    Type t = ss_type(ss, value->t);
    if (Type_is_comptime(t)) continue;
    t.isconst = false;
    u32 reg = s->start + (u32)(value - s->values) + 1;
    makeType(ss, _ctx, t);
    PUTS(" ");
    makeVariable(_ctx, s, reg);
    PUTS(" = ");
    makeVariableInit(ss, _ctx, s, reg);
    PUTS(";\n");
  }
}

static void makeFunctionBody(superscope *ss, printerfunction_context _ctx, function_instance *instance) {
  makeVariables(ss, _ctx, instance->spec);
  foreach (let inst, span(instance->spec->instructions, msList_len(instance->spec->instructions)))
    makeFunctionInstruction(ss, _ctx, instance->spec, *inst);
}

static void makeFunctionSignature(
    superscope *ss,
    printerfunction_context _ctx,
    usize function_index,
    usize instance_index,
    function_instance *instance
) {
  makeType(ss, _ctx, function_instance_ret(ss, instance));
  PUTS(" cshl_fn"), USETYPEPRINTER(usize, function_index + 1), PUTS("_"), USETYPEPRINTER(usize, instance_index);
  PUTS("(");
  u32 arg_count = 0;
  foreach (let k, range(0, function_instance_param_count(ss, instance))) {
    Type t = function_instance_type_at(ss, instance, k);
    if (Type_is_comptime(t)) continue;
    if (arg_count++) PUTS(", ");
    makeFunctionType(ss, _ctx, instance->spec->values[1 + k].t);
    PUTS(" arg");
    USETYPEPRINTER(usize, arg_count - 1);
  }
  if (!arg_count) PUTS("void");
  PUTS(")");
}

void makeMain(superscope *ss, printerfunction_context _ctx) {
  scope *s = ss->outermost;
  PUTS("int main(void){\n");
  foreach (let inst, span(s->instructions, msList_len(s->instructions)))
    makeFunctionInstruction(ss, _ctx, s, *inst);
  u32 result = scope_last_item(ss, s);
  Type result_type = function_register_type(ss, s, result);
  if (tu_is(r_bool, result_type.t) && !result_type.decorations) {
    PUTS("  return !(");
    makeRegisterPlace(ss, _ctx, s, result);
    PUTS(");\n");
  } else PUTS("  return 0;\n");
  PUTS("}\n");
}

void makeFuncs(superscope *ss, printerfunction_context _ctx) {
  foreach (let i, range(0, msList_len(ss->functions))) {
    function *function = &ss->functions[i];
    foreach (let j, range(0, msList_len(function->instances))) {
      function_instance *instance = &function->instances[j];
      if (!function_should_be_emitted(ss, instance)) continue;
      makeFunctionSignature(ss, _ctx, i, j, instance);
      PUTS(";\n");
    }
  }
  foreach (let i, range(0, msList_len(ss->functions))) {
    function *function = &ss->functions[i];
    foreach (let j, range(0, msList_len(function->instances))) {
      function_instance *instance = &function->instances[j];
      if (!function_should_be_emitted(ss, instance)) continue;
      makeFunctionSignature(ss, _ctx, i, j, instance);
      PUTS("{\n");
      makeFunctionBody(ss, _ctx, instance);
      PUTS("}\n");
    }
  }
}

#pragma once
#include "../cout.h"
#include "../ir/common.h"
#include "../type.h"
#include "wheels/macros.h"
#include "wheels/print/print_pre.h"

static Type function_instance_type_at(superscope *ss, function_instance *instance, usize index) {
  assertMessage(index < instance->sig.len, "function signature index out of range");
  return Type_at(ss_typeenv(ss), instance->sig.ptr[index]);
}

static bool function_should_be_emitted(superscope *ss, function_instance *instance) {
  assertMessage(instance->sig.len, "function instance has no signature");
  bool has_runtime_arg = false;
  foreach (let i, range(0, instance->sig.len - 1))
    has_runtime_arg |= !Type_is_comptime(function_instance_type_at(ss, instance, i));
  bool returns_runtime = !Type_is_comptime(function_instance_ret(ss, instance));
  assertMessage(!has_runtime_arg || returns_runtime, "runtime arguments require a runtime return");
  return returns_runtime;
}

static void makeFunctionType(superscope *ss, printerfunction_context _ctx, Type t) {
  if (Type_is_comptime(t)) PUTS("void_t");
  else makeType(ss, _ctx, t);
}

static void makeVariable(printerfunction_context _ctx, u32 reg) {
  PUTS("v");
  USETYPEPRINTER(u32, reg);
}

static Type function_register_type(superscope *ss, scope *s, u32 reg) {
  return ss_type(ss, scope_find(ss, s, reg)->t);
}

static u32 function_sign_bits(Type t, typeenv e) {
  while (tu_is(r_tag, t.t)) t = Type_at(e, tu_catch(r_tag, t.t).t);
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
    while (tu_is(r_tag, base.t)) base = Type_at(e, tu_catch(r_tag, base.t).t);
    r_struct strct = TypeTree_inner_struct(base.t, e);
    if (strct.start && instruction->ir_field.i < type_struct_len(e, strct))
      return !!type_struct_item(e, strct, instruction->ir_field.i)->t.ref_kind;
  }
  return false;
}

static void makeRegisterPlace(superscope *ss, printerfunction_context _ctx, scope *s, u32 reg) {
  Type t = function_register_type(ss, s, reg);
  typed_value *value = scope_find(ss, s, reg);
  if (tu_is(c_int, t.t)) {
    USETYPEPRINTER(bigint, value->v.c_int);
    return;
  }
  if (!scope_register_written(s, reg) && tu_is(r_bool, t.t) && value->v.value) {
    if (*(bool *)value->v.value) PUTS("true");
    else PUTS("false");
    return;
  }
  assertMessage(!Type_is_comptime(t), "unsupported comptime value in C output");
  assertMessage(t.ref_kind != ref_dref, "double-offset reference used as a C value");
  if (t.ref_kind == ref_ref && function_register_is_field_ref(ss, s, reg)) {
    PUTS("*(*(typeof(");
    makeVariable(_ctx, reg);
    PUTS(") *)");
    makeVariable(_ctx, reg);
    PUTS(")");
    return;
  }
  if (t.ref_kind == ref_ref) PUTS("*");
  makeVariable(_ctx, reg);
}

static void makeDrefSignBits(superscope *ss, printerfunction_context _ctx, scope *s, u32 reg) {
  Type t = function_register_type(ss, s, reg);
  if (t.ref_kind == ref_dref && function_sign_bits(t, ss_typeenv(ss))) {
    makeVariable(_ctx, reg);
    PUTS(".data.width");
  } else USETYPEPRINTER(u32, function_sign_bits(t, ss_typeenv(ss)));
}

static void makeDrefValue(superscope *ss, printerfunction_context _ctx, scope *s, u32 reg) {
  Type t = function_register_type(ss, s, reg);
  if (function_sign_bits(t, ss_typeenv(ss))) PUTS("(int64_t)");
  PUTS("cshl_load(");
  makeVariable(_ctx, reg);
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
    case enop_mul: PUTS("*"); break;
    case enop_div: PUTS("/"); break;
    case enop_mod: PUTS("%"); break;
    case enop_plus: PUTS("+"); break;
    case enop_minus: PUTS("-"); break;
    case enop_shl: PUTS("<<"); break;
    case enop_shr: PUTS(">>"); break;
    case enop_oband: PUTS("&"); break;
    case enop_obor: PUTS("|"); break;
    case enop_bxor: PUTS("^"); break;
    case enop_oand: PUTS("&&"); break;
    case enop_oor: PUTS("||"); break;
    case enop_eq: PUTS("=="); break;
    case enop_neq: PUTS("!="); break;
    case enop_les: PUTS("<"); break;
    case enop_mor: PUTS(">"); break;
    case enop_leq: PUTS("<="); break;
    case enop_moq: PUTS(">="); break;
    default: assertMessage(false, "unsupported raw operator");
  }
}

static void makeRawOperand(superscope *ss, printerfunction_context _ctx, scope *s, Type t, u32 reg) {
  if (t.ref_kind == ref_dref) makeDrefValue(ss, _ctx, s, reg);
  else makeRegisterPlace(ss, _ctx, s, reg);
}

static void makeShift(superscope *ss, printerfunction_context _ctx, scope *s, u32 d, u32 l, u32 r, Type dt, Type lt, Type rt, u32 op) {
  typeenv e = ss_typeenv(ss);
  Type base = dt;
  while (tu_is(r_tag, base.t)) base = Type_at(e, tu_catch(r_tag, base.t).t);
  usize bits = tu_match_exp(
      base.t,
      (r_sint, v, (usize)v.bits),
      (r_uint, v, (usize)v.bits),
      (r_bool, _, (usize)1),
      (default, (usize)0)
  );
  if (!bits) bits = sizeof(uint64_t) * CHAR_BIT;
  bool is_signed = tu_is(r_sint, base.t);
  if (op == enop_shl) {
    bool negative_branch = is_signed;
    if (negative_branch) {
      PUTS("((int64_t)");
      makeRawOperand(ss, _ctx, s, rt, r);
      PUTS(") < 0 ? ((int64_t)");
      makeRawOperand(ss, _ctx, s, lt, l);
      PUTS(") >> (-(int64_t)");
      makeRawOperand(ss, _ctx, s, rt, r);
      PUTS(") : (");
    }
    PUTS("(");
    if (is_signed) PUTS("(int64_t)");
    else PUTS("(uint64_t)");
    makeRawOperand(ss, _ctx, s, lt, l);
    PUTS(") * (");
    PUTS("((uint64_t)");
    makeRawOperand(ss, _ctx, s, rt, r);
    PUTS(") >= ");
    USETYPEPRINTER(usize, bits);
    PUTS(" ? 0 : (");
    if (is_signed) PUTS("(int64_t)");
    else PUTS("(uint64_t)");
    PUTS("1 << (");
    makeRawOperand(ss, _ctx, s, rt, r);
    PUTS(")))");
    if (negative_branch) PUTS(")");
  } else {
    bool negative_branch = is_signed;
    if (negative_branch) {
      PUTS("((int64_t)");
      makeRawOperand(ss, _ctx, s, rt, r);
      PUTS(") < 0 ? ((int64_t)");
      makeRawOperand(ss, _ctx, s, lt, l);
      PUTS(") << (-(int64_t)");
      makeRawOperand(ss, _ctx, s, rt, r);
      PUTS(") : (");
    }
    PUTS("(");
    if (is_signed) PUTS("(int64_t)");
    else PUTS("(uint64_t)");
    makeRawOperand(ss, _ctx, s, lt, l);
    PUTS(") >> (");
    PUTS("((uint64_t)");
    makeRawOperand(ss, _ctx, s, rt, r);
    PUTS(") >= ");
    USETYPEPRINTER(usize, bits);
    PUTS(" ? ");
    USETYPEPRINTER(usize, is_signed ? bits - 1 : 0);
    PUTS(" : ");
    makeRawOperand(ss, _ctx, s, rt, r);
    PUTS(")");
    if (negative_branch) PUTS(")");
  }
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
  assertMessage(callee->v.c_fun.fn && callee->v.c_fun.spec < msList_len(ss->functions[callee->v.c_fun.fn - 1].instances),
                "un-specialized function call");
  PUTS("cshl_fn");
  USETYPEPRINTER(usize, callee->v.c_fun.fn);
  PUTS("_");
  USETYPEPRINTER(usize, callee->v.c_fun.spec);
  PUTS("(");
  foreach (let i, range(first_arg, end_arg)) {
    if (i != first_arg) PUTS(", ");
    Type t = function_register_type(ss, s, i);
    if (Type_is_comptime(t)) PUTS("((void_t){})");
    else makeVariable(_ctx, i);
  }
  PUTS(")");
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
      (ir_getref, op, {
        Type dt = function_register_type(ss, s, op.d);
        PUTS("  ");
        if (dt.ref_kind == ref_dref) {
          makeVariable(_ctx, op.d);
          PUTS(".data = ");
          makeVariable(_ctx, op.l);
          PUTS(".data");
        } else {
          Type lt = function_register_type(ss, s, op.l);
          bool field_ref = lt.ref_kind && function_register_is_field_ref(ss, s, op.d);
          if (field_ref) {
            PUTS("*(typeof(");
            makeVariable(_ctx, op.l);
            PUTS(") *)");
            makeVariable(_ctx, op.d);
            PUTS(" = ");
            makeVariable(_ctx, op.l);
          } else {
            makeVariable(_ctx, op.d);
            PUTS(" = (typeof(");
            makeVariable(_ctx, op.d);
            PUTS("))");
            if (lt.ref_kind == ref_dref) makeVariable(_ctx, op.l), PUTS(".data");
            else if (lt.ref_kind) makeVariable(_ctx, op.l);
            else {
              PUTS("&");
              makeVariable(_ctx, op.l);
            }
          }
        }
        PUTS(";");
      }),
      (ir_getptr, op, {
        PUTS("  ");
        makeVariable(_ctx, op.d);
        PUTS(" = (typeof(");
        makeVariable(_ctx, op.d);
        PUTS("))");
        makeRegisterAddress(ss, _ctx, s, op.l);
        PUTS(";");
      }),
      (ir_putptr, op, {
        PUTS("  ");
        makeVariable(_ctx, op.d);
        PUTS(" = (typeof(");
        makeVariable(_ctx, op.d);
        PUTS("))");
        makeRegisterPlace(ss, _ctx, s, op.l);
        PUTS(";");
      }),
      (ir_cast, op, {
        Type dt = function_register_type(ss, s, op.d);
        Type lt = function_register_type(ss, s, op.l);
        PUTS("  ");
        if (lt.ref_kind == ref_dref) {
          PUTS("cshl_read(");
          makeVariable(_ctx, op.l);
          PUTS(".data, (unsigned char *)&");
          if (dt.ref_kind == ref_ref) PUTS("*");
          makeVariable(_ctx, op.d);
          PUTS(", ");
          makeRegisterSize(ss, _ctx, s, op.d);
          PUTS(", ");
          makeRegisterSize(ss, _ctx, s, op.d);
          PUTS(", ");
          makeDrefSignBits(ss, _ctx, s, op.l);
          PUTS(");");
        } else {
          if (dt.ref_kind == ref_ref) PUTS("*");
          makeVariable(_ctx, op.d);
          PUTS(" = (");
          makeType(ss, _ctx, dt);
          PUTS(")");
          makeRegisterPlace(ss, _ctx, s, op.l);
          PUTS(";");
        }
      }),
      (ir_rawop, op, {
        Type lt = function_register_type(ss, s, op.l);
        Type rt = function_register_type(ss, s, op.r);
        PUTS("  ");
        makeVariable(_ctx, op.d);
        PUTS(" = ");
        if (op_as_enum(op.o) == enop_shl || op_as_enum(op.o) == enop_shr)
          makeShift(ss, _ctx, s, op.d, op.l, op.r, function_register_type(ss, s, op.d), lt, rt, op_as_enum(op.o));
        else {
          makeRawOperand(ss, _ctx, s, lt, op.l);
          PUTS(" ");
          makeRawOperator(_ctx, op_as_enum(op.o));
          PUTS(" ");
          makeRawOperand(ss, _ctx, s, rt, op.r);
        }
        PUTS(";");
      }),
      (ir_rawdot, _, PUTS("")),
      (ir_field, op, {
        typeenv e = ss_typeenv(ss);
        Type lt = function_register_type(ss, s, op.l);
        r_bitfield bf = TypeTree_inner_bitfield(lt.t, e);
        PUTS("  ");
        makeVariable(_ctx, op.d);
        if (bf.start) {
          u32 offset = 0;
          foreach (let i, range(0, op.i)) offset += type_bitf_item(e, bf, i)->width;
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
          PUTS(" = (typeof(");
          makeVariable(_ctx, op.d);
          PUTS("))((unsigned char *)");
          makeRegisterAddress(ss, _ctx, s, op.l);
          PUTS(" + ");
          USETYPEPRINTER(usize, offset);
          PUTS(")");
        }
        PUTS(";");
      }),
      (ir_array, op, {
        PUTS("  ");
        makeVariable(_ctx, op.d);
        PUTS(" = (typeof(");
        makeVariable(_ctx, op.d);
        PUTS("))((unsigned char *)");
        makeRegisterAddress(ss, _ctx, s, op.l);
        PUTS(" + ");
        makeRegisterPlace(ss, _ctx, s, op.r);
        PUTS(" * sizeof(*");
        makeVariable(_ctx, op.d);
        PUTS("));");
      }),
      (ir_call, op, {
        Type dt = function_register_type(ss, s, op.d);
        PUTS("  ");
        if (dt.ref_kind == ref_dref) {
          makeVariable(_ctx, op.d);
          PUTS(" = ");
          makeCall(ss, _ctx, s, op.l, op.r, op.e);
        } else {
          makeVariable(_ctx, op.d);
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
            makeVariable(_ctx, op.d);
            PUTS(".data = ");
            makeVariable(_ctx, op.l);
            PUTS(".data;");
          } else {
            PUTS("  cshl_write(");
            makeVariable(_ctx, op.d);
            PUTS(".data, (const unsigned char *)&");
            makeRegisterPlace(ss, _ctx, s, op.l);
            PUTS(", ");
            makeRegisterSize(ss, _ctx, s, op.l);
            PUTS(");");
          }
        } else {
          if (lt.ref_kind == ref_dref) {
            PUTS("cshl_read(");
            makeVariable(_ctx, op.l);
            PUTS(".data, (unsigned char *)&");
            if (dt.ref_kind == ref_ref) PUTS("*");
            makeVariable(_ctx, op.d);
            PUTS(", ");
            makeRegisterSize(ss, _ctx, s, op.d);
            PUTS(", ");
            makeRegisterSize(ss, _ctx, s, op.d);
            PUTS(", ");
            makeDrefSignBits(ss, _ctx, s, op.l);
            PUTS(");");
          PUTS(";");          } else {
            bool field_ref = dt.ref_kind == ref_ref && function_register_is_field_ref(ss, s, op.d);
            if (field_ref) {
              PUTS("*(*(typeof(");
              makeVariable(_ctx, op.d);
              PUTS(") *)");
              makeVariable(_ctx, op.d);
              PUTS(")");
              PUTS(" = ");
            } else {
              if (dt.ref_kind == ref_ref) PUTS("*");
              makeVariable(_ctx, op.d);
              PUTS(" = (typeof(");
              if (dt.ref_kind == ref_ref) PUTS("*");
              makeVariable(_ctx, op.d);
              PUTS("))");
            }
            if (dt.ref_kind == ref_ref && lt.ref_kind) makeVariable(_ctx, op.l);
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
        PUTS("  ");
        if (dt.ref_kind == ref_dref) {
          makeVariable(_ctx, op.d);
          PUTS(".data = arg");
          USETYPEPRINTER(usize, op.l);
          PUTS(".data");
        } else {
          makeVariable(_ctx, op.d);
          PUTS(" = arg");
          USETYPEPRINTER(usize, op.l);
        }
        PUTS(";");
      }),
      (ir_ret, reg, {
        function *function = &ss->functions[s->instance.fn - 1];
        Type rt = function_instance_ret(ss, &function->instances[s->instance.index]);
        PUTS("  return ");
        if (rt.ref_kind) makeVariable(_ctx, reg);
        else makeRegisterPlace(ss, _ctx, s, reg);
        PUTS(";");
      }),
      (ir_loops, _, PUTS("  while (1) {")),
      (ir_loope, _, PUTS("  }")),
      (ir_break, _, PUTS("  break;")),
      (ir_cont, _, PUTS("  continue;")),
      (default, assertMessage(false, "unhandled instruction")),
  );
  PUTS("\n");
}

static void makeVariables(superscope *ss, printerfunction_context _ctx, scope *s) {
  foreach (let value, span(s->values, msList_len(s->values))) {
    Type t = ss_type(ss, value->t);
    if (Type_is_comptime(t)) continue;
    t.isconst = false;
    makeType(ss, _ctx, t);
    PUTS(" v"), USETYPEPRINTER(usize, s->start + (value - s->values) + 1), PUTS(" = {};\n");
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
  foreach (let k, range(0, instance->sig.len - 1)) {
    if (k) PUTS(", ");
    makeFunctionType(ss, _ctx, function_instance_type_at(ss, instance, k));
    PUTS(" arg");
    USETYPEPRINTER(usize, k);
  }
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

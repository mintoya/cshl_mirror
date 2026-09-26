#pragma once
#include "ir/coerce.h"
#include "ir/common.h"
#include "ir/scope.h"
#include "srcpos.h"
#include "type.h"
#include "wheels/macros.h"
// Compile the runtime text into the compiler so C output is self-contained.
void out_runtime(printerfunction_context _ctx) {
  static const unsigned char source[] = {
#embed "cshl.lib.c"
  };
  _ctx.put((const char *)source, _ctx.arb, sizeof(source), 0);
  PUTS("\n");
}

// Compile-time members have no storage. A union is natural only when every
// runtime member starts at zero and every member has its type's alignment.
static bool out_structIsUnnatural(typeenv e, r_struct t) {
  let n = type_struct_len(e, t);
  bool all_zero = true;
  foreach (let i, range(0, n)) {
    let item = type_struct_item(e, t, i);
    if (Type_is_comptime(item->t)) continue;
    if (item->alignment != Type_alignof(item->t, e)) return true;
    if (item->offset) all_zero = false;
  }
  if (all_zero) return false;

  foreach (let i, range(0, n)) {
    let a = type_struct_item(e, t, i);
    if (Type_is_comptime(a->t)) continue;
    let a_size = Type_sizeof(a->t, e);
    if (!a_size) continue;
    foreach (let j, range(i + 1, n)) {
      let b = type_struct_item(e, t, j);
      if (Type_is_comptime(b->t)) continue;
      let b_size = Type_sizeof(b->t, e);
      if (!b_size) continue;
      // Compare distances to avoid overflowing offset + size.
      if (a->offset <= b->offset ? (usize)b->offset - a->offset < a_size
                                 : (usize)a->offset - b->offset < b_size)
        return true;
    }
  }
  return false;
}

void out_memberName(fptr name, printerfunction_context _ctx) {
  PUTS("_");
  foreach (let i, range(0, name.len)) {
    u8 byte = (u8)name.ptr[i];
    char digits[2] = {'a' + (byte >> 4), 'a' + (byte & 15)};
    _ctx.put(digits, _ctx.arb, 2, 0);
  }
  PUTS("_");
}
// Each interned type gets an alias. Pointer aliases do not require a complete
// pointee, which permits recursive structures.
static typeidx out_typeIndex(typeenv e, Type t) {
  foreach (let i, range(0, e.tlist->len))
    if (!Type_cmp(e, Type_at(e, i + 1), t)) return i + 1;
  assertMessage(false, "C output: member type was not interned");
  return 0;
}
// A tag names a type, it does not identify one. Strip a real r_tag wrapper and
// compare the underlying trees, so a register carrying a structure under any
// name still names the interned typedef the type section defines. (The
// discriminator itself must not be cleared: a struct and a bit field share a
// payload layout and would then compare equal.)
static bool out_noCType(Type t);
static TypeTree out_stripTree(typeenv e, TypeTree t) {
  while (tu_is(r_tag, t)) t = type_tree(e, tu_catch(r_tag, t).t);
  return t;
}
static typeidx out_typeIndexOf(typeenv e, TypeTree t) {
  let probe = out_stripTree(e, t);
  foreach (let i, range(0, e.tlist->len)) {
    let o = Type_at(e, i + 1);
    // Only the entries the type section actually defines can name a C type.
    if (o.ref_kind || o.ptr_level || out_noCType(o)) continue;
    if (!TypeTree_cmp(e, out_stripTree(e, o.t), probe)) return i + 1;
  }
  return 0;
}
static void out_typeName(typeidx i, printerfunction_context _ctx) {
  PUTS("cshl_t"), USETYPEPRINTER(u32, i);
}
static bool out_isUnion(typeenv e, r_struct t) {
  if (out_structIsUnnatural(e, t)) return false;
  u32 count = 0;
  foreach (let i, range(0, type_struct_len(e, t))) {
    let m = type_struct_item(e, t, i);
    if (Type_is_comptime(m->t)) continue;
    if (m->offset) return false;
    count++;
  }
  return count > 1;
}
// Every register is a C object of its own type. Scalars and pointers are the
// natural ones; structures, arrays, bit fields and non-native integer widths
// are the typedefs the type section already builds. Only a dref needs the
// pointer-plus-bit-offset pair, because that is what the IR stores for it.
typedef enum : u8 {
  out_st_none = 0,
  out_st_obj,
  out_st_scalar,
  out_st_ptr,
  out_st_dref,
} out_storageKind;
static bool out_isNatBits(u32 bits) {
  return bits == 8 || bits == 16 || bits == 32 || bits == 64;
}
static out_storageKind out_storageKindOf(Type t, typeenv e) {
  if (out_noCType(t)) return out_st_none;
  if (t.ref_kind == ref_dref) return out_st_dref;
  if (t.ref_kind || t.ptr_level) return out_st_ptr;
  let s = Type_stripTag(t, e);
  if (tu_is(r_bool, s.t)) return out_st_scalar;
  if (tu_is(r_sint, s.t) && out_isNatBits(s.t.r_sint.bits)) return out_st_scalar;
  if (tu_is(r_uint, s.t) && out_isNatBits(s.t.r_uint.bits)) return out_st_scalar;
  return out_st_obj;
}
// A type the C output cannot spell: only the compile-time kinds. The comptime
// flag alone does not make a structure or an array unspellable, they still have
// storage and therefore still need a C type.
static bool out_noCType(Type t) {
  t.iscomptime = false;
  return Type_is_comptime(t);
}
static void out_typeDefinition(superscope *ss, typeidx idx, u8 *state, printerfunction_context _ctx) {
  if (state[idx]) return;
  state[idx] = 1;
  let e = ss_typeenv(ss);
  let t = Type_at(e, idx);
  if (out_noCType(t) || tu_is(r_null, t.t)) return;
  if (!t.ref_kind && !t.ptr_level) {
    if (tu_is(r_tag, t.t)) out_typeDefinition(ss, t.t.r_tag.t, state, _ctx);
    if (tu_is(r_arr, t.t)) out_typeDefinition(ss, t.t.r_arr.t, state, _ctx);
    if (tu_is(r_struct, t.t) && !out_structIsUnnatural(e, t.t.r_struct)) {
      foreach (let j, range(0, type_struct_len(e, t.t.r_struct))) {
        let m = type_struct_item(e, t.t.r_struct, j);
        if (!out_noCType(m->t)) out_typeDefinition(ss, out_typeIndex(e, m->t), state, _ctx);
      }
    }
  }
  if (!t.ref_kind && !t.ptr_level && tu_is(r_struct, t.t)) {
    bool un = out_isUnion(e, t.t.r_struct);
    if (un) PUTS("union ");
    else PUTS("struct ");
    out_typeName(idx, _ctx), PUTS(" {\n");
    usize curr = 0;
    u32 pad = 0;
    let n = type_struct_len(e, t.t.r_struct);
    // Layout order need not be declaration order.
    let order = mList_init(PRINTARGS_ALLOCATOR(), u32, n ? n : 1);
    defer { mList_deinit(order); };
    foreach (let j, range(0, n))
      if (!Type_is_comptime(type_struct_item(e, t.t.r_struct, j)->t)) mList_push(order, j);
    let ord = mList_arr(order);
    for (u32 j = 1; j < mList_len(order); j++) {
      u32 k = j, moving = ord[j];
      while (k && type_struct_item(e, t.t.r_struct, ord[k - 1])->offset > type_struct_item(e, t.t.r_struct, moving)->offset) {
        ord[k] = ord[k - 1];
        k--;
      }
      ord[k] = moving;
    }
    bool strange = out_structIsUnnatural(e, t.t.r_struct);
    // A zero-sized marker may lie inside a previous field without overlapping
    // its storage. C cannot place that marker in an ordinary member sequence.
    usize end = 0;
    foreach (let j, range(0, mList_len(order))) {
      let m = type_struct_item(e, t.t.r_struct, ord[j]);
      if (!un && m->offset < end) strange = true;
      end = MAX$(end, m->offset + Type_sizeof(m->t, e));
    }
    foreach (let j, range(0, mList_len(order))) {
      let m = type_struct_item(e, t.t.r_struct, ord[j]);
      if (!un && m->offset > curr) {
        PUTS("unsigned char cshl_pad"), USETYPEPRINTER(u32, pad++);
        PUTS("["), USETYPEPRINTER(usize, (usize)m->offset - curr), PUTS("];\n");
      }
      bool marker = strange || !Type_sizeof(m->t, e);
      if (marker) PUTS("unsigned char ");
      else out_typeName(out_typeIndex(e, m->t), _ctx), PUTS(" ");
      out_memberName(type_name(e, m->name), _ctx);
      if (marker) PUTS("[0]");
      PUTS(";\n");
      curr = m->offset + (strange ? 0 : Type_sizeof(m->t, e));
    }
    let size = Type_sizeof(t, e);
    if (un && size) {
      PUTS("unsigned char cshl_storage["), USETYPEPRINTER(usize, size), PUTS("];\n");
    }
    if (!un && size > curr) {
      PUTS("unsigned char cshl_pad"), USETYPEPRINTER(u32, pad++);
      PUTS("["), USETYPEPRINTER(usize, size - curr), PUTS("];\n");
    }
    PUTS("} __attribute__((packed));\n");
  } else {
    PUTS("typedef ");
    if (t.ref_kind == ref_dref) PUTS("double_offset");
    else if (t.ref_kind || t.ptr_level) PUTS("unsigned char *");
    // CSHL's void is an ordinary zero-sized type, so it needs a usable C type
    // too: an empty structure, never C's own void.
    else tu_match_void(
        t.t,
        (r_void, _, PUTS("struct {}")),
        (r_bool, _, PUTS("_Bool")),
        (r_uint, u, {
          if (u.bits == 8 || u.bits == 16 || u.bits == 32 || u.bits == 64) PUTS("uint"), USETYPEPRINTER(u16, u.bits), PUTS("_t");
          else PUTS("struct { unsigned char bytes["), USETYPEPRINTER(usize, Type_sizeof(t, e)), PUTS("]; }");
        }),
        (r_sint, u, {
          if (u.bits == 8 || u.bits == 16 || u.bits == 32 || u.bits == 64) PUTS("int"), USETYPEPRINTER(u16, u.bits), PUTS("_t");
          else PUTS("struct { unsigned char bytes["), USETYPEPRINTER(usize, Type_sizeof(t, e)), PUTS("]; }");
        }),
        (r_tag, a, out_typeName(a.t, _ctx)),
        (r_arr, a, { PUTS("struct { "), out_typeName(a.t, _ctx), PUTS(" data["), USETYPEPRINTER(u32, a.count), PUTS("]; }"); }),
        (r_bitfield, a, { PUTS("struct { unsigned char bytes["), USETYPEPRINTER(usize, Type_sizeof(t, e)), PUTS("]; }"); }),
        (default, assertMessage(false, "C output: unsupported type"))
    );
    PUTS(" "), out_typeName(idx, _ctx), PUTS(";\n");
  }
}
void makeTypes(superscope *ss, outputFunction out, printerfunction_context _ctx) {
  let e = ss_typeenv(ss);
  let memory = slice_alloc(ss->alloc, u8, e.tlist->len + 1);
  defer { slice_free(ss->alloc, memory); };
  let state = memory.ptr;
  memset(state, 0, memory.len);
  foreach (let i, range(0, e.tlist->len)) {
    let t = Type_at(e, i + 1);
    if (!t.ref_kind && !t.ptr_level && !out_noCType(t) && tu_is(r_struct, t.t)) {
      if (out_isUnion(e, t.t.r_struct)) PUTS("typedef union ");
      else PUTS("typedef struct ");
      out_typeName(i + 1, _ctx), PUTS(" "), out_typeName(i + 1, _ctx), PUTS(";\n");
    }
  }
  foreach (let i, range(0, e.tlist->len))
    out_typeDefinition(ss, i + 1, state, _ctx);
}

// Registers use byte storage: references and packed members can be unaligned.
// Operations themselves are emitted as C expressions and structured statements.
static void out_reg(scope *s, u32 r, printerfunction_context _ctx) {
  let owner = scope_owner(s, r);
  assertMessage(owner);
  PUTS("v"), USETYPEPRINTER(u32, owner->instance.fn), PUTS("_"), USETYPEPRINTER(u32, owner->instance.index);
  PUTS("_"), USETYPEPRINTER(u32, r);
}
static Type out_regType(superscope *ss, scope *s, u32 r) {
  return ss_type(ss, scope_find(ss, s, r)->t);
}
static out_storageKind out_regStorage(superscope *ss, scope *s, u32 r) {
  return out_storageKindOf(out_regType(ss, s, r), ss_typeenv(ss));
}
static bool out_hasStorage(superscope *ss, scope *s, u32 r) {
  return out_regStorage(ss, s, r) != out_st_none;
}
static Type out_valueType(superscope *ss, scope *s, u32 r);
static void out_storageTypeName(Type t, typeenv e, printerfunction_context _ctx);
static bool out_narrowInt(superscope *ss, scope *s, u32 r);
// A field access on a reference-typed member aliases the member slot: the
// register holds the slot's address and every use goes through it, so a later
// getref binds the stored reference itself.
static bool out_regIsAlias(superscope *ss, scope *s, u32 r) {
  let e = ss_typeenv(ss);
  foreach (let ip, range(0, msList_len(s->instructions))) {
    let inst = s->instructions[ip];
    if (!tu_is(ir_field, inst) || inst.ir_field.d != r) continue;
    let base = out_valueType(ss, s, inst.ir_field.l);
    let st = TypeTree_inner_struct(base.t, e);
    if (!st.start || inst.ir_field.i >= type_struct_len(e, st)) continue;
    if (type_struct_item(e, st, inst.ir_field.i)->t.ref_kind) return true;
  }
  return false;
}
// The address of a register's storage, as the cshl helpers and call arguments
// take it. An aliased register names the slot it stands for; every other
// register is a C object of its own, so its address is taken.
static void out_regAddr(superscope *ss, scope *s, u32 r, printerfunction_context _ctx) {
  if (out_regIsAlias(ss, s, r)) out_reg(s, r, _ctx);
  else {
    PUTS("(unsigned char *)&");
    out_reg(s, r, _ctx);
  }
}
// The reference a register holds: its own pointer for a reference, the pointer
// stored in the aliased slot for an alias, and a reference to the storage for a
// plain value.
static void out_refValue(superscope *ss, scope *s, u32 r, printerfunction_context _ctx) {
  if (out_regIsAlias(ss, s, r)) {
    PUTS("*(unsigned char **)");
    out_reg(s, r, _ctx);
  } else if (out_regType(ss, s, r).ref_kind) out_reg(s, r, _ctx);
  else out_regAddr(ss, s, r, _ctx);
}
// The data pointer of a register's place. A reference already holds it, and an
// alias holds the address of the slot it stands for; only a dref carries an
// extra bit offset and width.
static void out_placePtr(superscope *ss, scope *s, u32 r, printerfunction_context _ctx) {
  if (out_regType(ss, s, r).ref_kind == ref_dref) {
    PUTS("((double_offset *)"), out_regAddr(ss, s, r, _ctx), PUTS(")->ptr");
  } else out_refValue(ss, s, r, _ctx);
}
// A dref is the one place C has no type for, so it keeps the runtime pair.
static void out_place(superscope *ss, scope *s, u32 r, printerfunction_context _ctx) {
  if (out_regType(ss, s, r).ref_kind == ref_dref) PUTS("*(double_offset *)"), out_regAddr(ss, s, r, _ctx);
  else PUTS("(double_offset){ "), out_placePtr(ss, s, r, _ctx), PUTS(", 0, 0 }");
}
// Where a register's value is written: through a reference, or in the register.
static void out_store(superscope *ss, scope *s, u32 r, printerfunction_context _ctx) {
  if (out_regType(ss, s, r).ref_kind == ref_ref) {
    PUTS("*("), out_storageTypeName(out_valueType(ss, s, r), ss_typeenv(ss), _ctx), PUTS(" *)");
    out_refValue(ss, s, r, _ctx);
  } else out_reg(s, r, _ctx);
}
// The address a register's value occupies, for a byte-wise store.
static void out_storeAddr(superscope *ss, scope *s, u32 r, printerfunction_context _ctx) {
  if (out_regType(ss, s, r).ref_kind == ref_ref) out_refValue(ss, s, r, _ctx);
  else {
    PUTS("&");
    out_reg(s, r, _ctx);
  }
}
static Type out_valueType(superscope *ss, scope *s, u32 r) {
  let t = out_regType(ss, s, r);
  t.ref_kind = ref_none;
  return Type_stripTag(t, ss_typeenv(ss));
}
static u32 out_bits(Type t) {
  if (tu_is(r_bool, t.t)) return 1;
  if (tu_is(r_sint, t.t)) return t.t.r_sint.bits;
  if (tu_is(r_uint, t.t)) return t.t.r_uint.bits;
  assertMessage(false, "C output: expected an integer");
  return 0;
}
static void out_scalarType(Type t, printerfunction_context _ctx) {
  if (tu_is(r_bool, t.t)) PUTS("_Bool");
  else {
    bool sign = tu_is(r_sint, t.t);
    if (sign) PUTS("signed _BitInt(");
    else PUTS("unsigned _BitInt(");
    USETYPEPRINTER(u32, MAX$(out_bits(t), sign ? 2u : 1u)), PUTS(")");
  }
}
// An integer whose width C has no type for, kept as its own bytes.
static bool out_narrowInt(superscope *ss, scope *s, u32 r) {
  let t = out_valueType(ss, s, r);
  if (t.ptr_level) return false;
  let s2 = Type_stripTag(t, ss_typeenv(ss));
  if (tu_is(r_sint, s2.t)) return !out_isNatBits(s2.t.r_sint.bits);
  if (tu_is(r_uint, s2.t)) return !out_isNatBits(s2.t.r_uint.bits);
  return false;
}
// The C object backing a register. Native widths and pointers have natural C
// spellings; everything else is the typedef the type section already emitted.
static void out_storageTypeName(Type t, typeenv e, printerfunction_context _ctx) {
  if (t.ref_kind == ref_dref) PUTS("double_offset");
  else if (t.ref_kind || t.ptr_level) PUTS("unsigned char *");
  else {
    let s = Type_stripTag(t, e);
    if (tu_is(r_bool, s.t)) PUTS("_Bool");
    else if (tu_is(r_sint, s.t) && out_isNatBits(s.t.r_sint.bits)) PUTS("int"), USETYPEPRINTER(u16, s.t.r_sint.bits), PUTS("_t");
    else if (tu_is(r_uint, s.t) && out_isNatBits(s.t.r_uint.bits)) PUTS("uint"), USETYPEPRINTER(u16, s.t.r_uint.bits), PUTS("_t");
    else {
      let idx = out_typeIndexOf(e, s.t);
      assertMessage(idx, "C output: object type was not interned");
      out_typeName(idx, _ctx);
    }
  }
}
static void out_read(superscope *ss, scope *s, u32 r, printerfunction_context _ctx) {
  let rt = out_regType(ss, s, r);
  let t = out_valueType(ss, s, r);
  if (tu_is(c_int, t.t)) {
    PUTS("("), USETYPEPRINTER(bigint, scope_find(ss, s, r)->v.c_int), PUTS("wb)");
    return;
  }
  // A width C has no integer type for is kept as bytes, so it is read back as
  // the integer the IR calls it: drop the padding above the width, then let
  // the sign of the width fill it back in.
  if (out_narrowInt(ss, s, r)) {
    let e = ss_typeenv(ss);
    usize size = Type_sizeof(t, e);
    if (!size) return;
    // Reading the bytes back as the exact width keeps only the bits the width
    // names, and that same cast gives the sign its meaning.
    PUTS("({ unsigned _BitInt("), USETYPEPRINTER(usize, size * 8), PUTS(") _b = 0; memcpy(&_b, ");
    if (rt.ref_kind == ref_none) {
      PUTS("(unsigned char *)&");
      out_reg(s, r, _ctx);
    } else out_refValue(ss, s, r, _ctx);
    PUTS(", "), USETYPEPRINTER(usize, size), PUTS("); ("), out_scalarType(t, _ctx), PUTS(")_b; })");
    return;
  }
  // A plain register already is a C object of the value's type.
  if (rt.ref_kind == ref_none) {
    out_reg(s, r, _ctx);
    return;
  }
  // A reference reads the object it points at. A bool can expose a
  // noncanonical byte through a union member, so it reads through one byte.
  if (rt.ref_kind != ref_dref) {
    if (tu_is(r_bool, t.t)) PUTS("!!*(unsigned char *)");
    else PUTS("*("), out_storageTypeName(t, ss_typeenv(ss), _ctx), PUTS(" *)");
    out_refValue(ss, s, r, _ctx);
    return;
  }
  PUTS("({ ");
  // A union can expose a noncanonical byte through a bool member.
  if (tu_is(r_bool, t.t)) PUTS("unsigned char");
  else out_scalarType(t, _ctx);
  PUTS(" cshl_read_value = 0; cshl_read(");
  out_place(ss, s, r, _ctx), PUTS(", (unsigned char *)&cshl_read_value, sizeof(cshl_read_value), ");
  USETYPEPRINTER(usize, Type_sizeof(t, ss_typeenv(ss))), PUTS(", ");
  USETYPEPRINTER(u32, !t.ptr_level && tu_is(r_sint, t.t) ? out_bits(t) : 0), PUTS("); ");
  if (tu_is(r_bool, t.t)) PUTS("!!");
  PUTS("cshl_read_value; })");
}
static void out_copy(superscope *ss, scope *s, u32 dst, u32 src, printerfunction_context _ctx) {
  if (!out_hasStorage(ss, s, dst)) {
    *scope_find(ss, s, dst) = *scope_find(ss, s, src);
    return;
  }
  let e = ss_typeenv(ss);
  let t = out_valueType(ss, s, dst);
  usize size = Type_sizeof(t, e);
  if (!size) return;
  let dt = out_regType(ss, s, dst);
  // A dref destination is a bit field, the one thing C cannot name.
  if (dt.ref_kind == ref_dref) {
    PUTS("{ "), out_scalarType(t, _ctx), PUTS(" value = ("), out_scalarType(t, _ctx), PUTS(")"), out_read(ss, s, src, _ctx);
    PUTS("; cshl_write("), out_place(ss, s, dst, _ctx), PUTS(", (unsigned char *)&value, "), USETYPEPRINTER(usize, size), PUTS("); }\n");
    return;
  }
  // An integer whose width C has no type for keeps its bytes, so a move of one
  // is a move of those bytes.
  if (out_narrowInt(ss, s, dst)) {
    PUTS("{ "), out_scalarType(t, _ctx), PUTS(" value = "), out_read(ss, s, src, _ctx);
    PUTS("; memcpy("), out_storeAddr(ss, s, dst, _ctx), PUTS(", &value, "), USETYPEPRINTER(usize, size), PUTS("); }\n");
    return;
  }
  // Structures, arrays and bit fields are real C objects, so a move is an
  // assignment of whole objects, not a byte copy.
  if (out_storageKindOf(dt, e) == out_st_obj) {
    PUTS("*("), out_storageTypeName(t, e, _ctx), PUTS(" *)"), out_storeAddr(ss, s, dst, _ctx);
    PUTS(" = *("), out_storageTypeName(out_valueType(ss, s, src), e, _ctx), PUTS(" *)"), out_storeAddr(ss, s, src, _ctx);
    PUTS(";\n");
    return;
  }
  out_store(ss, s, dst, _ctx), PUTS(" = ");
  // Cast only when the value's C type actually changes.
  if (out_regType(ss, s, src).ref_kind == ref_none && Type_cmp(e, out_valueType(ss, s, src), t)) {
    PUTS("("), out_storageTypeName(t, e, _ctx), PUTS(")");
  }
  out_read(ss, s, src, _ctx), PUTS(";\n");
}
static void out_fnName(u32 fn, u32 spec, printerfunction_context _ctx) {
  PUTS("cshl_fn"), USETYPEPRINTER(u32, fn), PUTS("_"), USETYPEPRINTER(u32, spec);
}
static void out_scopeStorage(superscope *ss, scope *s, bool global, printerfunction_context _ctx) {
  let e = ss_typeenv(ss);
  foreach (let i, range(0, msList_len(s->values))) {
    let v = s->values[i];
    let t = ss_type(ss, v.t);
    let kind = out_storageKindOf(t, e);
    if (kind == out_st_none) continue;
    u32 reg = s->start + i + 1;
    usize size = Type_sizeof(t, e);
    if (global) PUTS("static ");
    out_storageTypeName(t, e, _ctx), PUTS(" "), out_reg(s, reg, _ctx);
    // A zero-sized type has nothing to initialize.
    if (size) {
      PUTS(" = ");
      if (kind == out_st_dref || kind == out_st_obj) PUTS("{0}");
      else if (v.v.value) {
        u64 raw = 0;
        memcpy(&raw, v.v.value, size);
        let st = Type_stripTag(t, e);
        if (tu_is(r_bool, st.t)) USETYPEPRINTER(u32, raw ? 1u : 0u);
        else if (tu_is(r_sint, st.t)) USETYPEPRINTER(i64, (i64)(raw << (64 - size * 8)) >> (64 - size * 8));
        else USETYPEPRINTER(u64, raw);
      } else PUTS("0");
    }
    PUTS(";\n");
  }
}
static const char *out_operator(u32 op) {
  return switch_exp(op_as_enum(op), (enop_mul, "*"), (enop_div, "/"), (enop_mod, "%"), (enop_plus, "+"), (enop_minus, "-"), (enop_shl, "<<"), (enop_shr, ">>"), (enop_oband, "&"), (enop_obor, "|"), (enop_bxor, "^"), (enop_oand, "&&"), (enop_oor, "||"), (enop_eq, "=="), (enop_neq, "!="), (enop_les, "<"), (enop_mor, ">"), (enop_leq, "<="), (enop_moq, ">="), (default, (assertMessage(false, "C output: unsupported operator"), "")));
}
static u32 out_operandBits(superscope *ss, scope *s, u32 r) {
  let t = out_valueType(ss, s, r);
  if (tu_is(c_int, t.t)) return MAX$(2u, (u32)(bigint_digits(scope_find(ss, s, r)->v.c_int) * sizeof(bigint_unit) * CHAR_BIT + 1));
  return out_bits(t) + 1;
}
static bigint out_comptimeOp(superscope *ss, scope *s, ir_rawop c) {
  let lhs = scope_find(ss, s, c.l)->v.c_int;
  let rhs = scope_find(ss, s, c.r)->v.c_int;
  let al = ss->alloc;
  assertMessage(tu_is(c_int, out_valueType(ss, s, c.l).t) && tu_is(c_int, out_valueType(ss, s, c.r).t), "C output: compile-time integer operation has runtime operands");
  return switch_exp(op_as_enum(c.o), (enop_mul, BInt.mul(al, lhs, rhs)), (enop_div, BInt.div(al, lhs, rhs).div), (enop_mod, BInt.div(al, lhs, rhs).mod), (enop_plus, BInt.add(al, lhs, rhs)), (enop_minus, BInt.sub(al, lhs, rhs)), (enop_shl, BInt.bit.shift(al, lhs, rhs)), (enop_shr, BInt.bit.shift(al, lhs, BInt.negate(al, rhs))), (enop_oband, BInt.bit.band(al, lhs, rhs)), (enop_obor, BInt.bit.bor(al, lhs, rhs)), (enop_bxor, BInt.sub(al, BInt.bit.bor(al, lhs, rhs), BInt.bit.band(al, lhs, rhs))), (default, (assertMessage(false, "C output: unsupported compile-time operation"), (bigint) nullptr)));
}
void makeScope(superscope *ss, scope *s, outputFunction out, printerfunction_context _ctx) {
  let e = ss_typeenv(ss);
  u32 runtime_depth = 0;
  foreach (let ip, range(0, msList_len(s->instructions))) {
    let inst = s->instructions[ip];
    if (tu_is(ir_ifs, inst) || tu_is(ir_loops, inst)) runtime_depth++;
    if (tu_is(ir_ife, inst) || tu_is(ir_loope, inst)) runtime_depth--;
    if (tu_is(ir_set, inst) && runtime_depth)
      assertMessage(out_hasStorage(ss, s, inst.ir_set.d), "C output: compile-time mutation under runtime control is unsupported");
    tu_match_void(
        inst,
        (ir_blocks, _, PUTS("{\n")),
        (ir_blocke, _, PUTS("}\n")),
        (ir_ifs, r, { PUTS("if ("), out_read(ss, s, r, _ctx), PUTS(") {\n"); }),
        (ir_ifd, _, PUTS("} else {\n")),
        (ir_ife, _, PUTS("}\n")),
        (ir_loops, _, PUTS("for (;;) {\n")),
        (ir_loope, _, PUTS("}\n")),
        (ir_break, _, PUTS("break;\n")),
        (ir_cont, _, PUTS("continue;\n")),
        (ir_set, c, out_copy(ss, s, c.d, c.l, _ctx)),
        (ir_farg, c, {
          out_reg(s, c.d, _ctx), PUTS(" = *("), out_storageTypeName(out_regType(ss, s, c.d), e, _ctx);
          PUTS(" *)args["), USETYPEPRINTER(u32, c.l), PUTS("];\n");
        }),
        (ir_getref, c, {
          if (out_regIsAlias(ss, s, c.d)) {
            PUTS("*(unsigned char **)");
            out_reg(s, c.d, _ctx);
          } else out_reg(s, c.d, _ctx);
          PUTS(" = ");
          out_refValue(ss, s, c.l, _ctx);
          PUTS(";\n");
        }),
        (ir_getptr, c, {
          out_reg(s, c.d, _ctx), PUTS(" = "), out_placePtr(ss, s, c.l, _ctx), PUTS(";\n");
        }),
        (ir_putptr, c, {
          out_reg(s, c.d, _ctx), PUTS(" = *(unsigned char **)"), out_placePtr(ss, s, c.l, _ctx), PUTS(";\n");
        }),
        (ir_field, c, {
          let t = out_valueType(ss, s, c.l);
          let st = TypeTree_inner_struct(t.t, e);
          let bf = TypeTree_inner_bitfield(t.t, e);
          if (bf.start) {
            u32 offset = 0;
            foreach (let j, range(0, c.i))
              offset += type_bitf_item(e, bf, j)->width;
            out_reg(s, c.d, _ctx), PUTS(" = (double_offset){ "), out_placePtr(ss, s, c.l, _ctx), PUTS(" + "), USETYPEPRINTER(u32, offset / 8);
            PUTS(", "), USETYPEPRINTER(u32, offset % 8), PUTS(", "), USETYPEPRINTER(u32, type_bitf_item(e, bf, c.i)->width);
            PUTS(" };\n");
          } else {
            let m = type_struct_item(e, st, c.i);
            out_reg(s, c.d, _ctx), PUTS(" = "), out_placePtr(ss, s, c.l, _ctx), PUTS(" + "), USETYPEPRINTER(u32, m->offset), PUTS(";\n");
          }
        }),
        (ir_array, c, {
          let a = TypeTree_inner_array(out_valueType(ss, s, c.l).t, e);
          out_reg(s, c.d, _ctx), PUTS(" = "), out_placePtr(ss, s, c.l, _ctx), PUTS(" + (size_t)"), out_read(ss, s, c.r, _ctx);
          PUTS(" * "), USETYPEPRINTER(usize, Type_sizeof(Type_at(e, a.t), e)), PUTS(";\n");
        }),
        (ir_cast, c, {
          let t = out_valueType(ss, s, c.d);
          assertMessage(!Type_is_comptime(t), "C output: runtime cast to comptime type");
          if (out_regType(ss, s, c.d).ref_kind == ref_dref) {
            PUTS("{ "), out_scalarType(t, _ctx), PUTS(" value = ("), out_scalarType(t, _ctx), PUTS(")"), out_read(ss, s, c.l, _ctx);
            PUTS("; cshl_write("), out_place(ss, s, c.d, _ctx), PUTS(", (unsigned char *)&value, "), USETYPEPRINTER(usize, Type_sizeof(t, e)), PUTS("); }\n");
          } else if (out_storageKindOf(out_regType(ss, s, c.d), e) == out_st_obj) {
            PUTS("{ "), out_scalarType(t, _ctx), PUTS(" value = ("), out_scalarType(t, _ctx), PUTS(")"), out_read(ss, s, c.l, _ctx);
            PUTS("; memcpy("), out_storeAddr(ss, s, c.d, _ctx), PUTS(", &value, "), USETYPEPRINTER(usize, Type_sizeof(t, e)), PUTS("); }\n");
          } else {
            out_store(ss, s, c.d, _ctx), PUTS(" = ("), out_scalarType(t, _ctx), PUTS(")"), out_read(ss, s, c.l, _ctx), PUTS(";\n");
          }
        }),
        (ir_rawop, c, {
          let t = out_valueType(ss, s, c.d);
          if (tu_is(c_int, t.t)) {
            scope_find(ss, s, c.d)->v.c_int = out_comptimeOp(ss, s, c);
            continue;
          }
          assertMessage(!Type_is_comptime(t), "C output: runtime operation with comptime result");
          u32 bits = out_operandBits(ss, s, c.l) + out_operandBits(ss, s, c.r) + 2;
          bool direct = out_regStorage(ss, s, c.d) == out_st_scalar;
          PUTS("{ signed _BitInt("), USETYPEPRINTER(u32, bits), PUTS(") lhs = "), out_read(ss, s, c.l, _ctx);
          PUTS(", rhs = "), out_read(ss, s, c.r, _ctx), PUTS("; ");
          if (direct) {
            out_reg(s, c.d, _ctx), PUTS(" = ("), out_scalarType(t, _ctx), PUTS(")(");
          } else {
            out_scalarType(t, _ctx), PUTS(" value = ");
          }
          let op = op_as_enum(c.o);
          if (op == enop_shl || op == enop_shr) {
            // Bigint shifts accept negative counts and never invoke C's oversized
            // or signed-left-shift undefined behavior.
            if (op == enop_shr) PUTS("((rhs = -rhs), ");
            PUTS("rhs < 0 ? (-rhs >= "), USETYPEPRINTER(u32, bits), PUTS(" ? (lhs < 0 ? -1 : 0) : lhs >> -rhs) : (rhs >= ");
            USETYPEPRINTER(u32, bits), PUTS(" ? 0 : (unsigned _BitInt("), USETYPEPRINTER(u32, bits), PUTS("))lhs << rhs)");
            if (op == enop_shr) PUTS(")");
          } else {
            PUTS("lhs "), _ctx.put(out_operator(c.o), _ctx.arb, strlen(out_operator(c.o)), 0), PUTS(" rhs");
          }
          if (direct) PUTS("); }\n");
          else if (out_regType(ss, s, c.d).ref_kind == ref_dref) {
            PUTS("; cshl_write("), out_place(ss, s, c.d, _ctx);
            PUTS(", (unsigned char *)&value, "), USETYPEPRINTER(usize, Type_sizeof(t, e)), PUTS("); }\n");
          } else {
            PUTS("; memcpy("), out_storeAddr(ss, s, c.d, _ctx), PUTS(", &value, "), USETYPEPRINTER(usize, Type_sizeof(t, e)), PUTS("); }\n");
          }
        }),
        (ir_call, c, {
          let fn = scope_find(ss, s, c.l)->v.c_fun;
          out_fnName(fn.fn, fn.spec, _ctx), PUTS("("), out_regAddr(ss, s, c.d, _ctx), PUTS(", (unsigned char *[]){");
          if (c.r == c.e) PUTS("0");
          foreach (let j, range(c.r, c.e)) {
            if (j != c.r) PUTS(", ");
            if (Type_is_comptime(out_regType(ss, s, j))) PUTS("0");
            else out_regAddr(ss, s, j, _ctx);
          }
          PUTS("});\n");
        }),
        (ir_ret, r, {
          let ret = ss->functions[s->instance.fn - 1].instances[s->instance.index].ret;
          usize size = Type_sizeof(ret, e);
          if (size && !Type_is_comptime(ret)) {
            // A reference result is the pointer itself, not what it points at.
            if (ret.ref_kind == ref_ref) {
              PUTS("*(unsigned char **)result = "), out_refValue(ss, s, r, _ctx), PUTS(";\n");
            } else if (ret.ref_kind == ref_dref) {
              PUTS("*(double_offset *)result = "), out_reg(s, r, _ctx), PUTS(";\n");
            } else {
              PUTS("*("), out_storageTypeName(ret, e, _ctx), PUTS(" *)result = "), out_read(ss, s, r, _ctx), PUTS(";\n");
            }
          }
          PUTS("return 0;\n");
        }),
        (default, assertMessage(false, "C output: unlowered instruction"))
    );
    // clang-format on
  }
}
void makeFuncs(superscope *ss, outputFunction out, printerfunction_context _ctx) {
  foreach (let i, range(0, msList_len(ss->functions))) {
    foreach (let j, range(0, msList_len(ss->functions[i].instances))) {
      if (Type_is_comptime(ss->functions[i].instances[j].ret)) continue;
      PUTS("static unsigned char "), out_fnName(i + 1, j, _ctx), PUTS("(unsigned char *result, unsigned char **args);\n");
    }
  }
  out_scopeStorage(ss, ss->outermost, true, _ctx);
  foreach (let i, range(0, msList_len(ss->functions))) {
    foreach (let j, range(0, msList_len(ss->functions[i].instances))) {
      let inst = ss->functions[i].instances[j];
      if (Type_is_comptime(inst.ret)) continue;
      PUTS("static unsigned char "), out_fnName(i + 1, j, _ctx), PUTS("(unsigned char *result, unsigned char **args) {\n");
      out_scopeStorage(ss, inst.spec, false, _ctx);
      makeScope(ss, inst.spec, out, _ctx);
      PUTS("}\n");
    }
  }
}
void makeC(superscope *ss, outputFunction out, printerfunction_context _ctx) {
  typedef struct {
    scope *s;
    slice(u8) values;
  } saved_scope;
  let saved = mList_init(ss->alloc, saved_scope, 8);
  defer {
    foreach (let item, span(mList_arr(saved), mList_len(saved))) {
      memcpy(item->s->values, item->values.ptr, item->values.len);
      slice_free(ss->alloc, item->values);
    }
    mList_deinit(saved);
  };
  let scopes = mList_init(ss->alloc, scope *, 8);
  defer { mList_deinit(scopes); };
  mList_push(scopes, ss->outermost);
  foreach (let i, range(0, msList_len(ss->functions)))
    foreach (let j, range(0, msList_len(ss->functions[i].instances)))
      mList_push(scopes, ss->functions[i].instances[j].spec);
  foreach (let sp, span(mList_arr(scopes), mList_len(scopes))) {
    let copy = slice_alloc(ss->alloc, u8, msList_len((*sp)->values) * sizeof(typed_value));
    memcpy(copy.ptr, (*sp)->values, copy.len);
    mList_push(saved, ((saved_scope){*sp, copy}));
  }
  out_runtime(_ctx);
  makeTypes(ss, out, _ctx);
  makeFuncs(ss, out, _ctx);
  PUTS("int main() {\n");
  makeScope(ss, ss->outermost, out, _ctx);
  PUTS("return 0;\n}\n");
}

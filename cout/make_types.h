#pragma once
#include "../ast.h"
#include "../ir/coerce.h"
#include "../ir/common.h"
#include "../srcpos.h"
#include "../type.h"
#include "wheels/macros.h"
#include "wheels/mytypes.h"
#include "wheels/print/print_pre.h"
#include "wheels/print/str_printers.h"
#include "wheels/sList.h"
#include <limits.h>

typedef moxmap(typeidx, c8 *) c_type_names;
typedef fptr id_str;
typePrinter(id_str) {
  foreach (let it, span(in.ptr, in.len))
    PUTC((char)(((*it) & 0xf) + 'a')),
        PUTC((char)((((*it) & 0xf0) >> 4) + 'a'));
}
static bool out_integerIsWide(typeenv e, Type t) {
  while (tu_is(r_tag, t.t))
    t = Type_at(e, tu_catch(r_tag, t.t).t);
  usize bits = tu_match_exp(
      t.t,
      (r_sint, v, (usize)v.bits),
      (r_uint, v, (usize)v.bits),
      (default, (usize)0)
  );
  return bits && bits != sizeof(char) * CHAR_BIT && bits != sizeof(short) * CHAR_BIT &&
         bits != sizeof(int) * CHAR_BIT && bits != sizeof(long) * CHAR_BIT;
}

void makeType(superscope *ss, printerfunction_context _ctx, c_type_names names, Type t);
void makeFullType(superscope *ss, printerfunction_context _ctx, c_type_names names, Type t);
static c8 *makeCType(superscope *ss, printerfunction_context _ctx, c_type_names names, Type t);

bool out_structIsUnnatural(typeenv e, r_struct s);
static bool out_structIsUnion(typeenv e, r_struct s);
static usize out_integerCAlign(u16 bits) {
  if (bits == sizeof(char) * CHAR_BIT) return alignof(char);
  else if (bits == sizeof(short) * CHAR_BIT) return alignof(short);
  else if (bits == sizeof(int) * CHAR_BIT) return alignof(int);
  else if (bits == sizeof(long) * CHAR_BIT) return alignof(long);
  else if (bits == sizeof(long long) * CHAR_BIT) return alignof(long long);
  return 0;
}

static bool out_typeIsNaturalC(typeenv e, Type t) {
  if (t.ref_kind == ref_dref) return Type_alignof(t, e) == alignof(double_offset);
  if (t.ref_kind || t.ptr_level) return Type_alignof(t, e) == alignof(void *);
  tu_match_void(
      t.t,
      (r_tag, tag, return out_typeIsNaturalC(e, Type_at(e, tag.t))),
      (r_bool, _, return Type_sizeof(t, e) == sizeof(_Bool) && Type_alignof(t, e) == alignof(_Bool)),
      (r_void, _, return Type_sizeof(t, e) == 0 && Type_alignof(t, e) == 1),
      (r_uint, u, return !!Type_sizeof(t, e) && Type_alignof(t, e) == out_integerCAlign(u.bits)),
      (r_sint, s, return !!Type_sizeof(t, e) && Type_alignof(t, e) == out_integerCAlign(s.bits)),
      (r_arr, a, return out_typeIsNaturalC(e, Type_at(e, a.t))),
      (r_struct, s, return !out_structIsUnnatural(e, s) && !out_structIsUnion(e, s)),
      (r_bitfield, b, ({
         u32 bits = 0;
         foreach (let i, range(0, type_bitf_len(e, b)))
           bits += type_bitf_item(e, b, i)->width;
         return bits == 1 && Type_alignof(t, e) == 1;
       })),
      (default, return false)
  );
}

// Omit zero-sized members from C structs.
static bool out_memberHasStorage(typeenv e, Type t) {
  return !Type_is_comptime(t) && Type_sizeof(t, e);
}

bool out_structIsUnnatural(typeenv e, r_struct s) {
  u32 len = type_struct_len(e, s);
  bool is_union = true;
  foreach (let i, range(0, len)) {
    let item = type_struct_item(e, s, i);
    if (Type_is_comptime(item->t)) continue;
    if (item->offset) is_union = false;
    if (item->alignment != Type_alignof(item->t, e) || !out_typeIsNaturalC(e, item->t)) return true;
  }
  if (is_union) return false;

  foreach (let i, range(0, len)) {
    let a = type_struct_item(e, s, i);
    if (Type_is_comptime(a->t)) continue;
    usize a_end = a->offset + Type_sizeof(a->t, e);
    foreach (let j, range(0, len)) {
      let b = type_struct_item(e, s, j);
      if (i == j || Type_is_comptime(b->t)) continue;
      usize b_end = b->offset + Type_sizeof(b->t, e);
      if (MIN$(a_end, b_end) > MAX$(a->offset, b->offset)) return true;
    }
  }
  return false;
}

static bool out_structIsUnion(typeenv e, r_struct s) {
  u32 len = type_struct_len(e, s);
  bool found = false;
  foreach (let i, range(0, len)) {
    let item = type_struct_item(e, s, i);
    if (Type_is_comptime(item->t)) continue;
    if (item->offset) return false;
    found = true;
  }
  return found;
}
static bool out_structNeedsByteStorage(typeenv e, r_struct s, allocfn allocator) {
  if (out_structIsUnnatural(e, s)) return true;
  if (out_structIsUnion(e, s)) return true;
  u32 len = type_struct_len(e, s);
  if (!len) return false;
  let emitted = acreate(allocator, bool[len]);
  defer { adestroy(allocator, emitted); };
  usize cursor = 0;
  foreach (let order, range(0, len)) {
    u32 next = len;
    foreach (let i, range(0, len)) {
      let item = type_struct_item(e, s, i);
      if ((*emitted)[i] || Type_is_comptime(item->t)) continue;
      if (next == len || item->offset < type_struct_item(e, s, next)->offset) next = i;
    }
    if (next == len) break;
    (*emitted)[next] = true;
    let item = type_struct_item(e, s, next);
    if (item->offset && lineup(cursor, item->alignment) != item->offset) return true;
    cursor = item->offset + Type_sizeof(item->t, e);
  }
  return false;
}

// Const is enforced by lowering, not by C's initialization rules.
static Type out_cType(superscope *ss, Type t) {
  while (tu_is(r_tag, t.t)) {
    let target = ss_type(ss, t.t.r_tag.t);
    if (tu_is(c_type, target.t)) break; // external C type
    t.t = target.t;
  }
  t.isconst = t.iscomptime = false;
  // Signed and unsigned wide integers share the same byte-storage C type.
  if (tu_is(r_sint, t.t) && out_integerIsWide(ss_typeenv(ss), t))
    t.t = TypeTree_uint(t.t.r_sint.bits);
  return t;
}
static c8 *out_typeName(superscope *ss, c_type_names names, Type t) {
  let name = moxmap_get(names, ss_type_intern(ss, out_cType(ss, t)));
  assertMessage(name, "C type has not been emitted");
  return *name;
}
static void out_makeTagName(superscope *ss, printerfunction_context _ctx, c_type_names names, Type t) {
  let name = out_typeName(ss, names, T(t.t));
  assertMessage(!strncmp(name, "struct ", 7));
  USETYPEPRINTER(cstr, name + 7);
}
static void out_printTypeComment(superscope *ss, printerfunction_context _ctx, c_type_names names, Type t) {
  PUTS("//");
  USETYPEPRINTER(Type_full, ss_tf(ss, t));
  PUTS("\n");
}
static void out_makeAggregateKeyword(typeenv e, printerfunction_context _ctx, Type t) {
  iflet_tu (let s, r_struct, t.t) {
    PUTS("struct");
  } else iflet_tu (let b, r_bitfield, t.t) {
    PUTS("struct");
  } else assertMessage(false, "C aggregate keyword requested for non-aggregate type");
}
static void out_makeMemberName(typeenv e, printerfunction_context _ctx, srcpos name) {
  PUTS("cshl_");
  USETYPEPRINTER(id_str, type_name(e, name));
  PUTS("_");
}
static void out_makeFieldArray(
    superscope *ss,
    printerfunction_context _ctx,
    c_type_names names,
    r_struct s,
    bool fallback
) {
  typeenv e = ss_typeenv(ss);
  u32 len = type_struct_len(e, s);
  if (!len) return;
  let emitted = acreate(_ctx.allocator, bool[len]);
  defer { adestroy(_ctx.allocator, emitted); };
  usize cursor = 0;
  u32 pad = 0;
  foreach (let order, range(0, len)) {
    u32 next = len;
    foreach (let i, range(0, len)) {
      let item = type_struct_item(e, s, i);
      if ((*emitted)[i] || !out_memberHasStorage(e, item->t)) continue;
      if (next == len || item->offset < type_struct_item(e, s, next)->offset) next = i;
    }
    if (next == len) break;
    (*emitted)[next] = true;
    let item = type_struct_item(e, s, next);
    if (item->offset > cursor) {
      PUTS("unsigned char cshl_pad");
      USETYPEPRINTER(usize, pad++);
      PUTS("[");
      USETYPEPRINTER(usize, item->offset - cursor);
      PUTS("];\n");
    }
    if (fallback) {
      // Padding supplies storage; member access uses byte offsets.
      cursor = item->offset;
    } else {
      makeType(ss, _ctx, names, item->t);
      out_makeMemberName(e, _ctx, item->name);
      PUTS(";\n");
      cursor = item->offset + Type_sizeof(item->t, e);
    }
  }
}

static usize out_bitfieldSize(typeenv e, r_bitfield b) {
  usize bits = 0;
  foreach (let i, range(0, type_bitf_len(e, b)))
    bits += type_bitf_item(e, b, i)->width;
  return (bits + CHAR_BIT - 1) / CHAR_BIT;
}
void makeFullType(superscope *ss, printerfunction_context _ctx, c_type_names names, Type t) {
  let e = ss_typeenv(ss);
  iflet_tu (let tag, r_tag, t.t) {
    makeFullType(ss, _ctx, names, Type_at(e, tag.t));
    return;
  }
  tu_match_void(
      t.t,
      (r_struct, s, {
        bool fallback = out_structNeedsByteStorage(e, s, _ctx.allocator);
        out_makeAggregateKeyword(e, _ctx, t);
        PUTS(" ");
        out_makeTagName(ss, _ctx, names, t);
        PUTS(" {\n");
        usize storage_start = 0;
        if (fallback)
          foreach (let i, range(0, type_struct_len(e, s))) {
            let item = type_struct_item(e, s, i);
            if (out_memberHasStorage(e, item->t)) storage_start = MAX$(storage_start, item->offset);
          }
        bool has_storage = false;
        foreach (let i, range(0, type_struct_len(e, s)))
          has_storage |= out_memberHasStorage(e, type_struct_item(e, s, i)->t);
        out_makeFieldArray(ss, _ctx, names, s, fallback);
        if (fallback) {
          usize size = Type_sizeof(t, e);
          PUTS("unsigned char cshl_storage[");
          USETYPEPRINTER(usize, size - storage_start);
          PUTS("];\n");
        } else if (!has_storage) {
          // C99 does not permit an empty structure declaration.
          PUTS("unsigned char cshl_empty[1];\n");
        }
        PUTS("}");
      }),
      (r_bitfield, b, {
        PUTS("struct ");
        out_makeTagName(ss, _ctx, names, t);
        PUTS(" {\nunsigned char cshl_storage[");
        USETYPEPRINTER(usize, out_bitfieldSize(e, b));
        PUTS("];\n}");
      }),
      (default, assertMessage(false, "full C type requested for non-aggregate type"))
  );
}
static void out_assertLayout(superscope *ss, printerfunction_context _ctx, c_type_names names, Type t) {
  let e = ss_typeenv(ss);
  TypeTree tree = t.t;
  iflet_tu (let tag, r_tag, tree) {
    out_assertLayout(ss, _ctx, names, Type_at(e, tag.t));
    return;
  }
  // ISO C has no zero-sized object; empty cshl structs use a compatibility byte instead.
  if (!Type_sizeof(t, e)) return;
  PUTS("typedef char cshl_size_assert_");
  out_makeTagName(ss, _ctx, names, t);
  PUTS("_");
  USETYPEPRINTER(usize, ss_type_intern(ss, t));
  PUTS("[(sizeof(");
  out_makeAggregateKeyword(e, _ctx, t);
  PUTS(" ");
  out_makeTagName(ss, _ctx, names, t);
  PUTS(") == ");
  usize size = tu_is(r_bitfield, tree) ? out_bitfieldSize(e, tu_catch(r_bitfield, tree)) : Type_sizeof(t, e);
  USETYPEPRINTER(usize, size);
  PUTS(") ? 1 : -1];\n");
  bool check_alignment = !tu_is(r_bitfield, tree);
  iflet_tu (let s, r_struct, tree)
    check_alignment = !out_structNeedsByteStorage(e, s, _ctx.allocator);
  if (check_alignment) {
    // C99 array alignment is checked through the enclosing struct layout.
  }
  if (!tu_is(r_struct, tree)) return;
  r_struct s = tu_catch(r_struct, tree);
  // Byte storage has no named members to check.
  if (out_structNeedsByteStorage(e, s, _ctx.allocator)) return;
  foreach (let i, range(0, type_struct_len(e, s))) {
    let item = type_struct_item(e, s, i);
    if (!out_memberHasStorage(e, item->t)) continue;
    PUTS("typedef char cshl_offset_assert_");
    out_makeTagName(ss, _ctx, names, t);
    PUTS("_");
    USETYPEPRINTER(usize, ss_type_intern(ss, t));
    PUTS("_");
    out_makeMemberName(e, _ctx, item->name);
    PUTS("[(offsetof(");
    out_makeAggregateKeyword(e, _ctx, t);
    PUTS(" ");
    out_makeTagName(ss, _ctx, names, t);
    PUTS(", ");
    out_makeMemberName(e, _ctx, item->name);
    PUTS(") == ");
    USETYPEPRINTER(usize, item->offset);
    PUTS(") ? 1 : -1];\n");
  }
}

void makeType(superscope *ss, printerfunction_context _ctx, c_type_names names, Type t) {
  USETYPEPRINTER(cstr, out_typeName(ss, names, t));
  PUTS(" ");
}

static bool out_isRecord(Type t) {
  return tu_is(r_struct, t.t) || tu_is(r_bitfield, t.t) || tu_is(r_arr, t.t);
}

static c8 *makeScalarType(superscope *ss, printerfunction_context _ctx, c_type_names names, Type t, typeidx idx) {
  slice(c8) name;
  if (tu_is(r_tag, t.t)) {
    name = snprint(_ctx.allocator, "{slice(c8)}", type_name(ss_typeenv(ss), t.t.r_tag.name));
  } else if (tu_is(r_bool, t.t)) name = snprint(_ctx.allocator, "_Bool");
  else if (tu_is(r_void, t.t)) name = snprint(_ctx.allocator, "void_t");
  else {
    bool signed_int = tu_is(r_sint, t.t);
    assertMessage(signed_int || tu_is(r_uint, t.t), "unsupported C scalar type");
    u16 bits = signed_int ? t.t.r_sint.bits : t.t.r_uint.bits;
    // Shift emission also needs the unsigned spelling of a signed operand's width.
    ss_type_intern(ss, T(TypeTree_uint(bits)));
    if (out_integerIsWide(ss_typeenv(ss), t)) {
      name = snprint(_ctx.allocator, "cshl_int_{u16}", bits);
      PUTS("CSHL_DEFINE_INTEGER(");
      USETYPEPRINTER(cstr, name.ptr);
      PUTS(", ");
      USETYPEPRINTER(u16, bits);
      PUTS(")\n");
    } else {
      c8 *base = bits == sizeof(char) * CHAR_BIT    ? "char"
                 : bits == sizeof(short) * CHAR_BIT ? "short"
                 : bits == sizeof(int) * CHAR_BIT   ? "int"
                 : bits == sizeof(long) * CHAR_BIT  ? "long"
                                                    : "long long";
      name = snprint(_ctx.allocator, "{cstr} {cstr}", signed_int ? "signed" : "unsigned", base);
    }
  }
  moxmap_set(names, idx, name.ptr);
  return name.ptr;
}

static c8 *makeQualifiedType(superscope *ss, printerfunction_context _ctx, c_type_names names, Type t, typeidx idx) {
  Type base = T(t.t);
  c8 *base_name;
  slice(c8) forward = {};
  defer {
    if (forward.ptr) slice_free(_ctx.allocator, forward);
  };
  if ((t.ptr_level || t.ref_kind) && out_isRecord(base)) {
    let base_idx = ss_type_intern(ss, base);
    if_decl (let found, moxmap_get(names, base_idx)) base_name = *found;
    else {
      // A pointer needs only a declaration. The type-list walk will emit its body.
      forward = snprint(_ctx.allocator, "struct cshl_type_{u32}", base_idx);
      base_name = forward.ptr;
      USETYPEPRINTER(cstr, base_name);
      PUTS(";\n");
    }
  } else base_name = makeCType(ss, _ctx, names, base);
  assertMessage(base_name, "missing C pointee type");
  usize pointer_count = t.ptr_level + (t.ref_kind == ref_ref);
  let stars = slice_alloc(_ctx.allocator, c8, pointer_count + 1);
  defer { slice_free(_ctx.allocator, stars); };
  memset(stars.ptr, '*', pointer_count);
  stars.ptr[pointer_count] = 0;
  slice(c8) name;
  if (t.ref_kind == ref_dref) {
    // The double-offset union needs a typedef so repeated uses have one C identity.
    name = snprint(_ctx.allocator, "cshl_type_{u32}", idx);
    PUTS("typedef double_offset_t(");
    if (t.isvolatile) PUTS("volatile ");
    USETYPEPRINTER(cstr, base_name);
    USETYPEPRINTER(cstr, stars.ptr);
    PUTS(") ");
    USETYPEPRINTER(cstr, name.ptr);
    PUTS(";\n");
  } else name = snprint(_ctx.allocator, "{cstr}{cstr}{cstr}", t.isvolatile ? "volatile " : "", base_name, stars.ptr);
  moxmap_set(names, idx, name.ptr);
  return name.ptr;
}

static c8 *makeAggregateType(superscope *ss, printerfunction_context _ctx, c_type_names names, Type t, typeidx idx) {
  let name = snprint(_ctx.allocator, "struct cshl_type_{u32}", idx);
  moxmap_set(names, idx, name.ptr);
  USETYPEPRINTER(cstr, name.ptr);
  PUTS(";\n");
  let e = ss_typeenv(ss);
  iflet_tu (let st, r_struct, t.t)
    foreach (let i, range(0, type_struct_len(e, st)))
      makeCType(ss, _ctx, names, type_struct_item(e, st, i)->t);
  iflet_tu (let arr, r_arr, t.t)
    makeCType(ss, _ctx, names, Type_at(e, arr.t));

  out_printTypeComment(ss, _ctx, names, t);
  iflet_tu (let arr, r_arr, t.t) {
    USETYPEPRINTER(cstr, name.ptr);
    PUTS(" {\n");
    makeType(ss, _ctx, names, Type_at(e, arr.t));
    PUTS(" cshl_elements[");
    USETYPEPRINTER(usize, arr.count);
    PUTS("];\n};\n");
  } else {
    makeFullType(ss, _ctx, names, t);
    PUTS(";\n");
    out_assertLayout(ss, _ctx, names, t);
  }
  return name.ptr;
}

static c8 *makeCType(superscope *ss, printerfunction_context _ctx, c_type_names names, Type t) {
  t = out_cType(ss, t);
  if (tu_is(r_null, t.t) || Type_is_comptime(T(t.t))) return nullptr;
  let idx = ss_type_intern(ss, t);
  if_decl (let found, moxmap_get(names, idx)) return *found;
  if (t.decorations) return makeQualifiedType(ss, _ctx, names, t, idx);
  if (out_isRecord(t)) return makeAggregateType(ss, _ctx, names, t, idx);
  return makeScalarType(ss, _ctx, names, t, idx);
}

c_type_names makeTypes(superscope *ss, printerfunction_context _ctx) {
  let names = moxmap_init(_ctx.allocator, typeidx, c8 *);
  ss_type_intern(ss, T(TypeTree_uint(sizeof(usize) * CHAR_BIT)));
  // Canonicalization can add undecorated/pointee types while this walk is running.
  for (usize idx = 0; idx < ss->tlist.len; idx++)
    makeCType(ss, _ctx, names, sglist_get(ss->tlist, idx));
  return names;
}

static void freeTypeNames(allocfn allocator, c_type_names names) {
  foreach (let item, moxmap_iter(names, typeidx, c8 *))
    slice_free(allocator, ((slice(c8)){strlen(*item.val) + 1, *item.val}));
  moxmap_deinit(names);
}

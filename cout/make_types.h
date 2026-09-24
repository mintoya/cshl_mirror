#pragma once
#include "../ast.h"
#include "../ir/common.h"
#include "../srcpos.h"
#include "../type.h"
#include "wheels/macros.h"
#include "wheels/mytypes.h"
#include "wheels/print/print_pre.h"
#include "wheels/print/str_printers.h"
#include "wheels/sList.h"
#include <limits.h>

// makeTypes
typedef fptr id_str;
typePrinter(id_str) {
  foreach (let it, span(in.ptr, in.len))
    PUTC((char)(((*it) & 0xf) + 'a')),
        PUTC((char)((((*it) & 0xf0) >> 4) + 'a'));
}
void makeInt(printerfunction_context _ctx, usize bits) {
  switch_tuple(
      bits,
      (sizeof(char) * CHAR_BIT, PUTS("char")),
      (sizeof(short) * CHAR_BIT, PUTS("short")),
      (sizeof(int) * CHAR_BIT, PUTS("int")),
      (sizeof(long) * CHAR_BIT, PUTS("long")),
      (default, {
        if (true) {
          PUTS("_BitInt(");
          USETYPEPRINTER(usize, bits);
          PUTS(")");
        } else {
          if /**/ (bits <= sizeof(char) * CHAR_BIT) PUTS("");
          else if (bits <= sizeof(short) * CHAR_BIT) PUTS("");
          else if (bits <= sizeof(int) * CHAR_BIT) PUTS("");
          else if (bits <= sizeof(long) * CHAR_BIT) PUTS("");
          else assertMessage(false, "int too big right now");
        }
      })
  );
}
void makeType(superscope *ss, printerfunction_context _ctx, Type t);
void makeTypeTree(superscope *ss, printerfunction_context _ctx, TypeTree t);
void makeFullType(superscope *ss, printerfunction_context _ctx, Type t);

bool out_structIsUnnatural(typeenv e, r_struct s);
static bool out_structIsUnion(typeenv e, r_struct s);
static usize out_integerCAlign(u16 bits) {
  return switch_exp(
      bits,
      (sizeof(char) * CHAR_BIT, alignof(char)),
      (sizeof(short) * CHAR_BIT, alignof(short)),
      (sizeof(int) * CHAR_BIT, alignof(int)),
      (sizeof(long) * CHAR_BIT, alignof(long)),
      (default, 0),
  );
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

static void out_makeTagName(superscope *ss, printerfunction_context _ctx, Type t) {
  let name = snprint(ss->alloc, "{Type_full}", ss_typeF(ss, t));
  defer { slice_free(ss->alloc, name); };
  USETYPEPRINTER(id_str, fp(P$(name, ($.len--, $))));
}
static void out_printTypeComment(superscope *ss, printerfunction_context _ctx, Type t) {
  PUTS("//");
  USETYPEPRINTER(Type_full, ss_typeF(ss, t));
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
      if ((*emitted)[i] || Type_is_comptime(item->t)) continue;
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
      PUTS("unsigned char ");
      out_makeMemberName(e, _ctx, item->name);
      PUTS("[0];\n");
      cursor = item->offset;
    } else {
      makeType(ss, _ctx, item->t);
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
void makeFullType(superscope *ss, printerfunction_context _ctx, Type t) {
  let e = ss_typeenv(ss);
  iflet_tu (let tag, r_tag, t.t) {
    makeFullType(ss, _ctx, Type_at(e, tag.t));
    return;
  }
  tu_match_void(
      t.t,
      (r_struct, s, {
        bool fallback = out_structNeedsByteStorage(e, s, _ctx.allocator);
        out_makeAggregateKeyword(e, _ctx, t);
        PUTS(" ");
        out_makeTagName(ss, _ctx, t);
        PUTS(" {\n");
        usize storage_start = 0;
        if (fallback)
          foreach (let i, range(0, type_struct_len(e, s))) {
            let item = type_struct_item(e, s, i);
            if (!Type_is_comptime(item->t)) storage_start = MAX$(storage_start, item->offset);
          }
        out_makeFieldArray(ss, _ctx, s, fallback);
        if (fallback) {
          usize size = Type_sizeof(t, e);
          PUTS("unsigned char cshl_storage[");
          USETYPEPRINTER(usize, size - storage_start);
          PUTS("];\n");
        }
        PUTS("}");
      }),
      (r_bitfield, b, {
        PUTS("struct ");
        out_makeTagName(ss, _ctx, t);
        PUTS(" {\nunsigned char cshl_storage[");
        USETYPEPRINTER(usize, out_bitfieldSize(e, b));
        PUTS("];\n}");
      }),
      (default, assertMessage(false, "full C type requested for non-aggregate type"))
  );
}
static void out_assertLayout(superscope *ss, printerfunction_context _ctx, Type t) {
  let e = ss_typeenv(ss);
  TypeTree tree = t.t;
  iflet_tu (let tag, r_tag, tree) {
    out_assertLayout(ss, _ctx, Type_at(e, tag.t));
    return;
  }
  PUTS("static_assert(sizeof(");
  out_makeAggregateKeyword(e, _ctx, t);
  PUTS(" ");
  out_makeTagName(ss, _ctx, t);
  PUTS(") == ");
  usize size = tu_is(r_bitfield, tree) ? out_bitfieldSize(e, tu_catch(r_bitfield, tree)) : Type_sizeof(t, e);
  USETYPEPRINTER(usize, size);
  PUTS(" , \"cshl alignment insurance\");\n");
  bool check_alignment = !tu_is(r_bitfield, tree);
  iflet_tu (let s, r_struct, tree)
    check_alignment = !out_structNeedsByteStorage(e, s, _ctx.allocator);
  if (check_alignment) {
    PUTS("static_assert(_Alignof(");
    out_makeAggregateKeyword(e, _ctx, t);
    PUTS(" ");
    out_makeTagName(ss, _ctx, t);
    PUTS(") == ");
    USETYPEPRINTER(usize, Type_alignof(t, e));
    PUTS(",\"cshl alignment insurance\");\n");
  }
  if (!tu_is(r_struct, tree)) return;
  r_struct s = tu_catch(r_struct, tree);
  foreach (let i, range(0, type_struct_len(e, s))) {
    let item = type_struct_item(e, s, i);
    if (Type_is_comptime(item->t)) continue;
    PUTS("static_assert(offsetof(");
    out_makeAggregateKeyword(e, _ctx, t);
    PUTS(" ");
    out_makeTagName(ss, _ctx, t);
    PUTS(", ");
    out_makeMemberName(e, _ctx, item->name);
    PUTS(") == ");
    USETYPEPRINTER(usize, item->offset);
    PUTS(");\n");
  }
}
void makeType(superscope *ss, printerfunction_context _ctx, Type t) {
  assertMessage(!Type_is_comptime(t)); // should have been handled by now
  if (t.ref_kind == ref_dref)
    PUTS("double_offset_t(");
  defer {
    if (t.ref_kind == ref_dref)
      PUTS(")");
  }
  if (t.isconst)
    PUTS("const ");
  if (t.isvolatile)
    PUTS("volatile ");
  makeTypeTree(ss, _ctx, t.t);
  foreach (let _, range(0, t.ptr_level + (t.ref_kind == ref_ref)))
    PUTS("*");
}
void makeTypeTree(superscope *ss, printerfunction_context _ctx, TypeTree t) {
  assertMessage(!Type_is_comptime(T(t))); // should have been handled by now
  tu_match_void(
      t,
      (c_int, _, ),
      (c_any, _, ),
      (c_ast, _, ),
      (c_fun, _, ),
      (r_null, _, ),
      (c_type, _, ),
      (builtin, _, ),
      (c_typeinfo, _, ),
      (c_inlineast, _, ),
      (r_bool, _, PUTS("_Bool")),
      (r_void, _, PUTS("void_t")),
      (r_sint, s, PUTS("signed   "), makeInt(_ctx, s.bits)),
      (r_uint, u, PUTS("unsigned "), makeInt(_ctx, u.bits)),
      (r_tag, it, USETYPEPRINTER(id_str, type_name(ss_typeenv(ss), it.name))),
      (r_arr, a, {
        PUTS("struct {\n");
        makeType(ss, _ctx, sglist_get(ss->tlist, a.t - 1));
        PUTS("_[");
        USETYPEPRINTER(usize, a.count);
        PUTS("];");
        PUTS("}\n");
      }),
      (r_struct, sn, out_makeAggregateKeyword(ss_typeenv(ss), _ctx, T(tu_of(r_struct, sn))), PUTS(" "), out_makeTagName(ss, _ctx, T(tu_of(r_struct, sn))), PUTS(" ")),
      (r_bitfield, sn, PUTS("struct "), out_makeTagName(ss, _ctx, T(tu_of(r_bitfield, sn))), PUTS(" ")),
  );
  PUTS(" ");
}
static bool out_isAggregate(Type t) {
  return !t.ptr_level && !t.ref_kind &&
         (tu_is(r_struct, t.t) || tu_is(r_bitfield, t.t));
}
void makeTypes(superscope *ss, outputFunction _put, printerfunction_context _ctx) {
  let e = ss_typeenv(ss);

  foreach (let idx, range(0, ss->tlist.len)) {
    let t = sglist_get(ss->tlist, idx);
    if (tu_is(r_null, t.t)) continue;
    if (out_isAggregate(t)) {
      out_makeAggregateKeyword(e, _ctx, t);
      PUTS(" ");
      out_makeTagName(ss, _ctx, t);
      PUTS(";\n");
    } else iflet_tu (let tag, r_tag, t.t) {
      let target = Type_at(e, tag.t);
      if (out_isAggregate(target)) {
        out_printTypeComment(ss, _ctx, t);
        PUTS("typedef ");
        out_makeAggregateKeyword(e, _ctx, target);
        PUTS(" ");
        out_makeTagName(ss, _ctx, target);
        PUTS(" ");
        USETYPEPRINTER(id_str, type_name(e, tag.name));
        PUTS(";\n");
      }
    }
  }

  foreach (let idx, range(0, ss->tlist.len)) {
    let t = sglist_get(ss->tlist, idx);
    if (tu_is(r_null, t.t) || Type_is_comptime(t) || out_isAggregate(t) || tu_is(r_arr, t.t)) continue;
    iflet_tu (let tag, r_tag, t.t)
      if (out_isAggregate(Type_at(e, tag.t))) continue;
    out_printTypeComment(ss, _ctx, t);
    PUTS("typedef ");
    makeType(ss, _ctx, t);
    PUTS(" cshlT_");
    USETYPEPRINTER(usize, idx + 1);
    PUTS(";\n");
  }

  foreach (let idx, range(0, ss->tlist.len)) {
    let t = sglist_get(ss->tlist, idx);
    if (tu_is(r_null, t.t) || Type_is_comptime(t) || !tu_is(r_arr, t.t)) continue;
    out_printTypeComment(ss, _ctx, t);
    PUTS("typedef ");
    makeType(ss, _ctx, t);
    PUTS(" cshlT_");
    USETYPEPRINTER(usize, idx + 1);
    PUTS(";\n");
  }

  foreach (let idx, range(0, ss->tlist.len)) {
    let t = sglist_get(ss->tlist, idx);
    if (tu_is(r_null, t.t) || Type_is_comptime(t) || !out_isAggregate(t)) continue;
    out_printTypeComment(ss, _ctx, t);
    PUTS("typedef ");
    makeFullType(ss, _ctx, t);
    PUTS(" cshlT_");
    USETYPEPRINTER(usize, idx + 1);
    PUTS(";\n");
    out_assertLayout(ss, _ctx, t);
  }
}

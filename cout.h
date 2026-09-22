#pragma once
#include "ir/common.h"
#include "srcpos.h"
#include "type.h"
#include "wheels/macros.h"
//
//
//
//
// struct thingtest {
//   char _pad0[2];
//   char _int[0];
//   char _pad1[1];
//   char _char[0];
//   char _pad2[3];
// };
//
//
void out_strangeType(superscope *ss, r_struct t, outputFunction out, printerfunction_context _ctx) {
  let e = ss_typeenv(ss);
  let size = TypeTree_sizeof((TypeTree)tu_of(r_struct, t), e);
  let n = type_struct_len(e, t);
  let order = mList_init(PRINTARGS_ALLOCATOR(), u32, n ? n : 1);
  defer { mList_deinit(order); };
  foreach (let i, range(0, n))
    if (!Type_is_comptime(type_struct_item(e, t, i)->t)) mList_push(order, i);
  let ordered = mList_arr(order);
  let count = (u32)mList_len(order);
  for (u32 i = 1; i < count; i++) {
    let moving = ordered[i];
    let offset = type_struct_item(e, t, moving)->offset;
    u32 j = i;
    while (j && type_struct_item(e, t, ordered[j - 1])->offset > offset) {
      ordered[j] = ordered[j - 1];
      j--;
    }
    ordered[j] = moving;
  }
  usize curr = 0;
  u32 pad = 0;
  PUTS("{\n");
  foreach (let k, range(0, count)) {
    let item = type_struct_item(e, t, ordered[k]);
    if (item->offset > curr)
      PUTS("char __pad"), USETYPEPRINTER(u32, pad++), PUTS("["), USETYPEPRINTER(usize, (usize)item->offset - curr), PUTS("];\n");
    PUTS("char "), USENAMEDPRINTER("slice(c8)", type_name(e, item->name)), PUTS("[0];\n");
    curr = item->offset;
  }
  PUTS("  char __pad"), USETYPEPRINTER(u32, pad++);
  PUTS("["), USETYPEPRINTER(usize, size - curr), PUTS("];\n");
  PUTS("};\n");
}
void makeTypes(superscope *ss, outputFunction out, printerfunction_context _ctx);
void makeFuncs(superscope *ss, outputFunction out, printerfunction_context _ctx);
void makeScope(superscope *ss, scope *s, outputFunction out, printerfunction_context _ctx);

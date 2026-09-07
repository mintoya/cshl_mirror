#pragma once
#include "wheels/macros.h"
#include "wheels/mytypes.h"
#include "wheels/print.h"
#include "wheels/print/print_pre.h"
#include <stdio.h>

sglist(struct {
  FILE *fileptr;
  const char *filename;
  const char *filecontent;
}) files; // index into this
sglist(struct {
  u32 fileIndex;
  u32 inner_offset;
  u32 length;
}) all_positions;
typedef u32 srcpos; // one based indexing for null obv
typePrinter(srcpos) {
  if (!in) return PUTS("(?)");
  else if (in >= all_positions.len)
    return PUTS("(index oob!)");
  let pos = sglist_get(all_positions, in - 1);
  if (pos.fileIndex >= files.len)
    return PUTS("(file oob!)");
  let file = sglist_get(files, pos.fileIndex);

  fseeko(file.fileptr, 0, SEEK_SET);
  u32 line = 1;
  u32 col = 1;
  for (u32 i = 0; i < pos.inner_offset; i++) {
    let c = fgetc(file.fileptr);
    if (c == EOF) break;
    if (c == '\n') {
      line++;
      col = 1;
    } else col++;
  }
  PUTS("file"), USENAMEDPRINTER("cstr", file.filename), PUTS(",");
  PUTS("line"), USETYPEPRINTER(u32, line), PUTS(",");
  PUTS("column"), USETYPEPRINTER(u32, col), PUTS(",");
}

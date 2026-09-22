#pragma once
#include "wheels/macros.h"
#include "wheels/mytypes.h"
#include "wheels/print.h"
#include "wheels/print/print_pre.h"
#include <stdio.h>

typedef struct {
  const char *filename;
  fptr filecontent;
  FILE *fileptr;
} fileItem;
typedef sglist(fileItem) filelist;
typedef struct {
  u32 fidx;
  u32 offset;
  u32 length;
} posItem;
typedef sglist(posItem) poslist;
typedef u32 srcpos; // one based indexing for null
typedef struct {
  srcpos pidx;
  poslist plist;
  filelist flist;
} srcpos_full;

static inline fptr fp_from_srcpos(srcpos_full pos) {
  if (!pos.pidx || pos.pidx > pos.plist.len) return nullFptr;
  let p = sglist_get(pos.plist, pos.pidx - 1);
  if (!p.fidx || p.fidx > pos.flist.len) return nullFptr;
  let content = sglist_get(pos.flist, p.fidx - 1).filecontent;
  if (!content.ptr || p.offset > content.len || p.length > content.len - p.offset)
    return nullFptr;
  return (fptr){p.length, content.ptr + p.offset};
}
typePrinter(srcpos_full) {
  if (!in.pidx || in.pidx > in.plist.len) return PUTS("(pos!!)");
  let pos = sglist_get(in.plist, in.pidx - 1);
  if (!pos.fidx || pos.fidx > in.flist.len) return PUTS("(file!!)");
  let file = sglist_get(in.flist, pos.fidx - 1);
  u32 line = 1;
  u32 col = 1;
  if (file.filecontent.ptr) {
    u32 limit = pos.offset < file.filecontent.len ? pos.offset : file.filecontent.len;
    for (u32 i = 0; i < limit; i++) {
      let c = file.filecontent.ptr[i];
      if (c == '\n') line++, col = 1;
      else col++;
    }
  } else if (file.fileptr) {
    fseek(file.fileptr, 0, SEEK_SET);
    for (u32 i = 0; i < pos.offset; i++) {
      let c = fgetc(file.fileptr);
      if (c == EOF) break;
      if (c == '\n') line++, col = 1;
      else col++;
    }
  }
  PUTS("(file "), USENAMEDPRINTER("cstr", file.filename), PUTS(",");
  PUTS("line "), USETYPEPRINTER(u32, line), PUTS(",");
  PUTS("col "), USETYPEPRINTER(u32, col), PUTS(")");
}

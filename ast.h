#pragma once
#include "srcpos.h"
#include "wheels/macros.h"
#include "wheels/print/print_pre.h"
#include <assert.h>
#include <wheels/errable.h>

static inline fptr read_file(allocfn allocator, const char *filename) {
  FILE *fileptr = fopen(filename, "rb");
  assertMessage(fileptr);
  defer { fclose(fileptr); };
  fseek(fileptr, 0, SEEK_END);
  usize len = ftell(fileptr);
  fseek(fileptr, 0, SEEK_SET);
  let resp = acreate(allocator, u8[len ?: 1]);
  usize len2 = fread(resp, sizeof(u8), len, fileptr);
  assertMessage(len2 == len);
  return (fptr){len, *resp};
}

typedef u32 astidx;
sliceDef(astidx);
typedef struct
    // __attribute__((packed))
    ast ast;
typedef sglist(ast) astlist;

static struct { // opNumbers
  u32
      call,
      dot,
      mul,
      div,
      mod,
      plus,
      minus,
      oand,
      oor,
      oband,
      obor,
      shr,
      shl,
      mor,
      les,
      moq,
      leq,
      eq,
      neq,
      pipe,
      colon,
      iexpand,
      as,
      set;
} opNumbers = {};
[[gnu::constructor]] static void initOpNumbers() {
  typedef union {
    char s[sizeof(u32) + 1];
    u32 n;
  } opS;
  opNumbers.call = /*   */ ((opS){"()"}).n;
  opNumbers.dot = /*    */ ((opS){"."}).n;
  opNumbers.colon = /*  */ ((opS){":"}).n;
  opNumbers.mul = /*    */ ((opS){"*"}).n;
  opNumbers.div = /*    */ ((opS){"/"}).n;
  opNumbers.mod = /*    */ ((opS){"%"}).n;
  opNumbers.plus = /*   */ ((opS){"+"}).n;
  opNumbers.minus = /*  */ ((opS){"-"}).n;
  opNumbers.oand = /*   */ ((opS){"and"}).n;
  opNumbers.oor = /*    */ ((opS){"or"}).n;
  opNumbers.oband = /*  */ ((opS){"band"}).n;
  opNumbers.obor = /*   */ ((opS){"bor"}).n;
  opNumbers.shr = /*    */ ((opS){"shr"}).n;
  opNumbers.shl = /*    */ ((opS){"shl"}).n;
  opNumbers.mor = /*    */ ((opS){">"}).n;
  opNumbers.les = /*    */ ((opS){"<"}).n;
  opNumbers.moq = /*    */ ((opS){">="}).n;
  opNumbers.leq = /*    */ ((opS){"<="}).n;
  opNumbers.eq = /*     */ ((opS){"=="}).n;
  opNumbers.neq = /*    */ ((opS){"!="}).n;
  opNumbers.pipe = /*   */ ((opS){"|>"}).n;
  opNumbers.set = /*    */ ((opS){"="}).n;
  opNumbers.iexpand = /**/ ((opS){"'"}).n;
  opNumbers.as = /*      */ ((opS){"as"}).n;
}
tu_def(
    (ast, u8),
    (astNull, nothing_t),
    (astEmpty, srcpos),
    (astId, srcpos),
    (astString, srcpos),
    (astNumber, srcpos),
    (astDecl, struct {
      // list of name type and value all next to eachother startign at idx
      astidx name_type_value;
      bool isconst; // weather ~ or = is used
    }),
    (astOp, struct {
      // left and right next to eaachother
      astidx left_right;
      union {
        c8 symliteral[sizeof(u32)];
        u32 number_rep;
      }; //
    }),
    // starting from start, null-termintaed
    // could remove pos if all of tthem were garunteed to have at least one member
    (astABlock, struct { srcpos pos;astidx start; }), // []
    (astBBlock, struct { srcpos pos;astidx start; }), // {}
    (astPBlock, struct { srcpos pos;astidx start; }), // ()
);
// static_assert(((ast){}).tag == TU_TAG(astNull)); // sentinel
static_assert(sizeof(ast), "");                  // 12, 9 if packed
static inline srcpos srcpos_ast(ast a, astlist alist) {
  return tu_match_exp(
      a,
      (astNull, _, (srcpos)0),

      (astEmpty, a, a),
      (astId, a, a),
      (astString, a, a),
      (astNumber, a, a),
      (astABlock, b, b.pos),
      (astBBlock, b, b.pos),
      (astPBlock, b, b.pos),
      // clang-format off
      (astOp, op, ({
         let off = op.left_right - 1;
         srcpos_ast(sglist_get(alist, off) , alist )
         ?: srcpos_ast(sglist_get(alist, off + 1) ,alist);
       })),
      (astDecl, d, ({
         let off = d.name_type_value - 1;
         srcpos_ast(sglist_get(alist, off)  ,alist )
         ?: srcpos_ast(sglist_get(alist, off + 1),alist )
         ?: srcpos_ast(sglist_get(alist, off + 2),alist );
       })),
      // clang-format on
  );
}
typedef struct {
  astidx idx;
  astlist alist;
  poslist plist;
  filelist flist;
} astidx_full;
typePrinter(astidx_full) {
  if (!in.idx) return PUTS("(null)");
  tu_match_void(
      sglist_get(in.alist, in.idx - 1),
      (astNull, _, PUTS("(null)")),
      (astEmpty, _, PUTS("(empty)")),
      (astId, id, PUTS("(Id "), USENAMEDPRINTER("slice(c8)", fp_from_srcpos((srcpos_full){id, in.plist, in.flist})), PUTS(")");),
      (astString, id, PUTS("(Str "), USENAMEDPRINTER("slice(c8)", fp_from_srcpos((srcpos_full){id, in.plist, in.flist})), PUTS(")");),
      (astNumber, id, PUTS("(Num "), USENAMEDPRINTER("slice(c8)", fp_from_srcpos((srcpos_full){id, in.plist, in.flist})), PUTS(")");),
      (astDecl, decl, {
        PUTS("(Dcl");
        if (decl.isconst) PUTS(" :const");
        PUTS(",");
        USETYPEPRINTER(astidx_full, P$(in, ($.idx = decl.name_type_value, $))), PUTS(",");
        USETYPEPRINTER(astidx_full, P$(in, ($.idx = decl.name_type_value + 1, $))), PUTS(",");
        USETYPEPRINTER(astidx_full, P$(in, ($.idx = decl.name_type_value + 2, $))), PUTS(")");
      }),
      (astOp, op, {
        PUTS("(Op \"");
        let s = (char[5]){};
        let l = op.symliteral[sizeof(u32) - 1] ? 4 : strlen(op.symliteral);
        memcpy(s, op.symliteral, l);
        USENAMEDPRINTER("cstr", s + 0);
        PUTS("\",");

        USETYPEPRINTER(astidx_full, P$(in, ($.idx = op.left_right, $))), PUTS(",");
        USETYPEPRINTER(astidx_full, P$(in, ($.idx = op.left_right + 1, $))), PUTS(")");
      }),
      (astPBlock, block, {
        PUTS("(PBlock");
        for (let idx = block.start - 1; !tu_is(astNull, sglist_get(in.alist, idx)); idx++)
          idx == block.start - 1 ?: PUTS(","), USETYPEPRINTER(astidx_full, P$(in, ($.idx = idx + 1, $)));
        PUTS(")");
      }),
      (astABlock, block, {
        PUTS("(ABlock");
        for (let idx = block.start - 1; !tu_is(astNull, sglist_get(in.alist, idx)); idx++)
          idx == block.start - 1 ?: PUTS(","), USETYPEPRINTER(astidx_full, P$(in, ($.idx = idx + 1, $)));
        PUTS(")");
      }),
      (astBBlock, block, {
        PUTS("(BBlock");
        for (let idx = block.start - 1; !tu_is(astNull, sglist_get(in.alist, idx)); idx++)
          idx == block.start - 1 ?: PUTS(","), USETYPEPRINTER(astidx_full, P$(in, ($.idx = idx + 1, $)));
        PUTS(")");
      }),
  );
}


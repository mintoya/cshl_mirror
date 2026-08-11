#pragma once
#include "wheels/mytypes.h"
#include "wheels/print/print_pre.h"
#include <stdio.h>
#include <string.h>
#include <wheels/allocator.h>
#include <wheels/allocators/fbaAllocator.h>
#include <wheels/assertMessage.h>
#include <wheels/fptr.h>
#include <wheels/macros.h>
#include <wheels/print.h>
#if !defined(_Nullable)
  #define _Nullable
#endif
extern __attribute__((noreturn)) void error_at(u8 *location, fptr message);
static inline __attribute__((noreturn)) void error_at_sc8(u8 *loc, slice(c8) message) { error_at(loc, (fptr){message.len, (u8 *)message.ptr}); };
static inline __attribute__((noreturn)) void error_at_str(u8 *loc, char *messag) { error_at(loc, fptr_CSP(messag)); };
#define error_at(loc, message)     \
  ({                               \
    var_ _msg = message;           \
    _Generic(                      \
        _msg,                      \
        char *: (error_at_str),    \
        slice(c8): (error_at_sc8), \
        default: (error_at)        \
    )(loc, _msg);                  \
  })

//
// ast {
typedef struct ast ast;
tu_def(
    (ast, u8),
    // empty statement
    (astEmptyStatement, nothing_t),
    // Identifier
    //  string, u8* ptr & usize len
    (astId, fptr),
    // Numbers
    //  0-9*
    //  x0-9 + a-f*
    //  b0-1
    (astNumber, fptr),
    // inside of ""
    (astString, fptr),
    // (astComment, fptr),
    // Declarations
    //  valid states :
    //    no type         : inferred type, like pi::3.1415926535
    //    no value        : forward declaration/uninitialized , print : fn(([*]i8),i32);
    //    no name or type : expression
    (astDecl, struct { ast * _Nullable name; ast *_Nullable type; ast *_Nullable value; int is_const; }),
    // Operations
    // normal math operations, but also function calls and the dot operator
    // nullable cause in  structtype[.f] left is nullable
    // literal operators like bor bxor bnor ...
    (astOp, struct { ast* _Nullable left ;ast*right;union{c8 symLiteral[sizeof(u32)]; u32 number_rep;}; }),

    // PBlocks
    // whatever is inside ()
    // ccomma-separated
    (astPBlock, struct {u8* loc ; sentList_t(ast *) stmts; }),
    // Blocks
    // whatever is inside {}
    // always semicolon-separated
    (astBBlock, struct {u8* loc ; sentList_t(ast *) stmts; }),
    // Blocks
    // whatever is inside []
    // always comma-separated
    (astABlock, struct {u8* loc ; sentList_t(ast *) items; }),
);
_Static_assert(sizeof(ast));
typePrinter("astTag", itypeof(ast, tag)) {
  tu_match_void(
      ((ast){.tag = in}),
      (astId, _, PUTS("astId");),
      (astNumber, _, PUTS("astNum");),
      (astString, _, PUTS("astStr");),
      (astDecl, _, PUTS("astDecl");),
      (astOp, _, PUTS("astOp");),
      (astBBlock, _, PUTS("astBBlock");),
      (astPBlock, _, PUTS("astPBlock");),
      (astABlock, _, PUTS("astABlock");),
      (astEmptyStatement, _, PUTS("astEmpty");),
  );
}
static inline isize fptr_toi(fptr p) {
  isize result = 0;
  int sign = 1;

  for (isize i = 0; i < p.len; i++) {
    if (p.ptr[i] == ' ') {
      continue;
    }

    if (p.ptr[i] == '-') {
      sign = -1;
      continue;
    }

    if (p.ptr[i] >= '0' && p.ptr[i] <= '9') {
      result = result * 10 + (p.ptr[i] - '0');
    }
  }

  return result * sign;
}
#define P_INDENT_IN "\x11"
#define P_INDENT_OUT "\x12"

typePrinter("ast", ast *) {
  if (!in) return PUTS("null\n");

  tu_match_void(
      (*in),
      (astId, id, {
        PUTS("Id '");
        USENAMEDPRINTER("slice(c8)", id);
        PUTS("'\n");
      }),
      (astNumber, num, {
        PUTS("Number '");
        USENAMEDPRINTER("slice(c8)", num);
        PUTS("'\n");
      }),
      (astString, str, {
        PUTS("String \"");
        USENAMEDPRINTER("slice(c8)", str);
        PUTS("\"\n");
      }),
      (astDecl, decl, {
        PUTS("Decl");
        if (decl.is_const) {
          PUTS(" [const]");
        }
        PUTS("\n" P_INDENT_IN);

        PUTS("name: ");
        USENAMEDPRINTER("ast", decl.name);

        PUTS("type: ");
        USENAMEDPRINTER("ast", decl.type);

        PUTS("value: ");
        USENAMEDPRINTER("ast", decl.value);

        PUTS(P_INDENT_OUT);
      }),
      (astOp, op, {
        PUTS("Op '");
        char s[countof(op.symLiteral) + 1] = {};
        memcpy(s, op.symLiteral, sizeof(op.symLiteral));
        USENAMEDPRINTER("cstr", (char *)s);
        PUTS("'\n" P_INDENT_IN);

        PUTS("l: ");
        USENAMEDPRINTER("ast", op.left);

        PUTS("r: ");
        USENAMEDPRINTER("ast", op.right);

        PUTS(P_INDENT_OUT);
      }),
      (astPBlock, block, {
        PUTS("PBlock\n" P_INDENT_IN);
        foreach (let stmt, vlap(sentList_vla(block.stmts))) {
          USENAMEDPRINTER("ast", stmt);
        }
        PUTS(P_INDENT_OUT);
      }),
      (astBBlock, block, {
        PUTS("BBlock\n" P_INDENT_IN);
        foreach (let stmt, vlap(sentList_vla(block.stmts))) {
          USENAMEDPRINTER("ast", stmt);
        }
        PUTS(P_INDENT_OUT);
      }),
      (astABlock, block, {
        PUTS("ABlock\n" P_INDENT_IN);
        foreach (let stmt, vlap(sentList_vla(block.items))) {
          USENAMEDPRINTER("ast", stmt);
        }
        PUTS(P_INDENT_OUT);
      }),
      (astEmptyStatement, _, { PUTS("Empty"); }),
      (default, unreachable())
  );
}

// }
//

extern ast *parse_source(allocfn allocator, fptr input);
static inline __attribute__((noreturn)) void error_ast(ast a, slice(c8) message) {
  tu_match_void(
      a,
      (astId, s, error_at(s.ptr, message);),
      (astNumber, s, error_at(s.ptr, message);),
      (astString, s, error_at(s.ptr, message);),

      (astDecl, d, error_ast(d.name[0], message);),
      (astOp, o, error_ast(o.right[0], message);),

      (astPBlock, b, error_at(b.loc, message);),
      (astBBlock, b, error_at(b.loc, message);),
      (astABlock, b, error_at(b.loc, message);),

      (default, error_at(nullptr, message);),
  );
  unreachable();
}

static inline slice(u8) readFileAlloc(allocfn allocator, const char *filename) {
  var_ fileptr = fopen(filename, "rb");
  assertMessage(fileptr);
  defer { fclose(fileptr); };

  fseek(fileptr, 0, SEEK_END);
  usize len = ftell(fileptr);
  fseek(fileptr, 0, SEEK_SET);

  var_ resp = acreate(allocator, u8[len ?: 1]);
  usize len2 = fread(resp, sizeof(u8), len, fileptr);
  assertMessage(len2 == len);
  return (slice(u8))slice_stat(*resp);
}
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
      set;
} opNumbers = {};

typedef struct DepthState {
  outputFunction inner_put;
  void *inner_arb;
  usize depth;
  usize indent_spaces;
  bool at_line_start;
} DepthState;

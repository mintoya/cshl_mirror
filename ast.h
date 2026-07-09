#pragma once
#include <string.h>
#include <wheels/allocator.h>
#include <wheels/assertMessage.h>
#include <wheels/fptr.h>
#include <wheels/macros.h>
#include <wheels/print.h>
#include <wheels/smallstream.h>
#include <wheels/smallstreams/smallfiles.h>
#if !defined(_Nullable)
  #define _Nullable
#endif

void error_at(u8 *location, slice(c8) message);
//
// ast {
typedef struct ast ast;
tu_def(
    (ast, u8),
    // Identifier
    //  string, u8* ptr & usize len
    (astId, fptr),
    // empty statement
    (astEmptyStatement, nothing_t),
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
typePrinter("astTag", itypeof(ast, tag)) {
  tu_match(
      ((ast){.tag = in}),
      (astId, _, PUTS("astId")),
      (astNumber, _, PUTS("astNum")),
      (astString, _, PUTS("astStr")),
      (astDecl, _, PUTS("astDecl")),
      (astOp, _, PUTS("astOp")),
      (astBBlock, _, PUTS("astBBlock")),
      (astPBlock, _, PUTS("astPBlock")),
      (astABlock, _, PUTS("astABlock")),
      (astEmptyStatement, _, PUTS("astEmpty")),
  );
}
static inline void print_ast_indent(int indent, outputFunction put, void *_arb) {
  for (int i = 0; i < indent; i++)
    PUTS("  ");
}
static void print_ast_recursive(ast *node, int indent, outputFunction put, void *_arb) {
  if (!node) {
    PUTS("null\n");
    return;
  }

  tu_match(
      (*node),
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
        PUTS("\n");

        print_ast_indent(indent + 1, put, _arb);
        PUTS("name: ");
        print_ast_recursive(decl.name, indent + 1, put, _arb);

        print_ast_indent(indent + 1, put, _arb);
        PUTS("type: ");
        print_ast_recursive(decl.type, indent + 1, put, _arb);

        print_ast_indent(indent + 1, put, _arb);
        PUTS("value: ");
        print_ast_recursive(decl.value, indent + 1, put, _arb);
      }),
      (astOp, op, {
        PUTS("Op '");
        char s[countof(op.symLiteral) + 1] = {};
        memcpy(s, op.symLiteral, sizeof(op.symLiteral));
        USENAMEDPRINTER("cstr", (char *)s);
        PUTS("'\n");

        print_ast_indent(indent + 1, put, _arb);
        PUTS("l: ");
        print_ast_recursive(op.left, indent + 1, put, _arb);

        print_ast_indent(indent + 1, put, _arb);
        PUTS("r: ");
        print_ast_recursive(op.right, indent + 1, put, _arb);
      }),
      (astPBlock, block, {
        PUTS("PBlock\n");
        for (ast **stmt = (ast **)block.stmts; stmt && *stmt; stmt++) {
          print_ast_indent(indent + 1, put, _arb);
          print_ast_recursive(*stmt, indent + 1, put, _arb);
        }
      }),
      (astBBlock, block, {
        PUTS("BBlock\n");
        for (ast **stmt = (ast **)block.stmts; stmt && *stmt; stmt++) {
          print_ast_indent(indent + 1, put, _arb);
          print_ast_recursive(*stmt, indent + 1, put, _arb);
        }
      }),
      (astABlock, block, {
        PUTS("ABlock\n");
        for (ast **item = (ast **)block.items; item && *item; item++) {
          print_ast_indent(indent + 1, put, _arb);
          print_ast_recursive(*item, indent + 1, put, _arb);
        }
      }),
      (astEmptyStatement, _, { PUTS("Empty"); }),
      (default, unreachable())
  );
}
typePrinter("ast", ast *) { print_ast_recursive(in, 0, put, _arb); }
// }
//
__attribute__((noreturn)) void error_ast(ast a, slice(c8) message) {
  tu_match(
      a,
      (astId, s, error_at(s.ptr, message)),
      (astNumber, s, error_at(s.ptr, message)),
      (astString, s, error_at(s.ptr, message)),

      (astDecl, d, error_ast(d.name[0], message)),
      (astOp, o, error_ast(o.right[0], message)),

      (astPBlock, b, error_at(b.loc, message)),
      (astBBlock, b, error_at(b.loc, message)),
      (astABlock, b, error_at(b.loc, message)),

      (default, error_at(nullptr, message)),
  );
  unreachable();
}

static inline slice(u8) readFileAlloc(AllocatorV allocator, const char *filename) {
  var_ fileptr = file_stream_open(
      allocator,
      filename,
      (fileopenflags){
          .access.read = 1,
          .status.sync = 1
      }
  );
  assertMessage(fileptr);
  defer { file_stream_close(allocator, fileptr); };

  assertMessage(fileptr->tell_max);
  usize len = *fileptr->tell_max;

  var_ resp = &aCreate(allocator, u8, len ?: 1);
  usize len2 = sGetsf(fileptr, ((fptr){sizeof(*resp), *resp}));
  assertMessage(len2 == len);
  return (slice(u8)){len, resp[0]};
}

//
// tokenizer defs {
typedef enum token : u8 {
  t_operator,
  t_punctuation,
  t_number,
  t_string,

  t_identifier,
} token;
typedef struct Token {
  fptr s;
  enum token kind;
} Token;
sliceDef(Token);
typePrinter(Token) {
  switch (in.kind) {
      // clang-format off
case t_operator    :PUTS("op ");break;
case t_punctuation :PUTS("pun");break;
case t_number      :PUTS("num");break;
case t_string      :PUTS("str");break;
case t_identifier  :PUTS("id ");break;
      // clang-format on
  }
  PUTS("'");
  USENAMEDPRINTER("slice(c8)", in.s);
  PUTS("'");
}

c8 *punctuations[] = {
    ":",
    ";",
    ",",
    "(",
    ")",
    "{",
    "}",
    "[",
    "]",
};
// }
//

struct { // operator precidence
  c8 *name;
  usize prec;
} operations[] = {

    {".", 0},
    {"*", 1},
    {"/", 1},
    {"%", 1},

    {"+", 2},
    {"-", 2},

    {"and" /* */, 3},
    {"not" /* */, 3},
    {"or" /*  */, 3},
    {"band" /**/, 3},
    {"bnot" /**/, 3},
    {"bor" /* */, 3},
    {"shr" /* */, 3},
    {"shl" /* */, 3},

    {"<", 4},
    {">", 4},
    {"<=", 4},
    {">=", 4},
    {"==", 4},
    {"!=", 4},

    {"|>", 5},

    {"=", ~(usize)0},

};

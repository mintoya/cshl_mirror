#if !defined(AST_H)
  #define AST_H
  #include "wheels/smallstream.h"
  #include <wheels/allocator.h>
  #include <wheels/assertMessage.h>
  #include <wheels/fptr.h>
  #include <wheels/print.h>
  #include <wheels/smallstreams/smallfiles.h>
  #include <wheels/tu_macros.h>
  #if !defined(_Nullable)
    #define _Nullable
  #endif

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
    (astOp, struct { ast* _Nullable left ;ast*right;union{c8 symLiteral[8]; u64 number_rep;}; }),

    // PBlocks
    // whatever is inside ()
    // ccomma-separated
    (astPBlock, struct { sentList_t(ast *) stmts; }),
    // Blocks
    // whatever is inside {}
    // always semicolon-separated
    (astBBlock, struct { sentList_t(ast *) stmts; }),
    // Blocks
    // whatever is inside []
    // always comma-separated
    (astABlock, struct { sentList_t(ast *) items; }),
);

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
_Static_assert(sizeof((astOp *)NULL)->symLiteral == sizeof((astOp *)NULL)->number_rep);

typedef struct Token {
  fptr s;
  enum token : u8 {
    t_operator,
    t_punctuation,
    t_number,
    t_string,

    t_identifier,
  } kind;
} Token;

sliceDef(Token);
typedef slice(Token) slice_Tokens;
sliceDef(slice_Tokens);
typedef slice(slice_Tokens) Tokens_2d;

typedef enum token token;

REGISTER_PRINTER(Token, {
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
});

static inline void print_ast_indent(int indent, outputFunction put, void *_arb) {
  for (int i = 0; i < indent; i++)
    PUTS("  ");
}
static void print_ast_recursive(ast *node, int indent, outputFunction put, void *_arb) {
  if (!node) {
    PUTS("null\n");
    return;
  }

  tu_match(*node, case (astId, id, {
    PUTS("Id '");
    USENAMEDPRINTER("slice(c8)", id);
    PUTS("'\n");
  }),
           case (astNumber, num, {
             PUTS("Number '");
             USENAMEDPRINTER("slice(c8)", num);
             PUTS("'\n");
           }),
           case (astString, str, {
             PUTS("String \"");
             USENAMEDPRINTER("slice(c8)", str);
             PUTS("\"\n");
           }),
           case (astDecl, decl, {
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
           case (astOp, op, {
             PUTS("Op '");
             usize sym_len = 0;
             while (sym_len < 8 && op.symLiteral[sym_len]) {
               sym_len++;
             }
             put((const char *)op.symLiteral, _arb, sym_len, 0);
             PUTS("'\n");

             print_ast_indent(indent + 1, put, _arb);
             PUTS("l: ");
             print_ast_recursive(op.left, indent + 1, put, _arb);

             print_ast_indent(indent + 1, put, _arb);
             PUTS("r: ");
             print_ast_recursive(op.right, indent + 1, put, _arb);
           }),
           case (astPBlock, block, {
             PUTS("PBlock\n");
             for (ast **stmt = (ast **)block.stmts; stmt && *stmt; stmt++) {
               print_ast_indent(indent + 1, put, _arb);
               print_ast_recursive(*stmt, indent + 1, put, _arb);
             }
           }),
           case (astBBlock, block, {
             PUTS("BBlock\n");
             for (ast **stmt = (ast **)block.stmts; stmt && *stmt; stmt++) {
               print_ast_indent(indent + 1, put, _arb);
               print_ast_recursive(*stmt, indent + 1, put, _arb);
             }
           }),
           case (astABlock, block, {
             PUTS("ABlock\n");
             for (ast **item = (ast **)block.items; item && *item; item++) {
               print_ast_indent(indent + 1, put, _arb);
               print_ast_recursive(*item, indent + 1, put, _arb);
             }
           }),
           case (astEmptyStatement, _, {}),
           default({
             unreachable();
           }));
}
REGISTER_SPECIAL_PRINTER("ast", ast *, { print_ast_recursive(in, 0, put, _arb); });

c8 *punctuations[] = {
    {":"},
    {";"},
    {","},
    {"("},
    {")"},
    {"{"},
    {"}"},
    {"["},
    {"]"},
};

struct {
  c8 *name;
  usize prec;
} operations[] = {

    {".", 0},
    {"*", 1},
    {"/", 1},
    {"%", 1},

    {"+", 2},
    {"-", 2},

    {"and", 3},
    {"not", 3},
    {"or", 3},
    {"band", 3},
    {"bnot", 3},
    {"bor", 3},
    {"shr", 3},
    {"shl", 3},

    {"<", 4},
    {">", 4},
    {"<=", 4},
    {">=", 4},
    {"==", 4},
    {"!=", 4},

    {"|>", 5},

    {"up", 6},
    {"in", 7},

    {"=", 8},

};

#endif

#include "wheels/fptr.h"
#include "wheels/mytypes.h"

typedef struct ast ast;
typedef u32 astidx;
tu_def(
    (ast, u8),
    // empty statement
    (astEmptyStatement, nothing_t),
    // Identifier
    //  string, u8* ptr & usize len
    (astId, fptr), // fptrs
    // Numbers
    //  0-9*
    //  x0-9 + a-f*
    //  b0-1
    (astNumber, fptr), // fptrs
    // inside of ""
    (astString, fptr), // fptrs
    // (astComment, fptr),
    // Declarations
    //  valid states :
    //    no type         : inferred type, like pi::3.1415926535
    //    no value        : forward declaration/uninitialized , print : fn(([*]i8),i32);
    //    no name or type : expression
    (astDecl, struct { astidx name; astidx type; astidx value; bool is_const; }),
    // Operations
    // normal math operations, but also function calls and the dot operator
    // nullable cause in  structtype[.f] left is nullable
    // literal operators like bor bxor bnor ...
    (astOp, struct { astidx left ;astidx right;union{c8 symLiteral[sizeof(u32)]; u32 number_rep;}; }),

    // PBlocks
    // whatever is inside ()
    // ccomma-separated
    (astPBlock, struct { sentList_t(astidx) stmts; }),
    // Blocks
    // whatever is inside {}
    // always semicolon-separated
    (astBBlock, struct { sentList_t(astidx) stmts; }),
    // Blocks
    // whatever is inside []
    // always comma-separated
    (astABlock, struct { sentList_t(astidx) items; }),
);
_Static_assert(sizeof(ast));

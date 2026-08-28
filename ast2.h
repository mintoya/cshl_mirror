#include "wheels/allocator.h"
#include "wheels/fptr.h"
#include "wheels/mytypes.h"
#include "wheels/sglist.h"

typedef struct __attribute__((packed)) ast ast;
typedef u32 astidx;

tu_def(
    (ast, u8),
    // empty statement
    (astEmptyStatement, nothing_t),
    // fat pointers. global index into fptr_list
    (astId, /*    */ usize), // fptrs
    (astNumber, /**/ usize), // fptrs
    (astString, /**/ usize), // fptrs
    // (astComment, fptr),
    // Declarations
    //  valid states :
    //    no type         : inferred type, like pi::3.1415926535
    //    no value        : forward declaration/uninitialized , print : fn(([*]i8),i32);
    //    no name or type : expression
    (astDecl, struct { bool is_const; }),
    // Operations
    // normal math operations, but also function calls and the dot operator
    // nullable cause in  structtype[.f] left is nullable
    // literal operators like bor bxor bnor ...
    (astOp, struct { union{c8 symLiteral[sizeof(u32)]; u32 number_rep;}; }),

    // PBlocks
    // whatever is inside ()
    // ccomma-separated
    (astPBlock, struct { astidx start_idx; }),
    // Blocks
    // whatever is inside {}
    // always semicolon-separated
    (astBBlock, struct { astidx start_idx; }),
    // Blocks
    // whatever is inside []
    // always comma-separated
    (astABlock, struct { astidx start_idx; }),
);
sglist(fptr) global_fptr_list;
sglist(ast) global_ast_list;
_Static_assert(sizeof(ast));

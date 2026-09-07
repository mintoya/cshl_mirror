#include "srcpos.h"
#include "wheels/macros.h"
#include <assert.h>

typedef u32 astidx;
// typedef struct __attribute__((packed)) ast ast;
tu_def(
    (ast, u8),
    (astNull, nothing_t),
    (astEmptyStatement, srcpos),
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
    (astABlock, struct { srcpos pos;astidx start ; }), // []
    (astBBlock, struct { srcpos pos;astidx start ; }), // {}
    (astPBlock, struct { srcpos pos;astidx start ; }), // ()
);
sglist(ast) ast_list;
static_assert(sizeof(ast), ""); // 12, 9 if packed
srcpos srcpos_ast(ast a) {
  return tu_match(
      a,
      (astNull, _, (srcpos)0),

      (astEmptyStatement, a, a),
      (astId, a, a),
      (astString, a, a),
      (astNumber, a, a),
      (astABlock, b, b.pos),
      (astBBlock, b, b.pos),
      (astPBlock, b, b.pos),
      // clang-format off
      (astOp, op, ({
         let off = op.left_right;
         srcpos_ast(sglist_get(ast_list, off))
         ?: srcpos_ast(sglist_get(ast_list, off));
       })),
      (astDecl, d, ({
         let off = d.name_type_value;
         srcpos_ast(sglist_get(ast_list, off))
         ?: srcpos_ast(sglist_get(ast_list, off + 1))
         ?: srcpos_ast(sglist_get(ast_list, off + 2));
       })),
      // clang-format on
  );
}

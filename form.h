#if !defined(FORM_HELPER_H)
  #define FORM_HELPER_H
  #include "ast.h"
  #include "wheels/macros.h"
  #include <wheels/macros.h>
  #include <wheels/sList.h>

static bool ast_match_internal(AllocatorV alloc, const ast *const node, u8 **fmt, u8 *fmt_end, msList(ast *) * list) {
  while (*fmt < fmt_end && **fmt == ' ')
    (*fmt)++;
  if (*fmt >= fmt_end)
    return true;

  if (!node) {
    if (**fmt == '_') {
      (*fmt)++;
      return true;
    }
    return false;
  }

  u8 c = *(*fmt)++;

  char type = '?';
  tu_match(
      *node,
      case (astId, id, { type = 'i'; }),
      case (astNumber, num, { type = 'n'; }),
      case (astString, str, { type = 's'; }),
      case (astDecl, decl, { type = 'd'; }),
      case (astOp, op, { type = 'o'; }),
      case (astPBlock, block, { type = 'P'; }),
      case (astBBlock, block, { type = 'B'; }),
      case (astABlock, block, { type = 'A'; }),
      case (astEmptyStatement, empty, { type = 'e'; }),
      default({ type = '?'; })
  );

  if (c != '.' && c != 'a' && c != type)
    return false;

  // Flattening magic: Only push value/operation nodes.
  // Ignore structural wrappers (B, P, A) so the list contains exactly the requested targets.
  if (c >= 'a' && c <= 'z') {
    msList_push(alloc, *list, node);
  }

  while (*fmt < fmt_end && **fmt == ' ')
    (*fmt)++;

  if (type == 'o') {
    bool ok = false;
    tu_match(*node, case (astOp, op, {
      ok = ast_match_internal(alloc, op.left, fmt, fmt_end, list) &&
           ast_match_internal(alloc, op.right, fmt, fmt_end, list);
    }));
    if (!ok) return false;
  } else if (type == 'P' || type == 'B' || type == 'A') {
    if (*fmt < fmt_end && **fmt == '+') {
      (*fmt)++;
      while (*fmt < fmt_end && **fmt == ' ')
        (*fmt)++;

      u8 *loop_start = *fmt;
      u8 *loop_end = loop_start;

      ast **stmts = nullptr;
      tu_match(*node, case (astPBlock, block, { stmts = (ast **)block.stmts; }), case (astBBlock, block, { stmts = (ast **)block.stmts; }), case (astABlock, block, { stmts = (ast **)block.items; }));

      if (stmts && *stmts) {
        for (ast **s = stmts; s && *s; s++) {
          u8 *temp_fmt = loop_start;

          if (ast_match_internal(alloc, *s, &temp_fmt, fmt_end, list)) {
            loop_end = temp_fmt;
          } else {
            return false; // STRICT FAILURE: If a child violates the repeated pattern, the whole thing fails
          }
        }
      } else {
        if (loop_start < fmt_end) {
          loop_end = loop_start + 1;
        }
      }
      *fmt = loop_end;
    } else {
      ast **stmts = nullptr;
      tu_match(*node, case (astPBlock, block, { stmts = (ast **)block.stmts; }), case (astBBlock, block, { stmts = (ast **)block.stmts; }), case (astABlock, block, { stmts = (ast **)block.items; }));

      if (stmts && *stmts)
        for (ast **s = stmts; s && *s; s++) {
          // STRICT ARITY MAX: Fail if there are more statements than format characters
          if (*fmt >= fmt_end) return false;

          if (!ast_match_internal(alloc, *s, fmt, fmt_end, list)) {
            return false;
          }
        }
    }
  }

  return true;
}
/**
 * ast_match pattern formatting string documentation.
 *
 * Pattern Matching Key:
 * ---------------------
 * Lowercase (Captures node into the returned list):
 * a : Matches exactly one statement of any type AND captures it
 * i : astId
 * n : astNumber
 * s : astString
 * d : astDecl (Leaf node, does not consume pattern for name/type/val)
 * o : astOp   (Automatically consumes pattern for left and right operands)
 * e : astEmptyStatement
 *
 * Uppercase & Symbols (Validates structure, DOES NOT capture):
 * P : astPBlock (...) (Enters block to match statements)
 * B : astBBlock {...} (Enters block to match statements)
 * A : astABlock [...] (Enters block to match items)
 * . : Matches exactly one statement of any type
 * + : Greedily matches the following pattern against all remaining statements
 * _ : Matches a null/missing node (e.g., empty left operand or missing type)
 *
 * Strict Arity Rules:
 * -------------------
 * - A block must contain EXACTLY the number of statements specified by the pattern.
 * - If `+` is used, EVERY remaining statement must match the pattern following it.
 *
 * Examples:
 * ---------
 * "Pn"        -> Matches `( 42 )`.
 * Captures: [astNumber]
 *
 * "B + d"     -> Matches `{ x: int = 1; y: int = 2; }`.
 * Captures: [astDecl, astDecl] (Fails if any statement isn't a Decl)
 *
 * "o B. B."   -> Matches `{ a; } + { b; }`.
 * Captures: [astOp] (Validates inner blocks have exactly 1 stmt, but ignores them)
 *
 * "Boii"      -> Matches `{ a + b; }`.
 * Captures: [astOp, astId, astId]
 *
 * "P i _"     -> Matches `( a )` if it was an Op missing its right operand.
 * Captures: [astId]
 */
static msList(ast *) ast_match(AllocatorV alloc, ast a, fptr fp) {
  var_ list = msList_init(alloc, ast *, fp.len);

  u8 *fmt_ptr = fp.ptr;
  u8 *fmt_end = fp.ptr + fp.len;

  bool ok = ast_match_internal(alloc, &a, &fmt_ptr, fmt_end, &list);

  while (fmt_ptr < fmt_end && *fmt_ptr == ' ')
    fmt_ptr++;

  // STRICT ARITY MIN: Fail if there are unconsumed format characters (e.g., fewer arguments than expected)
  if (!ok || fmt_ptr < fmt_end) {
    msList_deInit(alloc, list);
    return nullptr;
  }

  return list;
}

#endif

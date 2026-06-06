#if !defined(TOKENIZER_H1)
  #define TOKENIZER_H1 (1)
  #include "ast.h"
  #include "tok1.h"
  #include <wheels/macros.h>
  #include <wheels/mytypes.h>
  #include <wheels/sList.h>
  #include <wheels/tu_macros.h>

  // a file is a list of declarations
  #define __musttail___ __attribute__((musttail))
CONST_EXPR Token t_comma = (Token){(fptr){1, (u8[]){','}}, t_punctuation};
CONST_EXPR Token t_semi = (Token){(fptr){1, (u8[]){';'}}, t_punctuation};
CONST_EXPR Token t_colon = (Token){(fptr){1, (u8[]){':'}}, t_punctuation};
CONST_EXPR Token t_equals = (Token){(fptr){1, (u8[]){'='}}, t_operator};

// CONST_EXPR Token t_for = (Token){(fptr){3, (u8[]){"for"}}, t_operator};
// CONST_EXPR Token t_if = (Token){(fptr){2, (u8[]){"if"}}, t_operator};
CONST_EXPR Token t_dot = (Token){(fptr){1, (u8[]){'.'}}, t_operator};

CONST_EXPR Token t_lparen = (Token){(fptr){1, (u8[]){'('}}, t_punctuation};
CONST_EXPR Token t_rparen = (Token){(fptr){1, (u8[]){')'}}, t_punctuation};
CONST_EXPR Token t_lbrace = (Token){(fptr){1, (u8[]){'{'}}, t_punctuation};
CONST_EXPR Token t_rbrace = (Token){(fptr){1, (u8[]){'}'}}, t_punctuation};
CONST_EXPR Token t_lbracket = (Token){(fptr){1, (u8[]){'['}}, t_punctuation};
CONST_EXPR Token t_rbracket = (Token){(fptr){1, (u8[]){']'}}, t_punctuation};

bool Token_equal(Token a, Token b) {
  return a.kind == b.kind && fptr_eq(a.s, b.s);
}

bool isScope(Token t) {
  return EQUAL_ANY(
      1,
      Token_equal(t, t_lbrace),
      Token_equal(t, t_rbrace),
      Token_equal(t, t_lbracket),
      Token_equal(t, t_rbracket),
      Token_equal(t, t_lparen),
      Token_equal(t, t_rparen)
  );
}
Token opposite_scope(Token t) {
  if (Token_equal(t, t_lbrace)) return t_rbrace;
  if (Token_equal(t, t_rbrace)) return t_lbrace;
  if (Token_equal(t, t_lbracket)) return t_rbracket;
  if (Token_equal(t, t_rbracket)) return t_lbracket;
  if (Token_equal(t, t_lparen)) return t_rparen;
  if (Token_equal(t, t_rparen)) return t_lparen;
  assertMessage(false, "item isnt scope marker");
}
bool isVal(Token t) {
  switch (t.kind) {
    case t_identifier:
      return true;
    case t_string:
      return true;
    case t_number:
      return true;
    default:
      return false;
  }
}
Token *next_tok_balanced(AllocatorV allocator, slice(Token) tokens, Token t) {
  var_ sb = msList_stackBuffer(Token[32]);
  var_ ts = msList_initBuffer(sb);
  defer { msList_deInit(allocator, ts); };

  foreach (var_ tp, span(tokens.ptr, tokens.len))
    if (msList_len(ts)) {
      if (isScope(tp[0]))
        if (Token_equal(tp[0], opposite_scope(msList_last(ts))))
          msList_pop(ts);
        else
          msList_push(allocator, ts, tp[0]);
    } else if (Token_equal(tp[0], t))
      return tp;
    else if (isScope(tp[0]))
      msList_push(allocator, ts, tp[0]);

  if (msList_len(ts))
    error_at(msList_last(ts).s.ptr, snprint(stdAlloc, "unclosed {c8}", (char)msList_last(ts).s.ptr[0]));
  return nullptr;
}
struct next_op_result {
  Token *t;
  usize skipped_values;
};
struct next_op_result next_op_balanced(AllocatorV allocator, slice(Token) tokens) {
  var_ sb = msList_stackBuffer(Token[32]);
  var_ ts = msList_initBuffer(sb);
  defer { msList_deInit(allocator, ts); };

  usize skipped = 0;

  foreach (var_ tp, span(tokens.ptr, tokens.len))
    if (msList_len(ts)) {
      if (isScope(tp[0]))
        if (Token_equal(tp[0], opposite_scope(msList_last(ts))))
          msList_pop(ts);
        else msList_push(allocator, ts, tp[0]);
    } else if (tp->kind == t_operator) return (struct next_op_result){tp, skipped};
    else if (isScope(tp[0])) {
      msList_push(allocator, ts, tp[0]);
      skipped++;
    } else if (isVal(tp[0])) skipped++;

  if (msList_len(ts))
    error_at(msList_last(ts).s.ptr, snprint(stdAlloc, "unclosed {c8}", (char)msList_last(ts).s.ptr[0]));

  return (struct next_op_result){nullptr, skipped};
}

usize balanced_count(AllocatorV allocator, slice(Token) tokens, Token t) {
  usize count = 0;
  var_ sb = msList_stackBuffer(Token[32]);
  var_ ts = msList_initBuffer(sb);
  defer { msList_deInit(allocator, ts); };

  foreach (var_ tp, span(tokens.ptr, tokens.len))
    if (msList_len(ts)) {
      if (isScope(tp[0]))
        if (Token_equal(tp[0], opposite_scope(msList_last(ts))))
          msList_pop(ts);
        else msList_push(allocator, ts, tp[0]);
    } else if (Token_equal(tp[0], t))
      count++;
    else if (isScope(tp[0]))
      msList_push(allocator, ts, tp[0]);

  if (msList_len(ts))
    error_at(msList_last(ts).s.ptr, snprint(stdAlloc, "unclosed {c8}", (char)msList_last(ts).s.ptr[0]));
  return count;
}
usize getPrec(Token t) {
  assertMessage(t.kind == t_operator);

  foreach (var_ op, vla(operations))
    if (fptr_eq(fp(op.name), t.s))
      return op.prec + 1;

  error_at(
      t.s.ptr,
      snprint(stdAlloc, "compiler error : operator not in operator list {slice(c8)}", t.s)
  );
  return 0;
}

ast *parseBlock(AllocatorV allocator, slice(Token) tokens);
ast *astAValue(AllocatorV allocator, Token t) {
  assertMessage(isVal(t));
  switch (t.kind) {
    case t_identifier:
      return aValue(allocator, (ast)tu_of(astId, t.s));
    case t_string:
      return aValue(allocator, (ast)tu_of(astString, t.s));
    case t_number:
      return aValue(allocator, (ast)tu_of(astNumber, t.s));
    default:
      unreachable();
  }
}
bool isBlockStart(Token t) {
  return EQUAL_ANY(
      1,
      Token_equal(t, t_lbrace),
      Token_equal(t, t_lbracket),
      Token_equal(t, t_lparen),
  );
}

  // { operations
  #define slice_first(slice) ({var_ _slice = slice ; _slice.ptr[0]; })

ast *popVal(AllocatorV allocator, slice(Token) * backptr) {
  var_ back = *backptr;

  bool isblockStart = isBlockStart(back.ptr[0]);
  bool isValue = isVal(back.ptr[0]);
  assertMessage(isValue || isblockStart);
  if (isblockStart) {
    Token *end = next_tok_balanced(allocator, slice_split(back, (1, -1))[0], opposite_scope(back.ptr[0]));
    if (!end)
      error_at(back.ptr->s.ptr, snprint(stdAlloc, "unclosed '{c8}'", back.ptr[0].s.ptr[0]));
    var_ slices = slice_split(back, (0, end - back.ptr + 1), (end - back.ptr + 1, -1));
    *backptr = slices[1];
    return parseBlock(allocator, slices[0]);
  } else {
    *backptr = (slice(Token)){back.len - 1, back.ptr + 1};
    return astAValue(allocator, back.ptr[0]);
  }
}
ast *parseExprPartial(AllocatorV allocator, ast *_Nullable current, slice(Token) back);
ast *parseExpr(AllocatorV allocator, slice(Token) tokens) { return parseExprPartial(allocator, nullptr, tokens); }
ast *parseTokList(AllocatorV allocator, ast *_Nullable current, slice(Token) back) {
  assertMessage(!current);
  if (back.len == 0) return aValue(allocator, (ast)tu_of(astEmptyStatement, {}));
  if (back.len == 1) return (assertMessage(isVal(slice_last(back))), astAValue(allocator, slice_last(back)));

  if (isVal(back.ptr[0])) {
    ast *head = astAValue(allocator, back.ptr[0]);
    var_ rest = (slice(Token)){back.len - 1, back.ptr + 1};
    __musttail___ return parseExprPartial(allocator, head, rest);
  }
  if (isBlockStart(back.ptr[0])) {
    Token *end = next_tok_balanced(allocator, slice_split(back, (1, -1))[0], opposite_scope(back.ptr[0]));
    if (!end)
      error_at(back.ptr->s.ptr, snprint(stdAlloc, "unclosed '{c8}'", back.ptr[0].s.ptr[0]));
    var_ block_slice = slice_split(back, (0, end - back.ptr + 1), (end - back.ptr + 1, -1));
    ast *block = parseBlock(allocator, block_slice[0]);
    __musttail___ return parseExprPartial(allocator, block, block_slice[1]);
  }

  if (back.ptr[0].kind == t_operator && Token_equal(back.ptr[0], t_dot)) {
    var_ rhs_slice = (slice(Token)){back.len - 1, back.ptr + 1};
    ast *rhs = parseExpr(allocator, rhs_slice);
    return aValue(allocator, (ast)tu_of(astOp, {.left = nullptr, .right = rhs, .symLiteral = "."}));
  } else if (back.ptr[0].kind == t_operator)
    error_at(back.ptr[0].s.ptr, snprint(stdAlloc, "{c8} needs operator to the left", back.ptr->s.ptr[0]));

  assertMessage(false, "%s", snprint(stdAlloc, "tlist : items fell through : {slice : Token}", back).ptr);
}
ast *parseExprPartial(AllocatorV allocator, ast *_Nullable current, slice(Token) back) {
  if (back.len == 0) return current ?: aValue(allocator, (ast)tu_of(astEmptyStatement, {}));
  if (!current) __musttail___ return parseTokList(allocator, nullptr, back);

  if (back.ptr[0].kind == t_operator) {
    var_ slices = slice_split(back, (1, -1), (2, -1));
    var_ no = next_op_balanced(allocator, slices[0]);
    var_ prec = getPrec(back.ptr[0]);
    var_ nextPrec = no.t ? getPrec(no.t[0]) : prec - 1;
    if (no.t && no.skipped_values < 2 && nextPrec < prec) {
      var_ scan = slice_split(back, (1, -1))[0];
      Token *start_ptr = scan.ptr;

      if (isBlockStart(scan.ptr[0])) {
        Token *end = next_tok_balanced(allocator, slice_split(scan, (1, -1))[0], opposite_scope(scan.ptr[0]));
        if (end) scan = (slice(Token)){scan.len - (end - scan.ptr + 1), end + 1};
        else scan = (slice(Token)){0, scan.ptr + scan.len};
      } else {
        scan = (slice(Token)){scan.len - 1, scan.ptr + 1};
      }

      while (scan.len > 0) {
        if (scan.ptr[0].kind == t_operator) {
          if (getPrec(scan.ptr[0]) >= prec) break;

          scan = (slice(Token)){scan.len - 1, scan.ptr + 1};

          if (scan.len > 0 && (isVal(scan.ptr[0]) || isBlockStart(scan.ptr[0]))) {
            if (isBlockStart(scan.ptr[0])) {
              Token *end = next_tok_balanced(allocator, slice_split(scan, (1, -1))[0], opposite_scope(scan.ptr[0]));
              if (end) scan = (slice(Token)){scan.len - (end - scan.ptr + 1), end + 1};
              else scan = (slice(Token)){0, scan.ptr + scan.len};
            } else {
              scan = (slice(Token)){scan.len - 1, scan.ptr + 1};
            }
          } else {
            break;
          }
        } else {
          break;
        }
      }

      var_ right_slice = (slice(Token)){scan.ptr - start_ptr, start_ptr};
      var_ rest_slice = (slice(Token)){back.ptr + back.len - scan.ptr, scan.ptr};

      var_ nextVal = (ast)tu_of(
          astOp,
          {
              .left = current,
              .right = parseExpr(allocator, right_slice),
              .symLiteral = "",
          }
      );
      memcpy(nextVal.astOp.symLiteral, back.ptr[0].s.ptr, back.ptr[0].s.len);

      __musttail___ return parseExprPartial(allocator, aValue(allocator, nextVal), rest_slice);
    } else {
      var_ nextVal = (ast)tu_of(
          astOp,
          {
              .left = current,
              .right = nullptr,
              .symLiteral = "",
          }
      );
      var_ slicew = slice_split(back, (1, -1))[0];
      nextVal.astOp.right = popVal(allocator, &slicew);
      memcpy(nextVal.astOp.symLiteral, back.ptr[0].s.ptr, back.ptr[0].s.len);
      __musttail___ return parseExprPartial(allocator, aValue(allocator, nextVal), slicew);
    }
  }

  if (isVal(back.ptr[0])) {
    var_ ops = popVal(allocator, &back);
    ast *r = parseExprPartial(allocator, ops, back);
    return aValue(allocator, (ast)tu_of(astOp, {.left = current, .right = r, .symLiteral = "( )"}));
  }

  if (isBlockStart(back.ptr[0])) {
    Token *end = next_tok_balanced(allocator, slice_split(back, (1, -1))[0], opposite_scope(back.ptr[0]));
    if (!end)
      error_at(back.ptr->s.ptr, snprint(stdAlloc, "unclosed '{c8}'", back.ptr[0].s.ptr[0]));
    var_ block_slice = slice_split(back, (0, end - back.ptr + 1), (end - back.ptr + 1, -1));
    ast *block = parseBlock(allocator, block_slice[0]);
    ast *new_current = aValue(allocator, (ast)tu_of(astOp, {.left = current, .right = block, .symLiteral = "( )"}));
    __musttail___ return parseExprPartial(allocator, new_current, block_slice[1]);
  }

  assertMessage(false, "%s", snprint(stdAlloc, "expr : items fell through : {slice : Token}", back).ptr);
}

// }

ast *parseDecl(AllocatorV allocator, slice(Token) tokens) { // ... : ... (;|= ...)?
  var_ decl = (astDecl){0};
  decl.name = aValue(allocator, (ast)tu_of(astId, tokens.ptr[0].s));

  if (Token_equal(tokens.ptr[2], t_colon)) {
    decl.is_const = 1;
    var_ val_slice = (slice(Token)){tokens.len - 3, tokens.ptr + 3};
    if (val_slice.len)
      decl.value = parseExpr(allocator, val_slice);
  } else if (Token_equal(tokens.ptr[2], t_equals)) {
    decl.is_const = 0;
    var_ val_slice = (slice(Token)){tokens.len - 3, tokens.ptr + 3};
    if (val_slice.len)
      decl.value = parseExpr(allocator, val_slice);
  } else {
    var_ rem_slice = (slice(Token)){tokens.len - 2, tokens.ptr + 2};

    Token *eq_tok = next_tok_balanced(allocator, rem_slice, t_equals);
    Token *colon_tok = next_tok_balanced(allocator, rem_slice, t_colon);

    if (eq_tok) {
      decl.is_const = 0;
      var_ type_slice = (slice(Token)){eq_tok - rem_slice.ptr, rem_slice.ptr};
      var_ val_slice =
          (slice(Token)){rem_slice.len - type_slice.len - 1, eq_tok + 1};
      if (type_slice.len)
        decl.type = parseExpr(allocator, type_slice);
      if (val_slice.len)
        decl.value = parseExpr(allocator, val_slice);
    } else if (colon_tok) {
      decl.is_const = 1;
      var_ type_slice =
          (slice(Token)){colon_tok - rem_slice.ptr, rem_slice.ptr};
      var_ val_slice =
          (slice(Token)){rem_slice.len - type_slice.len - 1, colon_tok + 1};
      if (type_slice.len)
        decl.type = parseExpr(allocator, type_slice);
      if (val_slice.len)
        decl.value = parseExpr(allocator, val_slice);
    } else {
      decl.is_const = 0;
      if (rem_slice.len)
        decl.type = parseExpr(allocator, rem_slice);
    }
  }

  return aValue(allocator, (ast)tu_of(astDecl, decl));
}

ast *parseBBlock(AllocatorV allocator, slice(Token) tokens) { // {...}
  if_decl (var_ t, next_tok_balanced(allocator, tokens, t_comma))
    error_at(t->s.ptr, snprint(stdAlloc, "comma not allowed here"));

  usize count = balanced_count(allocator, tokens, t_semi);
  var_ rp = aCreate(allocator, ast *, count + 3);
  rp[0] = aValue(allocator, (ast)tu_of(astBBlock, ((astBBlock){rp + 1})));
  foreach (var_ item, span(rp + 1, count)) {
    Token *nplace;
    nplace = next_tok_balanced(allocator, tokens, t_semi);
    var_ slice = (slice(Token)){nplace - tokens.ptr, tokens.ptr};
    tokens = (slice(Token)){tokens.len - (nplace - tokens.ptr) - 1, nplace + 1};

    item[0] = // a : b
        (slice.len > 2 && slice.ptr[0].kind == t_identifier &&
         Token_equal(slice.ptr[1], t_colon))
            ? parseDecl(allocator, slice)
            : parseExpr(allocator, slice);
  }
  if (tokens.len)
    error_at(tokens.ptr->s.ptr, snprint(stdAlloc, "trailing semicolon required"));

  return rp[0];
}
ast *parseABlock(AllocatorV allocator, slice(Token) tokens) { // [...]
  if_decl (var_ t, next_tok_balanced(allocator, tokens, t_semi))
    error_at(t->s.ptr, snprint(stdAlloc, "semicolon not allowed here"));

  usize count = balanced_count(allocator, tokens, t_comma);

  var_ rp = aCreate(allocator, ast *, count + 3);
  rp[0] = aValue(allocator, (ast)tu_of(astABlock, {rp + 1}));
  foreach (var_ item, span(rp + 1, count)) {
    Token *nplace = next_tok_balanced(allocator, tokens, t_comma);
    var_ slice = (slice(Token)){nplace - tokens.ptr, tokens.ptr};
    tokens = (slice(Token)){tokens.len - (nplace - tokens.ptr) - 1, nplace + 1};

    item[0] = // a : b
        (slice.len > 2 && slice.ptr[0].kind == t_identifier &&
         Token_equal(slice.ptr[1], t_colon))
            ? parseDecl(allocator, slice)
            : parseExpr(allocator, slice);
  }
  if (tokens.len)
    (rp + count + 1)[0] = // a : b
        (tokens.len > 2 && tokens.ptr[0].kind == t_identifier &&
         Token_equal(tokens.ptr[1], t_colon))
            ? parseDecl(allocator, tokens)
            : parseExpr(allocator, tokens);
  return rp[0];
}
ast *parsePBlock(AllocatorV allocator, slice(Token) tokens) { // (...)
  if_decl (var_ t, next_tok_balanced(allocator, tokens, t_semi))
    error_at(t->s.ptr, snprint(stdAlloc, "semicolon not allowed here"));

  usize count = balanced_count(allocator, tokens, t_comma);

  var_ rp = aCreate(allocator, ast *, count + 3);
  rp[0] = aValue(allocator, (ast)tu_of(astPBlock, {rp + 1}));
  foreach (var_ item, span(rp + 1, count)) {
    Token *nplace;
    nplace = next_tok_balanced(allocator, tokens, t_comma);
    var_ slice = (slice(Token)){nplace - tokens.ptr, tokens.ptr};
    tokens = (slice(Token)){tokens.len - (nplace - tokens.ptr) - 1, nplace + 1};

    item[0] = // a : b
        (slice.len > 2 && slice.ptr[0].kind == t_identifier &&
         Token_equal(slice.ptr[1], t_colon))
            ? parseDecl(allocator, slice)
            : parseExpr(allocator, slice);
  }
  if (tokens.len)
    (rp + count + 1)[0] = // a : b
        (tokens.len > 2 && tokens.ptr[0].kind == t_identifier &&
         Token_equal(tokens.ptr[1], t_colon))
            ? parseDecl(allocator, tokens)
            : parseExpr(allocator, tokens);
  return rp[0];
}
ast *parseBlock(AllocatorV allocator, slice(Token) tokens) {
  if (!tokens.len)
    return aValue(allocator, (ast)tu_of(astEmptyStatement, {}));
  var_ inner = slice_split(tokens, (1, tokens.len - 1))[0];
  if (Token_equal(tokens.ptr[0], t_lbrace))
    return parseBBlock(allocator, inner);
  if (Token_equal(tokens.ptr[0], t_lbracket))
    return parseABlock(allocator, inner);
  if (Token_equal(tokens.ptr[0], t_lparen))
    return parsePBlock(allocator, inner);

  error_at(tokens.ptr->s.ptr, snprint(stdAlloc, "expected block start token"));
  return nullptr;
}
#endif

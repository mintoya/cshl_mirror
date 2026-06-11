#include <string.h>
#if !defined(TOKENIZER_H1)
  #define TOKENIZER_H1 (1)
  #include "ast.h"
  #include "tok1.h"
  #include <wheels/macros.h>
  #include <wheels/mytypes.h>
  #include <wheels/sList.h>
  #include <wheels/tu_macros.h>

  #define __musttail___ __attribute__((musttail))

  #if defined(_WIN32) && defined(__clang__)
    #define TCO_ABI __attribute__((sysv_abi))
  #else
    #define TCO_ABI
  #endif

CONST_EXPR Token t_comma = (Token){(fptr){1, (u8[]){','}}, t_punctuation};
CONST_EXPR Token t_semi = (Token){(fptr){1, (u8[]){';'}}, t_punctuation};
CONST_EXPR Token t_colon = (Token){(fptr){1, (u8[]){':'}}, t_punctuation};
CONST_EXPR Token t_equals = (Token){(fptr){1, (u8[]){'='}}, t_operator};

CONST_EXPR Token t_dot = (Token){(fptr){1, (u8[]){'.'}}, t_operator};
CONST_EXPR Token t_not = (Token){(fptr){3, (u8[]){"not"}}, t_operator};
CONST_EXPR Token t_bnot = (Token){(fptr){4, (u8[]){"bnot"}}, t_operator};

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
bool isBlockStart(Token t) {
  return EQUAL_ANY(
      1,
      Token_equal(t, t_lbrace),
      Token_equal(t, t_lbracket),
      Token_equal(t, t_lparen),
  );
}
bool isBlockEnd(Token t) {
  return EQUAL_ANY(
      1,
      Token_equal(t, t_rbrace),
      Token_equal(t, t_rbracket),
      Token_equal(t, t_rparen),
  );
}
bool Token_nullable_left(Token t) {
  return EQUAL_ANY(
      1,
      Token_equal(t, t_dot),
      Token_equal(t, t_not),
      Token_equal(t, t_bnot),
  );
}
bool isVal(Token t) {
  switch (t.kind) {
    case t_identifier:
    case t_string:
    case t_number:
      return true;
    default:
      return false;
  }
}
Token opposite_end_scope(Token t) { // TODO something about this
  if (Token_equal(t, t_lbrace)) return t_rbrace;
  if (Token_equal(t, t_rbrace)) error_at(t.s.ptr, snprint(stdAlloc, "unexpected '{c8}'", (c8)t.s.ptr[0]));
  if (Token_equal(t, t_lbracket)) return t_rbracket;
  if (Token_equal(t, t_rbracket)) error_at(t.s.ptr, snprint(stdAlloc, "unexpected '{c8}'", (c8)t.s.ptr[0]));
  if (Token_equal(t, t_lparen)) return t_rparen;
  if (Token_equal(t, t_rparen)) error_at(t.s.ptr, snprint(stdAlloc, "unexpected '{c8}'", (c8)t.s.ptr[0]));
  assertMessage(false, "item isnt scope marker");
}

Token *next_tok_balanced(AllocatorV allocator, slice(Token) tokens, Token t) {
  var_ sb = msList_stackBuffer(Token[32]);
  var_ ts = msList_initBuffer(sb);
  defer { msList_deInit(allocator, ts); };

  foreach (var_ tp, span(tokens.ptr, tokens.len))
    if (msList_len(ts)) {
      if (isScope(tp[0])) {
        if (Token_equal(tp[0], opposite_end_scope(msList_last(ts))))
          msList_pop(ts);
        else msList_push(allocator, ts, tp[0]);
      }
    } else if (Token_equal(tp[0], t))
      return tp;
    else if (isScope(tp[0])) msList_push(allocator, ts, tp[0]);

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

  for (var_ tp = tokens.ptr; tp < tokens.ptr + tokens.len; tp++)
    if (msList_len(ts)) {
      if (isScope(tp[0])) {
        if (Token_equal(tp[0], opposite_end_scope(msList_last(ts))))
          msList_pop(ts);
        else msList_push(allocator, ts, tp[0]);
      }
    } else if (tp->kind == t_operator) return (struct next_op_result){tp, skipped};
    else if (isScope(tp[0])) {
      msList_push(allocator, ts, tp[0]);
      var_ in = tp > tokens.ptr && (tp - 1)->s.ptr + (tp - 1)->s.len == tp->s.ptr;
      if (!in) skipped++;
    } else if (isVal(tp[0])) {
      skipped++;
    }

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
      if (isScope(tp[0])) {
        if (Token_equal(tp[0], opposite_end_scope(msList_last(ts))))
          msList_pop(ts);
        else msList_push(allocator, ts, tp[0]);
      }
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

  // { operations
  #define slice_first(slice) ({var_ _slice = slice ; _slice.ptr[0]; })

ast *popVal(AllocatorV allocator, slice(Token) * backptr) {
  var_ back = *backptr;
  assertMessage(back.len);

  bool isblockStart = isBlockStart(back.ptr[0]);
  bool isValue = isVal(back.ptr[0]);

  if (isblockStart) {
    Token *end = next_tok_balanced(allocator, slice_split(back, (1, -1))[0], opposite_end_scope(back.ptr[0]));
    if (!end)
      error_at(back.ptr->s.ptr, snprint(stdAlloc, "unclosed '{c8}'", back.ptr[0].s.ptr[0]));
    var_ slices = slice_split(back, (0, end - back.ptr + 1), (end - back.ptr + 1, -1));
    *backptr = slices[1];
    return parseBlock(allocator, slices[0]);
  } else if (isValue) {
    var_ a = astAValue(allocator, back.ptr[0]);
    var_ nh = slice_split(back, (1, -1))[0];

    while (nh.len && isBlockStart(nh.ptr[0])) {
      var_ parens = popVal(allocator, &nh);
      a = aValue(allocator, (ast)tu_of(astOp, {a, parens, "()"}));
    }

    *backptr = nh;
    return a;
  } else
    error_at(back.ptr[0].s.ptr, snprint(stdAlloc, "compiler expected value here"));
  return nullptr;
}
TCO_ABI ast *parseExprPartial(AllocatorV allocator, ast *_Nullable current, slice(Token) back);
TCO_ABI ast *parseExpr(AllocatorV allocator, slice(Token) tokens) { return parseExprPartial(allocator, nullptr, tokens); }
TCO_ABI ast *parseTokList(AllocatorV allocator, ast *_Nullable current, slice(Token) back) {
  assertMessage(!current);
  if (back.len == 0) return aValue(allocator, (ast)tu_of(astEmptyStatement, {}));
  if (Token_equal(back.ptr[0], t_colon)) error_at(back.ptr[0].s.ptr, snprint(stdAlloc, "exected value here"));
  if (back.len == 1) {
    if (isVal(slice_last(back)))
      return astAValue(allocator, slice_last(back));
    else error_at(back.ptr[0].s.ptr, snprint(stdAlloc, "expected value"));
  }

  if (isVal(back.ptr[0])) {
    ast *head = astAValue(allocator, back.ptr[0]);
    var_ rest = (slice(Token)){back.len - 1, back.ptr + 1};
    __musttail___ return parseExprPartial(allocator, head, rest);
  }
  if (isBlockStart(back.ptr[0])) {
    Token *end = next_tok_balanced(allocator, slice_split(back, (1, -1))[0], opposite_end_scope(back.ptr[0]));
    if (!end)
      error_at(back.ptr->s.ptr, snprint(stdAlloc, "unclosed '{c8}'", back.ptr[0].s.ptr[0]));
    var_ block_slice = slice_split(back, (0, end - back.ptr + 1), (end - back.ptr + 1, -1));
    ast *block = parseBlock(allocator, block_slice[0]);
    __musttail___ return parseExprPartial(allocator, block, block_slice[1]);
  }

  if (back.ptr[0].kind == t_operator && Token_nullable_left(back.ptr[0])) {
    ast *rhs = parseExpr(allocator, slice_split(back, (1, -1))[0]);
    var_ r = aValue(allocator, (ast)tu_of(astOp, {nullptr, rhs}));
    memcpy(r->astOp.symLiteral, back.ptr->s.ptr, back.ptr->s.len);
    return r;
  } else if (back.ptr[0].kind == t_operator)
    error_at(back.ptr[0].s.ptr, snprint(stdAlloc, "{slice(c8)} needs operator to the left", back.ptr->s));

  assertMessage(false, "%s", snprint(stdAlloc, "tlist : items fell through : {slice : Token}", back).ptr);
}
ast *parseOperations(AllocatorV allocator, ast *current, slice(Token) * backp) {
  assertMessage(backp);
  var_ back = *backp;
  assertMessage(current && back.len && back.ptr[0].kind == t_operator);

  var_ astbuffer = msList_stackBuffer(ast *[8]);
  var_ opsbuffer = msList_stackBuffer(Token[8]);
  var_ astList = msList_initBuffer(astbuffer);
  var_ opsList = msList_initBuffer(opsbuffer);
  defer { msList_deInit(allocator, astList); };
  defer { msList_deInit(allocator, opsList); };

  msList_push(allocator, astList, current);
  // clang-format off
  enum { astM, opM } tx = opM;
  while (back.len) {
    switch (tx) {
      case astM:  msList_push(allocator, astList, popVal(allocator, &back));  break;
      case opM: {
        if (back.ptr[0].kind != t_operator) goto label;
        msList_push(allocator, opsList, back.ptr[0]);
        back = slice_split(back, (1, -1))[0];
      } break;
    }
    tx = tx == astM ? opM : astM;
  }
  // clang-format on
  {label : };
  while (msList_len(opsList)) {
    usize min = msList_len(opsList);
    usize mp = -1;
    foreach (var_ op, pair(i, *msList_vla(opsList))) {
      usize p = getPrec(op);
      min = mp >= p ? i : min;
      mp = mp >= p ? p : mp;
    }

    var_ opNode = aValue(
        allocator,
        (ast)tu_of(astOp, {astList[min], astList[min + 1]})
    );
    memcpy(opNode->astOp.symLiteral, opsList[min].s.ptr, opsList[min].s.len);

    astList[min] = opNode;
    msList_rem(astList, min + 1);
    msList_rem(opsList, min);
  }

  *backp = back;
  return astList[0];
}
TCO_ABI ast *parseExprPartial(AllocatorV allocator, ast *_Nullable current, slice(Token) back) {
  if (!back.len) return current ?: aValue(allocator, (ast)tu_of(astEmptyStatement, {}));
  if (!current) __musttail___ return parseTokList(allocator, nullptr, back);
  if (Token_equal(back.ptr[0], t_colon)) error_at(back.ptr[0].s.ptr, snprint(stdAlloc, "exected value here"));

  if (back.ptr[0].kind == t_operator) {
    ast *nc = parseOperations(allocator, current, &back);
    __musttail___ return parseExprPartial(allocator, nc, back);
  }

  if (isVal(back.ptr[0]) || isBlockStart(back.ptr[0])) {
    var_ ops = popVal(allocator, &back);
    ast *r = parseExprPartial(allocator, ops, back);
    return aValue(allocator, (ast)tu_of(astOp, {current, r, "()"}));
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
    Token *nplace = next_tok_balanced(allocator, tokens, t_semi);
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
    Token *nplace = next_tok_balanced(allocator, tokens, t_comma);
    assertMessage(nplace);
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

#pragma once
#include "ast.h"
#include <wheels/macros.h>
#include <wheels/mytypes.h>
#include <wheels/sList.h>

#define __musttail___ __attribute__((musttail))

#if defined(_WIN32) && defined(__clang__)
  #define TCO_ABI __attribute__((sysv_abi))
#else
  #define TCO_ABI
#endif

__attribute__((noreturn)) void error_Tok(Token t, slice(c8) msg) {
  error_at(t.s.ptr, msg);
}

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
  if (Token_equal(t, t_rbrace)) error_Tok(t, snprint(stdAlloc, "unexpected '{c8}'", (c8)t.s.ptr[0]));
  if (Token_equal(t, t_lbracket)) return t_rbracket;
  if (Token_equal(t, t_rbracket)) error_Tok(t, snprint(stdAlloc, "unexpected '{c8}'", (c8)t.s.ptr[0]));
  if (Token_equal(t, t_lparen)) return t_rparen;
  if (Token_equal(t, t_rparen)) error_Tok(t, snprint(stdAlloc, "unexpected '{c8}'", (c8)t.s.ptr[0]));
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
    error_Tok(msList_last(ts), snprint(stdAlloc, "unclosed {c8}", (char)msList_last(ts).s.ptr[0]));
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
    error_Tok(msList_last(ts), snprint(stdAlloc, "unclosed {c8}", (char)msList_last(ts).s.ptr[0]));

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
    error_Tok(msList_last(ts), snprint(stdAlloc, "unclosed {c8}", (char)msList_last(ts).s.ptr[0]));
  return count;
}
usize getPrec(Token t) {
  assertMessage(t.kind == t_operator);

  foreach (var_ op, vla(operations))
    if (fptr_eq(fp(op.name), t.s))
      return op.prec + 1;

  error_Tok(
      t,
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
ast *popVal(AllocatorV allocator, slice(Token) * backptr) {
  var_ back = *backptr;
  assertMessage(back.len);

  bool isblockStart = isBlockStart(back.ptr[0]);
  bool isValue = isVal(back.ptr[0]);

  if (isblockStart) {
    Token *end = next_tok_balanced(allocator, slice_split(back, (1, -1))[0], opposite_end_scope(back.ptr[0]));
    if (!end)
      error_Tok(back.ptr[0], snprint(stdAlloc, "unclosed '{c8}'", back.ptr[0].s.ptr[0]));
    var_ slices = slice_split(back, (0, end - back.ptr + 1), (end - back.ptr + 1, -1));
    *backptr = slices[1];
    return parseBlock(allocator, slices[0]);
  } else if (isValue) {
    var_ a = astAValue(allocator, back.ptr[0]);
    var_ nh = slice_split(back, (1, -1))[0];

    // only pop it as part of the value if they are directly next to eachother
    // so in a(b) + x a(b) binds tight as possible
    while (nh.len && isBlockStart(nh.ptr[0]) && nh.ptr[0].s.ptr == P$(back.ptr[0].s, $.ptr + $.len)) {
      var_ parens = popVal(allocator, &nh);
      a = aValue(allocator, (ast)tu_of(astOp, {a, parens, "()"}));
    }

    *backptr = nh;
    return a;
  } else
    error_Tok(back.ptr[0], snprint(stdAlloc, "compiler expected value here"));
  return nullptr;
}
TCO_ABI ast *parseExprPartial(AllocatorV allocator, ast *_Nullable current, slice(Token) back);
TCO_ABI ast *parseExpr(AllocatorV allocator, slice(Token) tokens) {
  if_decl (var_ eq_tok, next_tok_balanced(allocator, tokens, t_equals)) {
    usize idx = eq_tok - tokens.ptr;
    var_ s = slice_split(tokens, (0, idx), (idx + 1, -1));
    var_ left = s[0];
    var_ right = s[1];

    ast *l_ast = parseExpr(allocator, left);
    ast *r_ast = parseExpr(allocator, right);

    var_ opNode = aValue(allocator, (ast)tu_of(astOp, {l_ast, r_ast}));
    memcpy(opNode->astOp.symLiteral, eq_tok->s.ptr, eq_tok->s.len);
    return opNode;
  } else return parseExprPartial(allocator, nullptr, tokens);
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
    var_ list = msList_vla(opsList);
    foreach (var_ opp, span(*list, countof(*list))) {
      var_ op = *opp;
      var_ i = opp - *list;
      usize p = getPrec(op);
      var_ mo = mp > p;
      if (EQUAL_ANY(1, Token_equal(op, t_equals), ))
        mo |= mp == p;
      min = mo ? i : min;
      mp = mo ? p : mp;
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
  if (!current) {
    if (back.ptr[0].kind == t_operator && Token_nullable_left(back.ptr[0])) {
      ast *rhs = parseExpr(allocator, slice_split(back, (1, -1))[0]);
      var_ r = aValue(allocator, (ast)tu_of(astOp, {nullptr, rhs}));
      memcpy(r->astOp.symLiteral, back.ptr->s.ptr, back.ptr->s.len);
      return r;
    } else if (back.ptr[0].kind == t_operator)
      error_Tok(back.ptr[0], snprint(stdAlloc, "{slice(c8)} needs operator to the left", back.ptr->s));
    else current = popVal(allocator, &back);
  }
  if (!back.len) return current ?: aValue(allocator, (ast)tu_of(astEmptyStatement, {}));

  if (back.ptr[0].kind == t_operator && Token_nullable_left(back.ptr[0])) {
  }
  current = current ?: popVal(allocator, &back);
  if (!back.len) return current ?: aValue(allocator, (ast)tu_of(astEmptyStatement, {}));

  if (Token_equal(back.ptr[0], t_colon)) error_Tok(back.ptr[0], snprint(stdAlloc, "exected value here"));

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

ast *parseBBlock(AllocatorV allocator, u8 *loc, slice(Token) tokens) { // {...}
  if_decl (var_ t, next_tok_balanced(allocator, tokens, t_comma))
    error_Tok(t[0], snprint(stdAlloc, "comma not allowed here"));

  usize count = balanced_count(allocator, tokens, t_semi);
  var_ rp = aCreate(allocator, ast *, count + 3);
  rp[0] = aValue(allocator, (ast)tu_of(astBBlock, ((astBBlock){loc, rp + 1})));
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
    error_Tok(tokens.ptr[0], snprint(stdAlloc, "trailing semicolon required"));

  return rp[0];
}
ast *parseABlock(AllocatorV allocator, u8 *loc, slice(Token) tokens) { // [...]
  if_decl (var_ t, next_tok_balanced(allocator, tokens, t_semi))
    error_Tok(t[0], snprint(stdAlloc, "semicolon not allowed here"));

  usize count = balanced_count(allocator, tokens, t_comma);

  var_ rp = aCreate(allocator, ast *, count + 3);
  rp[0] = aValue(allocator, (ast)tu_of(astABlock, {loc, rp + 1}));
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
ast *parsePBlock(AllocatorV allocator, u8 *loc, slice(Token) tokens) { // (...)
  if_decl (var_ t, next_tok_balanced(allocator, tokens, t_semi))
    error_Tok(t[0], snprint(stdAlloc, "semicolon not allowed here"));

  usize count = balanced_count(allocator, tokens, t_comma);

  var_ rp = aCreate(allocator, ast *, count + 3);
  rp[0] = aValue(allocator, (ast)tu_of(astPBlock, {loc, rp + 1}));
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
  var_ first_char = tokens.ptr[0].s.ptr;
  if (Token_equal(tokens.ptr[0], t_lbrace))
    return parseBBlock(allocator, first_char, inner);
  if (Token_equal(tokens.ptr[0], t_lbracket))
    return parseABlock(allocator, first_char, inner);
  if (Token_equal(tokens.ptr[0], t_lparen))
    return parsePBlock(allocator, first_char, inner);

  error_Tok(tokens.ptr[0], snprint(stdAlloc, "expected block start token"));
  return nullptr;
}

#include "tok1.h"
#include "wheels/macros.h"
#include "wheels/mytypes.h"
#include "wheels/sList.h"
#include "wheels/smallstream.h"
#include "wheels/tu_macros.h"
#include <string.h>
#if !defined(TOKENIZER_H1)
  #define TOKENIZER_H1 (1)
  #include "ast.h"

  // a file is a list of declarations
  #define __musttail___ __attribute__((musttail))
CONST_EXPR Token t_comma = (Token){(fptr){1, (u8[]){','}}, t_punctuation};
CONST_EXPR Token t_semi = (Token){(fptr){1, (u8[]){';'}}, t_punctuation};
CONST_EXPR Token t_colon = (Token){(fptr){1, (u8[]){':'}}, t_punctuation};
CONST_EXPR Token t_equals = (Token){(fptr){1, (u8[]){'='}}, t_operator};

CONST_EXPR Token t_for = (Token){(fptr){3, (u8[]){"for"}}, t_operator};
CONST_EXPR Token t_if = (Token){(fptr){2, (u8[]){"if"}}, t_operator};
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
        else
          msList_push(allocator, ts, tp[0]);
    } else if (Token_equal(tp[0], t))
      count++;
    else if (isScope(tp[0]))
      msList_push(allocator, ts, tp[0]);

  if (msList_len(ts))
    error_at(msList_last(ts).s.ptr, snprint(stdAlloc, "unclosed {c8}", (char)msList_last(ts).s.ptr[0]));
  return count;
}
bool isVal(Token t) {
  return EQUAL_ANY(t.kind, t_identifier, t_number, t_string);
}
usize getPrec(Token t) {
  assertMessage(t.kind == t_operator);

  foreach (var_ op, vla(operations))
    if (fptr_eq(fp(op.name), t.s))
      return op.prec;

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
  #define slice_last(slice) ({var_ _slice = slice ; _slice.ptr[_slice.len-1]; })
  #define slice_first(slice) ({var_ _slice = slice ; _slice.ptr[0]; })

ast *reduce_front(AllocatorV allocator, slice(Token) * front) {
  if (!front->len) return nullptr;
  Token last = front->ptr[front->len - 1];

  if (isVal(last)) {
    front->len--;
    return astAValue(allocator, last);
  }

  if (!isScope(last)) {
    error_at(last.s.ptr, snprint(stdAlloc, "expected value or block end, got '{slice(c8)}'", last.s));
    return nullptr;
  }

  var_ sb = msList_stackBuffer(Token[32]);
  var_ ts = msList_initBuffer(sb);
  defer { msList_deInit(allocator, ts); };

  msList_push(allocator, ts, last);

  usize i = front->len - 1;
  while (i > 0 && msList_len(ts)) {
    i--;
    Token t = front->ptr[i];
    if (isScope(t)) {
      if (Token_equal(t, opposite_scope(msList_last(ts))))
        msList_pop(ts);
      else
        msList_push(allocator, ts, t);
    }
  }

  if (msList_len(ts))
    error_at(msList_last(ts).s.ptr, snprint(stdAlloc, "unclosed {c8}", (char)msList_last(ts).s.ptr[0]));

  slice(Token) block_slice = {front->len - i, front->ptr + i};
  front->len = i;

  return parseBlock(allocator, block_slice);
}

ast *reduce_back(AllocatorV allocator, slice(Token) * back) {
  if (!back->len) return nullptr;
  Token first = back->ptr[0];

  if (isVal(first)) {
    back->ptr++;
    back->len--;
    return astAValue(allocator, first);
  }

  if (!isBlockStart(first)) {
    error_at(first.s.ptr, snprint(stdAlloc, "expected value or block start, got '{slice(c8)}'", first.s));
    return nullptr;
  }

  var_ sb = msList_stackBuffer(Token[32]);
  var_ ts = msList_initBuffer(sb);
  defer { msList_deInit(allocator, ts); };

  msList_push(allocator, ts, first);

  usize i = 1;
  for (; i < back->len && msList_len(ts); i++) {
    Token t = back->ptr[i];
    if (isScope(t)) {
      if (Token_equal(t, opposite_scope(msList_last(ts))))
        msList_pop(ts);
      else
        msList_push(allocator, ts, t);
    }
  }

  if (msList_len(ts))
    error_at(msList_last(ts).s.ptr, snprint(stdAlloc, "unclosed {c8}", (char)msList_last(ts).s.ptr[0]));

  slice(Token) block_slice = {i, back->ptr};
  back->ptr += i;
  back->len -= i;

  return parseBlock(allocator, block_slice);
}

ast *parseExpr_front(AllocatorV allocator, slice(Token) front, ast *_Nullable current, slice(Token) back) {
  assertMessage(back.len == 0);

  if (!current) {
    if (front.len && Token_equal(front.ptr[0], t_dot)) {
      current = nullptr; // Explicitly allow prefix dot operations
    } else {
      current = reduce_back(allocator, &front);
      if (!current) return nullptr;
    }
  }

  if (!front.len) return current;

  var_ t = front.ptr[0];
  switch (t.kind) {
    case t_operator: {
      if (Token_equal(t, t_dot)) {
      }
      front.ptr++;
      front.len--;

      if (!front.len) {
        error_at(t.s.ptr, snprint(stdAlloc, "expected expression after '{slice(c8)}'", t.s));
        return nullptr;
      }

      ast *right = reduce_back(allocator, &front);
      if (!right) return nullptr;

      ast *new_current = aValue(
          allocator,
          (ast)tu_of(astOp, {
                                .left = current,
                                .right = right,
                            })
      );
      assertMessage(t.s.len < sizeof(new_current->astOp.symLiteral));
      memcpy(new_current->astOp.symLiteral, t.s.ptr, t.s.len);
      __musttail___ return parseExpr_front(allocator, front, new_current, back);
    } break;
    case t_punctuation: {
      if (isBlockStart(t)) {
        ast *right = reduce_back(allocator, &front);
        if (!right) return nullptr;
        ast *new_current = aValue(
            allocator,
            (ast)tu_of(astOp, {.left = current, .right = right, .symLiteral = "( )"})
        );
        __musttail___ return parseExpr_front(allocator, front, new_current, back);
      }
      error_at(t.s.ptr, snprint(stdAlloc, "punctuation means nothing here"));
      return nullptr;
    } break;
    default: {
      ast *right = reduce_back(allocator, &front);
      if (!right) return nullptr;
      ast *new_current = aValue(
          allocator,
          (ast)tu_of(astOp, {.left = current, .right = right, .symLiteral = "( )"})
      );
      __musttail___ return parseExpr_front(allocator, front, new_current, back);
    } break;
  }
}

ast *parseExpr_back(AllocatorV allocator, slice(Token) front, ast *_Nullable current, slice(Token) back) {
  assertMessage(front.len == 0);

  if (!current) {
    current = reduce_front(allocator, &back);
    if (!current) return nullptr;
  }

  if (!back.len) return current;

  var_ t = back.ptr[back.len - 1];
  switch (t.kind) {
    case t_operator: {
      if (Token_equal(t, t_dot)) {
      }
      back.len--;

      if (!back.len) {
        error_at(t.s.ptr, snprint(stdAlloc, "expected expression before '{slice(c8)}'", t.s));
        return nullptr;
      }

      ast *left = reduce_front(allocator, &back);
      if (!left) return nullptr;

      ast *new_current = aValue(
          allocator,
          (ast)tu_of(astOp, {
                                .left = left,
                                .right = current,
                            })
      );
      assertMessage(t.s.len < sizeof(new_current->astOp.symLiteral));
      memcpy(new_current->astOp.symLiteral, t.s.ptr, t.s.len);
      __musttail___ return parseExpr_back(allocator, front, new_current, back);
    } break;
    case t_punctuation: {
      if (isScope(t)) {
        ast *left = reduce_front(allocator, &back);
        if (!left) return nullptr;
        ast *new_current = aValue(
            allocator,
            (ast)tu_of(astOp, {.left = left, .right = current, .symLiteral = "( )"})
        );
        __musttail___ return parseExpr_back(allocator, front, new_current, back);
      }
      error_at(t.s.ptr, snprint(stdAlloc, "punctuation means nothing here"));
      return nullptr;
    } break;
    default: {
      ast *left = reduce_front(allocator, &back);
      if (!left) return nullptr;
      ast *new_current = aValue(
          allocator,
          (ast)tu_of(astOp, {.left = left, .right = current, .symLiteral = "( )"})
      );
      __musttail___ return parseExpr_back(allocator, front, new_current, back);
    } break;
  }
}

ast *parseExpr_Ops(AllocatorV allocator, slice(Token) front, ast *_Nullable current, slice(Token) back) {
  assertMessage(front.len && back.len);
  assertMessage(
      EQUAL_ALL(
          t_operator,
          front.ptr[front.len - 1].kind,
          back.ptr[0].kind
      )
  );
  if (!current) goto double_operator;
  // ---------
  //
  //
  var_ front_prec = getPrec(slice_last(front));
  var_ back_prec = getPrec(slice_first(back));

  if (front_prec <= back_prec)
    return aValue(
        allocator,
        (ast)tu_of(astOp, {})
    );
  else
    return aValue(
        allocator,
        (ast)tu_of(astOp, {})
    );

  //
  //
  // ---------
  {cancel : } error_at(
      front.ptr[0].s.ptr,
      snprint(stdAlloc, "unhandled parseExpr case :\n{slice : Token}\n{ast}\n{slice : Token}", front, current, back)
  );

  {double_operator : } error_at(
      front.ptr[front.len].s.ptr,
      snprint(stdAlloc, "compound operators not supported yet")
  );
}

ast *parseExpr2(AllocatorV allocator, slice(Token) front, ast *_Nullable current, slice(Token) back) {
  if (!back.len && !front.len) {
    if (!current)
      return aValue(allocator, (ast)tu_of(astEmptyStatement, {}));
    return current;
  }

  if (front.len && (Token_equal(front.ptr[0], t_for) || Token_equal(front.ptr[0], t_if))) {

    slice(Token) rem = {front.len - 1, front.ptr + 1};
    if (!rem.len) error_at(front.ptr[0].s.ptr, snprint(stdAlloc, "if/for needs a body"));

    usize split_idx = 0;
    if (Token_equal(rem.ptr[0], t_lparen)) {
      slice(Token) temp = rem;
      reduce_back(allocator, &temp);
      split_idx = rem.len - temp.len;
    } else {
      slice(Token) temp = rem;
      reduce_back(allocator, &temp);

      while (temp.len && (Token_equal(temp.ptr[0], t_lparen) || Token_equal(temp.ptr[0], t_lbracket))) {
        reduce_back(allocator, &temp);
      }

      while (temp.len && temp.ptr[0].kind == t_operator) {
        temp.ptr++;
        temp.len--;
        reduce_back(allocator, &temp);

        while (temp.len && (Token_equal(temp.ptr[0], t_lparen) || Token_equal(temp.ptr[0], t_lbracket))) {
          reduce_back(allocator, &temp);
        }
      }
      split_idx = rem.len - temp.len;
    }

    slice(Token) cond_slice = {split_idx, rem.ptr};
    slice(Token) body_slice = {rem.len - split_idx, rem.ptr + split_idx};

    ast *cond = cond_slice.len ? parseExpr2(allocator, cond_slice, nullptr, (slice(Token)){}) : nullptr;
    ast *body = body_slice.len ? parseExpr2(allocator, body_slice, nullptr, (slice(Token)){}) : nullptr;

    var_ res = aValue(
        allocator,
        (ast)tu_of(
            astOp,
            {
                .left = cond,
                .right = body,
            }
        )
    );
    (Token_equal(front.ptr[0], t_for))
        ? memcpy(res->astOp.symLiteral, "for", 3)
        : memcpy(res->astOp.symLiteral, "if", 2);
    return res;
  }

  if (!back.len)
    __musttail___ return parseExpr_front(allocator, front, current, back);
  if (!front.len)
    __musttail___ return parseExpr_back(allocator, front, current, back);

  return parseExpr_Ops(allocator, front, current, back);
}
// }

ast *parseExpr(AllocatorV allocator, slice(Token) tokens) { return parseExpr2(allocator, tokens, nullptr, (slice(Token)){}); }

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
  usize count = balanced_count(allocator, tokens, t_semi);

  {
    var_ t = next_tok_balanced(allocator, tokens, t_comma);
    if (t)
      error_at(t->s.ptr, snprint(stdAlloc, "comma not allowed here"));
  }
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
  {
    var_ t = next_tok_balanced(allocator, tokens, t_semi);
    if (t)
      error_at(t->s.ptr, snprint(stdAlloc, "semicolon not allowed here"));
  }

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
  {
    var_ t = next_tok_balanced(allocator, tokens, t_semi);
    if (t)
      error_at(t->s.ptr, snprint(stdAlloc, "semicolon not allowed here"));
  }

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
    return NULL;
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

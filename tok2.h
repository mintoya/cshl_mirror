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

CONST_EXPR Token t_comma = (Token){(fptr){1, (u8[]){','}}, t_punctuation};
CONST_EXPR Token t_semi = (Token){(fptr){1, (u8[]){';'}}, t_punctuation};
CONST_EXPR Token t_colon = (Token){(fptr){1, (u8[]){':'}}, t_punctuation};
CONST_EXPR Token t_equals = (Token){(fptr){1, (u8[]){'='}}, t_operator};

CONST_EXPR Token t_for = (Token){(fptr){3, (u8[]){"for"}}, t_operator};
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

ast *parseBlock(AllocatorV allocator, slice(Token) tokens);

bool isBlockStart(Token t) {
  return EQUAL_ANY(
      1,
      Token_equal(t, t_lbrace),
      Token_equal(t, t_lbracket),
      Token_equal(t, t_lparen),
  );
}

// { operations
Token *find_root_op(AllocatorV allocator, slice(Token) tokens) {
  Token *best = NULL;
  int best_prec = ((uint)-1) >> 1;

  var_ sb = msList_stackBuffer(Token[32]);
  var_ ts = msList_initBuffer(sb);
  defer { msList_deInit(allocator, ts); };

  foreach (var_ tp, span(tokens.ptr, tokens.len)) {
    if (msList_len(ts)) {
      if (isScope(tp[0]))
        if (Token_equal(tp[0], opposite_scope(msList_last(ts))))
          msList_pop(ts);
        else
          msList_push(allocator, ts, tp[0]);
      continue;
    }
    if (isBlockStart(tp[0])) {
      msList_push(allocator, ts, tp[0]);
      continue;
    }
    if (tp[0].kind != t_operator)
      continue;

    for (usize j = 0; j < countof(operations); j++) {
      if (fptr_eq(tp[0].s, fp(operations[j].name))) {
        if ((int)operations[j].prec < best_prec) {
          best_prec = (int)operations[j].prec;
          best = tp;
        }
        break;
      }
    }
  }
  return best;
}
ast *parse_adj_chain(AllocatorV allocator, slice(Token) tokens) {
  if (!tokens.len)
    return NULL;

  var_ sb = msList_stackBuffer(ast *[32]);
  var_ atoms = msList_initBuffer(sb);
  defer { msList_deInit(allocator, atoms); };

  usize i = 0;
  while (i < tokens.len) {
    Token t = tokens.ptr[i];
    if (isBlockStart(t)) {
      Token close = opposite_scope(t);
      usize depth = 1, j = i + 1;
      for (; j < tokens.len && depth; j++) {
        if (isBlockStart(tokens.ptr[j]))
          depth++;
        else if (Token_equal(tokens.ptr[j], close))
          depth--;
      }
      msList_push(
          allocator, atoms, parseBlock(allocator, ((slice(Token)){j - i, tokens.ptr + i}))
      );
      i = j;
    } else if (isVal(t)) {
      ast *node =
          t.kind == t_identifier ? aValue(allocator, (ast)tu_of(astId, t.s))
          : t.kind == t_number   ? aValue(allocator, (ast)tu_of(astNumber, t.s))
                                 : aValue(allocator, (ast)tu_of(astString, t.s));
      msList_push(allocator, atoms, node);
      i++;
    } else {
      error_at(t.s.ptr, snprint(stdAlloc, "unexpected token in expression"));
      i++;
    }
  }

  usize n = msList_len(atoms);
  if (!n)
    return NULL;
  ast **a = (ast **)atoms;
  ast *result = a[n - 1];
  for (usize k = n - 1; k-- > 0;)
    result = aValue(allocator, (ast)tu_of(astOp, {a[k], result, "( )"}));
  return result;
}
ast *parseExpr1(AllocatorV allocator, slice(Token) tokens) {
  if (!tokens.len)
    return NULL;

  Token *op = find_root_op(allocator, tokens);
  if (!op)
    return parse_adj_chain(allocator, tokens);

  // special case: for <cond> <body>
  // right side is parsed as adjacency chain, last atom = body, rest = cond
  if (Token_equal(*op, t_for)) {
    slice(Token)
        right_slice = {tokens.len - (usize)(op - tokens.ptr) - 1, op + 1};
    if (!right_slice.len)
      error_at(op->s.ptr, snprint(stdAlloc, "for needs a body"));

    // find start of last top-level atom — that's the body
    usize body_start = 0, i = 0;
    while (i < right_slice.len) {
      body_start = i;
      Token t = right_slice.ptr[i];
      if (isBlockStart(t)) {
        Token close = opposite_scope(t);
        usize depth = 1;
        i++;
        for (; i < right_slice.len && depth; i++) {
          if (isBlockStart(right_slice.ptr[i]))
            depth++;
          else if (Token_equal(right_slice.ptr[i], close))
            depth--;
        }
      } else {
        i++;
      }
    }

    slice(Token) cond_slice = {body_start, right_slice.ptr};
    slice(Token) body_slice = {right_slice.len - body_start, right_slice.ptr + body_start};

    astOp node = {
        .left = cond_slice.len ? parseExpr1(allocator, cond_slice) : NULL,
        .right = parseExpr1(allocator, body_slice),
        .symLiteral = "for",
    };
    return aValue(allocator, (ast)tu_of(astOp, node));
  }

  slice(Token) left_slice = {(usize)(op - tokens.ptr), tokens.ptr};
  slice(Token)
      right_slice = {tokens.len - (usize)(op - tokens.ptr) - 1, op + 1};

  astOp node = {
      .left = left_slice.len ? parseExpr1(allocator, left_slice) : nullptr,
      .right = right_slice.len ? parseExpr1(allocator, right_slice) : nullptr,
      .symLiteral = "",
  };
  assertMessage(op->s.len < 8);
  memcpy(op->s.ptr, node.symLiteral, op->s.len);

  return aValue(allocator, (ast)tu_of(astOp, node));
}
// }
// { operations2

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
ast *astValue(AllocatorV allocator, Token t) {
  switch (t.kind) {
    case t_identifier:
      return aValue(allocator, (ast)tu_of(astId, t.s));
    case t_string:
      return aValue(allocator, (ast)tu_of(astString, t.s));
    case t_number:
      return aValue(allocator, (ast)tu_of(astNumber, t.s));
  }
}
ast *parseExpr2(AllocatorV allocator, slice(Token) front, ast *_Nullable current, slice(Token) back) {
  if (!back.len && !front.len) return current;

  if (!current) {
  }

  if (!back.len) goto cancel;
  if (!front.len) goto cancel;
  if (Token_equal(front.ptr[0], t_for)) goto cancel;

  {cancel : } error_at(
      front.ptr[0].s.ptr,
      snprint(stdAlloc, "unhandled parseExpr case :\n{slice : Token}\n{ast}\n{slice : Token}", front, current, back)
  );
}
// }

// ast *parseExpr(AllocatorV allocator, slice(Token) tokens) { return parseExpr1(allocator, tokens); }
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

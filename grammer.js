module.exports = grammar({
  name: 'cshl',

  extras: $ => [
    /\s+/,
    // Add comment rules here if needed
  ],

  conflicts: $ => [
    [$.expression, $.decl],
    [$._expression_list]
  ],

  precedences: $ => [
    [
      'unary',
      'dot',
      'call',
      'mul',
      'add',
      'bit',
      'cmp',
      'pipe',
      'assign'
    ]
  ],

  rules: {
    program: $ => repeat(seq($.expression, ';')),

    expression: $ => choice(
      $.id,
      $.string,
      $.int,
      $.decl,
      $.bblock,
      $.pblock,
      $.ablock,
      prec.left('call', seq($.expression, $.block)),
      prec.left('dot', seq($.expression, '.', $.expression)),
      prec.left('mul', seq($.expression, choice('*', '/', '%'), $.expression)),
      prec.left('add', seq($.expression, choice('+', '-'), $.expression)),
      prec.left('bit', seq($.expression, choice('and', 'not', 'or', 'band', 'bnot', 'bor', 'shr', 'shl', 'bxor'), $.expression)),
      prec.left('cmp', seq($.expression, choice('<', '>', '<=', '>=', '==', '!='), $.expression)),
      prec.left('unary', seq(choice('bnot', 'not', '.'), $.expression)),
      prec.left('call', seq($.expression, $.expression)),
      prec.left('pipe', seq($.expression, '|', $.expression)),
      prec.left('assign', seq($.expression, '=', $.expression))
    ),

    decl: $ => seq(
      $.id,
      ':',
      optional($.expression),
      optional(seq(
        choice(':', '='),
        $.expression
      ))
    ),

    block: $ => choice($.bblock, $.ablock, $.pblock),

    bblock: $ => seq('{', repeat(seq($.expression, ';')), '}'),

    _expression_list: $ => seq($.expression, repeat(seq(',', $.expression)), optional(',')),

    ablock: $ => seq('[', optional($._expression_list), ']'),
    pblock: $ => seq('(', optional($._expression_list), ')'),

    id: $ => /[a-zA-Z_][a-zA-Z0-9_]*/,
    string: $ => /"([^"\\]|\\.)*"/,
    int: $ => choice(
      /[0-9]+/,
      /0x[0-9a-fA-F]+/,
      /0b[01]+/
    )
  }
});

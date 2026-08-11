module.exports = grammar({
  name: 'cshl',

  extras: $ => [
    /\s+/,
  ],

  conflicts: $ => [
    [$._expression, $.decl],
    [$._expression_list],
    [$._expression, $._block],
    [$._expression],
    [$.op, $.op]
  ],

  precedences: $ => [
    [
      'unary',
      'access',
      'call',
      'mul',
      'add',
      'bit',
      'cmp',
      'pipe',
      'decl',
      'assign'
    ]
  ],

  rules: {
    program: $ => repeat($._statement),

    _statement: $ => choice(
      seq($._expression, ';'),
      $.empty_statement
    ),

    empty_statement: $ => ';',

    // Hidden! Passes its child directly up
    _expression: $ => choice(
      $.id,
      $.string,
      $.number,
      $.decl,
      $._block,
      $.op
    ),

    op: $ => choice(
      prec.left('call', seq(field('left', $._expression), field('right', $._expression))),
      
      prec.left('access', seq(field('left', $._expression), field('operator', '.'), field('right', $._expression))),
      prec.left('access', seq(field('left', $._expression), field('operator', ':'), field('right', $._expression))),
      
      prec.left('mul', seq(field('left', $._expression), field('operator', choice('*', '/', '%')), field('right', $._expression))),
      prec.left('add', seq(field('left', $._expression), field('operator', choice('+', '-')), field('right', $._expression))),
      prec.left('bit', seq(field('left', $._expression), field('operator', choice('and', 'or', 'band', 'bor', 'shr', 'shl', 'bxor')), field('right', $._expression))),
      prec.left('cmp', seq(field('left', $._expression), field('operator', choice('<', '>', '<=', '>=', '==', '!=')), field('right', $._expression))),
      prec.left('pipe', seq(field('left', $._expression), field('operator', '|>'), field('right', $._expression))),
      prec.left('assign', seq(field('left', $._expression), field('operator', '='), field('right', $._expression))),

      prec.left('unary', seq(field('operator', choice('bnot', 'not')), field('right', $._expression)))
    ),
    decl: $ => prec.right('decl', choice(
      // Score 3: Name ~ Type ~ Value (e ~ point ~ a + b)
      prec.dynamic(3, seq(
        field('name', choice($.id, $.op)),
        '~',
        field('type', $._expression),
        field('assignment_op', choice('~', '=')),
        field('value', $._expression)
      )),
      
      // Score 2: Name ~~ Value (e ~ ~ a + b)
      prec.dynamic(2, seq(
        field('name', choice($.id, $.op)),
        '~',
        field('assignment_op', choice('~', '=')),
        field('value', $._expression)
      )),
      
      // Score 1: Name ~ Type (e ~ point)
      prec.dynamic(1, seq(
        field('name', choice($.id, $.op)),
        '~',
        field('type', $._expression)
      ))
    )),
    // Hidden! Passes bblock/pblock/ablock directly up
    _block: $ => choice($.bblock, $.ablock, $.pblock),

    bblock: $ => seq('{', repeat($._statement), '}'),

    _expression_list: $ => seq($._expression, repeat(seq(',', $._expression)), optional(',')),

    ablock: $ => seq('[', optional($._expression_list), ']'),
    pblock: $ => seq('(', optional($._expression_list), ')'),

    id: $ => /@?[a-zA-Z_][a-zA-Z0-9_]*/,
    string: $ => /"([^"\\]|\\.)*"/,
    number: $ => choice(
      /[0-9]+/,
      /0x[0-9a-fA-F]+/,
      /0b[01]+/
    )
  }
});

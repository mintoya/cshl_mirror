export default grammar({
  name: 'cshl',

  extras: $ => [
    /\s+/,
  ],

  conflicts: $ => [
    [$.expression, $.decl],
    [$._expression_list],
    [$.expression, $.block],
    [$.expression]
  ],

  precedences: $ => [
    [
      'unary',
      'dot',
      'infix_call',
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
    program: $ => repeat($.statement),

    // Guarantees semicolons for expressions, but allows standalone empty semicolons
    statement: $ => choice(
      seq($.expression, ';'),
      $.empty_statement
    ),

    empty_statement: $ => ';',

    expression: $ => choice(
      $.id,
      $.string,
      $.number,
      $.decl,
      $.bblock,
      $.pblock,
      $.ablock,
      $.op
    ),

    // Grouping ops gives them a shared AST tag name ("op") for the C adapter
    op: $ => choice(
      prec.dynamic(1, prec.left('call', seq(field('left', $.expression), field('right', $.block)))),
      prec.left('call', seq(field('left', $.expression), field('right', $.expression))),
      
      prec.left('dot', seq(field('left', $.expression), field('operator', '.'), field('right', $.expression))),
      prec.left('infix_call', seq(field('left', $.expression), field('operator', ':'), field('right', $.expression))),
      
      prec.left('mul', seq(field('left', $.expression), field('operator', choice('*', '/', '%')), field('right', $.expression))),
      prec.left('add', seq(field('left', $.expression), field('operator', choice('+', '-')), field('right', $.expression))),
      prec.left('bit', seq(field('left', $.expression), field('operator', choice('and', 'not', 'or', 'band', 'bnot', 'bor', 'shr', 'shl', 'bxor')), field('right', $.expression))),
      prec.left('cmp', seq(field('left', $.expression), field('operator', choice('<', '>', '<=', '>=', '==', '!=')), field('right', $.expression))),
      prec.left('pipe', seq(field('left', $.expression), field('operator', '|>'), field('right', $.expression))),
      prec.left('assign', seq(field('left', $.expression), field('operator', '='), field('right', $.expression))),

      prec.left('unary', seq(field('operator', choice('bnot', 'not', '.')), field('right', $.expression)))
    ),

    decl: $ => prec.right(seq(
      field('name', choice($.id, $.op)),
      '~',
      optional(field('type', $.expression)),
      optional(seq(
        field('assignment_op', choice('~', '=')), 
        field('value', $.expression)
      ))
    )),
    block: $ => choice($.bblock, $.ablock, $.pblock),

    bblock: $ => seq('{', repeat($.statement), '}'),

    _expression_list: $ => seq($.expression, repeat(seq(',', $.expression)), optional(',')),

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

module.exports = grammar({
  name: 'cshl',
  extras: $ => [
    /\s+/,
    $.comment,
  ],
  conflicts: $ => [
    [$._expression, $.decl],
    [$.op, $._expression_list ],
    [$.op],
  ],
  precedences: $ => [
    [
      'access',
      'block-call',  // tight call
      'mul',
      'add',
      'bit',
      'cmp',
      'comma',       
      'call',        // loose call
      'pipe',
      'decl',
      'assign'
    ]
  ],
  rules: {
    program: $ => repeat($._statement),
    _statement: $ => choice(
      seq($._expression, ';'),
      // $.empty_statement
    ),
    // empty_statement: $ => ';',
    _expression: $ => choice(
      $.id,
      $.string,
      $.number,
      $.decl,
      $._block,
      $.op
    ),
    op: $ => choice(
      prec.left('block-call', seq(field('left', $._expression), field('right', $._block_immediate))),
      prec.left('call', seq(field('left', $._expression), field('right', $._expression))), 
      prec.left('access', seq(field('left', $._expression), field('operator', '.'), field('right', $._expression))),
      prec.left('access', seq(field('left', $._expression), field('operator', ':'), field('right', $._expression))),
      prec.left('mul', seq(field('left', $._expression), field('operator', choice('*', '/', '%')), field('right', $._expression))),
      prec.left('add', seq(field('left', $._expression), field('operator', choice('+', '-')), field('right', $._expression))),
      prec.left('bit', seq(field('left', $._expression), field('operator', choice('and', 'or', 'band', 'bor', 'shr', 'shl', 'bxor')), field('right', $._expression))),
      prec.left('cmp', seq(field('left', $._expression), field('operator', choice('<', '>', '<=', '>=', '==', '!=')), field('right', $._expression))),
      prec.left('pipe', seq(field('left', $._expression), field('operator', '|>'), field('right', $._expression))),
      prec.right('assign', seq(field('left', $._expression), field('operator', '='), field('right', $._expression))),
      prec.right('comma', seq(field('left', $._expression), field('operator', ','), field('right', $._expression))),
    ),
    decl: $ => prec.right('decl', choice(
      prec.dynamic(3, seq(
        field('name', choice($.id, $.op)),
        '~',
        field('type', $._expression),
        field('assignment_op', choice('~', '=')),
        field('value', $._expression)
      )),
      prec.dynamic(2, seq(
        field('name', choice($.id, $.op)),
        '~',
        field('assignment_op', choice('~', '=')),
        field('value', $._expression)
      )),
      prec.dynamic(1, seq(
        field('name', choice($.id, $.op)),
        '~',
        field('type', $._expression)
      ))
    )),

    _block: $ => choice($.bblock, $.ablock, $.pblock),
    bblock: $ => seq('{', repeat($._statement), '}'),
    ablock: $ => seq('[', optional($._expression_list), ']'),
    pblock: $ => seq('(', optional($._expression_list), ')'),

    _block_immediate: $ => choice(
      $._bblock_immediate,
      $._ablock_immediate,
      $._pblock_immediate,
    ),
    
    // Explicit hidden rules for immediate blocks to prevent 'alias' from swallowing children
    _bblock_immediate: $ => alias($.__bblock_immediate, $.bblock),
    __bblock_immediate: $ => seq(token.immediate('{'), repeat($._statement), '}'),
    
    _ablock_immediate: $ => alias($.__ablock_immediate, $.ablock),
    __ablock_immediate: $ => seq(token.immediate('['), optional($._expression_list), ']'),
    
    _pblock_immediate: $ => alias($.__pblock_immediate, $.pblock),
    __pblock_immediate: $ => seq(token.immediate('('), optional($._expression_list), ')'),

    _expression_list: $ => seq($._expression, repeat(seq(',', $._expression)), optional(',')),
    id: $ => /@?[a-zA-Z_][a-zA-Z0-9_]*/,
    string: $ => /"([^"\\]|\\.)*"/,
    number: $ => choice(
      /-[0-9]+/,
      /[0-9]+/,
      /0x[0-9a-fA-F]+/,
      /0b[01]+/
    ),
    
    // Comments
    comment: $ => token(choice(
      seq('//', /.*/),
      seq(
        '/*',
        /[^*]*\*+([^/*][^*]*\*+)*/,
        '/'
      )
    ))
  }
});

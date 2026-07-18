lexer grammar ExprLexer;
COLON   : ':';
SEMI    : ';';
EQUAL   : '=';
COMMA : ',' ;
LBRACE : '[' ;
RBRACE : ']' ;
LPAREN : '(' ;
RPAREN : ')' ;
LCURLY : '{' ;
RCURLY : '}' ;



DOT : '.' ;
MUL : '*' ;
DIV : '/' ;
MOD : '%' ;
ADD : '+' ;
SUB : '-' ;
AND : 'and' ;
NOT : 'not' ;
OR  : 'or' ;
BAND: 'band' ;
BNOT: 'bnot' ;
BOR : 'bor' ;
SHR : 'shr' ;
SHL : 'shl' ;
LES : '<' ;
MOR : '>' ;
LEQ : '<=' ;
MOQ : '>=' ;
EEQ : '==' ;
NEQ : '!=' ;
PIP : '|>' ;

ID: '@'? [!a-zA-Z_] [!a-zA-Z_0-9]* ;
WS: [ \t\n\r\f]+ -> skip ;
STRING : '"' ( '\\' . | ~['"\\] )* '"' ;
INT : '0' [xX] [0-9a-fA-F]+  // Hexadecimal
    | '0' [bB] [01]+         // Binary
    | [0-9]+                 // Decimal
    ;

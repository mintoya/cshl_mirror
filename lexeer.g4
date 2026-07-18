
parser grammar ExprParser;
options { tokenVocab=ExprLexer; }

program
    : (expression SEMI)* EOF ;
expression 
    : ID
    | STRING
    | INT
    | decl
    | bblock
    | pblock
    | ablock
    | expression block
    | expression DOT expression
    | expression (
        MUL
        |DIV
        |MOD
      ) expression
    | expression (
        ADD
        |SUB
      ) expression
    | expression (
        AND
        |NOT
        |OR
        |BAND
        |BNOT
        |BOR
        |SHR
        |SHL
      ) expression
    | expression (
        LES
        |MOR
        |LEQ
        |MOQ
        |EEQ
        |NEQ
      ) expression
    | BNOT expression
    | NOT expression
    | DOT expression
    | expression expression
    | expression PIP expression
    | expression EQUAL expression
    ;
decl
    : ID COLON expression? ( (COLON | EQUAL) expression )?
    ;

block
    : bblock | ablock | pblock
    ;

bblock
    : LCURLY (expression SEMI )* RCURLY
    ;

ablock
    : LBRACE (expression (COMMA expression)*)? COMMA? RBRACE
    ;
pblock
    : LPAREN (expression (COMMA expression)*)? COMMA? RPAREN
    ;

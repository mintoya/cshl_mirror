build commands : 
```sh
cc nob.c -o nob
./nob
// prints run instructions
```

# TODO
---
-  first class types
-  first class functions
-  first class ast
-  operator overloading


## grammer
### ids
- id     : identifier, c rules but can contain `!?$#`
- number : 
    - decimal : no prefix or `-`,0-9 , that means a -1 is not a - 1
    - hex     : `0x`,0-f
    - binary  : `0b`,0-1
- string : same c rules
### expressions
- empty  : `;`, yields void
- op     : any operation, including calls, and array indexing 
                `a b` is a function call to a using b, so `a(b)`
                can extend so `a b c d`  is `a(b(c(d)))`
                this is how types are constructed, 
                comptime arraay of 5 8-bit signed constant integers
                `@comptime @arr(5,(@const @sint 8)) `
- decl   :
    - full
        - mutable   : `name ~ type = value`
        - immutable : `name ~ type ~ value`
    - name  : can be an id, or some operation that yields an id
    - type  : full expression that should yield a type, optional
    - value : expression, optional if there is a type
### blocks
index-able in ast macros using []
- pblock :  `( ... )`, optional terminator, comma-separated
- bblock :  `{ ... }`, semicolon separated, non-optional terminator
- ablock :  `[ ... ]`, same as pblock


## Macros

Functions returning `@inlineast()` evaluate at compile time and expand their
returned AST in the caller's scope. Call them with ordinary function syntax:

```cshl
twice ~~ @fn((a ~@ast()), @inlineast(), {
  return @qot(@uqot(a) + @uqot(a));
});
x ~@sint(32) = 6;
twice(x + 1); // expands to (x + 1) + (x + 1)
```
also sadly, if a macro wantss to call another macro with an ast as a variable, it has t odo this ubly stuff
```cshl
@uqot @qot fn_that_takes_ast(@uqot(ast_variable),...);
```

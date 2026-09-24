build commands : 
```sh
cc nob.c -o nob
./nob
// prints run instructions
```

# CSHL
---
meant to be a c-like language with
- [x] first class types
- [x] first class functions
- [x] first class ast
- [x] operator overloading
- [ ] raii-ish stuff?


## grammer
- empty  : `;`
- import : `lib ~~ @import("path/to/file.cshl");` (a compile-time namespace; paths are relative to the importing file)
- using  : `@using(lib);` (make namespace members available in the current lexical block)
- include: `@include("path/to/file.cshl")` (alias for `@import`; returns a namespace)
- id     : identifier, c rules but can contain `!?$#`
- number : 
    - decimal : no prefix or `-`,0-9
    - hex     : `0x`,0-f
    - binary  : `0b`,0-1
- string : same c rules
- decl   :
    - full
        - mutable   : `name ~ type = value`
        - immutable : `name ~ type ~ value`
    - name  : can be an id, or some operation that yields an id
    - type  : full expression that should yield a type, optional
    - value : expression, optional if there is a type
- pblock :  `( ... )`, optional terminator, comma-separated
- bblock :  `{ ... }`, semicolon separated, non-optional terminator
- ablock :  `[ ... ]`, same as pblock


## Pointer casts

Use an explicit `as` expression to convert between native pointer types:

```cshl
x ~@sint(32) = 7;
bytes ~@ptr(@uint(8)) = @where(x) as @ptr(@uint(8));
original ~@ptr(@sint(32)) = bytes as @ptr(@sint(32));
```

Pointer casts preserve the address and support different pointee types and
pointer depths, including `@ptr(@void())`. Assignments, function arguments,
and returns never insert these casts automatically. Pointer-to-integer and
integer-to-pointer casts are not supported.

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

## Namespaces and imports

An imported file has its own scope. `@import` returns a compile-time namespace
value containing that scope; use `.` to access its members, or `@using` to make
its names available without qualification:

```cshl
// math.cshl
answer ~~ 40;
i32 ~~ @sint(32);
add ~~ @fn (x ~i32), i32, { return x + answer; };
```

```cshl
math ~~ @import("math.cshl");
a ~math.i32 = math.add(2);
@using(math);
b ~i32 = add(2);
a == b;
```

Namespaces can contain runtime variables and effects as well as compile-time
values, types, functions, quoted ASTs, and other namespaces. Each import executes
its child scope in source order; repeated imports share its storage and execute
its initialization only once. Qualified access and `@using` refer to that same
storage. Imported functions resolve names in their defining scope;
imports cannot see the caller's local names. Builtins are available inside an
imported file but are not exported as its members.

`@using` takes effect when encountered and lasts for the current lexical block.
Local declarations take precedence over names from namespaces used in the same
block. Different imported bindings with the same name produce `AMBIGUOUS_NAME`
when referenced; qualifying the name resolves that ambiguity. Using the same
binding through multiple paths is allowed. Used namespaces also contribute to
the importing namespace's exported names.

Imports resolve relative to the source file containing the import expression.
Each canonical file path is loaded once per compilation; recursive import cycles
are rejected. `@include(path)` is an alias for `@import(path)`. Use `@using @include "file.cshl";`
to bring its names into the current scope.

## Typeinfo namespaces

Function and struct typeinfo expose `.namespace`. Use `@typeof` when reflecting
on a function value:

```cshl
Family ~~ @fn((T ~@type()), @type(), {
  return @struct { value ~T; };
});
S ~~ Family(@sint(32));
ns ~~ @typeinfo(S).struct.namespace.parent;
ns and (ns == @typeinfo(@typeof(Family)).fn.namespace);
```

A struct namespace's `.parent` identifies the enclosing generic function when
it was created in a function specialization. Different specializations share
that function namespace; different functions remain distinct even with matching
signatures. Namespace `==` and `!=` compare identity, and namespace members remain
accessible through ordinary member lookup.

Other types' `.namespace` returns empty typeinfo. Further member access on empty
typeinfo stays empty. Empty typeinfo is false in conditions and compares equal
to another empty typeinfo; a valid namespace is true and differs from empty.

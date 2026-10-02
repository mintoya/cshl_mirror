# TODO
- [ ] defer like zig
- [ ] @compiles, like zig
- [ ] `@id("@cmp")` returning less/equal/greater, promising a total ordering; derive `==`, `<`, `<=`, `>`, and `>=` when their explicit overloads are absent. Equality-only types may define `==` without `@cmp`. Always derive `!=` as `not (a == b)`; disallow independent `!=` overloads.
- [x] structured error class for reporting and a 
- [ ] rudimentary LSP in lua
- [x] better type tree
- [x] gensym
- [ ] function pointers
- [ ] function args and return after comptime must be 'any'

void is allowed, so a void is just some empty struct

```chsl
x ~~ @fn (),void,{};
e = x();
```
```cshl
// functions are comptime, so they dont get variables
nothing_t _gen_fn1(){}
e = _gen_fn1();
```

closures

there is no closure construct. a @fn roots at the module and so cannot name a
variable of the frame it was written in:

```cshl
outer ~~ @fn (x ~@int()),@int(),{
  hidden ~ @int() = 7;
  inner ~~ @fn (y ~@int()),@int(),{ return hidden; }; // UNBOUND_ID
  return inner(1);
};
```

a closure is instead an ordinary struct: one member per capture, plus a "()"
overload so it can be called like anything else.

```cshl
type ~~ @type();
s32  ~~ @sint(32);

adder ~type~ @struct{
  Self ~type~ @this();
  captured ~ s32;
  @id("()") ~~ @fn (self ~Self, extra ~s32), s32, {
    base ~s32 = self.captured;
    return base + extra;
  };
};

make_adder ~~ @fn (start ~s32), adder, {
  c ~adder;
  c.captured = start;
  return c;
};

result ~~ @fn (), s32, {
  add5 ~adder = make_adder(5);
  return add5(2); // 7
};
```
```c
typedef struct _closuretype_1 {
  int captured;
} _closuretype_1;

int _closure_1(_closuretype_1 self, int extra) {
  int base = self.captured;
  return base + extra;
}
_closuretype_1 make_adder(int start) {
  _closuretype_1 c;
  c.captured = start;
  return c;
}
int result(void) {
  _closuretype_1 add5 = make_adder(5);
  return _closure_1(add5, 2);
}
```

a capture is explicit, so capturing by reference is just a `@ref` member. the
member stores the address, and the body binds it to a local ref to write through:

```cshl
counter ~s32 = 5;

cell ~type~ @struct{
  Self ~type~ @this();
  r ~ (ref s32);
  @id("()") ~~ @fn (self ~Self, j ~s32), s32, {
    v ~(ref s32) = self.r;
    v = v + j;
    return v;
  };
};

make ~~ @fn (), cell, {
  c ~cell;
  c.r = @ref counter;
  return c;
};

k ~cell = make();
k(2);      // counter is 7
```

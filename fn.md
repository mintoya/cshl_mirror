void is allowed, so a void is just some empty struct

```chsl
x ~~ @fn (),void,{};
e = x();
```
```c
// functions are comptime, so they dont get variables
nothing_t _gen_fn1(){}
e = _gen_fn1();
```
closures
```cshl
ct ~~@closureT ((@refT i32)),(i32),i32;
x ~~ @fn (i~i32),ct,{
  @closure (@ref i),(j~i32),i32,{
    return ( i = i + j );
  }|> return _;
};
x(1)(2);
```
```c

typedef struct _closuretype_1 {
  struct {
    int *i;
  } captures;
  struct {
    int j;
  } args;
  int *result;
  void (*function)(struct _closuretype_1);
} _closuretype_1;

void _closure_1(struct _closuretype_1 a) {
  int *i = a.captures.i; // all referances are a pointer that are themselves dereferanced
  int j = a.args.j;
  {
    (*a.result) = (*i) = (*i) + j;
    return;
  }
}
_closuretype_1 _gen_fn1(int i) {
  return (_closuretype_1){
      {&i},
      {},
      {},
      _closure_1
  };
};
int main(void) {
  int _result = {};
  {
    auto _1 = _gen_fn1(1);
    _1.args.j = 2;
    _1.result = &_result;
    _1.function(_1);
  }
  _result;
}
```

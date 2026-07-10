#pragma once
#include "ast.h"
#include "type.h"
#include <wheels/smap.h>

typedef struct {
  Type t;
  union {
    void *value;
    arrstype(c_int) c_int;
    arrstype(c_ast) c_ast;
    arrstype(c_type) c_type;
  };
} typed_value;
typedef struct value {
  typed_value *lvalue;
  typed_value rvalue;
} value;

#define value_access(value) (*({             \
  var_ _value_ptr = &(value);                \
  _value_ptr->lvalue ?: &_value_ptr->rvalue; \
}))
#define value_value(value) ({ \
  var_ _value = (value);      \
  _value.rvalue =             \
      _value.lvalue           \
          ? *_value.lvalue    \
          : _value.rvalue;    \
  _value.lvalue = nullptr;    \
  _value;                     \
})

typePrinter(typed_value) {
  PUTS("{t:");
  USETYPEPRINTER(Type, in.t);
  PUTS(",v:");

  tu_match(
      in.t,
      (c_int, _, USENAMEDPRINTER("bigint", in.c_int)),
      (c_ast, _, USENAMEDPRINTER("ast", in.c_ast)),
      (c_type, _, USETYPEPRINTER(Type, *in.c_type)),
      (default, {
        USETYPEPRINTER(fptr, ((fptr){Type_rsize(in.t), in.value}));
      })
  );

  PUTS("}");
}

typePrinter(value) {
  PUTS("{");
  in.lvalue ? PUTS("lvalue:") : PUTS("rvalue:");
  USETYPEPRINTER(typed_value, value_value(in).rvalue);
  PUTS("}");
}

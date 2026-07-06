#pragma once
#include "ast.h"
#include "type.h"
#include "wheels/macros.h"

typedef struct {
  Type t;
  union {
    void *value;
    arrstype(c_int) c_int;
    arrstype(c_ast) c_ast;
    arrstype(c_type) c_type;
    arrstype(c_func) c_func;
  };
} typed_value;
typedef struct value {
  typed_value *lvalue;
  typed_value rvalue;
} value;

#define value_access(value) (*({             \
  var_ _value_ptr = &(value);                \
  _value_ptr->lvalue ?: &_value_ptr->typed_value; \
}))
#define value_typed_value(value) ({         \
  var_ _value = (value);               \
  _value.typed_value = _value.lvalue        \
                      ? *_value.lvalue \
                      : _value.typed_value; \
  _value.lvalue = nullptr;             \
  _value;                              \
})

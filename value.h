#include "ast.h"
#include "type.h"
#include "wheels/macros.h"

typedef struct {
  Type t;
  union {
    void *value;
    arrstype(func) func;
    arrstype(c_int) c_int;
    arrstype(c_ast) c_ast;
    arrstype(c_type) c_type;
    arrstype(func) func_type;
  };
} rvalue;
typedef struct value {
  rvalue *lvalue;
  rvalue rvalue;
} value;

#define value_access(value) (*({             \
  var_ _value_ptr = &(value);                \
  _value_ptr->lvalue ?: &_value_ptr->rvalue; \
}))
#define value_rvalue(value) ({         \
  var_ _value = (value);               \
  _value.rvalue = _value.lvalue        \
                      ? *_value.lvalue \
                      : _value.rvalue; \
  _value.lvalue = nullptr;             \
  _value;                              \
})

#include "type.h"
#include "wheels/macros.h"
typedef struct value {
  Type t;
  union {
    void *value;
    arrstype(func) func;
    arrstype(c_int) c_int;
    arrstype(c_ast) c_ast;
    arrstype(c_type) c_type;
    arrstype(func_type) func_type;
  };
} value;

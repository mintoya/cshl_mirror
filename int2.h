#include "ast.h"
#include "wheels/mytypes.h"
#include <wheels/bigint.h>
#include <wheels/tu_macros.h>
// {types
typedef enum builtin_enum : u8 {
  builtin_none,

  builtin_$sint,
  builtin_$uint,
  builtin_$noExpand,
  builtin_$expand,
  builtin_$while,
  builtin_$type,
  builtin_$void,
  builtin_$ast,

  builtin_fn,
  builtin_return,
  builtin_struct,
} builtin_id;
typedef struct Type Type;
// obviously these types cant store their value, like the comptime types
tu_def(
    (runtime, u8),
    (r_uint, u16),
    (r_sint, u16),
    (r_ptr, Type *),
    (r_arr, struct { Type * t ; usize count; } *),
    (r_struct, sentList_t(struct { Type * t ; usize offset; })),
    // packed
    // will add floats later but this is the point
    // obviously no non-signed 0 bit numbers
    (r_bits, sentList_t(struct { u16 bitcount:15;u16 issig : 1; })),
    // at runtime only types matter
    (r_funct, sentList_t(Type *)),
);
tu_def(
    (comptime, u8),
    (c_int, bigint),
    (c_ast, ast *),
    (c_Type, Type *),
    (
        c_funct,
        struct {
          r_funct *rtime;
          struct {
            fptr name;
            void *value;
          } *parts;
          ast *block;
        }
    ),
);
tu_def(
    (Type, u8),
    (comptime, comptime),
    (runtime, runtime),
    (builtin, builtin_id),
);
int i = sizeof(Type);

// }

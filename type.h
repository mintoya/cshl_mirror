#if !defined(TYPES)
  #define TYPES (1)
  #include "ast.h"
  #include <wheels/bigint.h>
  #include <wheels/fptr.h>
  #include <wheels/macros.h>
  #include <wheels/mytypes.h>
  #include <wheels/sList.h>
  #include <wheels/mylist.h>

typedef struct Type Type;
typedef struct r_struct_item r_struct_item;
typedef struct r_bitf_item r_bitf_item;
typedef struct function_arg function_arg;
typedef enum : u8 {
  builtin_if,
  builtin_fn,
  builtin_ast,
  builtin_int,
  builtin_uint,
  builtin_type,
  builtin_while,
  builtin_return,
  builtin_typeof,
} builtin_id;
REGISTER_PRINTER(builtin_id, {
  switch (in) {
      // clang-format off
    case(builtin_if):PUTS("if");break;
    case(builtin_fn):PUTS("fn");break;
    case(builtin_ast):PUTS("ast");break;
    case(builtin_int):PUTS("int");break;
    case(builtin_uint):PUTS("uint");break;
    case(builtin_type):PUTS("type");break;
    case(builtin_while):PUTS("while");break;
    case(builtin_return):PUTS("return");break;
    case(builtin_typeof):PUTS("typeof");break;
      // clang-format on
  }
});
tu_def( // type
    (Type, u8),
    (builtin /*      */, builtin_id),
    // runtime
    (r_void /*       */, nothing_t),
    (r_uint /*       */, u16),
    (r_sint /*       */, u16),
    (r_ptr /*        */, Type *),
    (r_arr /*        */, struct { Type *type; usize count; }),
    (r_struct /*     */, mList(r_struct_item)),
    (r_bitfield /*   */, mList(r_bitf_item)),
    (r_func /*       */, mList(Type)),
    // compile time
    // they could all be nothing_t's, i'd rather show here what they a re but also
    // not have them take up any storage space
    (c_int /*        */, typeof(bigint[0])),
    (c_type /*       */, typeof(Type *[0])),
    (c_ast /*        */, typeof(ast *[0])),

    (func /*         */, typeof(mList(function_arg)[0])),
);
typedef struct r_struct_item {
  fptr name;
  Type type;
  usize offset;
} r_struct_item;
typedef struct r_bitf_item {
  fptr name;
  Type type;
  usize offset, width;
} r_bitf_item;
typedef struct function_arg {
  fptr name;
  Type type;
} function_arg;
REGISTER_PRINTER(Type, {
  tu_match(
      in,
      (builtin, builtin, {
        PUTS("builtin:");
        USETYPEPRINTER(builtin_id, builtin);
      }),
      (r_void, _, PUTS("void");),
      (r_uint, bits, PUTS("u"); USETYPEPRINTER(usize, bits);),
      (r_sint, bits, PUTS("i"); USETYPEPRINTER(usize, bits);),
      (r_ptr, type, {
        PUTS("[*]");
        USETYPEPRINTER(Type, *type);
      }),
      (r_arr, arr, {
        PUTS("[");
        USETYPEPRINTER(usize, arr.count);
        PUTS("]");
        USETYPEPRINTER(Type, *arr.type);
      }),
      (r_struct, st, {
        PUTS("struct{");
        defer { PUTS("}"); }
        foreach (var_ type, span(mList_arr(st), mList_len(st))) {
          if (type != mList_arr(st)) PUTS(",");

          USENAMEDPRINTER("slice(c8)", type->name);
          PUTS("@");
          USETYPEPRINTER(usize, type->offset);
          PUTS(":");
          USETYPEPRINTER(Type, type->type);
        }
      }),
      (r_bitfield, st, {
        PUTS("struct{");
        defer { PUTS("}"); }
        foreach (var_ type, span(mList_arr(st), mList_len(st))) {
          if (type != mList_arr(st)) PUTS(",");

          USENAMEDPRINTER("slice(c8)", type->name);
          PUTS("@");
          USETYPEPRINTER(usize, type->offset);
          PUTS("+");
          USETYPEPRINTER(usize, type->width);
          PUTS(":");
          USETYPEPRINTER(Type, type->type);
        }
      }),
      (c_int, i, PUTS("int");),
      (c_type, t, PUTS("type");),
      (c_ast, t, PUTS("ast");),
  );
});
#endif

#pragma once
#include "../ast.h"
#include "../type.h"
#include "common.h"
#include "scope.h"
#include "wheels/macros.h"
#include <wheels/fptr.h>
#include <wheels/mylist.h>
#include <wheels/oxmap.h>

sliceDef(typed_value);
sliceDef(Type);
i8 slice_typed_value_cmp(const void *a, const void *b) {
  let av = *(const slice(Type) *)a;
  let bv = *(const slice(Type) *)b;
  if_decl (let r, icmp(av.len, bv.len)) return r;
  foreach (let i, range(0, av.len))
    if_decl (let cm, Type_cmp(av.ptr[i], bv.ptr[i])) return cm;
  return 0;
}

static inline Type function_item_type(superscope *ss, scope *s, ast *item) {
  let decl = tu_catch(astDecl, *item, error_print(item, "function parameter must be a decl"));
  if (!decl.name) error_print(item, "function parameter needs a name");
  if (!decl.type) error_print(item, "function parameter needs a type");
  let name = id_from_ast(ss, s, decl.name);
  let t = value_asType_loc(temporary_interpret(ss, s, decl.type), ast_loc(*decl.type));

  typed_value placeholder;
  if (Type_is_comptime(t)) {
    if (tu_is(c_type, t.t))
      placeholder = Type_asValue(ss, T(tu_of(c_any, {}))); // dependant ones resulve as any correctlu
    else placeholder = TV(T(tu_of(c_any, {})));
  } else placeholder = TV(t);

  scope_bind(ss, s, scope_add_value(ss, s, placeholder), name);
  return t;
}

u32 lower_function(superscope *ss, scope *s, astPBlock args, ast *retT, ast *body) {
  let params = slice_stat(slice(astptr), *sentList_vla(args.stmts));
  // clang-format off
  msList_push(ss->alloc, ss->functions, {
      .params = params,
      .retT = retT,
      .body = body,
      .defscope = s,
      .mp = moxmap_init(ss->alloc, slice(Type), function_instance, slice_typed_value_cmp),
  });
  // clang-format on

  c_fun signature = mList_init(ss->alloc, mList_iType(signature));
  let res = scope_add_value(ss, s, TV(T(tu_of(c_fun, signature)), {.c_fun = msList_len(ss->functions)}));
  // generate example signature
  {
    let arg_scope = scope_inferior(ss, s);
    defer { scope_destroy(ss, arg_scope); };
    foreach (let p, span(params.ptr, params.len))
      mList_push(signature, function_item_type(ss, arg_scope, *p));
    mList_push(signature, value_asType_loc(temporary_interpret(ss, arg_scope, retT), ast_loc(*retT)));
  }
  let temp = (ast)tu_of(astPBlock, args);
  println(
      "[LOG]generating function from \n"
      "args : {ast} \n"
      "return:{ast}\n"
      "to {slice : Type}",

      &temp,
      retT,
      ((slice(Type)){mList_len(signature), mList_arr(signature)})
  );
  return res;
}
u32 lower_function_call(superscope *ss, scope *s, u32 fn, ast *args) {
  TODO(
      "type coersion if applicable"
      "generate new specialization if not present"
      "generate call instruction"
      "interpret if returning a comptime"
  );
}

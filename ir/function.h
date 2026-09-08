#pragma once
#include "../ast.h"
#include "../type.h"
#include "common.h"
// #include "interpret.h"
#include "scope.h"
#include "wheels/macros.h"
#include "wheels/mytypes.h"
#include "wheels/print/print_pre.h"
#include "wheels/sList.h"
#include <wheels/fptr.h>
#include <wheels/mylist.h>
#include <wheels/oxmap.h>

sliceDef(typed_value);
sliceDef(Type);
i8 slice_typed_value_cmp(const void *a, const void *b) {
  let av = *(const slice(typed_value) *)a;
  let bv = *(const slice(typed_value) *)b;
  if_decl (let r, icmp(av.len, bv.len)) return r;
  // ngl, switching on all the differences is annoying, strcmp for now
  foreach (let i, range(0, av.len)) {
    let sa = snprint(stdAlloc, "{typed_value}", av.ptr[i]);
    let sb = snprint(stdAlloc, "{typed_value}", bv.ptr[i]);
    defer { slice_free(stdAlloc, sa); };
    defer { slice_free(stdAlloc, sb); };
    if_decl (let cm, fptr_cmp(fp(sa), fp(sb)))
      return cm;
  }
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
      .instances = msList_init(ss->alloc, function_instance),
      .mp = moxmap_init(ss->alloc, slice(typed_value), u32, slice_typed_value_cmp),
  });
  // clang-format on

  c_fun signature = mList_init(ss->alloc, mList_iType(signature));
  let res = scope_add_value(ss, s, TV(T(tu_of(c_fun, signature)), {.c_fun.fn = msList_len(ss->functions)}));
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
static u32 lower_function_call_with(superscope *ss, scope *s, u32 fn, ast *args, u32 lead) {
  TODO("function specialization"
       "in order for functions to work, a function call here should act like "
       "templates, so a(b,c,d), c and d are comptime , shoud be like a<c,d>(b),"
       "since c and d are comptime, whenever it's called, it shoud generate a "
       "version unique to that c&d , also comptime unknown should have a null value");
  let alist = builtin_arg_list(&args);
  let function_value = *scope_find(ss, s, fn);
  let generic_signature = tu_catch(c_fun, function_value.t.t);
  let function_item = &ss->functions[function_value.v.c_fun.fn - 1];
  let function_arglist = function_item->params;
  let value_list = msList_init(ss->alloc, typed_value *);

  let sc = scope_inferior(ss, s);
  defer { scope_destroy(ss, sc); };
  usize idx = 0;
  if (lead) {
    let index = idx++;
    let first = mList_arr(generic_signature)[index];
    if (tu_is(c_any, first.t))
      scope_bind(ss, sc, lead, id_from_ast(ss, s, tu_catch(astDecl, *function_item->params.ptr[index]).name));
    if (Type_cmp(first, scope_find(ss, s, lead)->t))
      // emit a coersion
      assertMessage(false);
    msList_push(ss->alloc, value_list, scope_find(ss, s, lead));
  }
  foreach (let arg_item, span(alist.ptr, alist.len)) {
    let index = idx++;
    let thisitem = mList_arr(generic_signature)[index];
    let lead = lower(ss, s, *arg_item);
    if (tu_is(c_any, thisitem.t))
      scope_bind(ss, sc, lead, id_from_ast(ss, s, tu_catch(astDecl, *function_item->params.ptr[index]).name));
    if (Type_cmp(thisitem, scope_find(ss, s, lead)->t))
      // emit a coersion
      assertMessage(false);
    msList_push(ss->alloc, value_list, scope_find(ss, s, lead));
  }
}
u32 lower_function_call(superscope *ss, scope *s, u32 fn, ast *args) {
  return lower_function_call_with(ss, s, fn, args, 0);
}

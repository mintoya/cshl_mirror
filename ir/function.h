#pragma once
#include "../ast.h"
#include "../type.h"
#include "common.h"
#include "scope.h"
#include "wheels/macros.h"
#include "wheels/mytypes.h"
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
static u32 coerce_type(superscope *ss, scope *s, u32 left, Type to);
u32 lower_function_call(superscope *ss, scope *s, u32 fn, ast *args) {
  let it = scope_find(ss, s, fn);
  let fun = tu_catch(c_fun, it->t.t);
  let argsList = builtin_arg_list(&args);
  if (argsList.len != mList_len(fun) - 1)
    error_print(args, "expected {} args, got {}", mList_len(fun) - 1, argsList.len);

  let fstruct = (ss->functions)[it->v.c_fun - 1];
  let fnscope = scope_inferior(ss, s);

  let sig = acreate(ss->alloc, Type[argsList.len + 1]);
  let iargs = acreate(ss->alloc, u32[argsList.len]);

  foreach (let p, span(fstruct.params.ptr, fstruct.params.len)) {
    let idx = p - fstruct.params.ptr;
    let dcl = tu_catch(astDecl, **p, error_print(*p, "function parameter must be a decl"));
    let name = id_from_ast(ss, fnscope, dcl.name);
    let t = value_asType_loc(temporary_interpret(ss, fnscope, dcl.type), ast_loc(*dcl.type));
    (*sig)[idx] = t;

    if (tu_is(c_ast, t.t))
      scope_bind(ss, fnscope, (*iargs)[idx] = scope_add_value(ss, fnscope, TV_ast(argsList.ptr[idx])), name);
    else {
      (*iargs)[idx] = coerce_type(ss, s, lower(ss, s, argsList.ptr[idx]), t);
      let d = scope_add_value(ss, fnscope, TV(t));
      scope_add_inst(ss, fnscope, (ir)tu_of(ir_farg, {.l = idx, .d = d}));
      scope_bind(ss, fnscope, d, name);
    }
  }

  let retT = value_asType_loc(temporary_interpret(ss, fnscope, fstruct.retT), ast_loc(*fstruct.retT));
  (*sig)[argsList.len] = retT;

  if (Type_is_comptime(retT)) {
    lower(ss, fnscope, fstruct.body);
    let result = interpret(ss, fnscope);
    scope_destroy(ss, fnscope);
    return scope_add_value(ss, s, result);
  }

  let key = (slice(Type)){argsList.len + 1, *sig};
  if_decl (let found, moxmap_get(fstruct.mp, key)) {
    scope_destroy(ss, fnscope);
    let d = scope_add_value(ss, s, TV(found->ret));
    scope_add_inst(ss, s, (ir)tu_of(ir_call, {.l = fn, .d = d, .args = (slice(u32)){argsList.len, *iargs}}));
    return d;
  }

  lower(ss, fnscope, fstruct.body);
  moxmap_set(fstruct.mp, key, ((function_instance){.ret = retT, .spec = fnscope}));

  let d = scope_add_value(ss, s, TV(retT));
  scope_add_inst(ss, s, (ir)tu_of(ir_call, {.l = fn, .d = d, .args = (slice(u32)){argsList.len, *iargs}}));
  return d;
}

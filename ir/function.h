#pragma once
#include "../ast.h"
#include "../type.h"
#include "common.h"
// #include "interpret.h"
#include "lower.h"
#include "scope.h"
#include "wheels/macros.h"
#include "wheels/mytypes.h"
#include "wheels/print/print_pre.h"
#include "wheels/sList.h"
#include <string.h>
#include <wheels/fptr.h>
#include <wheels/mylist.h>
#include <wheels/oxmap.h>

sliceDef(typed_value);
sliceDef(Type);

i8 typed_value_cmp(typed_value a, typed_value b) {
  if (!memcmp(&a, &b, sizeof(a))) return 0;
  if_decl (let cmp, Type_cmp(a.t, b.t)) return cmp;
  assertMessage(Type_is_comptime(a.t) ?: (!a.v.value && !b.v.value), "should have nulled that out");
  if (!Type_is_comptime(a.t)) return 0;
  if (a.v.value == b.v.value) return 0;
  if (a.t.isref) { // recurse on comptime known referances
    let nt = a.t;
    nt.isref = false;
    assertMessage(a.v.value && b.v.value);
    return typed_value_cmp(
        (typed_value){nt, *(void **)a.v.value},
        (typed_value){nt, *(void **)b.v.value}
    );
  }
  tu_match_void(
      a.t.t,
      (r_void, _, return 0),
      (builtin, _, return 0), // store no data, and cought in type_cmp
      (c_int, _, return BInt.cmp(a.v.c_int, b.v.c_int)),
      (c_fun, _, {
        return icmp(a.v.c_fun.fn, b.v.c_fun.fn)
                   ?: icmp(a.v.c_fun.spec, b.v.c_fun.spec);
      }),
      (c_type, _, return Type_cmp(*a.v.c_type, *b.v.c_type)),
      (default),
  );
  return memcmp(a.v.value, b.v.value, Type_sizeof(a.t));
}
i8 slice_typed_value_cmp_hash(const void *a, const void *b) {
  let av = *(slice(typed_value) *)a;
  let bv = *(slice(typed_value) *)b;
  if_decl (let r, icmp(av.len, bv.len)) return r;
  foreach (let i, range(0, av.len))
    if_decl (let cmp, typed_value_cmp(av.ptr[i], bv.ptr[i])) return cmp;
  return 0;
}

static inline Type function_item_type errs(superscope *ss, scope *s, ast *item) {
  let decl = tu_catch(astDecl, *item, error_print(item, "function parameter must be a decl"));
  if (!decl.name) error_print(item, "function parameter needs a name");
  if (!decl.type) error_print(item, "function parameter needs a type");
  let name = try_err(id_from_ast, (ss, s, decl.name));
  let t = try_err(value_asType, (try_err(temporary_interpret, (ss, s, decl.type))));

  typed_value placeholder;
  if (Type_is_comptime(t)) {
    if (tu_is(c_type, t.t))
      placeholder = Type_asValue(ss, T(tu_of(c_any, {}))); // dependant ones resulve as any correctlu
    else placeholder = TV(T(tu_of(c_any, {})));
  } else placeholder = TV(t);

  scope_bind(ss, s, scope_add_value(ss, s, placeholder), name);
  return t;
}

u32 lower_function errs(superscope *ss, scope *s, astPBlock args, ast *retT, ast *body) {
  let params = slice_stat(slice(astptr), *sentList_vla(args.stmts));
  // clang-format off
  msList_push(ss->alloc, ss->functions, {
      .params = params,
      .retT = retT,
      .body = body,
      .instances = msList_init(ss->alloc, function_instance),
      .mp = moxmap_init(ss->alloc, slice(typed_value), u32, slice_typed_value_cmp_hash),
  });
  // clang-format on

  let arg_scope = scope_inferior(ss, s);
  c_fun signature = mList_init(ss->alloc, mList_iType(signature));
  let res = scope_add_value(ss, s, TV(T(tu_of(c_fun, signature)), {.c_fun.fn = msList_len(ss->functions)}));
  // generate example signature
  {
    foreach (let p, span(params.ptr, params.len))
      mList_push(signature, try_err(function_item_type, (ss, arg_scope, *p)));
    mList_push(signature, try_err(value_asType, (try_err(temporary_interpret, (ss, arg_scope, retT)))));
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
static u32 lower_specialized errs(superscope *ss, scope *s, u32 fn, slice(typed_value) key, slice(u32) aargs) {
  let function_item = &ss->functions[fn - 1];
  foreach (let instance, span(function_item->instances, msList_len(function_item->instances))) {
    if (instance->key_len != key.len) continue;
    if (!slice_typed_value_cmp_hash(
            &key,
            &(slice(typed_value)){key.len, instance->key}
        )) {
      let result = scope_add_value(ss, s, TV(instance->ret));
      scope_add_inst(ss, s, (ir)tu_of(ir_call, {.l = {.fn = fn, .spec = instance - function_item->instances}, .d = result, .args = aargs}));
      return result;
    }
  }

  let spec_number = msList_len(function_item->instances);
  let stable_key = slice_alloc(ss->alloc, typed_value, key.len);
  memcpy(stable_key.ptr, key.ptr, key.len * sizeof(*key.ptr));
  foreach (let item, span(stable_key.ptr, stable_key.len))
    if (tu_is(c_int, item->t.t) && item->v.c_int)
      item->v.c_int = BInt.copy(ss->alloc, item->v.c_int);

  let spec = scope_inferior(ss, s);
  let instance = (function_instance){.spec = spec, .key = stable_key.ptr, .key_len = stable_key.len};
  msList_push(ss->alloc, function_item->instances, instance);

  foreach (let param, span(key.ptr, key.len)) {
    let index = param - key.ptr;
    let item = tu_catch(astDecl, *function_item->params.ptr[index]);
    let name = try_err(id_from_ast, (ss, spec, item.name));
    u32 value;
    if (Type_is_comptime(param->t)) {
      value = scope_add_value(ss, spec, *param);
    } else {
      value = scope_add_value(ss, spec, TV(param->t));
      scope_add_inst(ss, spec, (ir)tu_of(ir_farg, {.l = index, .d = value}));
    }
    scope_bind(ss, spec, value, name);
  }

  let ret_value = try_err(temporary_interpret, (ss, spec, function_item->retT));
  instance.ret = try_err(value_asType, (ret_value));
  function_item->instances[spec_number].ret = instance.ret;

  try_err(lower, (ss, spec, function_item->body));

  let result = scope_add_value(ss, s, TV(instance.ret));
  // clang-format off
  scope_add_inst(ss, s, (ir)tu_of(ir_call, {
   .l = {.fn = fn, .spec = spec_number},
   .d = result,
   .args = aargs,
  }));
  // clang-format on
  return result;
}
static u32 lower_function_call_with errs(superscope *ss, scope *s, u32 fn, ast *args, u32 lead) {
  let alist = builtin_arg_list(&args);
  let function_value = *scope_find(ss, s, fn);
  let generic_signature = tu_catch(c_fun, function_value.t.t);
  let function_item = &ss->functions[function_value.v.c_fun.fn - 1];
  let function_arglist = function_item->params;
  let value_list = msList_init(ss->alloc, typed_value);
  defer { msList_deInit(ss->alloc, value_list); };
  slice(u32) argptrs = slice_alloc(ss->alloc, u32, mList_len(generic_signature) - 1);

  usize idx = 0;
  if (lead) {
    let index = idx++;
    let first = mList_arr(generic_signature)[index];
    lead = coerce_type(ss, s, lead, first);
    argptrs.ptr[index] = lead;
    msList_push(ss->alloc, value_list, *scope_find(ss, s, lead));
  }
  foreach (let arg_item, span(alist.ptr, alist.len)) {
    let index = idx++;
    let thisitem = mList_arr(generic_signature)[index];
    u32 lead;
    if (tu_is(c_ast, thisitem.t))
      lead = scope_add_value(ss, s, TV_ast(*arg_item));
    else lead = try_err(lower, (ss, s, *arg_item));

    lead = coerce_type(ss, s, lead, thisitem);
    msList_push(ss->alloc, value_list, *scope_find(ss, s, lead));
    argptrs.ptr[index] = lead;
  }
  if (mList_len(generic_signature) != msList_len(value_list) + 1)
    error_print(args, "unexpected arg length");
  foreach (let l, span(value_list, msList_len(value_list)))
    l->v.value = (!Type_is_comptime(l->t) && l->v.value) ? nullptr : l->v.value;
  return try_err(lower_specialized, (
      ss,
      s,
      function_value.v.c_fun.fn,
      ((slice(typed_value)){msList_len(value_list), value_list}),
      argptrs
  ));
}
u32 lower_function_call errs(superscope *ss, scope *s, u32 fn, ast *args) { return try_err(lower_function_call_with, (ss, s, fn, args, 0)); }

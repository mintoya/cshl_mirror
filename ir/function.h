#pragma once
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

static cmpres typed_value_cmp_typed(Type ta, value_store av, Type tb, value_store bv, typeenv e) {
  if_decl (let cmp, Type_cmp(e, ta, tb)) return cmp;
  assertMessage(Type_is_comptime(ta) ?: (!av.value && !bv.value), "should have nulled that out");
  if (!Type_is_comptime(ta)) return 0;
  if (av.value == bv.value) return 0;
  if (ta.isref) { // recurse on comptime known referances
    let nt = ta;
    nt.isref = false;
    assertMessage(av.value && bv.value);
    return typed_value_cmp_typed(
        nt,
        (value_store){.value = *(void **)av.value},
        nt,
        (value_store){.value = *(void **)bv.value},
        e
    );
  }
  tu_match_void(
      ta.t,
      (r_void, _, return 0),
      (builtin, _, return 0), // store no data, and cought in type_cmp
      (c_int, _, return BInt.cmp(av.c_int, bv.c_int)),
      (c_fun, _, {
        return icmp(av.c_fun.fn, bv.c_fun.fn)
                   ?: icmp(av.c_fun.spec, bv.c_fun.spec);
      }),
      (c_type, _, return icmp(av.c_type, bv.c_type)),
      // asts have no size, so memcmp below would call every ast equal
      (c_ast, _, return icmp(av.c_ast, bv.c_ast)),
      (default),
  );
  return cmp_memcmp(av.value, bv.value, Type_sizeof(ta, e));
}
cmpres typed_value_cmp(typeenv e, typed_value a, typed_value b) {
  if (!memcmp(&a, &b, sizeof(a))) return 0;
  return typed_value_cmp_typed(type_at(e, a.t), a.v, type_at(e, b.t), b.v, e);
}
cmpres slice_typed_value_cmp_hash(void *env, const void *a, const void *b) {
  let av = *(slice(typed_value) *)a;
  let bv = *(slice(typed_value) *)b;
  if_decl (let r, icmp(av.len, bv.len)) return r;
  foreach (let i, range(0, av.len))
    if_decl (let cmp, typed_value_cmp(ss_typeenv((superscope *)env), av.ptr[i], bv.ptr[i])) return cmp;
  return 0;
}

static inline Type function_item_type errs(superscope *ss, scope *s, astidx item) {
  let item_node = ast_at(ss->asts, item);
  let decl = tu_catch(astDecl, item_node, return_err("PARAM_NOT_DECL", T({})));
  let name_idx = ast_child(ss->asts, decl.name_type_value);
  let type_idx = ast_child(ss->asts, decl.name_type_value + 1);
  if (!name_idx) return_err("PARAM_NO_NAME", T({}));
  if (!type_idx) return_err("PARAM_NO_TYPE", T({}));
  let name = try_err(id_from_ast, (ss, s, name_idx), T({}));
  let t = try_err(value_asType, (ss, try_err(temporary_interpret, (ss, s, type_idx), T({}))), T({}));

  typed_value placeholder;
  if (Type_is_comptime(t)) {
    if (tu_is(c_type, t.t))
      placeholder = Type_asValue(ss, T(tu_of(c_any, {}))); // dependant ones resulve as any correctlu
    else placeholder = TV_any(ss);
  } else placeholder = TV_of(ss, t);

  try_err(scope_bind, (ss, s, scope_add_value(ss, s, placeholder), name), T({}));
  return t;
}

u32 lower_function errs(superscope *ss, scope *s, astidx args, astidx retT, astidx body) {
  let params = builtin_arg_list(ss->asts, args);
  // clang-format off
  let roots = scope_find_rootable(ss,s);
  msList_push(ss->alloc, ss->functions, {
      .params = params,
      .retT = retT,
      .body = body,
      .defscope = roots,
      .instances = msList_init(ss->alloc, function_instance),
      .mp = moxmap_init(ss->alloc, slice(typed_value), u32, (oxmap_cmp){ slice_typed_value_cmp_hash , ss}),
  });
  // clang-format on
  let arg_scope = scope_inferior(ss, roots);
  // reserve the run before evaluating the args, since filling it in can intern more types
  let signature = (c_fun){.start = ss_type_reserve(ss, params.len + 1)};
  let env = ss_typeenv(ss);
  foreach (let p, span(params.start, params.len))
    sglist_get(env.tlist, signature.start - 1 + (p - params.start)) =
        try_err(function_item_type, (ss, arg_scope, p), 0);
  sglist_get(env.tlist, signature.start - 1 + params.len) =
      try_err(value_asType, (ss, try_err(temporary_interpret, (ss, arg_scope, retT), 0)), 0);

  let res = scope_add_value(
      ss,
      s,
      TV_with(ss, T(tu_of(c_fun, signature)), VS(.c_fun.fn = msList_len(ss->functions)))
  );
  println(
      "[LOG]generating function from \n"
      "args : {astidx_full} \n"
      "return:{astidx_full}\n"
      "to {Type_full}",

      ss_ast_full(ss, args),
      ss_ast_full(ss, retT),
      ss_tf(ss, T(tu_of(c_fun, signature)))
  );
  return res;
}
static u32 lower_specialized errs(superscope *ss, scope *s, u32 fn, slice(typed_value) key, slice(u32) aargs) {
  let function_item = &ss->functions[fn - 1];
  if_decl (let inst, moxmap_get(function_item->mp, key)) {
    let instance = &function_item->instances[*inst];
    let result = scope_add_value(ss, s, TV_of(ss, instance->ret));
    try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_call, {.l = {.fn = fn, .spec = instance - function_item->instances}, .d = result, .args = aargs})), 0);
    return result;
  }

  let spec_number = msList_len(function_item->instances);
  let stable_key = key.len ? slice_alloc(ss->alloc, typed_value, key.len) : (slice(typed_value)){};
  if (key.len)
    memcpy(stable_key.ptr, key.ptr, key.len * sizeof(*key.ptr));
  foreach (let item, span(stable_key.ptr, stable_key.len))
    if (tu_is(c_int, ss_type(ss, item->t).t) && item->v.c_int)
      item->v.c_int = BInt.copy(ss->alloc, item->v.c_int);

  // copy values to where function was made
  let spec = P$(scope_inferior(ss, function_item->defscope), ($->rootable = false, $));
  let instance = (function_instance){.spec = spec, .key = stable_key.ptr, .key_len = stable_key.len};
  moxmap_set(function_item->mp, stable_key, (u32)spec_number);
  msList_push(ss->alloc, function_item->instances, instance);

  foreach (let param, span(key.ptr, key.len)) {
    let index = param - key.ptr;
    let item_node = ast_at(ss->asts, function_item->params.start + index);
    let item = tu_catch(astDecl, item_node);
    let name = try_err(id_from_ast, (ss, spec, ast_child(ss->asts, item.name_type_value)), 0);
    u32 value;
    if (Type_is_comptime(ss_type(ss, param->t))) {
      value = scope_add_value(ss, spec, *param);
    } else {
      // the type is already interned, the key came from cast_value
      value = scope_add_value(ss, spec, (typed_value){.t = param->t});
      try_err(scope_add_inst, (ss, spec, (ir)tu_of(ir_farg, {.l = index, .d = value})), 0);
    }
    try_err(scope_bind, (ss, spec, value, name), 0);
  }

  let ret_value = try_err(temporary_interpret, (ss, spec, function_item->retT), 0);
  instance.ret = try_err(value_asType, (ss, ret_value), 0);
  function_item->instances[spec_number].ret = instance.ret;

  Type *prev_ret = ss->current_ret;
  ss->current_ret = &instance.ret;
  defer { ss->current_ret = prev_ret; };
  try_err(lower, (ss, spec, function_item->body), 0);

  let result = scope_add_value(ss, s, TV_of(ss, instance.ret));
  // clang-format off
  try_err(scope_add_inst,(ss, s, (ir)tu_of(ir_call, {
   .l = {.fn = fn, .spec = spec_number},
   .d = result,
   .args = aargs,
  })),0);
  // clang-format on
  return result;
}
static u32 lower_function_call_values errs(superscope *ss, scope *s, u32 fn, slice(u32) vals) {
  let function_value = *scope_find(ss, s, fn);
  let generic_signature = tu_catchr(c_fun, ss_type(ss, function_value.t).t);
  if (type_run_len(ss_typeenv(ss), generic_signature.start) != vals.len + 1)
    return_err("WRONG_ARG_COUNT", 0);
  let value_list = msList_init(ss->alloc, typed_value);
  defer { msList_deInit(ss->alloc, value_list); };
  slice(u32) argptrs = vals.len ? slice_alloc(ss->alloc, u32, vals.len) : (slice(u32)){};
  foreach (let i, range(0, vals.len)) {
    let thisitem = type_at(ss_typeenv(ss), type_run_at(ss_typeenv(ss), generic_signature.start, i));
    u32 lead = try_err(cast_value, (ss, s, vals.ptr[i], thisitem), 0);
    argptrs.ptr[i] = lead;
    msList_push(ss->alloc, value_list, *scope_find(ss, s, lead));
  }
  foreach (let l, span(value_list, msList_len(value_list)))
    l->v.value = (!Type_is_comptime(ss_type(ss, l->t)) && l->v.value) ? nullptr : l->v.value;
  return try_err(lower_specialized, (ss, s, function_value.v.c_fun.fn, ((slice(typed_value)){msList_len(value_list), value_list}), argptrs), 0);
}
static u32 lower_function_call_with errs(superscope *ss, scope *s, u32 fn, astidx args, u32 lead) {
  let alist = builtin_arg_list(ss->asts, args);
  let function_value = *scope_find(ss, s, fn);
  let generic_signature = tu_catchr(c_fun, ss_type(ss, function_value.t).t);
  let function_item = &ss->functions[function_value.v.c_fun.fn - 1];
  let function_arglist = function_item->params;
  let value_list = msList_init(ss->alloc, typed_value);
  defer { msList_deInit(ss->alloc, value_list); };
  let env = ss_typeenv(ss);
  usize nargs = type_run_len(env, generic_signature.start) - 1;
  slice(u32) argptrs = nargs ? slice_alloc(ss->alloc, u32, nargs) : (slice(u32)){};

  usize idx = 0;
  if (lead) {
    let index = idx++;
    let first = type_at(env, type_run_at(env, generic_signature.start, index));
    lead = try_err(cast_value, (ss, s, lead, first), 0);
    argptrs.ptr[index] = lead;
    msList_push(ss->alloc, value_list, *scope_find(ss, s, lead));
  }
  foreach (let arg_item, span(alist.start, alist.len)) {
    let index = idx++;
    let thisitem = type_at(env, type_run_at(env, generic_signature.start, index));
    u32 lead = (tu_is(c_ast, thisitem.t))
                   ? scope_add_value(ss, s, TV_ast(ss, arg_item))
                   : try_err(lower, (ss, s, arg_item), 0);
    lead = try_err(cast_value, (ss, s, lead, thisitem), 0);
    msList_push(ss->alloc, value_list, *scope_find(ss, s, lead));
    argptrs.ptr[index] = lead;
  }
  if (type_run_len(env, generic_signature.start) != msList_len(value_list) + 1)
    return_err("WRONG_ARG_COUNT", 0);
  foreach (let l, span(value_list, msList_len(value_list)))
    l->v.value = (!Type_is_comptime(ss_type(ss, l->t)) && l->v.value) ? nullptr : l->v.value;
  return try_err(lower_specialized, (ss, s, function_value.v.c_fun.fn, ((slice(typed_value)){msList_len(value_list), value_list}), argptrs), 0);
}
u32 lower_function_call errs(superscope *ss, scope *s, u32 fn, astidx args) { return try_err(lower_function_call_with, (ss, s, fn, args, 0), 0); }

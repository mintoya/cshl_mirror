#pragma once
#include "common.h"
#include "interpret.h"
#include "scope.h"
#include "wheels/macros.h"

sliceDef(typed_value);
sliceDef(Type);

//
// cmp
//

static cmpres typed_value_cmp_typed(Type ta, value_store av, Type tb, value_store bv, typeenv e) {
  if_decl (let cmp, Type_cmp(e, ta, tb)) return cmp;
  assertMessage(Type_is_comptime(ta) ?: (!av.value && !bv.value), "should have nulled that out");
  if (!Type_is_comptime(ta)) return 0;
  if (av.value == bv.value) return 0;
  if (ta.ref_kind) { // recurse on comptime known referances
    let nt = ta;
    nt.ref_kind = ref_none;
    assertMessage(av.value && bv.value);
    return typed_value_cmp_typed(
        nt,
        (value_store){.value = *(void **)av.value},
        nt,
        (value_store){.value = *(void **)bv.value},
        e
    );
  }
  return tu_match_exp(
      ta.t,
      (r_void, _, cmp_eq),
      (builtin, _, cmp_eq), // store no data, and cought in type_cmp
      (c_int, _, BInt.cmp(av.c_int, bv.c_int)),
      (c_fun, _, (cmpres)(icmp(av.c_fun.fn, bv.c_fun.fn) ?: icmp(av.c_fun.spec, bv.c_fun.spec))),
      (c_type, _, icmp(av.c_type, bv.c_type)),
      // asts have no size, so memcmp below would call every ast equal
      (c_ast, _, icmp(av.c_ast, bv.c_ast)),
      (c_inlineast, _, icmp(av.c_ast, bv.c_ast)),
      (default, ({
         assertMessage(!Type_is_comptime(ta));
         cmp_memcmp(av.value, bv.value, Type_sizeof(ta, e));
       })),
  );
}
cmpres typed_value_cmp(typeenv e, typed_value a, typed_value b) {
  if (a.t == b.t && !memcmp(&a.v, &b.v, sizeof(a.v))) return 0;
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

//
//
//

static inline Type function_item_type errs(superscope *ss, scope *s, astidx item) {
  let placeholder = scope_find(ss, s, try_err(lower, (ss, s, item), T()));
  let t = ss_type(ss, placeholder->t);
  if (tu_is(c_type, t.t)) *placeholder = Type_asValue(ss, T(tu_of(c_any, {})));
  return t;
}

u32 lower_function errs(superscope *ss, scope *s, astidx args, astidx retT, astidx body) {
  let params = builtin_arg_list(ss, args);
  let arg_scope = scope_inferior(ss, s);
  let signature = (c_fun){.start = ss_type_reserve(ss, params.len + 1)};
  let env = ss_typeenv(ss);
  foreach (let p, span(params.start, params.len))
    sglist_get(env.tlist, signature.start - 1 + (p - params.start)) =
        try_err(function_item_type, (ss, arg_scope, p), 0);
  sglist_get(env.tlist, signature.start + params.len - 1) =
      try_err(value_asType, (ss, try_err(temporary_interpret, (ss, arg_scope, retT), 0)), 0);

  // clang-format off
  msList_push(ss->alloc, ss->functions, {
      .params = params,
      .retT = retT,
      .body = body,
      .defscope = s,
      .instances = msList_init(ss->alloc, function_instance),
      .mp = moxmap_init(ss->alloc, slice(typed_value), u32, (oxmap_cmp){slice_typed_value_cmp_hash, ss}),
  });
  // clang-format on
  let fn = (u32)msList_len(ss->functions);
  return scope_add_value(ss, s, TV_with(ss, T(tu_of(c_fun, signature)), VS(.c_fun = {.fn = fn})));
}

// a definition completes the bodyless declaration of the same name: that declaration already handed
// its function index and its register out to every name that landed on it, so the body goes into
// that entry and callers keep the register they were given. the register carries the signature their
// arguments get coerced against, so only a definition spelling that signature may complete it
bool function_complete_declaration errs(superscope *ss, scope *s, u32 declared, u32 definition_register) {
  let declared_value = scope_find(ss, s, declared);
  let definition_value = scope_find(ss, s, definition_register);
  let definition = &ss->functions[definition_value->v.c_fun.fn - 1];
  if (!definition->body) return false;
  let item = &ss->functions[declared_value->v.c_fun.fn - 1];
  if (item->body) return false;
  if (Type_cmp_ndecor(ss_type(ss, declared_value->t), ss_type(ss, definition_value->t), ss_typeenv(ss)))
    return_err("FUNCTION_SIGNATURE_MISMATCH", false);
  item->params = definition->params;
  item->retT = definition->retT;
  item->body = definition->body;
  item->defscope = definition->defscope;
  return true;
}

static u32 function_call_specialize errs(superscope *ss, scope *s, u32 fnreg, u32 spec_number) {
  return scope_add_value(ss, s, *P$(scope_find(ss, s, fnreg), ($->v.c_fun.spec = spec_number, $)));
}

static u32 lower_comptime_call_result errs(superscope *ss, scope *s, scope *spec) {
  let result = try_err(interpret, (ss, spec), 0);
  if (tu_is(c_inlineast, ss_type(ss, result.t).t)) {
    if (!result.v.c_ast) return_err("INLINEAST_UNKNOWN", 0);
    return try_err(lower, (ss, s, result.v.c_ast), 0);
  }
  return scope_add_value(ss, s, result);
}

static u32 lower_function_call_instance errs(superscope *ss, scope *s, u32 fnreg, u32 fn, slice(typed_value) key, u32 rpos, u32 nargs) {
  // `ss->functions` grows while bodies are lowered, and a push moves it: an entry may only be
  // read through a pointer taken after the last call that could have registered a function, so
  // the pointer is re-derived from the index after each of them
  if (!ss->functions[fn - 1].body) return_err("FUNCTION_NOT_DEFINED", 0);
  let function_item = &ss->functions[fn - 1];

  if_decl (let inst, moxmap_get(function_item->mp, key)) {
    let instance = &function_item->instances[*inst];
    if (Type_is_comptime(instance->ret)) {
      return try_err(lower_comptime_call_result, (ss, s, instance->spec), 0);
    }
    u32 callee = try_err(function_call_specialize, (ss, s, fnreg, *inst), 0);
    let result = scope_add_value(ss, s, TV_of(ss, instance->ret));
    try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_call, {.l = callee, .d = result, .r = rpos, .e = rpos + nargs})), 0);
    return result;
  }

  let stable_key = key.len ? slice_alloc(ss->alloc, typed_value, key.len) : (slice(typed_value)){};
  if (key.len) memcpy(stable_key.ptr, key.ptr, key.len * sizeof(*key.ptr));
  foreach (let item, span(stable_key.ptr, stable_key.len))
    if (tu_is(c_int, ss_type(ss, item->t).t) && item->v.c_int)
      item->v.c_int = BInt.copy(ss->alloc, item->v.c_int);

  let spec = scope_inferior(ss, function_item->defscope);
  let instance = (function_instance){.spec = spec};
  u32 spec_number = (u32)msList_len(function_item->instances);
  moxmap_set(function_item->mp, stable_key, spec_number);
  msList_push(ss->alloc, function_item->instances, instance);
  spec->instance.fn = fn;
  spec->instance.index = spec_number;

  let params = function_item->params;
  let retT = function_item->retT;
  foreach (let i, range(0, key.len)) {
    let item = tu_catchr(astDecl, ast_at(ss, params.start + i));
    let name = try_err(id_from_ast, (ss, spec, ast_child(ss, item.name_type_value)), 0);
    u32 value = 0;
    if (Type_is_comptime(ss_type(ss, key.ptr[i].t))) value = scope_add_value(ss, spec, key.ptr[i]);
    else {
      value = scope_add_value(ss, spec, (typed_value){.t = key.ptr[i].t});
      try_err(scope_add_inst, (ss, spec, (ir)tu_of(ir_farg, {.l = (u32)i, .d = value})), 0);
    }
    try_err(scope_bind, (ss, spec, value, name), 0);
  }

  let ret_value = try_err(temporary_interpret, (ss, spec, retT), 0);
  let ret = try_err(value_asType, (ss, ret_value), 0);
  function_item = &ss->functions[fn - 1];
  function_item->instances[spec_number].ret = ret;

  let body = function_item->body;
  try_err(lower, (ss, spec, body), 0);
  ret = ss->functions[fn - 1].instances[spec_number].ret;
  let fallback = scope_add_value(ss, spec, tu_is(c_any, ret.t) ? TV_void(ss) : TV_of(ss, ret));
  try_err(scope_add_inst, (ss, spec, (ir)tu_of(ir_ret, fallback)), 0);
  scope_finish(ss, spec);
  ret = ss->functions[fn - 1].instances[spec_number].ret;

  if (Type_is_comptime(ret)) {
    return try_err(lower_comptime_call_result, (ss, s, spec), 0);
  }
  u32 callee = try_err(function_call_specialize, (ss, s, fnreg, spec_number), 0);
  let result = scope_add_value(ss, s, TV_of(ss, ret));
  try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_call, {.l = callee, .d = result, .r = rpos, .e = rpos + nargs})), 0);
  return result;
}

// coerce every argument to the generic signature, then lay them out as one consecutive run of
// slots so ir_call can name the whole argument list with [r, e)
static u32 lower_function_call_args errs(superscope *ss, scope *s, u32 fn, slice(u32) aargs) {
  let function_value = *scope_find(ss, s, fn);
  let generic_signature = tu_catchr(c_fun, ss_type(ss, function_value.t).t);
  let env = ss_typeenv(ss);
  usize nparams = type_run_len(env, generic_signature.start) - 1;
  if (nparams != aargs.len) return_err("WRONG_ARG_COUNT", 0);

  let leads = nparams ? slice_alloc(ss->alloc, u32, nparams) : (slice(u32)){};
  defer {
    if (nparams) slice_free(ss->alloc, leads);
  };
  foreach (let i, range(0, nparams))
    leads.ptr[i] = try_err(scope_cast_value, (ss, s, aargs.ptr[i], type_at(env, type_run_at(env, generic_signature.start, i))), 0);

  let key = msList_init(ss->alloc, typed_value);
  defer { msList_deInit(ss->alloc, key); };
  u32 rpos = s->start + (u32)msList_len(s->values) + 1;
  foreach (let i, range(0, nparams)) {
    msList_push(ss->alloc, key, *scope_find(ss, s, leads.ptr[i]));
    let dst = scope_add_value(ss, s, *scope_find(ss, s, leads.ptr[i]));
    if (ss_type(ss, key[i].t).ref_kind)
      try_err(scope_emit_inst, (ss, s, (ir)tu_of(ir_getref, {.l = leads.ptr[i], .d = dst})), 0);
    else
      try_err(scope_add_inst, (ss, s, (ir)tu_of(ir_set, {.l = leads.ptr[i], .d = dst})), 0);
  }
  foreach (let l, span(key, msList_len(key)))
    l->v.value = (!Type_is_comptime(ss_type(ss, l->t)) && l->v.value) ? nullptr : l->v.value;
  return try_err(lower_function_call_instance, (ss, s, fn, function_value.v.c_fun.fn, ((slice(typed_value)){msList_len(key), key}), rpos, (u32)nparams), 0);
}

u32 lower_function_call_values errs(superscope *ss, scope *s, u32 fn, slice(u32) vals) {
  return try_err(lower_function_call_args, (ss, s, fn, vals), 0);
}
u32 lower_function_call_with errs(superscope *ss, scope *s, u32 fn, astidx args, u32 lead) {
  let alist = builtin_arg_list(ss, args);
  let function_value = *scope_find(ss, s, fn);
  let generic_signature = tu_catchr(c_fun, ss_type(ss, function_value.t).t);
  let env = ss_typeenv(ss);
  usize nparams = type_run_len(env, generic_signature.start) - 1;
  usize nargs = alist.len + (lead ? 1 : 0);
  if (nargs != nparams) return_err("WRONG_ARG_COUNT", 0);
  let regs = nparams ? slice_alloc(ss->alloc, u32, nparams) : (slice(u32)){};
  defer {
    if (nparams) slice_free(ss->alloc, regs);
  };
  usize idx = 0;
  if (lead) regs.ptr[idx++] = lead;
  foreach (let a, span(alist.start, alist.len)) {
    let want = type_at(env, type_run_at(env, generic_signature.start, idx));
    // a comptime argument has to be a value here, not an instruction that would make one later,
    // since the specialization it is part of is picked by that value
    regs.ptr[idx++] = TypeTree_is_ast(want.t) ? scope_add_value(ss, s, TV_ast(ss, a))
                      : Type_is_comptime(want)
                          ? scope_add_value(ss, s, try_err(temporary_interpret, (ss, s, a), 0))
                          : try_err(lower, (ss, s, a), 0);
  }
  return try_err(lower_function_call_args, (ss, s, fn, regs), 0);
}
u32 lower_function_call errs(superscope *ss, scope *s, u32 fn, u32 ofirst, astidx args) {
  return try_err(lower_function_call_with, (ss, s, fn, args, ofirst), 0);
}

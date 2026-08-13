#include "common2.h"
#include "wheels/fptr.h"
#include "wheels/macros.h"
#include "wheels/mytypes.h"
#include "wheels/print/print_pre.h"
#include "wheels/sList.h"
#include <stdcountof.h>

typed_value temporary_interpret(superscope *env, scope *s, ast *ats) {
  let ns = scope_inferior(env, s);
  defer { scope_destroy(env, ns); };

  u32 val_id = lower(env, ns, ats);
  interpret(env, ns); // run the instructions to evaluate the node

  if (!val_id) return (typed_value){tu_of(c_type, {}), {0}};

  typed_full *val = scope_find(env, ns, val_id);
  return (typed_value){val->t, val->v};
}

ast *full_unquote(superscope *env, scope *s, ast *x) {
  if (!x) return nullptr;
  return tu_match_exp(
      *x,
      (astEmptyStatement, _, x),
      (astId, id, x),
      (astNumber, n, x),
      (astString, str, x),
      // clang-format off
      (astDecl, decl, avalue(env->alloc, (ast)tu_of(astDecl, {
          .name  = full_unquote(env, s, decl.name),
          .type  = full_unquote(env, s, decl.type),
          .value = full_unquote(env, s, decl.value),
          .is_const =  decl.is_const,
      }))),
      (astPBlock, pb, avalue(env->alloc, (ast)tu_of(astPBlock, {
        .stmts = 
            *({
                let len = sentList_len(pb.stmts);
                let rest = acreate(env->alloc, arrof(ptrof(ast), len + 1));
                for(let i = 0; i < len; i++)
                  (*rest)[i] = full_unquote(env, s, pb.stmts[i]);
                rest;
              })
      }))),
      (astBBlock, pb, avalue(env->alloc, (ast)tu_of(astBBlock, {
        .stmts = 
            *({
                let len = sentList_len(pb.stmts);
                let rest = acreate(env->alloc, arrof(ptrof(ast), len + 1));
                for(let i = 0; i < len; i++)
                  (*rest)[i] = full_unquote(env, s, pb.stmts[i]);
                rest;
              })
      }))),
      (astABlock, pb, avalue(env->alloc, (ast)tu_of(astABlock, {
        .items = 
            *({
                let len = sentList_len(pb.items);
                let rest = acreate(env->alloc, arrof(ptrof(ast), len + 1));
                for(let i = 0; i < len; i++)
                  (*rest)[i] = full_unquote(env, s, pb.items[i]);
                rest;
              })
      }))),
      (astOp, op, ({
        ast *res;
        if (op.number_rep == opNumbers.call && op.left && fptr_eq(tu_catch(astId, *op.left), "@uqot")) {
          let yield_val = temporary_interpret(env, s, op.right);
          assert_type(c_ast, yield_val.t);
          res = yield_val.v.c_ast;
        } else res = avalue(env->alloc, (ast)tu_of(astOp, {
            .left = full_unquote(env, s, op.left),
            .right = full_unquote(env, s, op.right),
            .number_rep = op.number_rep
          }));
        
        res;
      }))
      // clang-format on
  );
}

void scope_add_inst(superscope *env, scope *s, ir i) { msList_push(env->alloc, s->instructions, i); }

static u32 pack_opstr(fptr s) {
  assertMessage(s.len <= 4, "op string must be 4 bytes or less");
  u32 v = 0;
  memcpy(&v, s.ptr, s.len);
  return v;
}

astptr ast_dot(astptr node, astptr name) {
  static ast ast_empty_singleton = (ast)tu_of(astEmptyStatement, {});
  let static const fallback = &ast_empty_singleton;
  assertMessage(node, "dot on null ast");

  if (tu_is(astString, *name)) {
    fptr raw = tu_catch(astString, *name);
    assertMessage(raw.len >= 2 && raw.ptr[0] == '"' && raw.ptr[raw.len - 1] == '"');
    u32 target = pack_opstr(slice_split(raw, (1, raw.len - 1))[0]);

    if (tu_is(astOp, *node)) {
      astOp op = tu_catch(astOp, *node);
      if (op.number_rep == target) return node;
    }
    return fallback;
  }

  assertMessage(tu_is(astId, *name), "ast member must be an identifier or a string");
  fptr member = tu_catch(astId, *name);

  if (fptr_eq(member, "left")) {
    if (tu_is(astOp, *node)) {
      astOp op = tu_catch(astOp, *node);
      if (op.left) return op.left;
    }
    return fallback;
  } else if (fptr_eq(member, "right")) {
    if (tu_is(astOp, *node)) {
      astOp op = tu_catch(astOp, *node);
      if (op.right) return op.right;
    }
    return fallback;
  } else if (fptr_eq(member, "bblock")) {
    return tu_is(astBBlock, *node) ? node : fallback;
  } else {
    assertMessage(false, "unknown ast member %s", snprint(stdAlloc, "{slice(c8)}", member));
  }
}

u32 lower_builtin(superscope *env, scope *s, builtin_id id, ast *right) {
#define noargs() assertMessage(sentList_len(tu_catch(astPBlock, *right).stmts) == 0)
  switch (id) {
    case builtin_fn: {
      let args = sentList_vla(tu_catch(astPBlock, *right).stmts);
      assertMessage(countof(*args) >= 2);
      let const fnargs = sentList_vla(tu_catch(astPBlock, *(*args)[0]).stmts);
      let const fnret = (*args)[1];
      let rf = acreate(env->alloc, ast * [countof(*fnargs) + 2]);

      let params = mList_init(env->alloc, Type, countof(*fnargs) + 1);
      defer { mList_deinit(params); };

      let ns = scope_inferior(env, s);
      defer { scope_destroy(env, ns); };

      foreach (let arg, span(*rf, countof(*rf) - 2)) {
        *arg = (*fnargs)[arg - *rf];
        u32 val_id = lower(env, ns, *arg);
        mList_push(params, scope_find(env, ns, val_id)->t);
      }
      (*rf)[countof(*rf) - 2] = fnret;
      (*rf)[countof(*rf) - 1] = nullptr; // Body injected later

      mList_push(params, value_asType(temporary_interpret(env, ns, fnret)));
      let fnt = avalue(env->alloc, (function){._ = slice_stat(*rf)});
      return scope_add_value(env, s, (typed_full){.t = *ss_getType(env, (Type)tu_of(r_func, params)), .func = fnt});
    } break;
      // clang-format off
    case builtin__this :TODO("");
    case builtin_struct:TODO("");
    case builtin_at    :TODO("");
    case builtin_uqot: {
      astptr inner = tu_match_exp(
          *right,
          (astPBlock, pb, assertMessage(sentList_len(pb.stmts) == 1), pb.stmts[0]),
          (default, right),
      );
      assertMessage(tu_is(astId, *inner), "@uqot expects a variable");
      u32 idx = ss_get_named(env, s, tu_catch(astId, *inner));
      typed_full *v = scope_find(env, s, idx);
      return scope_add_value(env, s, (typed_full){v->t, v->v});
    } break;
      // clang-format on
      // type returning
      // clang-format off
    case builtin_type :noargs();return scope_add_value(env,s,(typed_full){tu_of(c_type,{}),ss_getType(env, (Type)tu_of(c_type,{}))});break;
    case builtin_ast  :noargs();return scope_add_value(env,s,(typed_full){tu_of(c_type,{}),ss_getType(env, (Type)tu_of(c_ast, {}))});break;
    case builtin_any  :noargs();return scope_add_value(env,s,(typed_full){tu_of(c_type,{}),ss_getType(env, (Type)tu_of(c_any, {}))});break;
    case builtin_int  :noargs();return scope_add_value(env,s,(typed_full){tu_of(c_type,{}),ss_getType(env, (Type)tu_of(c_int, {}))});break;
    case builtin_sint:
    case builtin_uint: {
      let arg = temporary_interpret(env, s,tu_match_exp(
          *right,
          (astPBlock, pb, assertMessage(sentList_len(pb.stmts) == 1), pb.stmts[0]),
          (default, right),
      ));
      assert_type(c_int, arg.t); assertMessage(arg.v.c_int);
      const u16 n = arg.v.c_int[0];
      return scope_add_value(env, s, (typed_full){tu_of(c_type,{}),ss_getType(env,
            id==builtin_sint
              ?(Type)tu_of(r_sint,n)
              :(Type)tu_of(r_uint,n))
      });
    } break;
                             // type returning, arg is itself a type
    case builtin_ptrof:
    case builtin_volatile:
    case builtin_const: {
      let arg = temporary_interpret(env, s, tu_match_exp(*right, (astPBlock, pb, assertMessage(sentList_len(pb.stmts) == 1), pb.stmts[0]), (default, right), ));
      Type t = value_asType(arg);
      return scope_add_value(env, s, (typed_full){tu_of(c_type, {}), 
          id == builtin_const    ? ss_getType(env, (Type)tu_of(r_decor, {ss_getType(env, t), 1, 0})) :
          id == builtin_volatile ? ss_getType(env, (Type)tu_of(r_decor, {ss_getType(env, t), 0, 1})) :
           ss_getType(env, (Type)tu_of(r_decor, {ss_getType(env, (Type)tu_of(r_ptr, ss_getType(env, t))), true, false})) 
      });
    } break;
      // clang-format on
    case builtin_typeof: {
      let arg = temporary_interpret(env, s, tu_match_exp(*right, (astPBlock, pb, assertMessage(sentList_len(pb.stmts) == 1), pb.stmts[0]), (default, right), ));
      return scope_add_value(env, s, (typed_full){tu_of(c_type, {}), ss_getType(env, arg.t)});
    } break;
    case builtin_arrof: {
      let args = sentList_vla(tu_catch(astPBlock, *right).stmts);
      assertMessage(countof(*args) == 2);
      let narg = temporary_interpret(env, s, (*args)[0]);
      assert_type(c_int, narg.t);
      assertMessage(narg.v.c_int);
      let targ = temporary_interpret(env, s, (*args)[1]);
      Type t = value_asType(targ);
      usize count = narg.v.c_int[0];
      return scope_add_value(env, s, (typed_full){tu_of(c_type, {}), ss_getType(env, (Type)tu_of(r_arr, {ss_getType(env, t), count}))});
    } break;
      // clang-format on
      // control
      // clang-format off
    case builtin_break    :noargs(), scope_add_inst(env, s, (ir)tu_of(ir_break, {})); break;
    case builtin_continue :noargs(), scope_add_inst(env, s, (ir)tu_of(ir_continue, {})); break;
    case builtin_return   :noargs(), scope_add_inst(env, s, (ir)tu_of(ir_ret, lower(env, s, right))); break;
    // clang-format on
    case builtin_if: {
      let args = sentList_vla(tu_catch(astPBlock, *right).stmts);
      assertMessage(countof(*args) >= 2 && countof(*args) <= 3);
      // clang-format off
      let ifv = (ir)tu_of(ir_if, {
          .cnd = lower(env, s, (*args)[0]),
          .t = scope_inferior(env, s),
          .f = countof(*args) == 3 && (*args)[2] ? scope_inferior(env, s) : nullptr,
      });
      // clang-format on
      assertMessage(tu_catch(ir_if, ifv).cnd);
      lower(env, tu_catch(ir_if, ifv).t, (*args)[1]);
      if (tu_catch(ir_if, ifv).f)
        lower(env, tu_catch(ir_if, ifv).f, (*args)[2]);
      scope_add_inst(env, s, ifv);
    } break;
    case builtin_while: {
      let args = sentList_vla(tu_catch(astPBlock, *right).stmts);
      assertMessage(countof(*args) == 2);
      // clang-format off
      let whilev = (ir)tu_of(ir_while, {
          .cnd = lower(env, s, (*args)[0]),
          .t = scope_inferior(env, s),
      });
      // clang-format on
      assertMessage(tu_catch(ir_while, whilev).cnd);
      lower(env, tu_catch(ir_while, whilev).t, (*args)[1]);
      scope_add_inst(env, s, whilev);
    } break;
      // misc
    case builtin_qot: {
      // clang-format off
      astptr inner = full_unquote(env, s, tu_match_exp(
          *right,
          (astPBlock, pb, assertMessage(sentList_len(pb.stmts) == 1), pb.stmts[0]),
          (default, right),
      ));
      // clang-format on
      return scope_add_value(env, s, (typed_full){tu_of(c_ast, {}), {.v.c_ast = inner}});
    } break;
  }
  return 0;
#undef noargs
}

u32 lower_call(superscope *env, scope *s, ast *left, ast *right) {
  astptr *args_ast_ptr = nullptr;
  usize arg_count = 0;

  // Handles standard block arguments vs single AST chaining used in macros `if 1 (2)`
  if (tu_is(astPBlock, *right)) {
    args_ast_ptr = *sentList_vla(tu_catch(astPBlock, *right).stmts);
    arg_count = sentList_len(tu_catch(astPBlock, *right).stmts);
  } else {
    args_ast_ptr = &right;
    arg_count = 1;
  }

  u32 fn_val = 0;
  u32 implicit_arg = 0;
  bool inject_implicit = false;

  if (left && tu_is(astOp, *left)) {
    let op = tu_catch(astOp, *left);
    if (op.number_rep == opNumbers.colon) {
      assertMessage(tu_is(astId, *op.right), "RHS of method call must be an identifier");
      fptr method_name = tu_catch(astId, *op.right);

      u32 caller_idx = lower(env, s, op.left);
      Type caller_t = scope_find(env, s, caller_idx)->t;

      bool is_ptr = tu_is(r_ptr, caller_t);
      bool is_tagged = false;
      r_tag tag_data;

      if (tu_is(r_tag, caller_t)) {
        is_tagged = true;
        tag_data = tu_catch(r_tag, caller_t);
      } else if (is_ptr && tu_is(r_tag, *tu_catch(r_ptr, caller_t))) {
        is_tagged = true;
        tag_data = tu_catch(r_tag, *tu_catch(r_ptr, caller_t));
      }

      if (is_tagged) {
        fn_val = ss_find_tag_method(env, tag_data, method_name);
        assertMessage(fn_val, "Method not found for tagged type");

        inject_implicit = true;
        implicit_arg = caller_idx;
      }
    }
  }

  if (!fn_val)
    fn_val = lower(env, s, left);

  typed_full *fn_type_val = scope_find(env, s, fn_val);

  // If the RHS is a BBlock, it might be injecting the body into a function prototype
  // like `fn((a~ast), ast) { ... }`
  if (tu_is(astBBlock, *right) && tu_is(r_func, fn_type_val->t)) {
    if (fn_type_val->func && !(*function_body(*fn_type_val->func))) {
      *function_body(*fn_type_val->func) = right;
      return fn_val;
    }
  }

  if (tu_is(builtin, fn_type_val->t)) {
    return lower_builtin(env, s, tu_catch(builtin, fn_type_val->t), right);
  }

  assert_type(r_func, fn_type_val->t);
  let fn_params = tu_catch(r_func, fn_type_val->t);
  Type ret_type = mList_arr(fn_params)[mList_len(fn_params) - 1];

  let total_args = arg_count + (inject_implicit ? 1 : 0);
  let ir_args = mList_init(env->alloc, u32, total_args);
  defer { mList_deinit(ir_args); };

  if (inject_implicit)
    mList_push(ir_args, implicit_arg);

  // Magic: Pass c_ast parameters directly as unevaluated AST objects
  for (usize i = 0; i < arg_count; i++) {
    Type expected_t = mList_arr(fn_params)[i + (inject_implicit ? 1 : 0)];
    astptr arg_ast = args_ast_ptr[i];

    if (tu_is(c_ast, expected_t)) {
      typed_full ast_val = {tu_of(c_ast, {})};
      ast_val.v.c_ast = arg_ast;
      mList_push(ir_args, scope_add_value(env, s, ast_val));
    } else {
      mList_push(ir_args, lower(env, s, arg_ast));
    }
  }

  let d = scope_add_value(env, s, (typed_full){ret_type});

  u32 *args_ptr = mList_toOwned(env->alloc, ir_args);
  let call_inst = (ir_call){
      .l = fn_val,
      .d = d,
      .args = (slice(u32)){.ptr = args_ptr, .len = total_args}
  };

  scope_add_inst(env, s, (ir)tu_of(ir_call, call_inst));

  bool can_interpret = scope_find(env, s, call_inst.l)->v.value || scope_find(env, s, call_inst.l)->func;
  foreach (let i, range(0, total_args)) {
    if (tu_is(c_ast, scope_find(env, s, args_ptr[i])->t)) continue; // c_ast are values implicitly
    if (!scope_find(env, s, args_ptr[i])->v.value) {
      can_interpret = false;
      break;
    }
  }

  if (can_interpret)
    interpret_call(env, s, call_inst);

  return d;
}

u32 lower_dot(superscope *env, scope *s, u32 left, ast *name) {
  typed_full *left_val = scope_find(env, s, left);
  Type left_type = left_val->t;

  bool is_ptr = tu_is(r_ptr, left_type);
  Type base_type = is_ptr ? *tu_catch(r_ptr, left_type) : left_type;

  // Staging problem: `c_ast` fields won't be resolved until the macro is actively executing
  // So we bind the 'name' ast node into scope and defer evaluation to `interpret_op`
  if (tu_is(c_ast, base_type)) {
    typed_full name_val = {tu_of(c_ast, {})};
    name_val.v.c_ast = name;
    u32 r_val = scope_add_value(env, s, name_val);

    u32 member_idx = scope_add_value(env, s, (typed_full){tu_of(c_ast, {})});
    ir_op op_inst = {
        .l = left,
        .r = r_val,
        .d = member_idx,
        .op = opNumbers.dot,
    };
    scope_add_inst(env, s, (ir)tu_of(ir_op, op_inst));

    if (left_val->v.c_ast)
      interpret_op(env, s, op_inst);
    return member_idx;
  }

  assertMessage(name && tu_is(astId, *name), "member name must be an identifier");
  fptr member_name = tu_catch(astId, *name);

  assertMessage(tu_is(r_struct, base_type), "left must be a struct (or pointer to struct)");
  let members = tu_catch(r_struct, base_type);

  usize offset = 0;
  Type member_type = (Type)tu_of(c_any, {});
  bool found = false;
  usize count = mList_len(members);

  for (usize i = 0; i < count; i++) {
    let member = &mList_arr(members)[i];
    if (fptr_eq(member->name, member_name)) {
      member_type = member->init.t;
      found = true;
      break;
    }
    offset += Type_rsize(member->init.t); // assuming Type_rsize handles layout
  }
  assertMessage(found, "member not found in struct");

  u32 offset_val = scope_add_value(
      env, s, (typed_full){tu_of(c_int, {}), {.v.c_int = BInt.from.i64(env->alloc, offset)}}
  );

  u32 member_idx = scope_add_value(env, s, (typed_full){member_type});

  ir_op op_inst = {
      .l = left,
      .r = offset_val,
      .d = member_idx,
      .op = opNumbers.dot,
  };
  scope_add_inst(env, s, (ir)tu_of(ir_op, op_inst));

  if (left_val->v.value)
    interpret_op(env, s, op_inst);

  return member_idx;
}

u32 lower_op(superscope *env, scope *s, astOp op) {
  assertMessage(op.number_rep != opNumbers.call);
  if (op.number_rep == opNumbers.dot) return lower_dot(env, s, lower(env, s, op.left), op.right);

  u32 l_val = lower(env, s, op.left);
  u32 r_val = lower(env, s, op.right);

  Type l_type = scope_find(env, s, l_val)->t;

  bool is_tagged = false;
  r_tag tag_data;
  if (tu_is(r_tag, l_type)) {
    is_tagged = true;
    tag_data = tu_catch(r_tag, l_type);
  } else if (tu_is(r_ptr, l_type) && tu_is(r_tag, *tu_catch(r_ptr, l_type))) {
    is_tagged = true;
    tag_data = tu_catch(r_tag, *tu_catch(r_ptr, l_type));
  }

  char *mname = nullptr;
  if (op.number_rep == opNumbers.plus) mname = "__add";
  else if (op.number_rep == opNumbers.minus) mname = "__sub";
  else if (op.number_rep == opNumbers.mul) mname = "__mul";
  else if (op.number_rep == opNumbers.div) mname = "__div";
  else if (op.number_rep == opNumbers.mod) mname = "__mod";

  if (is_tagged && mname) {
    u32 fn_val = 0;
    if_decl (let methods, moxmap_get(env->tfunc, tag_data)) {
      for (usize i = 0; i < msList_len(*methods); i++) {
        if (fptr_eq((*methods)[i].name, mname)) {
          fn_val = (*methods)[i].fn;
          break;
        }
      }
    }

    if (fn_val) {
      typed_full *fn_type_val = scope_find(env, s, fn_val);
      assert_type(r_func, fn_type_val->t);
      let fn_params = tu_catch(r_func, fn_type_val->t);
      Type ret_type = mList_arr(fn_params)[mList_len(fn_params) - 1];

      let d = scope_add_value(env, s, (typed_full){ret_type});

      let ir_args = mList_init(env->alloc, u32, 2);
      defer { mList_deinit(ir_args); };
      mList_push(ir_args, l_val);
      mList_push(ir_args, r_val);

      u32 *args_ptr = mList_toOwned(env->alloc, ir_args);
      let call_inst = (ir_call){
          .l = fn_val,
          .d = d,
          .args = (slice(u32)){.ptr = args_ptr, .len = 2}
      };
      scope_add_inst(env, s, (ir)tu_of(ir_call, call_inst));

      bool can_interpret = scope_find(env, s, fn_val)->v.value || scope_find(env, s, fn_val)->func;
      if (can_interpret && scope_find(env, s, l_val)->v.value && scope_find(env, s, r_val)->v.value) {
        interpret_call(env, s, call_inst);
      }
      return d;
    }
  }

  let iop = (ir_op){
      .l = l_val,
      .r = r_val,
      .op = op.number_rep,
  };
  iop.d = scope_add_value(env, s, (typed_full){tu_of(c_any, {})});
  scope_add_inst(env, s, (ir)tu_of(ir_op, iop));
  interpret_op(env, s, iop);

  return iop.d;
}

u32 lower(superscope *env, scope *s, astptr ast) {
  if (!ast) return 0;
  return tu_match_exp(
      *ast,
      (astEmptyStatement, _, (u32)0),
      (astId, id, ss_get_named(env, s, id)),
      (astString, str, ss_compileString(env, s, str)),
      (astNumber, num, ss_compileInt(env, s, num)),
      (astDecl, decl, ({
         let t = decl.type
                     ? value_asType(temporary_interpret(env, s, decl.type))
                     : (Type)tu_of(c_any, {});

         u32 val = 0;

         // Struct allocation backing memory
         if (Type_rsize(t) > 0 && !tu_is(c_ast, t) && !tu_is(c_type, t) && !tu_is(c_any, t) && !Type_isint(t) && !tu_is(r_ptr, t)) {
           val = scope_add_value(env, s, (typed_full){t});
           typed_full *v = scope_find(env, s, val);
           v->v.value = acreate(env->alloc, arrof(u8, Type_rsize(t)));
           memset(v->v.value, 0, Type_rsize(t));
         } else {
           val = scope_add_value(env, s, (typed_full){t});
         }

         if (decl.value) {
           if (tu_is(c_ast, t)) scope_find(env, s, val)->v.c_ast = decl.value;
           else {
             u32 expr = lower(env, s, decl.value);

             // ---> HACKLESS LATE INFERENCE <---
             // If we didn't have an explicit type, steal it from the lowered expression
             if (tu_is(c_any, t)) {
               scope_find(env, s, val)->t = scope_find(env, s, expr)->t;
             }

             scope_add_inst(env, s, (ir)tu_of(ir_op, {.l = val, .r = expr, .d = val, .op = opNumbers.set}));
             interpret_op(env, s, (ir_op){.l = val, .r = expr, .d = val, .op = opNumbers.set});
           }
         }

         assertMessage(
             val &&
             (tu_is(c_any, t) || !Type_cmp(scope_find(env, s, val)->t, t))
         );

         if (decl.name) {
           tu_match_void(
               *decl.name,
               (astId, id, scope_bind(env, s, val, id)),
               (default, {
                 let res = temporary_interpret(env, s, decl.name);
                 assert_type(c_ast, res.t);
                 assertMessage(res.v.c_ast);
                 assertMessage(tu_is(astId, *res.v.c_ast));
                 scope_bind(env, s, val, tu_catch(astId, *res.v.c_ast));
               }),
           );
         }
         // if (decl.is_const) TODO();
         val;
       })),
      (astBBlock, ast_block, ({
         let block = scope_inferior(env, s);
         scope_add_inst(env, s, (ir)tu_of(ir_block, block));
         u32 last = 0;
         foreach (let it, vlap(sentList_vla(ast_block.stmts)))
           last = lower(env, block, it);
         last ?: scope_add_value(env, s, (typed_full){tu_of(r_void, {})});
       })),
      (astPBlock, ast_block, ({
         let vla = sentList_vla(ast_block.stmts);
         assertMessage(countof(*vla) == 1, "no tuples");
         lower(env, s, (*vla)[0]); // Parenthesis simply forward the node
       })),
      (astABlock, _, assertMessage(false, "lone [...]"), (u32)0),
      (astOp, op, ({
         op.number_rep != opNumbers.call
             ? lower_op(env, s, op)
             : lower_call(env, s, op.left, op.right);
       })),
  );
}

#include "common2.h"
#include "wheels/fptr.h"
#include "wheels/macros.h"
#include "wheels/mytypes.h"
#include "wheels/print/print_pre.h"
#include "wheels/sList.h"
#include <stdcountof.h>
#include <string.h>

//{builtin helpers
static slice(astptr) builtin_arg_list(ast **args) {
  iflet_tu (let block, astPBlock, **args)
    return slice_stat(slice(astptr), *sentList_vla(block.stmts));
  return (slice(astptr)){1, args};
}

static u32 lower_builtin_this(superscope *ss, scope *s, ast *args) {
  assertMessage(builtin_arg_list(&args).len == 0, "this takes no arguments");
  assertMessage(msList_len(ss->thisstack), "@this() used outside of a struct definition");
  return scope_add_rvalue(ss, s, Type_asValue(ss, *msList_last(ss->thisstack)));
}

static u32 lower_builtin_struct(superscope *ss, scope *s, ast *args) {
  TODO(
      "due to how tagged structs work, every single tagged struct is completley unique in the map"
      "create a tag type"
      "use it as the 'self'"
      "preload all the items types into the struct"
      "lower the values"
  );
}
let constexpr void_value = (typed_value){tu_of(r_void, {})};

// ---- @fn() ----
static u32 lower_builtin_fn(superscope *ss, scope *s, ast *args) {
  let list = builtin_arg_list(&args);
  if (list.len != 3) error_print(args, "fn takes (args,result,body)");
  let argdecls = tu_catch(astPBlock, *list.ptr[0], error_print(list.ptr[0], "args must be parenthesized"));

  let result = acreate(ss->alloc, function);
  result->_ = slice_stat(slice(astptr), *acreate(ss->alloc, astptr[sentList_len(argdecls.stmts) + 2]));

  memcpy(result->_.ptr, argdecls.stmts, sizeof(astptr[result->_.len - 2]));
  result->_.ptr[result->_.len - 2] = list.ptr[1];
  result->_.ptr[result->_.len - 1] = list.ptr[2];

  // println("generating function {slice:ast}", result->_);
  assertMessage(!msList_len(ss->thisstack), "todo");

  let temp = scope_inferior(ss, s);
  defer { scope_destroy(ss, temp); };

  let fn_params = mList_init(ss->alloc, Type, sentList_len(argdecls.stmts) + 1);
  foreach (let did, vlap(sentList_vla(argdecls.stmts)))
    iflet_tu (let _, astDecl, *did)
      mList_push(fn_params, scope_find(ss, temp, lower(ss, temp, did))->t);
    else mList_push(fn_params, value_asType(temporary_interpret_ast(ss, temp, did)));

  Type ret_type = (Type)tu_of(c_any, {});
  iflet_tu (let _, astDecl, *list.ptr[1])
    ret_type = scope_find(ss, temp, lower(ss, temp, list.ptr[1]))->t;
  else ret_type = value_asType(temporary_interpret_ast(ss, temp, list.ptr[1]));

  mList_push(fn_params, ret_type);

  return scope_add_rvalue(
      ss,
      s,
      (typed_value){
          P$(
              ((Type)tu_of(r_func, fn_params)),
              ((Type)tu_of(r_ctime, ss_getType(ss, $))),
              *ss_getType(ss, $)
          ),
          result,
      }
  );
}
// ---- @if(cnd, then, else) ----
static u32 lower_builtin_if(superscope *ss, scope *s, ast *args) {
  let list = builtin_arg_list(&args);
  if (!EQUAL_ANY(list.len, 2, 3)) error_print(args, "@if(cnd, then, else)");
  // clang-format off
  scope_add_inst(ss, s, (ir)tu_of(ir_if, {
        .cnd = lower(ss, s, list.ptr[0]),
        .t = P$(scope_inferior(ss, s), (lower(ss, $, list.ptr[1]), $)),
        .f = P$(scope_inferior(ss, s), (list.len == 3 ? (void)lower(ss, $, list.ptr[2]) : (void)0, $)),
  }));
  // clang-format on
  return scope_add_rvalue(ss, s, void_value);
}

// ---- @while(cnd, then) ----
static u32 lower_builtin_while(superscope *ss, scope *s, ast *args) {
  let list = builtin_arg_list(&args);
  if (!EQUAL_ANY(list.len, 1, 2)) error_print(args, "@while(cnd, then)");

  let cnd_scope = scope_inferior(ss, s);
  let cnd = lower(ss, cnd_scope, list.ptr[0]);

  let sc = scope_inferior(ss, s);
  if (list.len == 2) lower(ss, sc, list.ptr[1]);

  scope_add_inst(ss, s, (ir)tu_of(ir_while, {.cnd = cnd, .c = cnd_scope, .t = sc}));
  return scope_add_rvalue(ss, s, void_value);
}

// ---- return
static u32 lower_builtin_return(superscope *ss, scope *s, ast *args) {
  let list = builtin_arg_list(&args);
  if (list.len > 1) error_print(args, "return dont take args");
  let val = list.len ? lower(ss, s, list.ptr[0]) : scope_add_rvalue(ss, s, (typed_value){tu_of(r_void, {})});
  scope_add_inst(ss, s, (ir)tu_of(ir_ret, val));
  return scope_add_rvalue(ss, s, void_value);
}

// ---- @id(str) ----
static u32 lower_builtin_id(superscope *ss, scope *s, ast *args) {
  let str = temporary_interpret_ast(ss, s, args);
  let arr = tu_catch(r_arr, str.t, error_print(args, "id takes an array of i8"));
  if (tu_catch(r_sint, arr.t[0], error_print(args, "id takes an array of i8")) != 8)
    error_print(args, "id takes an array of i8");
  let copy = slice_clone(ss->alloc, ((fptr){arr.count, (u8 *)str.v.value}));
  return scope_add_rvalue(ss, s, (typed_value){tu_of(c_ast, {}), avalue(ss->alloc, (ast)tu_of(astId, copy))});
}

// ---- @qot(arg) / @uqot(arg) ----
static astptr qot_walk(superscope *ss, scope *s, astptr node) {
  if (!node) return avalue(ss->alloc, (ast)tu_of(astEmptyStatement, {}));
  let nd = *node;

  iflet_tu (let op, astOp, nd)
    if (op.number_rep == opNumbers.call && tu_is(astId, *op.left)) {
      if (fptr_eq(tu_catch(astId, *op.left), "@uqot")) { // TODO: expand to other stuff?
        let args = op.right;
        let list = builtin_arg_list(&args);
        assertMessage(list.len == 1, "@uqot takes exactly one argument");

        let tv = temporary_interpret_ast(ss, s, list.ptr[0]);
        assert_type(c_ast, strip_decor(tv.t));
        if (!tv.v.c_ast) error_print(node, "@uqot got a null ast?");
        return tv.v.c_ast;
      }
    }

  return avalue(
      ss->alloc,
      tu_match_exp(
          nd,
          (astEmptyStatement, _, (ast)tu_of(astEmptyStatement, {})),
          (astId, id, (ast)tu_of(astId, id)),
          (astNumber, num, (ast)tu_of(astNumber, num)),
          (astString, str, (ast)tu_of(astString, str)),
          (astDecl, decl, (ast)tu_of(astDecl, {.name = qot_walk(ss, s, decl.name), .type = qot_walk(ss, s, decl.type), .value = qot_walk(ss, s, decl.value), .is_const = decl.is_const})),
          (astOp, op, (ast)tu_of(astOp, {.left = qot_walk(ss, s, op.left), .right = qot_walk(ss, s, op.right), .number_rep = op.number_rep})),
          (astPBlock, block, ({
             let len = sentList_len(block.stmts);
             let new_stmts = *acreate(ss->alloc, astptr[len + 1]);
             foreach (let i, range(0, len))
               new_stmts[i] = qot_walk(ss, s, sentList_vla(block.stmts)[0][i]);
             new_stmts[len] = nullptr;
             (ast) tu_of(astPBlock, {block.loc, new_stmts});
           })),
          (astBBlock, block, ({
             let len = sentList_len(block.stmts);
             let new_stmts = *acreate(ss->alloc, astptr[len + 1]);
             foreach (let i, range(0, len))
               new_stmts[i] = qot_walk(ss, s, sentList_vla(block.stmts)[0][i]);
             new_stmts[len] = nullptr;
             (ast) tu_of(astBBlock, {block.loc, new_stmts});
           })),
          (astABlock, block, ({
             let len = sentList_len(block.items);
             let new_stmts = *acreate(ss->alloc, astptr[len + 1]);
             foreach (let i, range(0, len))
               new_stmts[i] = qot_walk(ss, s, sentList_vla(block.items)[0][i]);
             new_stmts[len] = nullptr;
             (ast) tu_of(astABlock, {block.loc, new_stmts});
           }))
      )
  );
}
static u32 lower_builtin_qot(superscope *ss, scope *s, ast *args) {
  ast *arg = args;
  iflet_tu (let t, astPBlock, *args) {
    let l = sentList_len(t.stmts);
    if (l == 0) arg = ({static ast ex = tu_of(astEmptyStatement , {});&ex; });
    else if (l == 1) arg = t.stmts[0];
    else error_print(args, "qot takes at most 1 arg");
  }
  assertMessage(arg);
  return scope_add_rvalue(ss, s, (typed_value){tu_of(c_ast, {}), qot_walk(ss, s, arg)});
}
static u32 lower_builtin_uqot(superscope *ss, scope *s, ast *args) {
  let list = builtin_arg_list(&args);
  if (list.len != 1) error_print(args, "@uqot(arg) takes exactly one argument");
  let quoted = temporary_interpret_ast(ss, s, list.ptr[0]);
  assert_type(c_ast, quoted.t);
  assertMessage(quoted.v.c_ast, "@uqot argument is null?");
  return lower(ss, s, quoted.v.c_ast);
}

// ---- @at(arg)/where ----
static u32 lower_builtin_at(superscope *ss, scope *s, ast *args) {
  let list = builtin_arg_list(&args);
  if (list.len != 1) error_print(args, "@at(arg) takes exactly one argument");
  u32 val = lower(ss, s, list.ptr[0]);
  let target = scope_find(ss, s, val);
  let peeled = Type_peel(target->t, false);
  iflet_tu (let pt, r_ptr, peeled.t) {
    target->v.value = nullptr; // the pointer is unknown now
    u32 dest = scope_add_rvalue(ss, s, (typed_value){*pt});
    scope_add_inst(ss, s, (ir)tu_of(ir_deref, {.d = dest, .s = val}));
    return dest;
  }
  error_print(args, "@at requires a pointer argument, got {Type}", target->t);
}
static u32 lower_builtin_where(superscope *ss, scope *s, ast *args) {
  let list = builtin_arg_list(&args);
  if (list.len != 1) error_print(args, "@at(arg) takes exactly one argument");
  let o = scope_find(ss, s, lower(ss, s, list.ptr[0]));
  if (Type_is_comptime(o->t)) error_print(args, "taking address of comptime value");
  o->v.value = nullptr;
  return scope_add_rvalue(
      ss,
      s,
      (typed_value){(Type)tu_of(r_ptr, &o->t)}
  );
}

// ---- @ast() / @any() / @int() / @type() helper----
static u32 lower_builtin_bare_type(superscope *ss, scope *s, ast *args, Type t) {
  let list = builtin_arg_list(&args);
  assertMessage(list.len == 0, "@(type)() takes no arguments");
  return scope_add_rvalue(ss, s, Type_asValue(ss, t));
}

// ---- @const(T) / @volatile(T) ----
static u32 lower_builtin_decor(superscope *ss, scope *s, ast *args, bool is_const, bool is_volatile) {
  let list = builtin_arg_list(&args);
  if (list.len != 1) error_print(args, "@const/volatile(T) takes exactly one argument");
  Type inner = value_asType(temporary_interpret_ast(ss, s, list.ptr[0]));
  Type decorated = (Type)tu_of(r_decor, {.t = ss_getType(ss, inner), .constant = is_const, .valatile = is_volatile});
  return scope_add_rvalue(ss, s, Type_asValue(ss, *ss_getType(ss, decorated)));
}

// ---- @sint(N) / @uint(N) ----
static u32 lower_builtin_intty(superscope *ss, scope *s, ast *args, bool is_signed) {
  let list = builtin_arg_list(&args);
  if (list.len != 1) error_print(args, "@sint/uint(N) takes exactly one argument");
  bigint largest_int = BInt.from.i64(ss->alloc, (u16)~0);
  defer { msList_deInit(ss->alloc, largest_int); };
  bigint bigbits = int_as_bigint(ss, temporary_interpret_ast(ss, s, list.ptr[0]));
  defer { msList_deInit(ss->alloc, bigbits); };
  if (bigint_cmp(bigbits, nullptr) <= 0) error_print(args, "integer width must be positive");
  if (bigint_cmp(bigbits, largest_int) > 0) error_print(args, "integer width too large");
  return P$(
      (is_signed ? (Type)tu_of(r_sint, *(u16 *)bigbits) : (Type)tu_of(r_uint, *(u16 *)bigbits)),
      scope_add_rvalue(ss, s, Type_asValue(ss, $))
  );
}

// ---- @typeof(V) ----
static u32 lower_builtin_typeof(superscope *ss, scope *s, ast *args) {
  return scope_add_rvalue(ss, s, Type_asValue(ss, temporary_interpret_ast(ss, s, args).t));
}

// ---- @arrof(N, T) / @ptrof(T) ----
static u32 lower_builtin_arrof(superscope *ss, scope *s, ast *args) {
  let list = builtin_arg_list(&args);
  assertMessage(list.len == 2, "@arrof(N, T) takes exactly two arguments");
  let count_tv = int_as_bigint(ss, temporary_interpret_ast(ss, s, list.ptr[0]));
  defer { msList_deInit(ss->alloc, count_tv); };
  bigint_expand(ss->alloc, &count_tv, sizeof(usize) / sizeof(bigint_unit) + 1);

  Type elem = value_asType(temporary_interpret_ast(ss, s, list.ptr[1]));
  Type arr = (Type)tu_of(r_arr, {ss_getType(ss, elem), *(usize *)count_tv});
  return scope_add_rvalue(ss, s, Type_asValue(ss, arr));
}
static u32 lower_builtin_ptrof(superscope *ss, scope *s, ast *args) {
  let list = builtin_arg_list(&args);
  if (list.len != 1) error_print(args, "@ptrof takes one argument");
  Type inner = value_asType(temporary_interpret_ast(ss, s, list.ptr[0]));
  return scope_add_rvalue(ss, s, Type_asValue(ss, (Type)tu_of(r_ptr, ss_getType(ss, inner))));
}

//}
u32 lower_builtin(superscope *ss, scope *s, builtin_id builtin, ast *args) {
  switch (builtin) {
      // clang-format off
    case builtin_fn:        return lower_builtin_fn(ss, s, args);
    case builtin_if:        return lower_builtin_if(ss, s, args);
    case builtin_while:     return lower_builtin_while(ss, s, args);
    case builtin_return:    return lower_builtin_return(ss, s, args);
    case builtin_break:
    case builtin_continue:
      if(builtin_arg_list(&args).len)error_print(args, "break/continue take no args");
      scope_add_inst(ss, s,builtin == builtin_break? (ir)tu_of(ir_break, {}):(ir)tu_of(ir_continue, {}));
      return scope_add_rvalue(ss, s, (typed_value){tu_of(r_void, {})});
    case builtin_ast:       return lower_builtin_bare_type(ss, s, args, (Type)tu_of(c_ast, {}));
    case builtin__id:       return lower_builtin_id(ss, s, args);
    case builtin_any:       return lower_builtin_bare_type(ss, s, args, (Type)tu_of(c_any, {}));
    case builtin_void:      return lower_builtin_bare_type(ss, s, args, (Type)tu_of(r_void, {}));
    case builtin_int:       return lower_builtin_bare_type(ss, s, args, (Type)tu_of(c_int, {}));
    case builtin_type:      return lower_builtin_bare_type(ss, s, args, (Type)tu_of(c_type, {}));
    case builtin_const:     return lower_builtin_decor(ss, s, args, true, false);
    case builtin_volatile:  return lower_builtin_decor(ss, s, args, false, true);
    case builtin_sint:      return lower_builtin_intty(ss, s, args, true);
    case builtin_uint:      return lower_builtin_intty(ss, s, args, false);
    case builtin_typeof:    return lower_builtin_typeof(ss, s, args);
    case builtin_arrof:     return lower_builtin_arrof(ss, s, args);
    case builtin_ptrof:     return lower_builtin_ptrof(ss, s, args);
    case builtin_struct:    return lower_builtin_struct(ss, s, args);
    case builtin__this:     return lower_builtin_this(ss, s, args);
    case builtin_qot:       return lower_builtin_qot(ss, s, args);
    case builtin_uqot:      return lower_builtin_uqot(ss, s, args);
    case builtin_at:        return lower_builtin_at(ss, s, args);
    case builtin_where:        return lower_builtin_where(ss, s, args);
      // clang-format on
  }
  assertMessage(false, "unhandled builtin");
}
u32 lower_call(superscope *ss, scope *s, u32 callable, u32 optional_instance, ast *args);

static inline u32 lower_ast_val(superscope *ss, scope *s, ast *child);
u32 lower_call_type(superscope *ss, scope *s, u32 callable, u32 optional_instance, ast *args, typed_value tv) {
  let t = tv.t;
  tu_match_void(
      t,
      (builtin, b, return lower_builtin(ss, s, b, args)),

      (c_type, t, lower_call_type(ss, s, callable, 0, args, *scope_find(ss, s, callable))),
      (r_struct, st, {
        foreach (let item, mList_iter(st))
          if (fptr_eq(item.name, "()"))
            return lower_call(ss, s, scope_add_rvalue(ss, s, (typed_value){item.t}), callable, args);
        error_print(args, "struct has no '()' call operator defined");
      }),

      (r_tag, tt, return lower_call_type(ss, s, callable, optional_instance, args, (typed_value){*tt.t, tv.v})),
      (r_ctime, ctt, return lower_call_type(ss, s, callable, optional_instance, args, (typed_value){*ctt, tv.v})),
      (r_decor, dt, return lower_call_type(ss, s, callable, optional_instance, args, (typed_value){*dt.t, tv.v})),
      (r_ptr, pt, return lower_call_type(ss, s, callable, optional_instance, args, (typed_value){*pt, tv.v})),

      (c_ast, _, {
        iflet_tu (let block, astABlock, *args)
          if (sentList_len(block.items) == 1) {
            let index = int_as_bigint(ss, temporary_interpret_ast(ss, s, block.items[0]));
            bigint_expand(ss->alloc, &index, sizeof(usize) / sizeof(bigint_unit) + 1);
            if (bigint_cmp(index, 0) < 0) error_print(args, "index must be positive");
            let list = tu_match_exp(
                *tv.v.c_ast,
                (astBBlock, bb, bb.stmts),
                (astPBlock, bb, bb.stmts),
                (astABlock, bb, bb.items),
                (default, (ast **)nullptr),
            );
            let tindex = *(usize *)index;
            return lower_ast_val(ss, s, list && tindex < sentList_len(list) ? list[tindex] : nullptr);
          }
        // asts lower themselves: an ast used as a callable evaluates to a value
        // (e.g. a quoted @fn(...)), then the call applies to that value. The
        // evaluated value becomes a fresh rvalue so call resolution (and any
        // eager interpret_call) looks it up, not the original ast.
        if (tv.v.c_ast) {
          let val = temporary_interpret_ast(ss, s, tv.v.c_ast);
          u32 resolved = scope_add_rvalue(ss, s, val);
          return lower_call_type(ss, s, resolved, optional_instance, args, val);
        }
        error_print(args, "ast does not support non-index call");
      }),
      // (r_bitfield , _ , ),
      // (r_sint , _ , ),
      // (r_arr , _ , ),
      // (r_void , _ , ),
      // (r_uint , _ , ),
      // (c_int , _ , ),
      (c_any, _, {
        // a value whose type is not statically known (e.g. a function returned
        // through an `any` ret type). We can't resolve the call at lower time,
        // so emit a runtime call: interpret_call reads the concrete function
        // the payload now holds and dispatches on it.
        u32 dest = scope_add_rvalue(ss, s, (typed_value){tu_of(c_any, {})});
        let args_list = mList_init(ss->alloc, u32, 2);
        if (optional_instance) mList_push(args_list, optional_instance);
        if (tu_is(astPBlock, *args)) {
          let block = tu_catch(astPBlock, *args);
          foreach (let it, vlap(sentList_vla(block.stmts)))
            mList_push(args_list, lower(ss, s, it));
        } else
          mList_push(args_list, lower(ss, s, args));
        scope_add_inst(ss, s, (ir)tu_of(ir_call, {.l = callable, .d = dest, .args = {mList_len(args_list), mList_toOwned(ss->alloc, args_list)}}));
        return dest;
      }),
      (default, assertMessage(false, "type does not support call")),

      (r_func, f, {
        // Standard function call. `f` is the unwrapped param+ret list:
        // callable types are r_ctime(r_func(...)) envelopes, and the
        // recursion above already peeled the decorators off.
        let params = f;
        Type ret_type = mList_arr(params)[mList_len(params) - 1];

        u32 dest = scope_add_rvalue(ss, s, (typed_value){ret_type});
        let args_list = mList_init(ss->alloc, u32, 2);

        if (optional_instance) mList_push(args_list, optional_instance);

        // ast-typed parameters receive the raw ast, not the evaluated value
        // (automatic quoting, so macros can inspect their arguments)
        usize pidx = optional_instance ? 1 : 0;
        if (tu_is(astPBlock, *args)) {
          let block = tu_catch(astPBlock, *args);
          foreach (let it, vlap(sentList_vla(block.stmts))) {
            Type pt = pidx < mList_len(params) - 1 ? mList_arr(params)[pidx] : (Type)tu_of(c_any, {});
            if (tu_is(c_ast, strip_decor(pt)))
              mList_push(args_list, scope_add_rvalue(ss, s, (typed_value){tu_of(c_ast, {}), it}));
            else
              mList_push(args_list, lower(ss, s, it));
            pidx++;
          }
        } else {
          Type pt = pidx < mList_len(params) - 1 ? mList_arr(params)[pidx] : (Type)tu_of(c_any, {});
          if (tu_is(c_ast, strip_decor(pt)))
            mList_push(args_list, scope_add_rvalue(ss, s, (typed_value){tu_of(c_ast, {}), args}));
          else
            mList_push(args_list, lower(ss, s, args));
        }

        // Functions whose result is compile-time are evaluated here, while
        // the current scope (and any enclosing macro params) is still live;
        // only runtime results are deferred to interpret time.
        let c = (ir_call){.l = callable, .d = dest, .args = {mList_len(args_list), mList_toOwned(ss->alloc, args_list)}};
        if (Type_is_comptime(ret_type))
          interpret_call(ss, s, c);
        else
          scope_add_inst(ss, s, (ir)tu_of(ir_call, c));
        return dest;
      }),
  );
  return 0;
}
u32 lower_call(superscope *ss, scope *s, u32 callable, u32 optional_instance, ast *args) {
  return lower_call_type(ss, s, callable, optional_instance, args, *scope_find(ss, s, callable));
}
//{ast member access
static inline u32 lower_ast_val(superscope *ss, scope *s, ast *child) {
  return scope_add_rvalue(
      ss,
      s,
      (typed_value){tu_of(c_ast, {}), child ?: avalue(ss->alloc, (ast)tu_of(astEmptyStatement, {}))}
  );
}
static u32 lower_ast_access(superscope *ss, scope *s, ast *self, ast *right) {
  if (!self) return lower_ast_val(ss, s, nullptr);
  tu_match_void(
      *right,
      (astString, str, {
        iflet_tu (let op, astOp, *self) {
          let inner = string_escape(ss->alloc, str);
          defer { slice_free(ss->alloc, inner); };
          if (inner.len > 4) error_print(right, "operator too long");
          u32 u = 0;
          memcpy(&u, inner.ptr, inner.len);
          if (op.number_rep == u)
            return lower_ast_val(ss, s, self);
        } else return lower_ast_val(ss, s, nullptr);
      }),
      (astId, id, {
        if (fptr_eq(id, "block"))
          return tu_match_exp(
              *self,
              (astBBlock, bb, lower_ast_val(ss, s, self)),
              (astPBlock, bb, lower_ast_val(ss, s, self)),
              (astABlock, bb, lower_ast_val(ss, s, self)),
              (default, lower_ast_val(ss, s, nullptr)),
          );
        else if (fptr_eq(id, "bblock") || fptr_eq(id, "ablock") || fptr_eq(id, "pblock"))
          return tu_match_exp(
              *self,
              (astBBlock, bb, lower_ast_val(ss, s, fptr_eq(id, "bblock") ? self : nullptr)),
              (astPBlock, bb, lower_ast_val(ss, s, fptr_eq(id, "pblock") ? self : nullptr)),
              (astABlock, bb, lower_ast_val(ss, s, fptr_eq(id, "ablock") ? self : nullptr)),
              (default, lower_ast_val(ss, s, nullptr)),
          );
        else if (fptr_eq(id, "op"))
          return tu_match_exp(*self, (astOp, bb, lower_ast_val(ss, s, self)), (default, lower_ast_val(ss, s, nullptr)));
        else if (fptr_eq(id, "opleft"))
          return tu_match_exp(*self, (astOp, bb, lower_ast_val(ss, s, bb.left)), (default, lower_ast_val(ss, s, nullptr)));
        else if (fptr_eq(id, "opright"))
          return tu_match_exp(*self, (astOp, bb, lower_ast_val(ss, s, bb.right)), (default, lower_ast_val(ss, s, nullptr)));
        else if (fptr_eq(id, "number"))
          return tu_match_exp(*self, (astNumber, bb, lower_ast_val(ss, s, self)), (default, lower_ast_val(ss, s, nullptr)));
        else if (fptr_eq(id, "string"))
          return tu_match_exp(*self, (astString, bb, lower_ast_val(ss, s, self)), (default, lower_ast_val(ss, s, nullptr)));
        else if (fptr_eq(id, "decl"))
          return tu_match_exp(*self, (astDecl, bb, lower_ast_val(ss, s, self)), (default, lower_ast_val(ss, s, nullptr)));
        else if (fptr_eq(id, "dname"))
          return tu_match_exp(*self, (astDecl, bb, lower_ast_val(ss, s, bb.name)), (default, lower_ast_val(ss, s, nullptr)));
        else if (fptr_eq(id, "dvalue"))
          return tu_match_exp(*self, (astDecl, bb, lower_ast_val(ss, s, bb.value)), (default, lower_ast_val(ss, s, nullptr)));
        else if (fptr_eq(id, "dtype"))
          return tu_match_exp(*self, (astDecl, bb, lower_ast_val(ss, s, bb.type)), (default, lower_ast_val(ss, s, nullptr)));
        else if (fptr_eq(id, "id"))
          return tu_match_exp(*self, (astId, bb, lower_ast_val(ss, s, self)), (default, lower_ast_val(ss, s, nullptr)));
      }),
      (default, assertMessage(false, "expected id, string or number for member access"))
  );
  error_print(right, "failed to get {ast} from {ast}", right, self);
  return lower_ast_val(ss, s, nullptr);
}
//}
u32 lower_dot(superscope *ss, scope *s, u32 left, ast *right) {
  let left_val = scope_find(ss, s, left);
  if (tu_is(c_ast, strip_decor(left_val->t)))
    return lower_ast_access(ss, s, left_val->v.c_ast, right);
  let field_name =
      tu_match_exp(
          *right,
          (astId, id, id),
          (astNumber, num, num),
          (default, (error_print(right, "expected id, string or number for member access"), nullFptr))
      );
  let current_type = Type_peel(left_val->t, true); // auto-deref, like member access
  iflet_tu (let st, r_struct, current_type.t) {
    foreach (let item, mList_iter(st)) {
      if (fptr_eq(item.name, field_name)) {
        typed_value member_val = {item.t};

        if (Type_is_comptime(member_val.t))
          member_val.t = (Type)tu_of(r_ctime, ss_getType(ss, member_val.t));
        else if (current_type.is_const || current_type.is_volatile)
          member_val.t = (Type)tu_of(r_decor, {.t = ss_getType(ss, member_val.t), .constant = current_type.is_const, .valatile = current_type.is_volatile});

        return scope_add_rvalue(ss, s, member_val);
      }
    }
    assertMessage(false, "struct member not found");
  }
  error_print(right, "{Type} does not support dot", current_type.t);
}

u32 lower_operator_overload(superscope *ss, scope *s, u32 left, enum_op op, ast *right) {
  // Operators are packed symbols: main.c's initOpNumbers packs each operator's
  // source text into a u32 via the opS union (see opNumbers). Map the enum to
  // its packed symbol, then unpack those bytes back into the name string.
  u32 packed = 0;
  switch (op) {
      // clang-format off
    case enop_mul:   packed = opNumbers.mul;   break;
    case enop_div:   packed = opNumbers.div;   break;
    case enop_mod:   packed = opNumbers.mod;   break;
    case enop_plus:  packed = opNumbers.plus;  break;
    case enop_minus: packed = opNumbers.minus; break;
    case enop_oand:  packed = opNumbers.oand;  break;
    case enop_oor:   packed = opNumbers.oor;   break;
    case enop_oband: packed = opNumbers.oband; break;
    case enop_obor:  packed = opNumbers.obor;  break;
    case enop_shr:   packed = opNumbers.shr;   break;
    case enop_shl:   packed = opNumbers.shl;   break;
    case enop_mor:   packed = opNumbers.mor;   break;
    case enop_les:   packed = opNumbers.les;   break;
    case enop_moq:   packed = opNumbers.moq;   break;
    case enop_leq:   packed = opNumbers.leq;   break;
    case enop_set:   packed = opNumbers.set;   break;
    default: return 0;
      // clang-format on
  }
  char opname[5] = {};
  memcpy(opname, &packed, sizeof(u32));
  let left_val = scope_find(ss, s, left);
  let current_type = Type_peel(left_val->t, true); // auto-deref, like member access
  iflet_tu (let st, r_struct, current_type.t) {
    foreach (let item, mList_iter(st))
      if (fptr_eq(item.name, fp((char *)opname)))
        return P$(
            scope_add_rvalue(ss, s, (typed_value){item.t}),
            lower_call(ss, s, $, left, right)
        );
    return 0;
  }
  return 0;
}

u32 lower_boolean_op(superscope *ss, scope *s, u32 left, enum_boolean_op bop, ast *right) {
  u32 right_val = lower(ss, s, right);

  let left_tv = scope_find(ss, s, left);
  let right_tv = scope_find(ss, s, right_val);

  if (tu_is(c_ast, strip_decor(left_tv->t)) || tu_is(c_ast, strip_decor(right_tv->t))) {
    if (!EQUAL_ANY(bop, bop_oand, bop_oor)) error_print(right, "only or/and for asts");
    bool r = bop == bop_oand ? (interp_tv_truthy(left_tv) && interp_tv_truthy(right_tv))
                             : (interp_tv_truthy(left_tv) || interp_tv_truthy(right_tv));
    return scope_add_rvalue(ss, s, (typed_value){tu_of(c_int, {}), BInt.from.i64(ss->alloc, r ? 1 : 0)});
  }

  assertMessage(
      Type_isint(left_tv->t) && Type_isint(right_tv->t),
      "boolean/comparison operators currently only support integer operands"
  );

  u32 dest = scope_add_rvalue(ss, s, (typed_value){(Type)tu_of(r_uint, (u16)1)});

  scope_add_inst(
      ss, s, (ir)tu_of(ir_op, {.l = left, .r = right_val, .d = dest, .op = (u32)bop})
  );

  return dest;
}
u32 lower_math_op(superscope *ss, scope *s, u32 left, enum_op opn, ast *right) {
  u32 right_val = lower(ss, s, right);

  let left_tv = scope_find(ss, s, left);
  let right_tv = scope_find(ss, s, right_val);

  if (!Type_isint(left_tv->t) && Type_isint(right_tv->t)) error_print(right, "math between non-numbers");
  Type dest_type = left_tv->t;
  if (tu_is(c_int, dest_type) && !tu_is(c_int, right_tv->t))
    dest_type = right_tv->t;

  let dest = scope_add_rvalue(ss, s, (typed_value){dest_type});
  let op = (ir_op){.l = left, .r = right_val, .d = dest, .op = (u32)opn};
  if (Type_is_comptime(dest_type))
    interpret_op(ss, s, op);
  else scope_add_inst(ss, s, (ir)tu_of(ir_op, op));
  return dest;
}

u32 lower_op(superscope *ss, scope *s, u32 left, enum_op opn, ast *right) {
  switch (opn) {
      // clang-format off
    default: assertMessage(false, "unhandled math operator");
    case enop_dot: return lower_dot(ss, s, left, right);
    case enop_colon: assertMessage(false, "colon without function call");
    case enop_call: return lower_call(ss, s, left, 0, right);
      // clang-format on

    case enop_pipe: {
      let pipscope = scope_inferior(ss, s);
      scope_bind(ss, pipscope, left, fp("_"));
      scope_add_inst(ss, s, (ir)tu_of(ir_block, pipscope));

      u32 inner_res = lower(ss, pipscope, right);

      let inner_tv = scope_find(ss, pipscope, inner_res);
      u32 hoisted_res = scope_add_rvalue(ss, s, *inner_tv);

      scope_add_inst(
          ss,
          s,
          (ir)tu_of(ir_op, {.l = inner_res, .r = 0, .d = hoisted_res, .op = enop_set})
      );
      return hoisted_res;
    }
    case enop_eq:
    case enop_neq: {
      iflet_tu (let str, astString, *right) {
        str = string_escape(ss->alloc, str);
        defer { slice_free(ss->alloc, str); };
        let lv = scope_find(ss, s, left);
        if (tu_is(c_ast, strip_decor(lv->t))) {
          bool eq = lv->v.c_ast && tu_is(astId, *lv->v.c_ast) &&
                    fptr_eq(tu_catch(astId, *lv->v.c_ast), str);
          if (opn == enop_neq) eq = !eq;
          return scope_add_rvalue(ss, s, (typed_value){tu_of(c_int, {}), BInt.from.i64(ss->alloc, eq ? 1 : 0)});
        }
      }
    }
    case enop_set:
    case enop_obor:
    case enop_oband:
    case enop_plus:
    case enop_minus:
    case enop_mul:
    case enop_div:
    case enop_mod:
    case enop_shr:
    case enop_shl:
    case enop_oand:
    case enop_oor:
    case enop_mor:
    case enop_les:
    case enop_moq:
    case enop_leq: {
      if_decl (let overload_result, lower_operator_overload(ss, s, left, opn, right))
        return overload_result;

      switch (opn) {
        case enop_set: {
          u32 right_val = lower(ss, s, right);

          let l_tv = scope_find(ss, s, left);
          let r_tv = scope_find(ss, s, right_val);

          Type l_type = *ss_getType(ss, l_tv->t);
          let uq = Type_peel(*ss_getType(ss, r_tv->t), false);
          Type r_type = uq.t;

          if (tu_is(c_any, l_type))
            l_tv->t = r_type;
          else if (Type_cmp(l_type, r_type)) error_print(right, "{Type}!={Type}", l_type, r_type);
          assertMessage(!uq.is_const, "cannot assign to a const variable");

          let op = (ir_op){.l = right_val, .r = 0, .d = left, .op = enop_set};
          if (Type_is_comptime(l_tv->t)) interpret_op(ss, s, op);
          else scope_add_inst(ss, s, (ir)tu_of(ir_op, op));
          return left;
        }
          // clang-format off
        case enop_obor:
        case enop_oband: assertMessage(false, "unhandled bitwise ops");
        case enop_plus:
        case enop_minus:
        case enop_mul:
        case enop_div:
        case enop_mod:
        case enop_shr:
        case enop_shl: return lower_math_op(ss, s, left, opn, right);
        default: return lower_boolean_op(ss, s, left, (enum_boolean_op)opn, right);
          // clang-format on
      }
    }
  }
}
u32 lower_call(superscope *ss, scope *s, u32 callable, u32 optional_instance, ast *args);
// MARK: rest
u32 try_coerce(superscope *ss, scope *s, Type t, u32 v) {
  let o = *scope_find(ss, s, v);
  if (!Type_cmp(o.t, t)) return v;
  let ot = strip_decor(o.t);
  if (!Type_cmp(ot, t)) return v;
  tu_match_void(
      ot,
      (c_int, rs, {
        if (Type_isint(t)) {
          if (!o.v.value) assertMessage(false, "add coersion operations");
          let m = (typed_value){t, acreate(ss->alloc, u8[Type_rsize(strip_decor(t)) ?: 1])};
          bigint_into_int(ss, &m, o.v.c_int);
          return scope_add_rvalue(ss, s, m);
        }
      }),
      (default, return 0)
  );
  return 0;
}
u32 clone_decl(superscope *ss, scope *s, u32 v) {
  let o = *scope_find(ss, s, v);
  tu_match_void(
      strip_decor(o.t),
      (c_int, i, return scope_add_rvalue(ss, s, (typed_value){tu_of(c_int, {}), BInt.copy(ss->alloc, o.v.c_int)})),
      (default, {
        if (!Type_is_comptime(o.t)) {
          let m = acreate(ss->alloc, u8[Type_rsize(strip_decor(o.t)) ?: 1]);
          if (o.v.value) memcpy(m, o.v.value, sizeof(*m));
          return scope_add_rvalue(ss, s, (typed_value){o.t, m});
        } else return v;
      }),
  );
}
u32 lower(superscope *env, scope *s, astptr ats) {
  if (!ats) return 0;
  return tu_match_exp(
      *ats,
      (astEmptyStatement, _, (u32)0),
      (astId, id, ss_get_named(env, s, id)),
      (astString, str, ss_compileString(env, s, str)),
      (astNumber, num, ss_compileInt(env, s, num)),
      (astDecl, decl, ({
         // println("parsing decl {ast}", ats);
         let t = decl.type
                     ? value_asType(temporary_interpret_ast(env, s, decl.type))
                     : (Type)tu_of(c_any, {});
         // println("\ttype:{Type}", t);
         u32 val = decl.value && !tu_is(astEmptyStatement, *decl.value)
                       ? lower(env, s, decl.value)
                       : scope_add_rvalue(env, s, (typed_value){t});
         // println("\tvalue:{*:typed_value}", scope_find(env, s, val));
         {
           let nt = scope_find(env, s, val)->t;
           if (!tu_is(c_any, t) && Type_cmp(t, nt) && !(val = try_coerce(env, s, t, val)))
             error_print(ats, "{Type}!={Type}", t, nt);
           else if (!decl.name && !decl.type && tu_is(c_ast, strip_decor(nt)))
             val = try_coerce(env, s, (Type)tu_of(c_any, {}), val);
         }
         val = clone_decl(env, s, val);
         if (decl.is_const) {
           let resolved_val = scope_find(env, s, val);
           assertMessage(!tu_is(c_any, resolved_val->t), "cannot apply const to anytype");
           resolved_val->t = (Type)tu_of(r_decor, {ss_getType(env, resolved_val->t), true, false});
         }
         if (decl.name) {
           tu_match_void(
               *decl.name,
               (astId, id, scope_bind(env, s, val, id)),
               (default, {
                 let res = temporary_interpret_ast(env, s, decl.name);
                 assert_type(c_ast, res.t);
                 assertMessage(res.v.c_ast);
                 assertMessage(tu_is(astId, *res.v.c_ast));
                 scope_bind(env, s, val, tu_catch(astId, *res.v.c_ast));
               }),
           );
         }
         let resolved_val = scope_find(env, s, val);
         resolved_val->t = *ss_getType(env, resolved_val->t);
         val;
       })),
      (astBBlock, ast_block, ({
         let block = scope_inferior(env, s);
         scope_add_inst(env, s, (ir)tu_of(ir_block, block));
         u32 last = 0;
         foreach (let it, vlap(sentList_vla(ast_block.stmts))) {
           last = lower(env, block, it);
           if (!tu_is(astDecl, *it)) {
             let tv = scope_find(env, block, last);
             if (tv && tu_is(c_ast, strip_decor(tv->t)))
               last = try_coerce(env, block, (Type)tu_of(c_any, {}), last);
           }
         }
         last ?: scope_add_rvalue(env, s, (typed_value){tu_of(r_void, {})});
       })),
      (astPBlock, ast_block, ({
         let vla = sentList_vla(ast_block.stmts);
         assertMessage(countof(*vla) == 1, "no tuples");
         lower(env, s, (*vla)[0]);
       })),
      (astABlock, _, assertMessage(false, "lone [...]"), (u32)0),
      (astOp, op, ({
         u32 res = 0;
         if (op.number_rep == opNumbers.call && tu_or(astOp, *op.left, (astOp){}).number_rep == opNumbers.colon) {
           let colon_op = tu_catch(astOp, *op.left);
           u32 instance = lower(env, s, colon_op.left);
           res = lower_call(env, s, lower_dot(env, s, instance, colon_op.right), instance, op.right);
         } else res = lower_op(env, s, lower(env, s, op.left), op_as_enum(op.number_rep), op.right);
         res;
       })),
  );
}

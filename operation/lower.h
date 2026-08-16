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

// ---- @fn() ----
static u32 lower_builtin_fn(superscope *ss, scope *s, ast *args) {
  let list = builtin_arg_list(&args);
  if (list.len != 3) error_print(args, "fn takes (args,result,body)");
  let argdecls = tu_catch(astPBlock, *list.ptr[0], error_print(list.ptr[0], "args must be parenthesized"));
  foreach (let did, vlap(sentList_vla(argdecls.stmts)))
    tu_match_void(
        did[0],
        (astDecl, dcl, ),
        (astId, id, ),
        (default, error_print(did, "functoin arg must be decl or type"))
    );
  tu_match_void(
      *list.ptr[1],
      (astDecl, dcl, ),
      (astId, id, ),
      (default, error_print(*list.ptr[1], "functoin result must be decl or type"))
  );
  function result = {slice_stat(slice(astptr), *acreate(ss->alloc, astptr[sentList_len(argdecls.stmts) + 2]))};
  memcpy(result._.ptr, argdecls.stmts, sizeof(astptr[result._.len - 2]));
  result._.ptr[result._.len - 2] = list.ptr[1];
  result._.ptr[result._.len - 1] = list.ptr[2];
  println("generating function {slice:ast}", result);
  TODO("lower functions more");
  assertMessage(!msList_len(ss->thisstack), "todo");
}

// ---- @if(cnd, then, else) ----
static u32 lower_builtin_if(superscope *ss, scope *s, ast *args) {
  // empty statment is not that hard
  let list = builtin_arg_list(&args);
  if (!EQUAL_ANY(list.len, 2, 3)) error_print(args, "@if(cnd, then, else)");
  // clang-format off
  scope_add_inst(ss, s, (ir)tu_of(ir_if, {
        .cnd = lower(ss, s, list.ptr[0]),
        .t = P$(scope_inferior(ss, s), (lower(ss, $, list.ptr[1]), $)),
        .f = P$(scope_inferior(ss, s), (list.len == 3 ? (void)lower(ss, $, list.ptr[2]) : (void)0, $)),
  }));
  // clang-format on
  return scope_add_rvalue(ss, s, (typed_value){tu_of(r_void, {})});
}

// ---- @while(cnd, then) ----
static u32 lower_builtin_while(superscope *ss, scope *s, ast *args) {
  let list = builtin_arg_list(&args);
  if (!EQUAL_ANY(list.len, 1, 2)) error_print(args, "@while(cnd, then)");

  let sc = scope_inferior(ss, s);
  let cnd = lower(ss, sc, list.ptr[0]);

  if (list.len == 2) lower(ss, sc, list.ptr[1]);

  scope_add_inst(ss, s, (ir)tu_of(ir_while, {.cnd = cnd, .t = sc}));
  return scope_add_rvalue(ss, s, (typed_value){tu_of(r_void, {})});
}

// ---- return / break / continue ----
static u32 lower_builtin_return(superscope *ss, scope *s, ast *args) {
  let list = builtin_arg_list(&args);
  if (list.len > 1) error_print(args, "return takes zero or one argument");
  let val = list.len ? lower(ss, s, list.ptr[0]) : scope_add_rvalue(ss, s, (typed_value){tu_of(r_void, {})});
  scope_add_inst(ss, s, (ir)tu_of(ir_ret, val));
  return scope_add_rvalue(ss, s, (typed_value){tu_of(r_void, {})});
}

// ---- @toplevel(decl) ----
static u32 lower_builtin_toplevel(superscope *ss, scope *s, ast *args) {
  TODO("does this work?");
  let list = builtin_arg_list(&args);
  assertMessage(list.len == 1, "@toplevel(decl) takes exactly one argument");
  assertMessage(tu_is(astDecl, *list.ptr[0]), "@toplevel(decl) argument must be a declaration");

  // This only implements half the spec: "creates symbol in the current
  // file's scope." The other half - "when called with expression, yields
  // an r_func" - is template/generic specialization: the callable's type
  // here is c_ast, but lower_call_type has no arm for c_ast callables yet
  // (it's the commented-out `// (c_ast , _ , ), // TODO` line). Wiring
  // that up needs a decision on how "still generic" params are spelled
  // (e.g. left as @any / untyped decls?) and what specialization caching
  // looks like, so I left it unimplemented rather than guess.
  ast *decl_ast = list.ptr[0];
  u32 val = scope_add_rvalue(ss, ss->outermost, (typed_value){tu_of(c_ast, {}), decl_ast});

  tu_match_void(
      *tu_catch(astDecl, *decl_ast).name,
      (astId, id, scope_bind(ss, ss->outermost, val, id)),
      (default, assertMessage(false, "@toplevel(decl) requires a plain identifier name"))
  );
  return val;
}

// ---- @id(str) ----
static u32 lower_builtin_id(superscope *ss, scope *s, ast *args) {
  let str = temporary_interpret_ast(ss, s, args);
  let arr = tu_catch(r_arr, str.t, error_print(args, "id takes an array of i8"));
  let len = arr.count;
  if (tu_catch(r_sint, arr.t[0], error_print(args, "id takes an array of i8")) != 8) error_print(args, "id takes an array of i8");
  let copy = slice_clone(ss->alloc, ((fptr){len, (u8 *)str.v.value}));
  return scope_add_rvalue(ss, s, (typed_value){tu_of(c_ast, {}), avalue(ss->alloc, (ast)tu_of(astId, copy))});
}

// ---- @qot(arg) / @uqot(arg) ----
static astptr qot_walk(superscope *ss, scope *s, astptr node) {
  let nd = node ? *node : ({return nullptr ; (ast){}; });

  iflet_tu (let op, astOp, nd)
    if (op.number_rep == opNumbers.call && tu_is(astId, *op.left)) {
      if (fptr_eq(tu_catch(astId, *op.left), "@uqot")) { // TODO: alias'ed uqot?
        let args = op.right;
        let list = builtin_arg_list(&args);
        assertMessage(list.len == 1, "@uqot takes exactly one argument");

        let unquoted_tv = temporary_interpret_ast(ss, s, list.ptr[0]);
        assert_type(c_ast, unquoted_tv.t);
        assertMessage(unquoted_tv.v.c_ast, "@uqot must evaluate to an ast");

        return unquoted_tv.v.c_ast;
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
          (astOp, op, (ast)tu_of(astOp, {qot_walk(ss, s, op.left), qot_walk(ss, s, op.right), op.number_rep})),
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
  assertMessage(list.len == 1, "@uqot(arg) takes exactly one argument");
  let quoted = temporary_interpret_ast(ss, s, list.ptr[0]);
  assert_type(c_ast, quoted.t);
  assertMessage(quoted.v.c_ast, "@uqot argument did not evaluate to a known ast");
  return lower(ss, s, quoted.v.c_ast);
}

// ---- @at(arg) ----
static u32 lower_builtin_at(superscope *ss, scope *s, ast *args) {
  let list = builtin_arg_list(&args);
  assertMessage(list.len == 1, "@at(arg) takes exactly one argument");

  u32 val = lower(ss, s, list.ptr[0]);
  let target = scope_find_origional(ss, s, val);
  iflet_tu (let t, r_ctime, target->t) target->t = *t; // make comptime not comptime
  assertMessage(!Type_is_comptime(target->t), "cannot take the address of a compile-time-only value");
  target->v.value = nullptr;

  Type ptr_type = (Type)tu_of(r_ptr, ss_getType(ss, target->t));
  let dest = scope_add_rvalue(ss, s, (typed_value){ptr_type});
  // clang-format off
  scope_add_inst(ss, s, (ir)tu_of(ir_op, {
        .l = val, 
        .r = (TODO("address of"), 0), 
        .d = dest, 
        .op = (TODO("address of"), 0)})
  );
  // clang-format on
  return dest;
}

// ---- @ast() / @any() / @int() / @type() helper----
static u32 lower_builtin_bare_type(superscope *ss, scope *s, ast *args, Type t, const char *name) {
  let list = builtin_arg_list(&args);
  assertMessage(list.len == 0, "@%s() takes no arguments", name);
  return scope_add_rvalue(ss, s, Type_asValue(ss, t));
}

// ---- @const(T) / @volatile(T) ----
static u32 lower_builtin_decor(superscope *ss, scope *s, ast *args, bool is_const, bool is_volatile) {
  let list = builtin_arg_list(&args);
  assertMessage(list.len == 1, "@%s(T) takes exactly one argument", is_const ? "const" : "volatile");
  Type inner = value_asType(temporary_interpret_ast(ss, s, list.ptr[0]));
  Type decorated = (Type)tu_of(r_decor, {.t = ss_getType(ss, inner), .constant = is_const, .valatile = is_volatile});
  return scope_add_rvalue(ss, s, Type_asValue(ss, *ss_getType(ss, decorated)));
}

// ---- @sint(N) / @uint(N) ----
static u32 lower_builtin_intty(superscope *ss, scope *s, ast *args, bool is_signed) {
  let list = builtin_arg_list(&args);
  assertMessage(list.len == 1, "@%s(N) takes exactly one argument", is_signed ? "sint" : "uint");
  let bits_tv = temporary_interpret_ast(ss, s, list.ptr[0]);
  assert_type(c_int, bits_tv.t);
  assertMessage(bits_tv.v.c_int, "@%s bit width must be a known compile-time integer", is_signed ? "sint" : "uint");

  BInt.expand(ss->alloc, &bits_tv.v.c_int, sizeof(u16) / sizeof(bigint_unit) + 1);
  u16 bits = *(u16 *)bits_tv.v.c_int; //
  Type t = is_signed ? (Type)tu_of(r_sint, bits) : (Type)tu_of(r_uint, bits);
  return scope_add_rvalue(ss, s, Type_asValue(ss, t));
}

// ---- @typeof(V) ----
static u32 lower_builtin_typeof(superscope *ss, scope *s, ast *args) {
  return scope_add_rvalue(ss, s, Type_asValue(ss, temporary_interpret_ast(ss, s, args).t));
}

// ---- @arrof(N, T) / @ptrof(T) ----
static u32 lower_builtin_arrof(superscope *ss, scope *s, ast *args) {
  let list = builtin_arg_list(&args);
  assertMessage(list.len == 2, "@arrof(N, T) takes exactly two arguments");
  let count_tv = temporary_interpret_ast(ss, s, list.ptr[0]);
  assert_type(c_int, count_tv.t);
  assertMessage(count_tv.v.c_int, "@arrof count must be a known compile-time integer");
  BInt.expand(ss->alloc, &count_tv.v.c_int, sizeof(usize) / sizeof(bigint_unit) + 1);
  usize count = *(usize *)count_tv.v.c_int;
  Type elem = value_asType(temporary_interpret_ast(ss, s, list.ptr[1]));
  Type arr = (Type)tu_of(r_arr, {ss_getType(ss, elem), count});
  return scope_add_rvalue(ss, s, Type_asValue(ss, arr));
}
static u32 lower_builtin_ptrof(superscope *ss, scope *s, ast *args) {
  let list = builtin_arg_list(&args);
  assertMessage(list.len == 1, "@ptrof(T) takes exactly one argument");
  Type inner = value_asType(temporary_interpret_ast(ss, s, list.ptr[0]));
  Type ptr = (Type)tu_of(r_ptr, ss_getType(ss, inner));
  return scope_add_rvalue(ss, s, Type_asValue(ss, ptr));
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
    case builtin_toplevel:  return lower_builtin_toplevel(ss, s, args);
    case builtin_ast:       return lower_builtin_bare_type(ss, s, args, (Type)tu_of(c_ast, {}), "ast");
    case builtin__id:       return lower_builtin_id(ss, s, args);
    case builtin_any:       return lower_builtin_bare_type(ss, s, args, (Type)tu_of(c_any, {}), "any");
    case builtin_int:       return lower_builtin_bare_type(ss, s, args, (Type)tu_of(c_int, {}), "int");
    case builtin_type:      return lower_builtin_bare_type(ss, s, args, (Type)tu_of(c_type, {}), "type");
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
      // clang-format on
  }
  assertMessage(false, "unhandled builtin");
}

u32 lower_call(superscope *ss, scope *s, u32 callable, u32 optional_instance, ast *args);
u32 lower_call_type(superscope *ss, scope *s, u32 callable, u32 optional_instance, ast *args, Type t) {
  tu_match_void(
      t,
      (builtin, b, return lower_builtin(ss, s, b, args)),

      (c_type, t, lower_call_type(ss, s, callable, 0, args, scope_find_origional(ss, s, callable)->t)),
      (r_struct, st, {
        foreach (let item, mList_iter(st))
          if (fptr_eq(item.name, "()"))
            return lower_call(ss, s, scope_add_rvalue(ss, s, (typed_value){item.t}), callable, args);
        error_print(args, "struct has no '()' call operator defined");
      }),

      (r_tag, tt, return lower_call_type(ss, s, callable, optional_instance, args, *tt.t)),
      (r_ctime, ctt, return lower_call_type(ss, s, callable, optional_instance, args, *ctt)),
      (r_decor, dt, return lower_call_type(ss, s, callable, optional_instance, args, *dt.t)),
      (r_ptr, pt, return lower_call_type(ss, s, callable, optional_instance, args, *pt)),

      // (c_ast , _ , ), // TODO
      // (r_void , _ , ),
      // (r_uint , _ , ),
      // (r_sint , _ , ),
      // (r_arr , _ , ),
      // (c_int , _ , ),
      // (c_any , _ , ),
      (default, assertMessage(false, "type does not support call")),

      (r_func, _, {
        // Standard function call
        let fn_val = scope_find_origional(ss, s, callable);
        let params = tu_catch(r_func, fn_val->t);
        Type ret_type = mList_arr(params)[mList_len(params) - 1];

        u32 dest = scope_add_rvalue(ss, s, (typed_value){ret_type});
        let args_list = mList_init(ss->alloc, u32, 2);

        if (optional_instance) mList_push(args_list, optional_instance);

        if (tu_is(astPBlock, *args)) {
          let block = tu_catch(astPBlock, *args);
          foreach (let it, vlap(sentList_vla(block.stmts))) {
            mList_push(args_list, lower(ss, s, it));
          }
        } else mList_push(args_list, lower(ss, s, args));

        scope_add_inst(ss, s, (ir)tu_of(ir_call, {.l = callable, .d = dest, .args = {mList_len(args_list), mList_toOwned(ss->alloc, args_list)}}));
        return dest;
      }),
  );
  return 0;
}
u32 lower_call(superscope *ss, scope *s, u32 callable, u32 optional_instance, ast *args) {
  let fn_val = scope_find_origional(ss, s, callable);
  return lower_call_type(ss, s, callable, optional_instance, args, fn_val->t);
}
u32 lower_dot(superscope *ss, scope *s, u32 left, ast *right) {
  fptr field_name;
  tu_match_void(
      *right,
      (astId, id, field_name = id;),
      (astString, str, field_name = slice_split(str, (1, str.len - 1))[0];),
      (default, assertMessage(false, "expected id or string for member access"))
  );

  let left_val = scope_find_origional(ss, s, left);
  Type current_type = left_val->t;

  bool inherited_const = false;
  bool inherited_valatile = false;

  while (true) {
    tu_match_void(
        current_type,
        (r_tag, tag, current_type = *tag.t; continue;),
        (r_ptr, pt, current_type = *pt; continue;),
        (r_ctime, ct, current_type = *ct; continue;),
        (r_decor, dec, {
          inherited_const |= dec.constant;
          inherited_valatile |= dec.valatile;
          current_type = *dec.t;
          continue;
        }),
        (r_struct, st, {
          foreach (let item, mList_iter(st)) {
            if (fptr_eq(item.name, field_name)) {
              typed_value member_val = {item.t};

              if (Type_is_comptime(member_val.t))
                member_val.t = (Type)tu_of(r_ctime, ss_getType(ss, member_val.t));
              else if (inherited_const || inherited_valatile)
                member_val.t = (Type)tu_of(r_decor, {.t = ss_getType(ss, member_val.t), .constant = inherited_const, .valatile = inherited_valatile});

              return scope_add_rvalue(ss, s, member_val);
            }
          }
          assertMessage(false, "struct member not found");
        }),
        (default, error_print(right, "{Type} does not support dot", current_type)),
    );
    break;
  }
  return 0;
}
static inline fptr fptr_from_opnumber(u32 n) {
  static char buf[sizeof(u32)];
  memcpy(buf, &n, sizeof(u32));
  usize len = 0;
  while (len < sizeof(u32) && buf[len])
    len++;
  return (fptr){len, (u8 *)buf};
}

u32 lower_operator_overload(superscope *ss, scope *s, u32 left, enum_op op, ast *right) {
  static char opname[5] = {};
  memcpy(&op, opname, sizeof(op));
  let op_name = fp((char *)opname);
  let left_val = scope_find_origional(ss, s, left);
  Type current_type = left_val->t;

  while (true) {
    tu_match_void(
        current_type,
        (r_tag, tag, current_type = *tag.t; continue;),
        (r_ptr, pt, current_type = *pt; continue;),
        (r_ctime, ct, current_type = *ct; continue;),
        (r_decor, dec, current_type = *dec.t; continue;),
        (r_struct, st, {
          foreach (let item, mList_iter(st)) {
            if (fptr_eq(item.name, op_name)) {
              u32 method_val = scope_add_rvalue(ss, s, (typed_value){item.t});
              return lower_call(ss, s, method_val, left, right);
            }
          }
          return 0;
        }),
        (default, return 0;),
    );
    break;
  }
  return 0;
}

u32 lower_boolean_op(superscope *ss, scope *s, u32 left, enum_boolean_op bop, ast *right) {
  u32 right_val = lower(ss, s, right);

  let left_tv = scope_find_origional(ss, s, left);
  let right_tv = scope_find_origional(ss, s, right_val);

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

  let left_tv = scope_find_origional(ss, s, left);
  let right_tv = scope_find_origional(ss, s, right_val);

  assertMessage(Type_isint(left_tv->t) && Type_isint(right_tv->t));

  u32 dest = scope_add_rvalue(ss, s, (typed_value){left_tv->t}); // TODO: bigger value promotion?

  scope_add_inst(ss, s, (ir)tu_of(ir_op, {.l = left, .r = right_val, .d = dest, .op = (u32)opn}));

  return dest;
}

u32 lower_op(superscope *ss, scope *s, u32 left, enum_op opn, ast *right) {
  switch (opn) {
      // clang-format off
    default: assertMessage(false, "unhandled math operator");
    case enop_dot: return lower_dot(ss, s, left, right);
    case enop_colon: assertMessage(false, "colon operator must be followed by a function call");
    case enop_call: return lower_call(ss, s, left, 0, right);
      // clang-format on

    case enop_pipe: {
      let pipscope = scope_inferior(ss, s);
      scope_bind(ss, pipscope, left, fp("_"));
      scope_add_inst(ss, s, (ir)tu_of(ir_block, pipscope));

      u32 inner_res = lower(ss, pipscope, right);

      let inner_tv = scope_find_origional(ss, pipscope, inner_res);
      u32 hoisted_res = scope_add_rvalue(ss, s, *inner_tv);

      scope_add_inst(
          ss,
          s,
          (ir)tu_of(ir_op, {.l = inner_res, .r = 0, .d = hoisted_res, .op = enop_set})
      );
      return hoisted_res;
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
    case enop_leq:
    case enop_eq:
    case enop_neq: {
      static char opname[5] = {};
      memcpy(opname, &opn, sizeof(opn));
      let op_name = fp((char *)opname);
      if_decl (let overload_result, lower_operator_overload(ss, s, left, opn, right))
        return overload_result;

      switch (opn) {
        case enop_set: {
          u32 right_val = lower(ss, s, right);

          let l_tv = scope_find_origional(ss, s, left);
          let r_tv = scope_find_origional(ss, s, right_val);

          Type l_type = *ss_getType(ss, l_tv->t);
          Type r_type = *ss_getType(ss, r_tv->t);

          bool is_const = false;

          iflet_tu (let dec, r_decor, l_type) {
            is_const = dec.constant;
            l_type = *dec.t; // Peeling off const/volatile
          }

          iflet_tu (let dec, r_decor, r_type)
            r_type = *dec.t; // RHS qualifiers don't matter for the underlying type match

          if (tu_is(c_any, l_type))
            l_tv->t = r_type;
          else
            // Both are unqualified now, so structural comparison works perfectly
            assertMessage(!Type_cmp(l_type, r_type), "type mismatch in assignment");
          assertMessage(!is_const, "cannot assign to a const variable");

          scope_add_inst(ss, s, (ir)tu_of(ir_op, {.l = right_val, .r = 0, .d = left, .op = enop_set}));

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

// Forward declaration for recursively resolving aliases

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
                     ? value_asType(temporary_interpret_ast(env, s, decl.type))
                     : (Type)tu_of(c_any, {});
         u32 val = decl.value
                       ? tu_is(c_ast, t)
                             ? scope_add_rvalue(env, s, (typed_value){t, decl.value})
                             : lower(env, s, decl.value)
                       : scope_add_rvalue(env, s, (typed_value){t});
         assertMessage(tu_is(c_any, t) || !Type_cmp(t, scope_find_origional(env, s, val)->t));
         if (decl.is_const) {
           let resolved_val = scope_find_origional(env, s, val);
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
         let resolved_val = scope_find_origional(env, s, val);
         resolved_val->t = *ss_getType(env, resolved_val->t);
         val;
       })),
      (astBBlock, ast_block, ({
         let block = scope_inferior(env, s);
         scope_add_inst(env, s, (ir)tu_of(ir_block, block));
         u32 last = 0;
         foreach (let it, vlap(sentList_vla(ast_block.stmts)))
           last = lower(env, block, it);
         last ?: scope_add_rvalue(env, s, (typed_value){tu_of(r_void, {})});
       })),
      (astPBlock, ast_block, ({
         let vla = sentList_vla(ast_block.stmts);
         assertMessage(countof(*vla) == 1, "no tuples");
         let sn = scope_inferior(env, s);
         msList_push(env->alloc, s->instructions, (ir)tu_of(ir_block, sn));
         lower(env, sn, (*vla)[0]);
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

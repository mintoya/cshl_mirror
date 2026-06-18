#include "ast.h"
#include "form.h"
#include "typ.h"
#include "wheels/smallstream.h"
#include <assert.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wheels/allocators/arenaAllocator.h>
#include <wheels/bigint.h>
#include <wheels/funct.h>
#include <wheels/macros.h>
#include <wheels/mytypes.h>
#include <wheels/sList.h>
#include <wheels/shmap.h>
#include <wheels/tu_macros.h>

#define log(msg, ...) println("(log:{cstr})\t" msg, (char *)__FUNCTION__ __VA_OPT__(, __VA_ARGS__))

__attribute__((noreturn)) void error_ast(ast a, slice(c8) message) {
  tu_match(
      a,
      case (astId, s, { error_at(s.ptr, message); }),
      case (astNumber, s, { error_at(s.ptr, message); }),
      case (astString, s, { error_at(s.ptr, message); }),

      case (astDecl, d, { error_ast(d.name[0], message); }),
      case (astOp, o, { error_ast(o.right[0], message); }),

      case (astPBlock, b, { error_ast(b.stmts[0][0], message); }),
      case (astBBlock, b, { error_ast(b.stmts[0][0], message); }),
      case (astABlock, b, { error_ast(b.items[0][0], message); }),

      case (astEmptyStatement, _, { error_at(nullptr, message); }),
      default(abort();),
  );
}

// {operators
typedef itypeof(astOp, symLiteral) astOp_sym;
typedef itypeof(astOp, number_rep) astOp_num;
typedef union astOp_union {
  astOp_sym sym;
  astOp_num num;
} astOp_union;

var_ callOp = /* */ (astOp_union){"()"};
var_ dotOp = /*  */ (astOp_union){"."};
var_ mulOp = /*  */ (astOp_union){"*"};
var_ divOp = /*  */ (astOp_union){"/"};
var_ modOp = /*  */ (astOp_union){"%"};
var_ addOp = /*  */ (astOp_union){"+"};
var_ subOp = /*  */ (astOp_union){"-"};
var_ andOp = /*  */ (astOp_union){"and"};
var_ notOp = /*  */ (astOp_union){"not"};
var_ orOp = /*   */ (astOp_union){"or"};
var_ bandOp = /* */ (astOp_union){"band"};
var_ bnotOp = /* */ (astOp_union){"bnot"};
var_ borOp = /*  */ (astOp_union){"bor"};
var_ shrOp = /*  */ (astOp_union){"shr"};
var_ shlOp = /*  */ (astOp_union){"shl"};
var_ lesOp = /*  */ (astOp_union){"<"};
var_ morOp = /*  */ (astOp_union){">"};
var_ leqOp = /*  */ (astOp_union){"<="};
var_ moqOp = /*  */ (astOp_union){">="};
var_ eqOp = /*   */ (astOp_union){"=="};
var_ neqOp = /*  */ (astOp_union){"!="};
var_ pipeOp = /* */ (astOp_union){"|>"};
var_ setOp = /*  */ (astOp_union){"="};
// }

// {type
int buffer_cmp(const void *a, const void *b) {
  var_ ap = (fptr *)a;
  var_ bp = (fptr *)b;
  return fptr_cmp(*ap, *bp);
}

static var_ t_void = (itype_t)tu_of(it_void, {});

#define getType(x) getType((itype_t)x)
itype_t *(getType)(itype_t t) {

#define mHmap_setorgetinitmanaged(map, key, value)                                \
  ({                                                                              \
    map = map ?: mHmap_init(stdAlloc, typeof(key), typeof((*map)(nullptr, key))); \
    if (HMap_load((HMap *)map) > 75)                                              \
      HMap_manage((HMap **)&map, stdAlloc, HMap_getMetaSize((HMap *)map) * 5);    \
    var_ _saved = mHmap_get(map, key);                                            \
    if (!_saved) {                                                                \
      mHmap_set(map, key, value);                                                 \
      _saved = mHmap_get(map, key);                                               \
      assertMessage(_saved);                                                      \
    }                                                                             \
    _saved;                                                                       \
  })

  tu_match(
      t,
      case (it_void, _, {
        return &t_void;
      }),

      case (it_uint, bits, {
        static mHmap(u16, itype_t *) map = nullptr;
        return *mHmap_setorgetinitmanaged(
            map, bits, aValue(stdAlloc, (itype_t)tu_of(it_uint, bits))
        );
      }),
      case (it_sint, bits, {
        static mHmap(u16, itype_t *) map = nullptr;
        return *mHmap_setorgetinitmanaged(
            map, bits, aValue(stdAlloc, (itype_t)tu_of(it_sint, bits))
        );
      }),
      case (it_ptr, ptr, {
        var_ saved = getType(ptr[0]);
        static mHmap(itype_t *, itype_t *) map = nullptr;
        return *mHmap_setorgetinitmanaged(map, saved, aValue(stdAlloc, (itype_t)tu_of(it_ptr, saved)));
      }),
      case (it_arr, arr, {
        var_ saved = getType(arr.type[0]);
        typedef struct {
          typeof(saved) t;
          usize s;
        } arr_t;

        arr_t v = (arr_t){saved, arr.length};

        static mHmap(arr_t, itype_t *) map = nullptr;
        return *mHmap_setorgetinitmanaged(map, v, aValue(stdAlloc, (itype_t)tu_of(it_arr, {saved, arr.length})));
      }),
      case (it_fun, args_ret, {
        static msList(fptr) ptrs = nullptr;
        ptrs = ptrs ?: msList_init(stdAlloc, fptr);
        static msList(itype_t *) ptrtypes = nullptr;
        ptrtypes = ptrtypes ?: msList_init(stdAlloc, itype_t *);

        usize len = sentList_length(args_ret, sizeof(*args_ret));
        foreach (var_ f, span(args_ret, len))
          *f = getType(*f[0]);

        var_ ptr = (fptr){(len + 1) * sizeof(*args_ret), (u8 *)args_ret};
        var_ w = bbsearch(&ptr, ptrs, msList_len(ptrs), sizeof(*ptrs), buffer_cmp);
        usize idx = ((fptr *)w.p - ptrs);

        if (!w.f) {
          var_ np = (u8 *)aAlloc(stdAlloc, ptr.len);
          var_ npf = (fptr){ptr.len, np};
          memcpy(np, ptr.ptr, ptr.len);
          msList_ins(stdAlloc, ptrs, idx, npf);
          var_ new_t = aValue(stdAlloc, (itype_t)tu_of(it_fun, (typeof(args_ret))npf.ptr));
          msList_ins(stdAlloc, ptrtypes, idx, new_t);
        }

        return ptrtypes[idx];
      }),
      case (it_struct, fields, {
        static msList(fptr) ptrs = nullptr;
        ptrs = ptrs ?: msList_init(stdAlloc, fptr);
        static msList(itype_t *) ptrtypes = nullptr;
        ptrtypes = ptrtypes ?: msList_init(stdAlloc, itype_t *);

        usize len = sentList_length(fields, sizeof(*fields));
        foreach (var_ f, span(fields, len))
          f->type = getType(f->type[0]);

        var_ ptr = (fptr){(len + 1) * sizeof(*fields), (u8 *)fields};
        var_ w = bbsearch(&ptr, ptrs, msList_len(ptrs), sizeof(*ptrs), buffer_cmp);
        usize idx = ((fptr *)w.p - ptrs);

        if (!w.f) {
          var_ np = (u8 *)aAlloc(stdAlloc, ptr.len);
          var_ npf = (fptr){ptr.len, np};
          memcpy(np, ptr.ptr, ptr.len);
          msList_ins(stdAlloc, ptrs, idx, npf);
          var_ new_t = aValue(stdAlloc, (itype_t)tu_of(it_struct, (typeof(fields))npf.ptr));
          msList_ins(stdAlloc, ptrtypes, idx, new_t);
        }

        return ptrtypes[idx];
      }),
  );

  assertMessage(false, "unreachable?");
  return nullptr;
}

// }

// {values
/*
 * layout :
 *  type : type_value
 *  union {
 *    function_value : {names: []fptr, rvalue_init: value , builtin_id } ;
 *    type_value : type_value ;
 *    ast_value  : ast ;
 *    runtime_value : iptr ;
 *  }
 */
typedef struct rtval rtval;
typedef enum type_value_enum : u8 {
  builtin_none,

  builtin_$sint,
  builtin_$uint,
  builtin_$noExpand,
  builtin_$expand,
  builtin_$while,
  builtin_$type,
  builtin_$void,
  builtin_$ast,

  builtin_fn,
  builtin_return,
  builtin_struct,
} builtin_id;
tu_def(
    (type_value, u8),
    // something
    (tv_type, itype_t *_Nullable),
    (tv_ast, nothing_t),
    (tv_runtime, itype_t *),
    (tv_strct, struct { struct { fptr name; rtval *_Nullable value; } * fields ; itype_t* layout; }),
    (tv_function, struct { fptr name; rtval * _Nullable value; } *),
    (tv_builtin, builtin_id),
);

typedef struct rtval {
  type_value type;
  union {
    /*
      main = fn(( arg : []str ),r : int = 0) : { };
      names will be "arg" , "r" ; nullablity in the ptr field
      values will have to store int for r as 0, others null

      not null terminated, same length as the function's signature
    */

    ast *function_value; // active for it_fun
    ast *ast_value;      // active for it_ast
    iptr runtime_value;  // active for everything else
  };
} value;

_Static_assert(!builtin_none, "");

static var_ ty_ast = (value){};
static var_ ty_type = (value){};
static var_ ty_void = (value){};

itype_t *value_Typeof(const value v) {
  if (tu_is(tv_runtime, v.type))
    return tu_assert(tv_runtime, v.type);
}
itype_t *value_isInt(const value v) {
  var_ type = value_Typeof(v);
  if (type && EQUAL_ANY(1, tu_is(it_sint, type[0]), tu_is(it_uint, type[0]))) {
    return type;
  } else return nullptr;
}
itype_t *value_isSInt(const value v) {
  var_ type = value_Typeof(v);
  if (type && tu_is(it_sint, type[0])) {
    return type;
  } else return nullptr;
}
itype_t *value_isUInt(const value v) {
  var_ type = value_Typeof(v);
  if (type && tu_is(it_uint, type[0])) {
    return type;
  } else return nullptr;
}
itype_t *value_isStruct(const value v) {
  var_ type = value_Typeof(v);
  if (type && tu_is(it_struct, type[0])) {
    return type;
  } else return nullptr;
}
itype_t *value_isFn(const value v) {
  var_ type = value_Typeof(v);
  if (type && tu_is(it_fun, type[0])) {
    return type;
  } else return nullptr;
}
itype_t *value_isArr(const value v) {
  var_ type = value_Typeof(v);
  if (type && tu_is(it_arr, type[0])) {
    return type;
  } else return nullptr;
}
itype_t *value_isPtr(const value v) {
  var_ type = value_Typeof(v);
  if (type && tu_is(it_ptr, type[0])) {
    return type;
  } else return nullptr;
}
itype_t *value_isType(const value v) {
  if (tu_is(tv_type, v.type)) tu_assert(tv_type, v.type);
  return nullptr;
}
// }

// {state

typedef msHmap(value) scope;
typedef struct eval_state {
  mList(scope) scopes;
  msList(u8) stack, heap;
  msList(struct {
    u16 count;
    u16 curr;
    u16 size;
    iptr place;
  }) heap_state; // sorted
} state;
state state_init(AllocatorV allocator) {
  var_ l = mList_init(allocator, scope);
  mList_push(l, msHmap_init(allocator, value));
  return (state){
      .scopes = l,
      .stack = msList_init(allocator, u8),
      .heap = msList_init(allocator, u8),
      .heap_state = msList_init(allocator, ptrstype(itypeof(state, heap_state))),
  };
}

iptr state_stack_current(state s) { return msList_len(s.stack); }
iptr state_stack_push(state *s, usize size) {
  size = lineup(size, alignof(myAlign));
  usize res = msList_len(s->stack);
  msList_pushArr(
      mList_allocator(s->scopes),
      s->stack,
      *VLAP((u8 *)0, size)
  );
  return res + alignof(myAlign);
}
iptr state_heap_alloc(state *s, usize size) {
  size = lineup(size, alignof(myAlign));
  typedef ptrstype(itypeof(state, heap_state)) regiontype;
  foreach (regiontype *region, span(s->heap_state, msList_len(s->heap_state))) {
    if (region->size - region->curr >= size) {
      iptr res = region->place + region->curr;
      region->count++;
      region->curr += size;

      if (region != s->heap_state) {
        var_ t = *region;
        usize idx = region - s->heap_state;
        msList_rem(s->heap_state, idx);
        msList_ins(mList_allocator(s->scopes), s->heap_state, 0, t);
      }

      return res + alignof(myAlign);
    }
  }
  usize nrs = 1024;
  nrs = nrs < size ? size : nrs;

  usize res = msList_len(s->heap);

  msList_pushArr(
      mList_allocator(s->scopes),
      s->heap,
      *VLAP((u8 *)0, nrs)
  );

  msList_ins(
      mList_allocator(s->scopes),
      s->heap_state,
      0,
      ((regiontype){
          .count = 1,
          .curr = size,
          .size = nrs,
          .place = res,
      })
  );

  return res + alignof(myAlign);
}
void state_heap_free(state *s, iptr place) {
  place -= alignof(myAlign);
  typedef ptrstype(itypeof(state, heap_state)) regiontype;
  foreach (regiontype *region, span(s->heap_state, msList_len(s->heap_state)))
    if (region->place <= place && region->place + region->size > place) {
      region->count--;
      if (!region->count && region != s->heap_state && s->heap_state->count) {
        var_ t = *region;
        t.curr = 0;
        usize idx = region - s->heap_state;
        msList_rem(s->heap_state, idx);
        msList_ins(mList_allocator(s->scopes), s->heap_state, 0, t);
      }
      break;
    }
}
u8 *state_fromV(state *s, iptr place) {
  u8 *a = place < 0 ? s->heap : s->stack;
  place = place < 0 ? -place : place;
  assertMessage(place >= alignof(myAlign));
  place -= alignof(myAlign);
  return a + place;
}
value state_create(state *s, type_value type, bool isHeap) {
  log("creating : {type_value}", type);
  usize size = 0;
  if (tu_is(tv_runtime, type))
    size = lineup(
        itype_size(REF(itype, {0, tu_assert(tv_runtime, type)}, )),
        alignof(myAlign)
    );
  var_ res = (value){};
  res.type = type;
  if (size)
    res.runtime_value =
        isHeap
            ? -state_heap_alloc(s, size)
            : state_stack_push(s, size);
  return res;
}
void state_destroy(state *s, value v) {
  usize size = 0;
  if (tu_is(tv_runtime, v.type))
    size = lineup(
        itype_size(REF(itype, {0, tu_assert(tv_runtime, v.type)}, )),
        alignof(myAlign)
    );
  if (size)
    if (v.runtime_value < 0) state_heap_free(s, -v.runtime_value);
}

// }

// {literal conversion

i64 astStr_ton(fptr str_number) {
  u8 *ptr = str_number.ptr;
  usize len = str_number.len;
  bool neg = ptr[0] == '-';
  if (neg) {
    ptr++;
    len--;
  }
  u64 val = 0;
  if (len > 2 && ptr[1] == 'x')
    for (usize i = 2; i < len; i++)
      val = val * 16 + (ptr[i] >= 'a' ? ptr[i] - 'a' + 10 : ptr[i] - '0');
  else if (len > 2 && ptr[1] == 'b')
    for (usize i = 2; i < len; i++)
      val = val * 2 + (ptr[i] - '0');
  else
    for (usize i = 0; i < len; i++)
      val = val * 10 + (ptr[i] - '0');
  return neg ? -val : val;
}
usize astStr_len(fptr fp) {
  if (fp.len < 2) return 0;

  usize actual_len = 0;
  u8 *ptr = fp.ptr;

  for (usize i = 1; i < fp.len - 1; i++) {
    if (ptr[i] == '\\') i++;
    actual_len++;
  }

  return actual_len;
}
void astStr_tom(fptr from, fptr *to) {
  if (from.len < 2) {
    to->len = 0;
    return;
  }

  u8 *src = from.ptr;
  u8 *dst = to->ptr;
  usize di = 0;

  for (usize i = 1; i < from.len - 1; i++) {
    if (src[i] == '\\') {
      i++;
      switch (src[i]) {
        case 'n':
          dst[di++] = '\n';
          break;
        case 'r':
          dst[di++] = '\r';
          break;
        case 't':
          dst[di++] = '\t';
          break;
        case '0':
          dst[di++] = '\0';
          break;
        case '\\':
          dst[di++] = '\\';
          break;
        case '"':
          dst[di++] = '"';
          break;
        default:
          dst[di++] = src[i];
          break;
      }
    } else {
      dst[di++] = src[i];
    }
  }

  to->len = di;
}
// }

void eval_coerce(
    state *evalState,
    type_value *_Nullable expected,
    value *v
) {
  if (!expected) return;
  var_ to_type_full = expected[0];
  if (tu_is(tv_builtin, to_type_full))
    assertMessage(false, "builtins dont have a type "); // TODO give builtins a type

  if (tu_is(tv_strct, to_type_full)) {
    if (tu_assert(tv_strct, to_type_full).layout == value_Typeof(*v)) return;
    else assertMessage(false);
  }

  var_ to_type = value_Typeof((value){.type = to_type_full});
  if (to_type == value_Typeof(*v)) return;

  if (to_type && (tu_is(it_uint, to_type[0]) || tu_is(it_sint, to_type[0]))) {
    var_ item_type = value_isInt(*v);
    if (!item_type) assertMessage(false);

    u16 to_bits = 0;
    u16 from_bits = 0;
    if (tu_is(it_uint, to_type[0]))
      to_bits = tu_assert(it_uint, to_type[0]);
    else
      to_bits = tu_assert(it_sint, to_type[0]);

    if_decl (var_ t, value_isSInt(*v))
      from_bits = tu_assert(it_sint, t[0]);
    if_decl (var_ t, value_isUInt(*v))
      from_bits = tu_assert(it_uint, t[0]);
    assertMessage(from_bits);

    if (to_bits < from_bits) assertMessage(false, "bitcast would loose precision");
    // set some bits;
    v[0].type = expected[0];
  } else assertMessage(false);
}

value eval(
    state *evalState,
    type_value *_Nullable expected,
    ast astv,
    bool *retflag
);

value *eval_symbol(
    state *evalState,
    fptr name
) {

  // {builtin resolution

#pragma push_macro("sbin")
#define sbin(id)                      \
  ({                                  \
    static value _v = {               \
        .type = tu_of(tv_builtin, id) \
    };                                \
    &_v;                              \
  })
  if /**/ (fptr_eq(name, "@sint" /*    */)) return sbin(builtin_$sint);
  else if (fptr_eq(name, "@uint" /*    */)) return sbin(builtin_$uint);
  else if (fptr_eq(name, "@noExpand" /**/)) return sbin(builtin_$noExpand);
  else if (fptr_eq(name, "@expand" /*  */)) return sbin(builtin_$expand);
  else if (fptr_eq(name, "@while" /*   */)) return sbin(builtin_$while);
  else if (fptr_eq(name, "@type" /*    */)) return sbin(builtin_$type);
  else if (fptr_eq(name, "@ast" /*     */)) return sbin(builtin_$ast);
  else if (fptr_eq(name, "@void" /*    */)) return sbin(builtin_$void);
  else if (fptr_eq(name, "return" /*   */)) return sbin(builtin_return);
  else if (fptr_eq(name, "struct" /*   */)) return sbin(builtin_struct);
  else if (fptr_eq(name, "fn" /*       */)) return sbin(builtin_fn);
  // }
#pragma pop_macro("sbin")

  foreach (var_ i, range(mList_len(evalState->scopes), 0)) {
    var_ s = mList_arr(evalState->scopes)[i - 1];
    var_ sy = msHmap_get(s, name);
    if (sy) return sy;
  }
  error_at(name.ptr, snprint(stdAlloc, "unknown symbol {slice(c8)}", name));
}

#define tu_catch(type, item, ...)             \
  ({                                          \
    var_ item_eval = item;                    \
    tu_is(type, item_eval)                    \
        ? item_eval.type                      \
        : ({                                  \
            __VA_ARGS__;                      \
            assertMessage(false, "uncaught"); \
            (type){};                         \
          });                                 \
  })

value eval_builtin(
    state *evalState,
    builtin_id builtin,
    ast right,
    type_value *_Nullable expected,
    bool retflag[1]
) {
  assertMessage(builtin);
  switch (builtin) {
    case builtin_return: {
      retflag[0] = 1;
      return eval(evalState, expected, right, REF(bool, {}));
    } break;
    case builtin_$type:
    case builtin_$void:
    case builtin_$ast: {
      log("{i8},{ast}", builtin, &right);
      return builtin == builtin_$ast
                 ? ty_ast
             : builtin == builtin_$type
                 ? ty_type
                 : ty_void;
    } break;
    case builtin_$uint:
    case builtin_$sint: {
      log("signed type {i8},\n{ast}", builtin, &right);
      var_ match = ast_match(stdAlloc, right, fp("Pn"));
      defer { msList_deInit(stdAlloc, match); };
      log("{ptr}", match);
      if (!match || !msList_len(match))
        error_ast(right, snprint(stdAlloc, "expected exactly one integer argument, e.g., (32)"));
      i64 n = astStr_ton(match[0]->astNumber);
      log("{i64}", n);

      if (n < 1 || n > UINT16_MAX)
        error_ast(right, snprint(stdAlloc, "expected positive number within bounds"));

      bool isSigned = builtin == builtin_$sint;

      itype_t *ftyp = getType((isSigned ? (itype_t)tu_of(it_sint, (u16)n) : (itype_t)tu_of(it_uint, (u16)n)));

      value res = {.type = tu_of(tv_type, ftyp)};
      log("{type_value}", res.type);

      return res;
    } break;
    case builtin_struct: {
      var_ matches = ast_match(stdAlloc, right, fp("B + d"));
      defer { msList_deInit(stdAlloc, matches); };

      if (!matches)
        error_ast(right, snprint(stdAlloc, "struct expects a block of field declarations"));

      var_ field_count = msList_len(matches);
      var_ field_types = &aCreate(stdAlloc, itype_t *, field_count + 1);
      defer { aFree(stdAlloc, field_types, sizeof(*field_types)); };

      for (usize i = 0; i < field_count; i++) {
        var_ decl_ast = matches[i][0];
        var_ type_expr = tu_catch(astDecl, decl_ast, error_ast(decl_ast, snprint(stdAlloc, "expected declaration"))).type;
        if (!type_expr)
          error_ast(decl_ast, snprint(stdAlloc, "field must have a type"));

        bool retflag = false;
        var_ type_val = eval(evalState, &ty_type.type, type_expr[0], &retflag);
        assertMessage(!retflag, "returned inside struct initialization");
        if (!tu_is(tv_type, type_val.type))
          error_ast(type_expr[0], snprint(stdAlloc, "field type must evaluate to a type"));
        field_types[0][i] = tu_assert(tv_type, type_val.type);
      }

      itype_t *struct_type = getType((itype_t)tu_of(it_struct, *field_types));

      var_ res = (value){.type = tu_of(tv_type, struct_type)};
      log("{type_value}", res.type);
      return res;
    }
    default:
      assertMessage(false, "forgot something , %i", builtin);
  }
}
value eval_call(
    state *evalState,
    value left,
    ast right,
    type_value *_Nullable expected,
    bool retflag[1]
) {
  if (tu_is(tv_builtin, left.type))
    return eval_builtin(
        evalState,
        tu_assert(tv_builtin, left.type),
        right,
        expected,
        retflag
    );

  log("call op with {ast}", &right);
  log("call op with {type_value}", left.type);

  assertMessage(false, "fallthrough");
}
value eval_op(
    state *evalState,
    value *left,
    ast right,
    u64 op,
    bool retflag[1]
) {
  assertMessage(retflag);
  if (op == callOp.num) {
    assertMessage(left);
    return eval_call(evalState, left[0], right, nullptr, retflag);
  }
  if (
      EQUAL_ANY(
          op,
          addOp.num,
          subOp.num,
          mulOp.num,
          divOp.num,
          modOp.num,
      )
  ) {
    assertMessage(left);
    var_ rvalue = eval(evalState, &left->type, right, retflag);
    var_ arena = arena_new_ext(mList_allocator(evalState->scopes), 1024);
    defer { arena_cleanup(arena); };

    var_ type = value_Typeof(left[0]);
    bool iss = tu_is(it_sint, type[0]);
    usize bc =
        iss
            ? tu_assert(it_sint, type[0])
            : tu_assert(it_uint, type[0]);

    var_ rb =
        BInt.from.bits(arena, state_fromV(evalState, rvalue.runtime_value), bc, iss);
    var_ lb =
        BInt.from.bits(arena, state_fromV(evalState, left[0].runtime_value), bc, iss);
    log("{bigint}", lb);
    if (op == addOp.num)
      log("+", rb);
    else if (op == subOp.num)
      log("-", rb);
    else if (op == mulOp.num)
      log("*", rb);
    else if (op == divOp.num)
      log("//", rb);
    else if (op == modOp.num)
      log("%", rb);
    log("{bigint}", rb);

    log("=");

    if (op == addOp.num)
      BInt.advanced.add_ip(arena, &lb, rb, 0);
    else if (op == subOp.num)
      BInt.advanced.sub_ip(arena, &lb, rb, 0);
    else if (op == mulOp.num)
      lb = BInt.mul(arena, rb, lb);
    else if (op == divOp.num)
      lb = BInt.div(arena, lb, rb).div;
    else if (op == modOp.num)
      lb = BInt.div(arena, lb, rb).mod;
    log("{bigint}", lb);

    var_ res = state_create(evalState, left->type, true);
    usize tsize = ({ itype t = (itype){.type = tu_assert(tv_runtime, res.type)};itype_size(&t); });

    BInt.expand(arena, &lb, tsize / sizeof(bigint_unit) + 1);

    memcpy(
        state_fromV(evalState, res.runtime_value),
        lb,
        tsize
    );
    return res;
  }

  log("unhandled {} ,{ast}", op, &right);
  assertMessage(false);
}

value eval(
    state *evalState,
    type_value *_Nullable expected,
    ast astv,
    bool *retflag
) {
  assertMessage(retflag);
  var_ allocator = msHmap_allocator(mList_last(evalState->scopes));

  value res = ty_void;

  tu_match(
      astv,
      case (astId, id, {
        value *sym = eval_symbol(evalState, id);
        if (sym) res = *sym;
        else error_ast(astv, snprint(stdAlloc, "unknown symbol '{slice(c8)}'", id));
      }),

      // {literals
      case (astNumber, num, {
        var_ rs = state_create(
            evalState,
            // ty_get((itype_t)tu_of(it_sint, 64)),
            (type_value)tu_of(
                tv_runtime,
                getType(tu_of(it_sint, 64))
            ),
            false
        );
        var_ n = astStr_ton(num);
        memcpy(state_fromV(evalState, rs.runtime_value), &n, sizeof n);
        res = rs;
      }),

      case (astString, str, {
        var_ rs = state_create(
            evalState,
            (type_value)tu_of(
                tv_runtime,
                getType(tu_of(
                    it_arr,
                    {
                        .type = getType(tu_of(it_sint, 8)),
                        .length = astStr_len(str),
                    }
                ))
            ),
            false
        );
        memcpy(state_fromV(evalState, rs.runtime_value), str.ptr, str.len);
        res = rs;
      }),
      // }

      case (astDecl, decl, {
        value typev = {};
        value val = {};

        if (decl.type)
          typev = eval(evalState, &ty_type.type, decl.type[0], retflag);

        type_value expected_tv = {};
        if (decl.type) {
          if (tu_is(tv_type, typev.type)) expected_tv = (type_value)tu_of(tv_runtime, tu_assert(tv_type, typev.type));
          else expected_tv = typev.type;
        }

        if (decl.value)
          val = eval(evalState, decl.type ? &expected_tv : nullptr, decl.value[0], retflag);
        else if (decl.type)
          val = state_create(evalState, expected_tv, true);

        if (decl.name) {
          var_ id = tu_catch(astId, decl.name[0], error_ast(decl.name[0], snprint(stdAlloc, "expected identifier")));
          if (msHmap_get(mList_last(evalState->scopes), id))
            error_at(id.ptr, snprint(stdAlloc, "this variable is taken for this scope"));

          msHmap_set(mList_last(evalState->scopes), id, val);
        }
        res = val;
      }),

      case (astOp, op, {
        if (op.left)
          res = eval_op(
              evalState,
              (value[1]){eval(evalState, nullptr, op.left[0], retflag)},
              op.right[0],
              op.number_rep,
              retflag
          );
        else
          res = eval_op(evalState, nullptr, op.right[0], op.number_rep, retflag);
      }),

      // {blocks
      case (astBBlock, block, {
        if (expected) {
          var_ rty = expected[0];
          assertMessage(!tu_is(tv_builtin, rty));

          log("unhandled : {ast}", &astv);
          assertMessage(false);
        } else {
          AllocatorV arena = arena_new_ext(allocator, 1024);
          var_ nextScope = msHmap_init(arena, value);
          mList_push(evalState->scopes, nextScope);
          defer { // the scope is also created with the allocator
            arena_cleanup(msHmap_allocator(mList_pop(evalState->scopes)));
          };

          foreach (var_ stmnt, vlap(sentList_vla(block.stmts))) {
            var_ t = eval(evalState, nullptr, stmnt[0], retflag);
            if (*retflag) {
              res = t;
              goto coerce;
            }
          }
        }
      }),

      case (astPBlock, block, {
        var_ s = ast_match(allocator, astv, fp("Pa"));
        if (!s || !msList_len(s))
          error_ast(astv, snprint(stdAlloc, "parenthesis by itself with multiple args"));
        res = eval(evalState, expected, s[0][0], retflag);
      }),

      case (astABlock, block, {
        assertMessage(false, "lol");
      }),
      // }

      case (astEmptyStatement, empty, {}),

      default({
        error_ast(astv, snprint(stdAlloc, "forgot something , {ast}", astv));
      })
  );

  {
  coerce:
    return (eval_coerce(evalState, expected, &res), res);
  }
}

REGISTER_PRINTER(type_value, {
  if (tu_is(tv_builtin, in)) {
    switch (tu_assert(tv_builtin, in)) {
      case builtin_$sint: {
        PUTS("@sint");
      } break;
      case builtin_$uint: {
        PUTS("@uint");
      } break;
      case builtin_$noExpand: {
        PUTS("@noExpand");
      } break;
      case builtin_$expand: {
        PUTS("@expand");
      } break;
      case builtin_$while: {
        PUTS("@while");
      } break;
      case builtin_$type: {
        PUTS("@type");
      } break;
      case builtin_$void: {
        PUTS("@void");
      } break;
      case builtin_$ast: {
        PUTS("@ast");
      } break;
      case builtin_return: {
        PUTS("'return'");
      } break;
      case builtin_struct: {
        PUTS("'struct'");
      } break;
      default: {
        PUTS("uninitialized?");
        USETYPEPRINTER(usize, tu_assert(tv_builtin, in));
      } break;
    }
  } else if (tu_is(tv_strct, in)) {
    // This is a struct value (instance), not a type. Print with field names.
    var_ st = tu_assert(tv_strct, in);
    PUTS("struct{");
    if (st.layout && tu_is(it_struct, st.layout[0])) {
      var_ layout_st = tu_assert(it_struct, st.layout[0]);
      var_ vlap = sentList_vla(layout_st);
      bool first = true;
      foreach (var_ it, pair(i, *vlap)) {
        if (!first) PUTS(",");
        first = false;
        USENAMEDPRINTER("slice(c8)", st.fields[i].name);
        PUTS(":");
        USENAMEDPRINTER("itype_t", it.type[0]);
      }
    }
    PUTS("}");
  } else {
    // tv_runtime or tv_type
    itype_t *t = nullptr;
    if (tu_is(tv_runtime, in)) {
      t = tu_assert(tv_runtime, in);
    } else if (tu_is(tv_type, in)) {
      t = tu_assert(tv_type, in);
      // We'll print the type directly; only add "typeof(" for non-struct types.
    }

    if (t) {
      if (tu_is(it_fun, t[0])) {
        // function type: fn((params...), ret)
        var_ fn = tu_assert(it_fun, t[0]);
        var_ len = sentList_length(fn, sizeof(*fn));
        PUTS("fn((");
for (usize i = 0; i < len - 1; i++) {
          if (i > 0) PUTS(",");
          USENAMEDPRINTER("itype_t", fn[i]);
        }
        PUTS("),");
        USENAMEDPRINTER("itype_t", fn[len - 1]);
        PUTS(")");
      } else if (tu_is(it_struct, t[0])) {
        // struct type – print field types (no names, they are not stored here)
        var_ st = tu_assert(it_struct, t[0]);
        var_ vlap = sentList_vla(st);
        PUTS("struct{");
        bool first = true;
        foreach (var_ it, pair(i, *vlap)) {
          if (!first) PUTS(",");
          first = false;
          USENAMEDPRINTER("itype_t", it.type[0]);
        }
        PUTS("}");
      } else {
        // primitive types – wrap with "typeof(" only if it's a tv_type
        if (tu_is(tv_type, in)) PUTS("typeof(");
        USETYPEPRINTER(itype_t, t[0]);
        if (tu_is(tv_type, in)) PUTS(")");
      }
    }
  }
});

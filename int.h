#include "ast.h"
#include "form.h"
#include "typ.h"
#include <assert.h>
#include <stdalign.h>
#include <stdint.h>
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

static var_ t_ast = (itype_t)tu_of(it_ast, {});
static var_ t_type = (itype_t)tu_of(it_type, {});
static var_ t_void = (itype_t)tu_of(it_void, {});

itype_t *getType(itype_t t) {

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
      case (it_ast, _, {
        return &t_ast;
      }),
      case (it_type, _, {
        return &t_type;
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

typedef struct {
  itype_t *type; // type
  fptr *names;   // array of member names , fun | struct
  rtval *values; // array of initial values , fun | struct
  bool isconst;
  enum : u8 {
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
} type_value;

typedef struct rtval {
  type_value type;
  union {
    /*
      main = fn(( arg : []str ),r : int = 0) : { };
      names will be "arg" , "r" ; nullablity in the ptr field
      values will have to store int for r as 0, others null

      not null terminated, same length as the function's signature
    */

    ast *function_value;   // active for it_fun
    type_value type_value; // active for it_type
    ast *ast_value;        // active for it_ast
    iptr runtime_value;    // active for everything else
  };
} value;

_Static_assert(!builtin_none, "");
#define ty_ptr(itype_ptr) ((type_value){.type = (itype_ptr)})
#define ty_get(struct) ty_ptr(getType((itype_t) struct))

static var_ ty_ast = (value){.type = &t_type, .type_value = &t_ast};
static var_ ty_type = (value){.type = &t_type, .type_value = &t_type};
static var_ ty_void = (value){.type = &t_type, .type_value = &t_void};

itype_t *value_Typeof(const value v) {
  if (v.type.builtin_id) return nullptr;
  return v.type.type;
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
itype_t *value_isAst(const value v) {
  var_ type = value_Typeof(v);
  if (type && tu_is(it_ast, type[0])) {
    return type;
  } else return nullptr;
}
itype_t *value_isType(const value v) {
  var_ type = value_Typeof(v);
  if (type && tu_is(it_type, type[0])) {
    return type;
  } else return nullptr;
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
  assertMessage(type.type && !type.builtin_id);
  static_assert(sizeof(type) == 32);
  log("creating : {type_value}", type);
  usize size = lineup(itype_size(REF(itype, {0, type.type})), alignof(myAlign));
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
  var_ tyep = v.type.type;
  usize size = lineup(itype_size(REF(itype, {0, tyep})), alignof(myAlign));

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
  if (to_type_full.builtin_id) assertMessage(false, "builtins dont have a type "); // TODO give builtins a type

  if (tu_is(it_struct, to_type_full.type[0])) {
    if (to_type_full.type == value_Typeof(*v) && to_type_full.names == (v[0]).type_value.names) return;
    else assertMessage(false);
  }

  var_ to_type = to_type_full.type;
  if (to_type == value_Typeof(*v)) return;

  if (tu_is(it_uint, to_type[0]) || tu_is(it_sint, to_type[0])) {
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
#define sbin(id)                     \
  ({                                 \
    static value _v = {              \
        .type = {.builtin_id = (id)} \
    };                               \
    &_v;                             \
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
    itypeof(itypeof(value, type), builtin_id) builtin,
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

      itype_t *resolved_type = getType(isSigned ? (itype_t)tu_of(it_sint, (u16)n) : (itype_t)tu_of(it_uint, (u16)n));

      value res = {
          .type.type = getType((itype_t)tu_of(it_type, {})),
          .type_value.type = resolved_type,
      };
      log("{value}", res);

      return res;
    } break;
  }
}
value eval_call(
    state *evalState,
    value left,
    ast right,
    type_value *_Nullable expected,
    bool retflag[1]
) {
  if (left.type.builtin_id)
    return eval_builtin(
        evalState,
        left.type.builtin_id,
        right,
        expected,
        retflag
    );
  assertMessage(false, "fallthrough");
}
bigint bigint_from_mem(AllocatorV allocator, u8 *mem, u16 bits, bool is_signed) {
  usize byte_count = (bits + 7) / 8;
  usize unit_count = (byte_count + sizeof(bigint_unit) - 1) / sizeof(bigint_unit);

  bigint res = msList_init(allocator, bigint_unit, unit_count);

  msList_pushArr(allocator, res, *VLAP((bigint_unit *)mem, unit_count));

  u8 leftover_bits = bits % (sizeof(bigint_unit) * 8);
  if (leftover_bits > 0) {
    res[unit_count - 1] &= ((bigint_unit)1 << leftover_bits) - 1;
  }

  if (is_signed && (mem[(bits - 1) / 8] & (1 << ((bits - 1) % 8)))) {
    if (leftover_bits > 0) {
      res[unit_count - 1] |= ~(((bigint_unit)1 << leftover_bits) - 1);
    }
  }

  bigint_trim(&res);
  return res;
}
value eval_op(
    state *evalState,
    value *left,
    ast right,
    u64 op,
    bool *retflag
) {
  assertMessage(retflag);
  if (op == callOp.num) {
    assertMessage(left);
    return eval_call(evalState, left[0], right, nullptr, retflag);
  } else if (op == addOp.num) {
    var_ r_val = eval(evalState, nullptr, right, retflag);
    if (*retflag) return r_val;

    if (left) {
      var_ l_type = value_isInt(left[0]);
      var_ r_type = value_isInt(r_val);
      assertMessage(l_type && r_type, "Both operands must be integers for addition");

      bool l_sign = tu_is(it_sint, l_type[0]);
      bool r_sign = tu_is(it_sint, r_type[0]);
      u16 l_bits = l_sign ? tu_assert(it_sint, l_type[0]) : tu_assert(it_uint, l_type[0]);
      u16 r_bits = r_sign ? tu_assert(it_sint, r_type[0]) : tu_assert(it_uint, r_type[0]);

      // Grab a temporary allocator for the bigint math so we don't leak it into the permanent state
      var_ temp_arena = arena_new_ext(stdAlloc, 1024);
      defer { arena_cleanup(temp_arena); };

      // Convert memory to bigints
      bigint b_left = bigint_from_mem(
          temp_arena, state_fromV(evalState, left->runtime_value), l_bits, l_sign
      );
      bigint b_right = bigint_from_mem(
          temp_arena, state_fromV(evalState, r_val.runtime_value), r_bits, r_sign
      );

      // Evaluate using your custom library!
      bigint b_sum = bigint_add(temp_arena, b_left, b_right);

      // Determine result type (standard promotion, adjust if your language rules differ)
      u16 res_bits = l_bits > r_bits ? l_bits : r_bits;
      bool res_sign = (l_bits == r_bits) ? (l_sign && r_sign) : (l_bits > r_bits ? l_sign : r_sign);
      var_ res_itype = getType(res_sign ? (itype_t)tu_of(it_sint, res_bits) : (itype_t)tu_of(it_uint, res_bits));

      // Allocate space in the evalState for the result
      var_ res = state_create(evalState, ty_ptr(res_itype), false);
      u8 *res_mem = state_fromV(evalState, res.runtime_value);

      // Zero out the newly allocated memory first
      usize res_byte_count = (res_bits + 7) / 8;
      memset(res_mem, 0, res_byte_count);

      // Copy the bigint result bytes back into the target memory
      usize units_to_copy = msList_len(b_sum);
      usize bytes_to_copy = units_to_copy * sizeof(bigint_unit);
      if (bytes_to_copy > res_byte_count) bytes_to_copy = res_byte_count; // Truncate if sum overflowed target width

      memcpy(res_mem, b_sum, bytes_to_copy);

      return res;
    }
    return r_val;
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
            ty_get((itype_t)tu_of(it_sint, 64)),
            false
        );
        var_ n = astStr_ton(num);
        memcpy(state_fromV(evalState, rs.runtime_value), &n, sizeof n);
        res = rs;
      }),

      case (astString, str, {
        var_ rs = state_create(
            evalState,
            ty_get(tu_of(it_arr, {getType((itype_t)tu_of(it_sint, 8)), astStr_len(str)})),
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
          typev = eval(evalState, REF(ty_get(t_type)), decl.type[0], retflag);

        if (decl.value)
          val = eval(evalState, decl.type ? &typev.type_value : nullptr, decl.value[0], retflag);
        else if (decl.type)
          val = state_create(evalState, typev.type_value, true);

        if (decl.name) {
          var_ id = tu_catch(astId, decl.name[0], error_ast(decl.name[0], snprint(stdAlloc, "expected identifier")));
          if (msHmap_get(mList_last(evalState->scopes), id))
            error_at(id.ptr, snprint(stdAlloc, "this variable is taken for this scope"));

          val.type.isconst = decl.is_const;
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
          assertMessage(!rty.builtin_id);

          rty.isconst;
          rty.names;
          rty.values;
          rty.type;
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
  if (in.builtin_id) {
    switch (in.builtin_id) {
        // clang-format off
      case builtin_$sint    :{PUTS("@sint")    ;}break;
      case builtin_$uint    :{PUTS("@uint")    ;}break;
      case builtin_$noExpand:{PUTS("@noExpand");}break;
      case builtin_$expand  :{PUTS("@expand")  ;}break;
      case builtin_$while   :{PUTS("@while")   ;}break;
      case builtin_$type    :{PUTS("@type")    ;}break;
      case builtin_$void    :{PUTS("@void")    ;}break;
      case builtin_$ast     :{PUTS("@ast")     ;}break;
      case builtin_return   :{PUTS("'return'") ;}break;
      case builtin_struct   :{PUTS("'struct'") ;}break;
      default:{PUTS("uninitialized?");USETYPEPRINTER(usize, in.builtin_id);}break;
        // clang-format on
    }
  } else {
    assertMessage(in.type);
    if (tu_is(it_fun, in.type[0])) {
      var_ fn = tu_assert(it_fun, in.type[0]);
      var_ len = sentList_length(fn, sizeof(*fn));
      PUTS("fn((");
      foreach (var_ it, span(fn, len - 1)) {
        USENAMEDPRINTER("itype_t", it[0]);
        PUTS(",");
      }
      PUTS(",");
      fn[len - 1];
      PUTS(")");
    } else if (tu_is(it_struct, in.type[0])) {
      var_ st = tu_assert(it_struct, in.type[0]);
      var_ vlap = sentList_vla(st);

      PUTS("struct{");
      foreach (var_ it, pair(i, *vlap)) {
        USENAMEDPRINTER("slice(c8)", in.names[i]);
        PUTS(":");
        USENAMEDPRINTER("itype_t", it.type[0]);
        PUTS(",");
        // if (in.values[i]) {
        //   if (in.isconst) PUTS(":");
        //   else PUTS("=");
        // }
      }
      PUTS("}");
    } else
      USETYPEPRINTER(itype_t, in.type[0]);
  }
});
REGISTER_PRINTER(value, {
  PUTS("value : ");
  if (in.type.type == &t_type) {
    PUTS("typeof(");
    USETYPEPRINTER(type_value, in.type_value);
    PUTS(")");
  }
});

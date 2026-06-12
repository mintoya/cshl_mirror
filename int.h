#include "ast.h"
#include "typ.h"
#include "wheels/funct.h"
#include "wheels/macros.h"
#include "wheels/mytypes.h"
#include "wheels/smallstream.h"
#include "wheels/tu_macros.h"
#include <stdalign.h>
#include <string.h>
#include <wheels/hhmap.h>
#include <wheels/sList.h>
#include <wheels/shmap.h>

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
var_ asnOp = /*  */ (astOp_union){"and"};
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
  println("interning : {itype_t}", t);
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

  assertMessage(false, "nothing should get here");
  return nullptr;
}

// }

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
typedef struct rtval *value;
typedef struct {
  itype_t *type;     // type
  fptr *structNames; // array of struct member names
  enum : u8 {
    builtin_none, //

    builtin_$sint,
    builtin_$uint,
    builtin_$noExpand,
    builtin_$expand,
    builtin_$while,
    builtin_$type,
    builtin_$void,
    builtin_$ast,

    builtin_return,
    builtin_struct,
  } builtin_id;
} type_value;
typedef struct rtval {
  type_value *type;
  union {
    /*
      main = fn(( arg : []str ),r : int = 0) : { };
      names will be "arg" , "r" ; nullablity in the ptr field
      values will have to store int for r as 0, others null

      none null terminated, same length as the function's signature
    */
    struct {
      fptr *names;
      value *rvalue_init;
      ast *fnblock; /*    */ // pointer to block containing function
    } function_value; /*  */ // active for it_fun
    type_value type_value;   // active for it_type *and
    ast *ast_value;          // active for it_ast
    iptr runtime_value;      // active for everything else
  };
} *value;
_Static_assert(!builtin_none, "");

itype_t *value_Typeof(const value v) { return v->type->type; }
itype_t *value_isInt(const value v) {
  var_ type = value_Typeof(v);
  if (EQUAL_ANY(1, tu_is(it_sint, type[0]), tu_is(it_uint, type[0]))) {
    return type;
  } else return nullptr;
}
itype_t *value_isSInt(const value v) {
  var_ type = value_Typeof(v);
  if (tu_is(it_sint, type[0])) {
    return type;
  } else return nullptr;
}
itype_t *value_isUInt(const value v) {
  var_ type = value_Typeof(v);
  if (tu_is(it_uint, type[0])) {
    return type;
  } else return nullptr;
}
itype_t *value_isStruct(const value v) {
  var_ type = value_Typeof(v);
  if (tu_is(it_struct, type[0])) {
    return type;
  } else return nullptr;
}
itype_t *value_isFn(const value v) {
  var_ type = value_Typeof(v);
  if (tu_is(it_fun, type[0])) {
    return type;
  } else return nullptr;
}
itype_t *value_isArr(const value v) {
  var_ type = value_Typeof(v);
  if (tu_is(it_arr, type[0])) {
    return type;
  } else return nullptr;
}
itype_t *value_isPtr(const value v) {
  var_ type = value_Typeof(v);
  if (tu_is(it_ptr, type[0])) {
    return type;
  } else return nullptr;
}
itype_t *value_isAst(const value v) {
  var_ type = value_Typeof(v);
  if (tu_is(it_ast, type[0])) {
    return type;
  } else return nullptr;
}
itype_t *value_isType(const value v) {
  var_ type = value_Typeof(v);
  if (tu_is(it_type, type[0])) {
    return type;
  } else return nullptr;
}

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
  return (state){
      .scopes = mList_init(allocator, scope),
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
      *VLAP((u8 *)0, lineup(size, alignof(usize)))
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
        usize idx = region - s->heap_state;
        msList_rem(s->heap_state, idx);
        msList_ins(mList_allocator(s->scopes), s->heap_state, 0, t);
      }
      break;
    }
}
u8 *state_fromV(state *s, iptr place) {
  u8 *a = place < 0 ? s->heap : s->stack;
  assertMessage(place > alignof(myAlign));
  place = place < 0 ? -place : place;
  place -= alignof(myAlign);
  return a + place;
}

value state_create(AllocatorV allocator, state *s, type_value *type, bool isHeap) {
  usize size = lineup(itype_size(REF(itype, {0, type->type})), alignof(myAlign));
  var_ res = aCreate(allocator, ptrstype(value));
  res->type = type;
  if (size)
    res->runtime_value =
        isHeap
            ? -state_heap_alloc(s, size)
            : state_stack_push(s, size);
  return res;
}
void state_destroy(AllocatorV allocator, state *s, value v) {
  var_ tyep = v->type->type;
  usize size = lineup(itype_size(REF(itype, {0, tyep})), alignof(myAlign));
  defer { aFree(allocator, v, sizeof(*v)); };

  if (size)
    if (v->runtime_value < 0) state_heap_free(s, -v->runtime_value);
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

  // structs must also have same feilds
  if (tu_is(it_struct, to_type_full.type[0])) {
    if (to_type_full.type == value_Typeof(*v) && to_type_full.structNames == (v[0])->type_value.structNames) return;
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

    if (to_bits < from_bits) assertMessage(false);
    // set some bits;
    v[0]->type = expected;
  } else assertMessage(false);
}

value eval(
    state *evalState,
    itype_t *_Nullable expected,
    ast astv,
    bool *retflag
);

value eval_symbol(
    state *evalState,
    fptr name
) {

  // {builtin resolution

#define STATIC_BUILTIN(id)                        \
  ({                                              \
    static type_value _tv = {.builtin_id = (id)}; \
    static ptrstype(value) _v = {.type = &_tv};   \
    &_v;                                          \
  })

  // Usage in eval_symbol becomes:
  if /**/ (fptr_eq(name, "@sint" /*    */)) return STATIC_BUILTIN(builtin_$sint);
  else if (fptr_eq(name, "@uint" /*    */)) return STATIC_BUILTIN(builtin_$uint);
  else if (fptr_eq(name, "@noExpand" /**/)) return STATIC_BUILTIN(builtin_$noExpand);
  else if (fptr_eq(name, "@expand" /*  */)) return STATIC_BUILTIN(builtin_$expand);
  else if (fptr_eq(name, "@while" /*   */)) return STATIC_BUILTIN(builtin_$while);
  else if (fptr_eq(name, "@type" /*    */)) return STATIC_BUILTIN(builtin_$type);
  else if (fptr_eq(name, "@ast" /*     */)) return STATIC_BUILTIN(builtin_$ast);
  else if (fptr_eq(name, "@void" /*    */)) return STATIC_BUILTIN(builtin_$void);
  else if (fptr_eq(name, "return" /*   */)) return STATIC_BUILTIN(builtin_return);
  else if (fptr_eq(name, "struct" /*   */)) return STATIC_BUILTIN(builtin_struct);
  // }

  foreach (var_ i, range(mList_len(evalState->scopes), 0)) {
    var_ s = mList_arr(evalState->scopes)[i - 1];
    var_ sy = msHmap_get(s, name);
    if (sy) return sy[0];
  }
  assertMessage(false);
}

value eval_call(
    state *evalState,
    ast *left,
    ast *right,
    bool *retflag
) {
  assertMessage(retflag);
  assertMessage(
      EQUAL_ANY(
          1,
          tu_is(astBBlock, right[0]), // {...}
          tu_is(astPBlock, right[0]), // (...)
          tu_is(astABlock, right[0])  // [...]
      )
  );
  var_ l = eval(evalState, nullptr, left[0], 0);
  // var_ r= eval(evalState, nullptr, left[0], 0);
}
value eval_op(
    state *evalState,
    ast *_Nullable left,
    ast *right,
    u64 op,
    value *_Nullable vctx,
    bool *retflag
) {
  assertMessage(retflag);
  assertMessage(!!vctx ^ !!left);
  if (vctx) {
  }
}

value eval(
    state *evalState,
    itype_t *_Nullable expected,
    ast astv,
    bool *retflag
) {
  assertMessage(retflag);
  var_ allocator = msHmap_allocator(mList_last(evalState->scopes));
}

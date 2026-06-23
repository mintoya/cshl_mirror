#include "ast.h"
#include "form.h"
#include "wheels/sList.h"
#include "wheels/shmap.h"
#include "wheels/smallstream.h"
#include <assert.h>
#include <stdcountof.h>
#include <wheels/allocators/arenaAllocator.h>
#include <wheels/bigint.h>
#include <wheels/macros.h>
#include <wheels/mytypes.h>
#include <wheels/tu_macros.h>
#define log(msg, ...) println("(log:{cstr})\t" msg, (char *)__FUNCTION__, __VA_ARGS__)
// {types
typedef enum builtin_enum : u8 {
  builtin_none,

  builtin_$sint,
  builtin_$uint,
  builtin_$noExpand,
  builtin_$expand,
  builtin_$while,
  builtin_$type,
  builtin_$void,
  builtin_$ast,
  builtin_$any,

  builtin_fn,
  builtin_return,
  builtin_struct,
} builtin_id;
REGISTER_PRINTER(builtin_id, {
  switch (in) {
      // clang-format off
    case builtin_none/*     */:PUTS("none/invalid")/**/;break;

    case builtin_$any/*     */:PUTS("$any")/*     */;break;
    case builtin_$sint/*    */:PUTS("$sint")/*    */;break;
    case builtin_$uint/*    */:PUTS("$uint")/*    */;break;
    case builtin_$noExpand/**/:PUTS("$noExpand")/**/;break;
    case builtin_$expand/*  */:PUTS("$expand")/*  */;break;
    case builtin_$while/*   */:PUTS("$while")/*   */;break;
    case builtin_$type/*    */:PUTS("$type")/*    */;break;
    case builtin_$void/*    */:PUTS("$void")/*    */;break;
    case builtin_$ast/*     */:PUTS("$ast")/*     */;break;
    case builtin_fn/*       */:PUTS("fn")/*       */;break;
    case builtin_return/*   */:PUTS("return")/*   */;break;
    case builtin_struct/*   */:PUTS("struct")/*   */;break;
      // clang-format on
  }
});
typedef struct runtime runtime;
typedef struct runtime_struct runtime_struct;
typedef struct runtime_array *runtime_array;
typedef struct runtime_bitset runtime_bitset;
typedef struct Type Type;

// obviously these types cant store their value, like the comptime types
tu_def(
    (runtime, u8),
    (r_void, nothing_t),
    (r_uint, u16),
    (r_sint, u16),

    (r_ptr, runtime *),
    (r_arr, runtime_array),

    // represents both structs and unions, even with wierd alignment requirements
    (r_struct, sentList_t(runtime_struct)),
    // struct but only bit-prcise members
    (r_bits, sentList_t(runtime_bitset)),
    // at runtime only types matter
    (r_funct, sentList_t(runtime)),
);
_Static_assert(!TU_TAG(r_void)); // to make sure at leas {} init doesnt cause a seg fault
typedef struct runtime_struct {
  runtime t;
  fptr name;
  usize offset;
} runtime_struct;
typedef struct runtime_array {
  runtime t;
  usize count;
} *runtime_array;
typedef struct runtime_bitset {
  runtime t;
  usize bitcount;
} runtime_bitset;
REGISTER_PRINTER(runtime, {
  tu_match(
      in,
      case (r_uint, u, {PUTS("u");USETYPEPRINTER(usize, u); }),
      case (r_sint, s, {PUTS("s");USETYPEPRINTER(usize, s); }),
      case (r_void, _, { PUTS("void"); }),
      case (r_ptr, p, { PUTS("[*]"); USETYPEPRINTER(runtime, *p); }),

      case (r_arr, p, {
        PUTS("[");
        USETYPEPRINTER(usize, p->count);
        PUTS("]");
        USETYPEPRINTER(runtime, p->t);
      }),
      case (r_bits, b, {
        PUTS("bm{");
        var_ vla = sentList_vla(b);
        foreach (var_ item, vlap(vla)) {
          USETYPEPRINTER(runtime, item.t);
          PUTS(":");
          USETYPEPRINTER(usize, item.bitcount);
          PUTS(";");
        }
        PUTS("}");
      }),
      case (r_struct, b, {
        PUTS("st{");
        var_ vla = sentList_vla(b);
        foreach (var_ item, vlap(vla)) {
          USETYPEPRINTER(fptr, item.name);
          PUTS(":");
          USETYPEPRINTER(runtime, item.t);
          PUTS("@");
          USETYPEPRINTER(usize, item.offset);
          PUTS(";");
        }
        PUTS("}");
      }),
      case (r_funct, f, {
        var_ vla = sentList_vla(f);
        PUTS("fn((");
        foreach (var_ v, span(*vla, countof(*vla) - 1)) {
          if (v != *vla) PUTS(",");
          USETYPEPRINTER(runtime, *v);
        }
        PUTS("),");
        USETYPEPRINTER(runtime, vla[0][countof(*vla) - 1]);
        PUTS(")");
      }),
      default(unreachable();)

  );
});

#define runtime_of(type, value) ((runtime)tu_of(type, value))

usize runtime_alignof(runtime rt) {
  tu_match(
      rt,
      case (r_void, _, { return 1; }),
      case (r_funct, _, { return 1; }),

      case (r_uint, bits, { return lineup(bits, 8) / 8; }),
      case (r_sint, bits, { return lineup(bits, 8) / 8; }),

      case (r_ptr, _, { return runtime_alignof(runtime_of(r_uint, sizeof(void *) * 8)); }),
      case (r_arr, arry, { return runtime_alignof(arry->t); }),
      case (r_struct, fields, {
        usize max = 1;
        var_ vla = sentList_vla(fields);
        foreach (var_ f, vlap(vla))
          max = MAX$(max, runtime_alignof(f.t));
        return max;
      }),
      case (r_bits, fields, {
        usize t = 0;
        var_ vla = sentList_vla(fields);
        foreach (var_ f, vlap(vla))
          t += f.bitcount;
        return runtime_alignof(runtime_of(r_uint, t));
      }),
  );
  assertMessage(false, "unreachable?");
}
usize runtime_sizeof(runtime rt) {
  tu_match(
      rt,
      case (r_void, _, { return 0; }),
      case (r_funct, _, { return 0; }),

      case (r_uint, bits, { return lineup(bits, 8) / 8; }),
      case (r_sint, bits, { return lineup(bits, 8) / 8; }),

      case (r_ptr, _, { return runtime_sizeof(runtime_of(r_uint, sizeof(void *) * 8)); }),
      case (r_arr, arry, { return arry->count * runtime_sizeof(arry->t); }),
      case (r_struct, fields, {
        usize max = 1;
        var_ vla = sentList_vla(fields);
        foreach (var_ f, vlap(vla))
          max = MAX$(max, runtime_sizeof(f.t) + f.offset);
        return lineup(max, runtime_alignof(rt));
      }),
      case (r_bits, fields, {
        usize t = 0;
        var_ vla = sentList_vla(fields);
        foreach (var_ f, vlap(vla))
          t += f.bitcount;
        return runtime_sizeof(runtime_of(r_uint, t));
      }),

  );
  assertMessage(false, "unreachable?");
}

typedef struct comptime_funct comptime_funct;
tu_def(
    // written so that all items are nullable,
    (comptime, u8),
    (c_int, bigint),
    (c_ast, ast *),
    (c_Type, Type *),
    (c_funct, comptime_funct *),
    (c_any, nothing_t),
);
#define comptime_of(type, value) ((comptime)tu_of(type, value))
tu_def(
    (Type, u8),
    (comptime, comptime),
    (runtime, runtime),
    (builtin, builtin_id),
);
#define Type_of(time, type, ...) (Type) tu_of(time, tu_of(type, __VA_ARGS__))

var_ typeof_type = Type_of(comptime, c_Type, nullptr);
var_ typeof_ast = Type_of(comptime, c_ast, nullptr);
var_ typeof_int = Type_of(comptime, c_int, nullptr);
var_ typeof_any = Type_of(comptime, c_any, {});

typedef struct comptime_funct {
  sentList_t(struct {
    Type t;
    fptr name;
    void *initial_value;
    bool isconst;
  }) args;
  itypeof(astBBlock, stmts) block;
} comptime_funct;
REGISTER_PRINTER(comptime, {
  tu_match(
      in,
      case (c_int, i, {
        PUTS("c_int:");
        USETYPEPRINTER(bigint, i);
      }),
      case (c_ast, a, { PUTS("c_ast"); }),
      case (c_Type, t, {
        PUTS("c_Type:");
        if (t) {
          USENAMEDPRINTER("Type", *t);
        } else PUTS("type");
      }),
      case (c_funct, f, {
        PUTS("c_funct((");
        var_ vla = sentList_vla(f->args);
        foreach (var_ a, span(vla[0], countof(vla[0]) - 1)) {
          if (a != vla[0]) PUTS(",");
          USENAMEDPRINTER("slice(c8)", a->name);
          PUTS(":");
          USENAMEDPRINTER("Type", a->t);
        }
        PUTS("),");
        var_ ret = vla[0][countof(vla[0]) - 1];
        USENAMEDPRINTER("slice(c8)", ret.name);
        PUTS(":");
        USENAMEDPRINTER("Type", ret.t);
        PUTS(")");
      }),
      default(unreachable();)
  );
});
bool Type_eq(Type a, Type b);
REGISTER_PRINTER(Type, {
  if (Type_eq(in, typeof_type)) {
    PUTS("type");
    return;
  } else if (Type_eq(in, typeof_ast)) {
    PUTS("ast");
    return;
  } else if (Type_eq(in, typeof_int)) {
    PUTS("int");
    return;
  } else if (Type_eq(in, typeof_any)) {
    PUTS("any");
    return;
  }
  tu_match(
      in,
      case (builtin, b, {
        PUTS("builtin:");
        USETYPEPRINTER(builtin_id, b);
      }),
      case (comptime, c, {
        PUTS("comptime:");
        USETYPEPRINTER(comptime, c);
      }),
      case (runtime, r, {
        PUTS("runtime:");
        USETYPEPRINTER(runtime, r);
      }),
      default(assertMessage(false, "%i", in.tag);)
  );
});

usize Type_sizeof(Type t) {
  tu_match(
      t,
      case (builtin, _, { return 0; }),
      case (comptime, _, { return 0; }),
      case (runtime, it, { return runtime_sizeof(it); }),

  );
  assertMessage(false, "unreachable?");
}
_Static_assert(sizeof(Type));

// }

__attribute__((noreturn)) void error_ast(ast a, slice(c8) message) {
  tu_match(
      a,
      case (astId, s, { error_at(s.ptr, message); }),
      case (astNumber, s, { error_at(s.ptr, message); }),
      case (astString, s, { error_at(s.ptr, message); }),

      case (astDecl, d, { error_ast(d.name[0], message); }),
      case (astOp, o, { error_ast(o.right[0], message); }),

      case (astPBlock, b, { error_at(b.loc, message); }),
      case (astBBlock, b, { error_at(b.loc, message); }),
      case (astABlock, b, { error_at(b.loc, message); }),

      default(error_at(nullptr, message);),
  );
  unreachable();
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
// {literal conversion

bigint astStr_ton(AllocatorV allocator, fptr str) {
  bigint res = nullptr;
  if (str.len > 2 && fptr_eq(slice_split(str, (0, 2))[0], "0x"))
    res = BInt.from.fptr(allocator, 16, slice_split(str, (2, -1))[0]);
  else if (str.len > 2 && fptr_eq(slice_split(str, (0, 2))[0], "0b"))
    res = BInt.from.fptr(allocator, 2, slice_split(str, (2, -1))[0]);
  else res = BInt.from.fptr(allocator, 10, str);
  return res;
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
          // clang-format off
        case 'n': dst[di++] = '\n'; break;
        case 'r': dst[di++] = '\r'; break;
        case 't': dst[di++] = '\t'; break;
        case '0': dst[di++] = '\0'; break;
        case '"': dst[di++] = '"'; break;
        default: dst [di++] = src[i]; break;
          // clang-format on
      }
    } else dst[di++] = src[i];
  }

  to->len = di;
}
// }
//
// {value
typedef struct value_struct {
  Type t;
  bool isConst : 1;
  iptr rtvalue : sizeof(iptr) * 8 - 1;
} value;
bool runtime_type_eq(runtime a, runtime b) {
  if (a.tag != b.tag) return false;
  if (fptr_eq(((fptr){sizeof(a), (u8 *)&a}), ((fptr){sizeof(b), (u8 *)&b}))) return true;
  tu_match(
      a,
      case (r_void, _, return true;),
      case (r_uint, ua, return ua == tu_assert(r_uint, b);),
      case (r_sint, sa, return sa == tu_assert(r_sint, b);),
      case (r_ptr, pa, return runtime_type_eq(*pa, *tu_assert(r_ptr, b));),
      case (r_arr, pa, {
        var_ pb = tu_assert(r_arr, b);
        return pb->count == pa->count && runtime_type_eq(pa->t, pb->t);
      }),
      case (r_struct, sa, {
        var_ sb = tu_assert(r_struct, b);
        var_ vla = sentList_vla(sa);
        var_ vlb = sentList_vla(sb);
        if (countof(*vla) != countof(*vlb)) return false;
        foreach (var_ i, range(0, countof(*vla))) {
          var_ ai = vla[0][i];
          var_ bi = vlb[0][i];
          if (!fptr_eq(ai.name, bi.name)) return false;
          if (ai.offset != bi.offset) return false;
          if (!runtime_type_eq(ai.t, bi.t)) return false;
        }
        return true;
      }),
      case (r_funct, fa, {
        var_ fb = tu_assert(r_funct, b);
        var_ vla = sentList_vla(fa);
        var_ vlb = sentList_vla(fb);
        foreach (var_ i, range(0, countof(*vla))) {
          var_ ai = vla[0][i];
          var_ bi = vlb[0][i];
          if (!runtime_type_eq(ai, bi)) return false;
        }
        return true;
      }),
      case (r_bits, ba, {
        var_ bb = tu_assert(r_bits, b);
        var_ vla = sentList_vla(ba);
        var_ vlb = sentList_vla(bb);
        foreach (var_ i, range(0, countof(*vla))) {
          var_ ai = vla[0][i];
          var_ bi = vlb[0][i];
          if (ai.bitcount != bi.bitcount) return false;
          if (!runtime_type_eq(ai.t, bi.t)) return false;
        }
        return true;
      })
  );
}
bool comptime_type_eq(comptime a, comptime b) {
  if (a.tag != b.tag) return false;
  if (fptr_eq(((fptr){sizeof(a), (u8 *)&a}), ((fptr){sizeof(b), (u8 *)&b}))) return true;
  tu_match(
      a,
      case (c_any, _, {
        return true; // TODO probably fine
      }),
      case (c_ast, _, {
        return true; // TODO probably fine
      }),
      case (c_int, _, {
        return true;
      }),
      case (c_funct, fa, {
        var_ fb = tu_assert(c_funct, b);
        var_ faa = *fa;
        var_ fba = *fb;
        var_ vla = sentList_vla(faa.args);
        var_ vlb = sentList_vla(fba.args);
        if (countof(*vla) != countof(*vlb)) return false;
        foreach (var_ i, range(0, countof(*vla))) {
          var_ ai = vla[0][i];
          var_ bi = vlb[0][i];
          if (!Type_eq(ai.t, bi.t)) return false;
        }
        return true;
      }),
      case (c_Type, ta, {
        var_ tb = tu_assert(c_Type, b);
        if (!ta || !tb) return ta == tb;
        var_ taa = ta[0];
        var_ tba = tb[0];
        if (taa.tag != tba.tag) return false;
        tu_match(
            taa,
            case (builtin, ba, {
              var_ bb = tu_assert(builtin, tba);
              return ba == bb;
            }),
            case (comptime, ca, {
              var_ cb = tu_assert(comptime, tba);
              return comptime_type_eq(ca, cb);
            }),
            case (runtime, ra, {
              var_ rb = tu_assert(runtime, tba);
              return runtime_type_eq(ra, rb);
            }),

        );
      })
  );
}
bool Type_eq(Type a, Type b) {
  if (a.tag != b.tag) return false;
  tu_match(
      a,
      case (builtin, ba, {
        var_ bb = tu_assert(builtin, b);
        return ba == bb;
      }),
      case (comptime, ca, {
        var_ cb = tu_assert(comptime, b);
        return comptime_type_eq(ca, cb);
      }),
      case (runtime, ra, {
        var_ rb = tu_assert(runtime, b);
        return runtime_type_eq(ra, rb);
      }),

  );
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
} EState;
EState state_init(AllocatorV allocator) {
  var_ l = mList_init(allocator, scope);
  mList_push(l, msHmap_init(allocator, value));
  return (EState){
      .scopes = l,
      .stack = msList_init(allocator, u8),
      .heap = msList_init(allocator, u8),
      .heap_state = msList_init(allocator, ptrstype(itypeof(EState, heap_state))),
  };
}

iptr state_stack_current(EState s) { return msList_len(s.stack); }
iptr state_stack_push(EState *s, usize size) {
  size = lineup(size, alignof(myAlign));
  usize res = msList_len(s->stack);
  msList_pushArr(
      mList_allocator(s->scopes),
      s->stack,
      *VLAP((u8 *)0, size)
  );
  return res + alignof(myAlign);
}
iptr state_heap_alloc(EState *s, usize size) {
  size = lineup(size, alignof(myAlign));
  typedef ptrstype(itypeof(EState, heap_state)) regiontype;
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
void state_heap_free(EState *s, iptr place) {
  place -= alignof(myAlign);
  typedef ptrstype(itypeof(EState, heap_state)) regiontype;
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
u8 *state_fromV(EState *s, iptr place) {
  u8 *a = place < 0 ? s->heap : s->stack;
  place = place < 0 ? -place : place;
  assertMessage(place >= alignof(myAlign));
  place -= alignof(myAlign);
  return a + place;
}
u8 *state_fromVal(EState *s, value v) {
  return state_fromV(s, v.rtvalue);
}
value state_create(EState *s, Type type, bool isHeap) {
  log("creating : {Type}", type);
  usize size = lineup(Type_sizeof(type), alignof(myAlign));
  // assertMessage(!!isHeap ^ !!size);
  var_ res = (value){
      .t = type,
  };
  if (size)
    res.rtvalue =
        isHeap
            ? -state_heap_alloc(s, size)
            : state_stack_push(s, size);
  return res;
}
void state_destroy(EState *s, value v) {
  if (tu_is(runtime, v.t) && lineup(Type_sizeof(v.t), alignof(myAlign)))
    if (v.rtvalue < 0) state_heap_free(s, -v.rtvalue);
}
AllocatorV state_last_allocator(EState *s) {
  assertMessage(mList_len(s->scopes));
  return msHmap_allocator(mList_last(s->scopes));
}
void state_set(EState *s, astId id, value v) {
  if (msHmap_get(mList_last(s->scopes), id))
    error_ast((ast)tu_of(astId, id), snprint(stdAlloc, "value in the same scope with the same name"));
  msHmap_set(mList_last(s->scopes), id, v);
}
void state_push(EState *s) {
  var_ nextScopeStorage =
      arena_new_ext(
          state_last_allocator(s),
          1024
      );
  var_ nextScope = msHmap_init(nextScopeStorage, value);
  mList_push(s->scopes, nextScope);
}
void state_pop(EState *s) {
  var_ nextScope = mList_pop(s->scopes);
  var_ allocator = msHmap_allocator(nextScope);
  arena_cleanup(allocator);
}
// }

void coerce_value(value *v, Type *expected) {
  assertMessage(v);
  if (!expected) return;
  if (Type_eq(expected[0], typeof_any)) return;
  if (Type_eq(v->t, *expected)) return;
  // the type of a type is a type, handled in type eq
  if (Type_eq(typeof_type, expected[0])) {
    if (Type_eq(typeof_any, v->t)) return;
    if (Type_eq(typeof_ast, v->t)) return;
    if (Type_eq(typeof_int, v->t)) return;
  }
  assertMessage(false, "unhandled coersion");
}
Type value_toType(ast err_loc, value v) {
  tu_match(
      v.t,
      case (comptime, c, {
        tu_match(
            c,
            case (c_int, i, {
              if (i) error_ast(err_loc, snprint(stdAlloc, "this is a value not a type"));
              return typeof_int;
            }),
            case (c_any, a, { return typeof_any; }),
            case (c_ast, a, {
              if (a) error_ast(err_loc, snprint(stdAlloc, "this is a value not a type"));
              return typeof_ast;
            }),
            case (c_Type, t, return t ? t[0] : typeof_type;),
            default(error_ast(err_loc, snprint(stdAlloc, "this is a value not a type"));)
        );
      }),
      default(error_ast(err_loc, snprint(stdAlloc, "this is a value not a type"));)
  );
  error_ast(err_loc, snprint(stdAlloc, "this is a value not a type"));
}
value Type_toValue(ast err_loc, Type t) {
  return (value){Type_of(comptime, c_Type, aValue(stdAlloc, t))}; // TODO
}
value eval(
    EState *evalState,
    Type *_Nullable expected,
    ast part,
    bool retflag[1]
);
bigint value_toI(AllocatorV alloc, EState *evs, value v) {
  tu_match(
      v.t,
      case (runtime, r, {
        tu_match(
            r,
            case (r_sint, s, return BInt.from.bits(alloc, state_fromVal(evs, v), Type_sizeof(v.t) * 8, true);),
            case (r_uint, u, return BInt.from.bits(alloc, state_fromVal(evs, v), Type_sizeof(v.t) * 8, false);),
            default(return nullptr;),
        );
      }),
      case (comptime, c, {
        if_tu_is (bigint b, c_int, c) {
          return bigint_copy(alloc, b);
        } else return nullptr;
      }),
      default(return nullptr;)
  );
  return nullptr;
}

ptrstype(itypeof(comptime_funct, args)) eval_fn_arg(
    EState *evalState, ast part
) {
  typedef ptrstype(itypeof(comptime_funct, args)) function_arg;
  var_ res = (function_arg){0};
  tu_match(
      part,
      case (astDecl, d, {
        if (d.name) res.name = tu_assert(astId, d.name[0]);
        if (d.type) {
          bool rf = false;
          var_ tv = eval(evalState, &typeof_type, d.type[0], &rf);
          if (rf) error_ast(d.type[0], snprint(stdAlloc, "returned while evaluating a function"));
          res.t = value_toType(d.type[0], tv);
        } else error_ast(part, snprint(stdAlloc, "arguments must have a type"));
        res.isconst = d.is_const;
        if (d.value) {
          bool rf = false;
          var_ v = eval(evalState, d.type ? &res.t : nullptr, d.value[0], &rf);
          if (rf) error_ast(d.type[0], snprint(stdAlloc, "returned while evaluating a function"));
          res.initial_value = aValue(
              state_last_allocator(evalState), v // TODO: wrong allocator
          );
        }
      }),
      case (astId, id, {
        bool rf = false;
        var_ tv = eval(evalState, &typeof_type, part, &rf);
        if (rf) error_ast(part, snprint(stdAlloc, "returned while evaluating a function"));
        res.t = value_toType(part, tv);
      }),
      case (astEmptyStatement, empty, {
        res.t = Type_of(runtime, r_void, {});
      }),
      default(error_ast(part, snprint(stdAlloc, "{astTag} not supported here", part.tag));),
  );
  return res;
}
value eval_fncall(
    EState *evalState,
    comptime_funct *f,
    ast args
) {
  var_ allocator = state_last_allocator(evalState);
  if_tu_is (var_ pb, astPBlock, args) {
    AllocatorV arena = arena_new_ext(allocator, 1024);
    defer { arena_clear(arena); };

    var_ argsList = ast_match(arena, args, fp("P+a"));
    usize arg_count = argsList ? msList_len(argsList) : 0;

    var_ vla_params = sentList_vla(f->args);
    usize param_count = countof(vla_params[0]) - 1;

    if (arg_count != param_count)
      error_ast(args, snprint(stdAlloc, "expected {} args, got {}", param_count, arg_count));

    value *eval_args = aCreate(arena, value, arg_count);
    for (usize i = 0; i < arg_count; i++) {
      bool rf = false;
      eval_args[i] = eval(evalState, &vla_params[0][i].t, argsList[i][0], &rf);
      if (rf) error_ast(argsList[i][0], snprint(stdAlloc, "returned while evaluating argument"));
    }

    state_push(evalState);
    defer { state_pop(evalState); };

    for (usize i = 0; i < arg_count; i++)
      state_set(evalState, vla_params[0][i].name, eval_args[i]);
    bool rflag = false;
    var_ bb = (astBBlock){.stmts = f->block};
    Type ret_type = vla_params[0][param_count].t;

    value res = eval(evalState, nullptr, (ast)tu_of(astBBlock, bb), &rflag);
    coerce_value(&res, &ret_type);

    return res;
  }
  error_ast(args, snprint(stdAlloc, "expected parenthesized arguments"));
}
value eval_call(
    EState *evalState,
    value left,
    ast args,
    bool retflag[1]
) {
  var_ allocator = state_last_allocator(evalState);
  if_tu_is (var_ builtin, builtin, left.t) {
    switch (builtin) {
      case builtin_$ast:
        return (value){typeof_ast};
      case builtin_$type:
        return (value){typeof_type};
      case builtin_$any:
        return (value){typeof_any};
      case builtin_return: {
        bool r2 = false;
        var_ res = eval(evalState, nullptr, args, &r2);
        assertMessage(!r2, "returned inside a return statement?");
        *retflag = true;
        return res;
      }
      case builtin_fn: {
        AllocatorV arena = arena_new_ext(allocator, 1024);
        defer { arena_clear(arena); };
        if_tu_is (var_ sts, astPBlock, args) {
          usize len = sentList_length(sts.stmts, sizeof(*sts.stmts));
          if (len > 2 || len < 1)
            error_ast(args, snprint(stdAlloc, "expected 2 args"));
          var_ retV = len == 2 ? sts.stmts[1][0] : (ast)tu_of(astEmptyStatement, {});
          var_ argsV = sts.stmts[0];

          var_ argsList = ast_match(arena, argsV[0], fp("P+a"));
          usize arglen = argsList ? msList_len(argsList) : 0;

          typedef ptrstype(itypeof(comptime_funct, args)) function_arg;
          var_ rfunc = (comptime_funct){
              .args = aCreate(allocator, function_arg, arglen + 2),
              .block = nullptr
          };

          foreach (var_ i, range(0, arglen))
            rfunc.args[i] = eval_fn_arg(evalState, argsList[i][0]);

          rfunc.args[arglen] = eval_fn_arg(evalState, retV);

          return (value){Type_of(comptime, c_funct, aValue(allocator, rfunc))};
        } else error_ast(args, snprint(stdAlloc, "expected parenthesized args"));
      }
      default:
        assertMessage(false, "unhandled builtin %i", builtin);
    }
  }
  if_tu_is (var_ c_val, comptime, left.t) {
    tu_match(
        c_val,
        case (c_funct, f, {
          if (!f->block) {
            if_tu_is (var_ bb, astBBlock, args) {
              var_ rfunc = *f;
              rfunc.block = tu_assert(astBBlock, args).stmts;
              return (value){Type_of(comptime, c_funct, aValue(allocator, rfunc))}; // TODO:
            }
            error_ast(args, snprint(stdAlloc, "expected block for function body"));
          } else
            return eval_fncall(evalState, f, args);
          goto fail;
        }),
        default(goto fail;)
    );
  }
  {fail : } assertMessage(
      false,
      "unhandled call: \n%s",
      snprint(
          stdAlloc,
          "{Type}\n{ast}\n",
          left.t,
          &args
      )
          .ptr
  );
}
value eval_op(
    EState *evalState,

    value *_Nullable left,
    ast right,
    usize op,

    bool retflag[1]
) {
  var_ allocator = state_last_allocator(evalState);

  if (op == callOp.num) {
    assertMessage(left);
    return eval_call(evalState, left[0], right, retflag);
  } else if (
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
    var_ lv = *left;
    var_ rv = eval(evalState, &left->t, right, retflag);
    if (*retflag) return rv;

    var_ arena = arena_new_ext(allocator, 1024);
    defer { arena_cleanup(arena); };

    var_ lb = (bigint)value_toI(arena, evalState, lv);
    var_ rb = (bigint)value_toI(arena, evalState, rv);
    print("{bigint} {cstr} {bigint}  = ", lb, (char *)&op, rb);
    if (!lb || !rb) error_ast(right, snprint(stdAlloc, "could'nt convert these to integers"));
    if (op == addOp.num)
      BInt.advanced.add_ip(arena, &lb, rb, 0);
    else if (op == subOp.num)
      BInt.advanced.sub_ip(arena, &lb, rb, 0);
    else if (op == mulOp.num)
      lb = BInt.mul(arena, lb, rb);
    else if (op == divOp.num)
      lb = BInt.div(arena, lb, rb).div;
    else if (op == modOp.num)
      lb = BInt.div(arena, lb, rb).mod;
    else assertMessage(false, "unreachable?");
    println("{bigint}", lb);
    return (value){Type_of(comptime, c_int, bigint_copy(allocator, lb))};
  }
  assertMessage(false, "unreachable?");
}
value state_getId(EState *s, astId id) {
  foreach (var_ i, range(mList_len(s->scopes), 0))
    if_decl (var_ v, msHmap_get(mList_arr(s->scopes)[i - 1], id))
      return *v;
  if (fptr_eq(id, "@ast")) return (value){(Type)tu_of(builtin, builtin_$ast)};
  if (fptr_eq(id, "@any")) return (value){(Type)tu_of(builtin, builtin_$any)};
  if (fptr_eq(id, "@type")) return (value){(Type)tu_of(builtin, builtin_$type)};
  if (fptr_eq(id, "return")) return (value){(Type)tu_of(builtin, builtin_return)};
  if (fptr_eq(id, "fn")) return (value){(Type)tu_of(builtin, builtin_fn)};
  if (fptr_eq(id, "struct")) return (value){(Type)tu_of(builtin, builtin_struct)};
  error_ast((ast)tu_of(astId, id), snprint(stdAlloc, "unidentified symbol '{slice(c8)}'", id));
}
value eval(
    EState *evalState,
    Type *_Nullable expected,
    ast part,
    bool retflag[1]
) {
  assertMessage(retflag, "must have return flag");
  assertMessage(!*retflag, "return flag is already set?");
  var_ allocator = state_last_allocator(evalState);
  var_ result = (value){};
  tu_match(
      part,
      //
      // trivial
      //
      case (astEmptyStatement, _, {}),
      case (astId, id, result = state_getId(evalState, id);),
      case (astNumber, n, result = (value){Type_of(comptime, c_int, astStr_ton(allocator, n))};),
      case (astString, s, {
        usize l = astStr_len(s);
        var_ arrt = aValue(allocator, ((ptrstype(r_arr)){runtime_of(r_sint, 8), l})); // TODO: intern
        result = state_create(evalState, Type_of(runtime, r_arr, arrt), false);
        var_ to = (fptr){l, state_fromV(evalState, result.rtvalue)};
        astStr_tom(s, &to);
      }),
      case (astPBlock, pb, {
        var_ vla = sentList_vla(pb.stmts);
        assertMessage(countof(vla[0]) == 1, "no tuples in this language");
        result = eval(evalState, expected, vla[0][0][0], retflag);
      }),

      case (astOp, op, {
        var_ opn = op.number_rep;
        var_ left = (value){};
        if (op.left) {
          left = eval(evalState, nullptr, op.left[0], retflag);
          result = eval_op(evalState, &left, op.right[0], opn, retflag);
        } // TODO: check rflag
        else
          result = eval_op(evalState, nullptr, op.right[0], opn, retflag);
      }),
      case (astDecl, decl, {
        var_ type = (Type){};
        if (decl.type) {
          var_ t = eval(evalState, &typeof_type, decl.type[0], retflag);
          if (*retflag) {
            result = t;
            goto coersion;
          }
          type = value_toType(decl.type[0], t);
        }
        var_ val = (value){};
        if (decl.value) {
          var_ v = eval(evalState, decl.type ? &type : nullptr, decl.value[0], retflag);
          if (*retflag) {
            result = v;
            goto coersion;
          }
          val = v;
        }
        val.isConst = decl.is_const;
        state_set(evalState, tu_assert(astId, decl.name[0]), val);
        result = val;
      }),
      case (astBBlock, bb, {
        if (expected) {
          var_ left = Type_toValue(part, expected[0]);
          result = eval_op(evalState, &left, part, callOp.num, retflag);
        } else {
          var_ vla = sentList_vla(bb.stmts);
          state_push(evalState);
          defer { state_pop(evalState); };
          result = (value){};
          foreach (var_ st, vlap(vla)) {
            if (st)
              result = eval(evalState, expected, *st, retflag);
            if (*retflag) goto coersion;
          }
        }
      }),
      default({
        assertMessage(false, "%s", snprint(stdAlloc, "{astTag}", part.tag).ptr);
      }),

  );
  {coersion : } coerce_value(&result, expected);
  return result;
}

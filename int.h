#include "ast.h"
#include "wheels/smallstream.h"
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

  builtin_fn,
  builtin_return,
  builtin_struct,
} builtin_id;
REGISTER_PRINTER(builtin_id, {
  switch (in) {
      // clang-format off
    case builtin_none/*     */:PUTS("none/invalid")/**/;break;
    case builtin_$sint/*    */:PUTS("$sint")/*       */;break;
    case builtin_$uint/*    */:PUTS("$uint")/*       */;break;
    case builtin_$noExpand/**/:PUTS("$noExpand")/*   */;break;
    case builtin_$expand/*  */:PUTS("$expand")/*     */;break;
    case builtin_$while/*   */:PUTS("$while")/*      */;break;
    case builtin_$type/*    */:PUTS("$type")/*       */;break;
    case builtin_$void/*    */:PUTS("$void")/*       */;break;
    case builtin_$ast/*     */:PUTS("$ast")/*        */;break;
    case builtin_fn/*       */:PUTS("fn")/*          */;break;
    case builtin_return/*   */:PUTS("return")/*      */;break;
    case builtin_struct/*   */:PUTS("struct")/*      */;break;
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
        foreach (var_ v, span(*vla, countof(*vla) - 1))
          USETYPEPRINTER(runtime, *v);
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
    (c_int, bigint), // bigint's are nullable
    (c_ast, ast *),
    (c_Type, Type *),
    (c_funct, comptime_funct *),
);
#define comptime_of(type, value) ((comptime)tu_of(type, value))
tu_def(
    (Type, u8),
    (comptime, comptime),
    (runtime, runtime),
    (builtin, builtin_id),
);
#define Type_of(time, type, ...) (Type) tu_of(time, ((time)tu_of(type, (type)__VA_ARGS__)))
typedef struct comptime_funct {
  sentList_t(struct {
    Type t;
    fptr name;
    void *initial_value;
  }) args;
  astBBlock block;
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
        if (t) USENAMEDPRINTER("Type", *t);
      }),
      case (c_funct, f, { PUTS("c_funct"); }),
      default(unreachable();)
  );
});
REGISTER_PRINTER(Type, {
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
      default(unreachable();)
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
// {literal conversion

bigint astStr_ton(AllocatorV allocator, fptr str_number) {
  if (str_number.len > 2 && fptr_eq(slice_split(str_number, (0, 2))[0], "0x"))
    return BInt.from.fptr(allocator, 16, slice_split(str_number, (2, -1))[0]);
  else if (str_number.len > 2 && fptr_eq(slice_split(str_number, (0, 2))[0], "0b"))
    return BInt.from.fptr(allocator, 2, slice_split(str_number, (2, -1))[0]);
  else return BInt.from.fptr(allocator, 10, str_number);
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
  iptr rtvalue;
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
bool Type_eq(Type a, Type b);
bool comptime_type_eq(comptime a, comptime b) {
  if (a.tag != b.tag) return false;
  if (fptr_eq(((fptr){sizeof(a), (u8 *)&a}), ((fptr){sizeof(b), (u8 *)&b}))) return true;
  tu_match(
      a,
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
AllocatorV state_last_allocator(EState s) {
  assertMessage(msList_len(s.scopes));
  return msHmap_allocator(msList_last(s.scopes));
}
// }

void coerce_value(value *v, Type *expected);
value eval(
    EState *evalState,
    Type *_Nullable expected,
    ast part,
    bool retflag[1]
) {
  var_ allocator = state_last_allocator(*evalState);
  var_ result = (value){};
  tu_match(
      part,
      case (astEmptyStatement, _, {}),

      case (astNumber, n, { result = (value){Type_of(comptime, c_int, astStr_ton(allocator, n))}; }),
      case (astString, s, {
        usize l = astStr_len(s);
        var_ arrt = aValue(allocator, ((ptrstype(r_arr)){runtime_of(r_sint, 8), l})); // TODO: intern
        result = state_create(evalState, Type_of(runtime, r_arr, arrt), false);
        var_ to = (fptr){l, state_fromV(evalState, result.rtvalue)};
        astStr_tom(s, &to);
      }),

  );
  coerce_value(&result, expected);
  return result;
}

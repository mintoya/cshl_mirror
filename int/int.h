#include "../ast.h"
#include "../form.h"
#include <assert.h>
#include <string.h>
#include <wheels/allocators/arenaAllocator.h>
#include <wheels/bigint.h>
#include <wheels/macros.h>
#include <wheels/mytypes.h>
#include <wheels/sList.h>
#include <wheels/shmap.h>
#include <wheels/smallstream.h>
#include <wheels/tu_macros.h>
// {types
typedef enum builtin_enum : u8 {
  builtin_none,

  builtin_$sint,
  builtin_$uint,
  builtin_$noExpand, // x
  builtin_$expand,   // x
  builtin_$if,       // x
  builtin_$while,    // x
  builtin_$type,
  builtin_$typeof,
  builtin_$void,
  builtin_$ast,
  builtin_$int,
  builtin_$any,
  builtin_fn,
  builtin_return,
  builtin_struct, // x
} builtin_id;
REGISTER_PRINTER(builtin_id, {
  switch (in) {
      // clang-format off
    case builtin_none/*     */:PUTS("none/invalid")/**/;break;

    case builtin_$int/*    */:PUTS("$int")/*     */;break;
    case builtin_$typeof/* */:PUTS("$typeof")/*     */;break;
    case builtin_$if/*     */:PUTS("$if")/*     */;break;
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
typedef struct runtime_arrayStruct *runtime_array;
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
typedef struct runtime_struct {
  runtime t;
  fptr name;
  usize offset;
} runtime_struct;
typedef struct runtime_arrayStruct {
  runtime t;
  usize count;
} *runtime_array;
typedef struct runtime_bitset {
  runtime t;
  fptr name;
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
typedef struct value_struct value;
typedef struct comptime_struct comptime_struct;
typedef struct comptime_struct {
  r_struct variable;
  // separate because of forward declaration
  sentList_t(fptr) names;
  sentList_t(value) vals;
} comptime_struct;
tu_def(
    // written so that all items are nullable,
    (comptime, u8),
    (c_invalid /**/, nothing_t),

    (c_int /*    */, bigint),
    (c_ast /*    */, ast *),
    (c_Type /*   */, Type *),
    (c_funct /*  */, comptime_funct *),
    (c_struct /* */, comptime_struct *),

    (c_any /*    */, nothing_t),
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
var_ typeof_void = Type_of(runtime, r_void, {});

typedef

    struct {
  Type t;
  fptr name;
  void *initial_value;
  bool isconst;
} comptime_funct_arg;
typedef struct comptime_funct {
  sentList_t(comptime_funct_arg) args;
  ast *block;
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
      case (c_any, _, { PUTS("any"); }),
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
  if (str.len > 2 && fptr_eq((slice_split(str, (0, 2))[0]), "0x"))
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
typedef struct rvalue_struct {
  Type t;
  iptr isConst : 1;
  iptr rtvalue : sizeof(iptr) * 8 - 1;
} rvalue;

typedef struct value_struct {
  rvalue *lvalue;
  rvalue rvalue;
} value; // a tagged union would be the same size anyway
// rvalue value_acess(value v) { v.lvalue ? *v.lvalue : v.value; }
#define value_acess(v) (*((v).lvalue ? (v).lvalue : &(v).rvalue))
#define value_ap(val) (val->lvalue ? val->lvalue : &val->rvalue)
#define value_of(v)                                                 \
  ((value){                                                         \
      .lvalue = _Generic((v), rvalue *: (v), default: (rvalue *)0), \
      .rvalue = _Generic((v), rvalue: (v), default: (rvalue){0})    \
  })

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
      case (c_invalid, _, {
        return true; // TODO probably fine
      }),
      case (c_struct, _, {
        assertMessage(false, "unimplemented"); // TODO
      }),
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
typedef msHmap(rvalue) scope;
typedef struct eval_state {
  mList(scope) scopes;
  msList(u8) stack, heap;
  msList(comptime_funct *) callstack;
  msList(struct {
    u16 count;
    u16 curr;
    u16 size;
    iptr place;
  }) heap_state; // sorted

  mHmap(umax, msList(fptr)) buffers;
  mHmap(bigint, AllocatorV) bigint_refs;
} EState;
EState state_init(AllocatorV allocator) {
  var_ l = mList_init(allocator, scope);
  mList_push(l, msHmap_init(allocator, rvalue));
  return (EState){
      .scopes = l,
      .stack = msList_init(allocator, u8, 4096),
      .callstack = msList_init(allocator, comptime_funct *),
      .heap = msList_init(allocator, u8, 512),
      .heap_state = msList_init(allocator, ptrstype(itypeof(EState, heap_state))),
      .buffers = mHmap_init(allocator, umax, msList(fptr), 100),
      .bigint_refs = mHmap_init(allocator, bigint, AllocatorV, 100),
  };
}

iptr state_stack_current(EState s) { return msList_len(s.stack); }
void state_stack_set(EState *s, iptr place) { msList_len(s->stack) = place; }
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
  var_ list = place < 0 ? s->heap : s->stack;
  place = place < 0 ? -place : place;
  assertMessage(place >= alignof(myAlign));
  place -= alignof(myAlign);
  assertMessage(place < msList_len(list), "accesed out-if-bounds memory");
  return list + place;
}
u8 *state_fromVal(EState *s, value v) {
  return state_fromV(s, value_acess(v).rtvalue);
}
AllocatorV state_last_allocator(EState *s) {
  assertMessage(mList_len(s->scopes));
  return msHmap_allocator(mList_last(s->scopes));
}

value state_create(EState *s, Type type, bool isHeap) {
  usize size = lineup(Type_sizeof(type), alignof(myAlign));
  var_ res = (rvalue){
      .t = type,
  };
  if (size)
    res.rtvalue =
        isHeap
            ? -state_heap_alloc(s, size)
            : state_stack_push(s, size);
  if_tu_is (var_ vt, comptime, res.t)
    if_tu_is (var_ inte, c_int, vt) {
      var_ alloc = state_last_allocator(s);
      var_ cpy = bigint_copy(alloc, inte);
      res.t.comptime.c_int = cpy;
      mHmap_set(s->bigint_refs, cpy, alloc);
    }
  return value_of(res);
}
void state_destroy(EState *s, value vv) {
  if (vv.lvalue) return;
  var_ v = value_acess(vv);
  if (tu_is(runtime, v.t) && lineup(Type_sizeof(v.t), alignof(myAlign)))
    if (v.rtvalue < 0) state_heap_free(s, -v.rtvalue);
  if_tu_is (var_ vt, comptime, v.t)
    if_tu_is (var_ inte, c_int, vt) {
      var_ alloc = mHmap_get(s->bigint_refs, inte);
      assertMessage(alloc);
      mHmap_rem(s->bigint_refs, inte);
      msList_deInit(*alloc, inte);
    }
}
value state_clone(EState *s, value v) {
  tu_match(
      value_acess(v).t,
      case (comptime, ctim, {
        return state_create(s, value_acess(v).t, false);
      }),
      case (runtime, rtim, {
        var_ r = state_create(s, value_acess(v).t, false);
        if (!tu_is(r_void, rtim)) {
          var_ src = state_fromVal(s, v);
          var_ dst = state_fromVal(s, r);
          memcpy(dst, src, Type_sizeof(value_acess(v).t));
        }
        return r;
      }),
      case (builtin, bltin, { return v; }),
  );
}
void state_overwrite(EState *s, rvalue *to, value from) {
  assertMessage(Type_eq(to->t, value_acess(from).t));
  tu_match(
      value_acess(from).t,
      case (comptime, ctim, {
        if_tu_is (var_ bint, c_int, ctim) {
          var_ old_int = to->t.comptime.c_int;
          var_ alloc = mHmap_get(s->bigint_refs, old_int);
          var_ allocator = alloc ? *alloc : state_last_allocator(s);

          if (alloc) {
            mHmap_rem(s->bigint_refs, old_int);
            msList_deInit(*alloc, old_int);
          }

          var_ new_int = bigint_copy(allocator, bint);
          to->t.comptime.c_int = new_int;
          mHmap_set(s->bigint_refs, new_int, allocator);
        } else *to = value_acess(from);
      }),
      case (runtime, rtim, {
        if (!tu_is(r_void, rtim)) {
          var_ src = state_fromVal(s, from);
          var_ dst = state_fromVal(s, value_of(to));
          memcpy(dst, src, Type_sizeof(value_acess(from).t));
        }
      }),
      default({ *to = value_acess(from); }),
  );
}
void state_setVal(EState *s, astId id, rvalue v) {
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
  var_ nextScope = msHmap_init(nextScopeStorage, rvalue);
  mList_push(s->scopes, nextScope);
}
void state_pop(EState *s) {
  var_ nextScope = mList_pop(s->scopes);
  var_ allocator = msHmap_allocator(nextScope);
  arena_cleanup(allocator);
}
void state_pushFn(EState *s, comptime_funct *fn) {
  msList_push(mList_allocator(s->scopes), s->callstack, fn);
  state_push(s);
}
void state_popFn(EState *s) {
  msList_pop(s->callstack);
  state_pop(s);
}
comptime_funct *_Nullable state_last_callfn(EState *s) {
  if (msList_len(s->callstack))
    return msList_last(s->callstack);
  else return nullptr;
}
// }

Type Type__(EState *s, Type t);
bool value_isType(value vv) {
  var_ v = value_acess(vv);
  if (!tu_is(comptime, v.t)) return false;
  var_ c = tu_catch(comptime, v.t, return false;);
  tu_match(
      c,
      case (c_int, i, return !i;),
      case (c_any, _, return true;),
      case (c_ast, a, return !a;),
      case (c_Type, t, return true;),
      case (c_funct, f, return f && !f->block;),
      default(return false;)
  );
}
Type value_toType(EState *s, ast err_loc, value vv) {
  var_ v = value_acess(vv);
  tu_match(
      v.t,
      case (comptime, c, {
        tu_match(
            c,
            case (c_int, i, {
              if (i) goto not_a_type;
              return typeof_int;
            }),
            case (c_any, a, { return typeof_any; }),
            case (c_ast, a, {
              if (a) goto not_a_type;
              return typeof_ast;
            }),
            case (c_Type, t, return t ? t[0] : typeof_type;),
            case (c_funct, f, {
              var_ allocator = state_last_allocator(s);
              var_ av = aValue(allocator, *f);
              defer { aFree(allocator, av, sizeof(*av)); };
              if (f && !f->block)
                return Type__(s, (Type)tu_of(comptime, tu_of(c_funct, av)));
              goto not_a_type;
            }),
            default({not_a_type : } error_ast(err_loc, snprint(stdAlloc, "3:this is a value not a type"));)
        );
      }),
      default(error_ast(err_loc, snprint(stdAlloc, "4:this is a value not a type"));)
  );
  error_ast(err_loc, snprint(stdAlloc, "5:this is a value not a type"));
}
int fptr_cmp_bbs(const void *a, const void *b) {
  var_ af = *(fptr *)a;
  var_ bf = *(fptr *)b;
  return fptr_cmp(af, bf);
}
fptr fptr_store(EState *s, fptr f) {
  var_ allocator = mList_allocator(s->scopes);
  if (!f.len) return nullFptr;
  umax hash = HMap_hash(f);
  if_decl (var_ ptr, mHmap_get(s->buffers, hash)) {
    var_ s = bbsearch(&f, *ptr, msList_len(*ptr), sizeof(fptr), fptr_cmp_bbs);
    if (s.f) return *(fptr *)s.p;
  }
  var_ nb = aCreate(allocator, u8, f.len ?: 1);
  f.ptr = memcpy(nb, f.ptr, f.len);

  msList(fptr) list = nullptr;
  if_decl (var_ l, mHmap_get(s->buffers, hash)) {
    list = *l;
  } else list = msList_init(allocator, fptr, 1);

  var_ i = bbsearch(&f, list, msList_len(list), sizeof(fptr), fptr_cmp_bbs);
  msList_ins(allocator, list, (fptr *)i.p - list, f);
  mHmap_setManaged(s->buffers, 40, 2, hash, list);

  return f;
}

runtime runtime__(EState *s, runtime r) {
  tu_match(
      r,
      case (r_void, _, return runtime_of(r_void, {});),
      case (r_uint, u, return runtime_of(r_uint, u);),
      case (r_sint, s, return runtime_of(r_sint, s);),
      case (r_ptr, p, {
        if (!p) return runtime_of(r_ptr, p);
        *p = runtime__(s, *p);
        p = (typeof(p))fptr_store(s, (fptr){sizeof(*p), (u8 *)p}).ptr;
        return runtime_of(r_ptr, p);
      }),
      case (r_arr, a, {
        if (!a) return runtime_of(r_arr, a);
        a->t = runtime__(s, a->t);
        a = (typeof(a))fptr_store(s, (fptr){sizeof(*a), (u8 *)a}).ptr;
        return runtime_of(r_arr, a);
      }),
      case (r_struct, st, {
        if (!st) return runtime_of(r_struct, st);
        var_ list = sentList_vla(st);
        foreach (var_ t, range(list[0], list[1])) {
          if (t) {
            t->name = fptr_store(s, t->name);
            t->t = runtime__(s, t->t);
          }
        }
        st = (typeof(st))fptr_store(s, (fptr){sizeof(*list) + sizeof(**list), (u8 *)list}).ptr;
        return runtime_of(r_struct, st);
      }),
      case (r_bits, bs, {
        if (!bs) return runtime_of(r_bits, bs);
        var_ list = sentList_vla(bs);
        foreach (var_ t, range(list[0], list[1])) {
          if (t) {
            t->name = fptr_store(s, t->name);
            t->t = runtime__(s, t->t);
          }
        }
        bs = (typeof(bs))fptr_store(s, (fptr){sizeof(*list) + sizeof(**list), (u8 *)list}).ptr;
        return runtime_of(r_bits, bs);
      }),
      case (r_funct, fn, {
        if (!fn) return runtime_of(r_funct, fn);
        var_ list = sentList_vla(fn);
        foreach (var_ t, range(list[0], list[1])) {
          if (t)
            *t = runtime__(s, *t);
        }
        fn = (typeof(fn))fptr_store(s, (fptr){sizeof(*list) + sizeof(**list), (u8 *)list}).ptr;
        return runtime_of(r_funct, fn);
      }),
      default(assertMessage(false);),
  );
}
comptime comptime__(EState *s, comptime c) {
  tu_match(
      c,
      case (c_invalid, _, return comptime_of(c_invalid, {});),
      case (c_int, i, return comptime_of(c_int, i);),
      case (c_ast, a, return comptime_of(c_ast, a);),
      case (c_any, a, return comptime_of(c_any, {});),
      case (c_Type, t, {
        if (!t) return comptime_of(c_Type, t);
        *t = Type__(s, *t);
        return comptime_of(c_Type, t);
      }),
      case (c_funct, fn, {
        if (!fn) return comptime_of(c_funct, fn);
        var_ list = sentList_vla(fn->args);
        foreach (var_ t, range(list[0], list[1])) {
          if (t) {
            t->t = Type__(s, t->t);
            if (t->name.len)
              t->name = fptr_store(s, t->name);
          }
        }
        fn->args = (typeof(fn->args))fptr_store(s, (fptr){sizeof(*list) + sizeof(**list), (u8 *)list}).ptr;
        return comptime_of(c_funct, fn);
      }),
      case (c_struct, st, {
        if (!st) return comptime_of(c_struct, st);
        st->variable = tu_assert(r_struct, runtime__(s, runtime_of(r_struct, st->variable)));

        var_ n_list = sentList_vla(st->names);
        foreach (var_ n, range(n_list[0], n_list[1])) {
          if (n) *n = fptr_store(s, *n);
        }
        st->names = (typeof(st->names))fptr_store(s, (fptr){sizeof(*n_list) + sizeof(**n_list), (u8 *)n_list}).ptr;

        var_ v_list = sentList_vla(st->vals);
        foreach (var_ v, range(v_list[0], v_list[1]))
          if (v)
            value_ap(v)->t = Type__(s, value_ap(v)->t);
        st->vals = (typeof(st->vals))fptr_store(s, (fptr){sizeof(*v_list) + sizeof(**v_list), (u8 *)v_list}).ptr;

        return comptime_of(c_struct, st);
      }),
      default(assertMessage(false);),
  );
}

Type Type__(EState *s, Type t) {
  tu_match(
      t,
      case (runtime, r, return (Type)tu_of(runtime, runtime__(s, r));),
      case (comptime, c, return (Type)tu_of(comptime, comptime__(s, c));),
      case (builtin, b, return t;),
      default(assertMessage(false);),
  );
}
Type *TypeP__(EState *s, Type t) {
  return (Type *)fptr_store(s, (fptr){sizeof(Type), (u8 *)REF(Type__(s, t))}).ptr;
}

Type Typeof_value(EState *s, value vv) {
  var_ v = value_acess(vv);
  tu_match(
      v.t,
      case (comptime, c, {
        tu_match(
            c,
            case (c_int, _, return typeof_int;),
            case (c_ast, _, return typeof_ast;),
            case (c_Type, _, return typeof_type;),
            case (c_any, _, return typeof_any;),

            case (c_funct, f, {
              if (!f || !f->block) return v.t;

              var_ alloc = state_last_allocator(s);
              var_ sig = aValue(alloc, *f);
              defer { aFree(alloc, sig, sizeof(*sig)); };
              sig->block = nullptr;
              return Type__(s, Type_of(comptime, c_funct, sig));
            }),

            case (c_struct, _, return v.t;),
            default(assertMessage(false, "unhandled comptime typeof");)
        );
      }),
      default(return v.t;),
  );
}
bigint value_toI(AllocatorV alloc, EState *evs, value vv) {
  var_ v = value_acess(vv);
  tu_match(
      v.t,
      case (runtime, r, {
        tu_match(
            r,
            case (r_sint, s, return BInt.from.bits(alloc, state_fromVal(evs, (value){.rvalue = v}), Type_sizeof(v.t) * 8, true);),
            case (r_uint, u, return BInt.from.bits(alloc, state_fromVal(evs, (value){.rvalue = v}), Type_sizeof(v.t) * 8, false);),
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
void coerce_value(EState *s, value *vv, Type *expected) {
  assertMessage(vv);
  if (!expected) return;
  if (Type_eq(expected[0], typeof_any)) return;

  if (Type_eq(typeof_void, expected[0])) {
    *vv = (typeof(*vv)){.rvalue = {.t = typeof_void}};
    return;
  }
  if (Type_eq(value_ap(vv)->t, *expected)) return;

  // TODO : check if this is fine
  if (vv->lvalue) { // convert to rvalue
    vv->rvalue = vv->lvalue[0];
    vv->lvalue = 0;
  }
  if (
      Type_eq(typeof_type, expected[0]) &&
      value_isType(*vv)
  ) return;
  /*
    if (
        Type_eq(typeof_int, expected[0])
    ) {
      if_tu_is (var_ rtime, runtime, value_acess(*vv).t)
        if (tu_is(r_sint, rtime) || tu_is(r_uint, rtime)) {
          var_ allocator = state_last_allocator(s);
          var_ b_val = value_toI(allocator, s, *vv);
          defer { msList_deInit(allocator, b_val); };
          *vv = state_create(s, Type_of(comptime, c_int, b_val), false);
          return;
        }
    } else if (
        Type_eq(typeof_int, value_acess(*vv).t)
    ) {
      if_tu_is (var_ rtime, runtime, expected[0])
        if (tu_is(r_sint, rtime) || tu_is(r_uint, rtime)) {
          var_ result = state_create(s, expected[0], false);
          usize bits =
              tu_is(r_sint, rtime)
                  ? tu_assert(r_sint, rtime)
                  : tu_assert(r_uint, rtime);
          usize bytes = lineup(bits, 8) / 8;
          u8 *loc = state_fromVal(s, result);
          var_ b_val = tu_assert(c_int, tu_assert(comptime, value_acess(*vv).t));
          if (b_val) memcpy(loc, b_val, bytes);
          else memset(loc, 0, bytes);
          *vv = result;
          return;
        }
    }*/
  {fail : } assertMessage(
      false,
      "unhandled coersion , %s",
      snprint(
          stdAlloc,
          "{Type}->{Type}",
          value_ap(vv)->t,
          expected[0]
      )
          .ptr
  );
}

value Type_toValue(EState *s, ast err_loc, Type t) {
  return (value){0, Type__(s, Type_of(comptime, c_Type, TypeP__(s, t)))};
}
value eval(
    EState *evalState,
    Type *_Nullable expected,
    ast part,
    bool retflag[1]
);

ptrstype(itypeof(comptime_funct, args)) eval_fn_arg(
    EState *evalState, ast part
) {
  typedef ptrstype(itypeof(comptime_funct, args)) function_arg;
  var_ res = (function_arg){0};
  tu_match(
      part,
      case (astEmptyStatement, empty, {
        res.t = typeof_void;
      }),
      case (astDecl, d, {
        if (d.name) res.name = tu_assert(astId, d.name[0]);
        if (d.type) {
          bool rf = false;
          var_ tv = eval(evalState, &typeof_type, d.type[0], &rf);
          if (rf) error_ast(d.type[0], snprint(stdAlloc, "returned while evaluating a function"));
          res.t = value_toType(evalState, d.type[0], tv);
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
      default({
        bool rf = false;
        var_ tv = eval(evalState, &typeof_type, part, &rf);
        if (rf) error_ast(part, snprint(stdAlloc, "returned while evaluating a function"));
        res.t = value_toType(evalState, part, tv);
      }),
  );
  return res;
}
value eval_fncall(
    EState *evalState,
    comptime_funct *f,
    ast args
) {
  var_ allocator = state_last_allocator(evalState);

  AllocatorV arena = arena_new_ext(allocator, 128);
  defer { arena_cleanup(arena); };

  var_ argsList = ast_match(arena, args, fp("P+a"));
  if (!argsList) {
    argsList = msList_init(arena, ast *);
    msList_push(arena, argsList, &args);
  }

  usize arg_count = argsList ? msList_len(argsList) : 0;

  var_ vla_params = sentList_vla(f->args);
  usize param_count = countof(vla_params[0]) - 1;
  if (arg_count != param_count)
    error_ast(
        args,
        snprint(
            stdAlloc,
            "expected {} args, got {}",
            param_count,
            arg_count
        )
    );
  value *eval_args = aCreate(arena, value, arg_count);
  for (usize i = 0; i < arg_count; i++) {
    bool rf = false;
    eval_args[i] = eval(evalState, &vla_params[0][i].t, argsList[i][0], &rf);
    if (rf) error_ast(argsList[i][0], snprint(stdAlloc, "returned while evaluating argument"));
  }

  state_pushFn(evalState, f);
  defer { state_popFn(evalState); };
  Type ret_type = vla_params[0][param_count].t;
  var_ TypeSize = Type_sizeof(ret_type);

  iptr ret_rtvalue =
      TypeSize
          ? ret_rtvalue = state_stack_push(evalState, TypeSize)
          : 0;

  var_ stack_checkpoint = state_stack_current(evalState[0]);
  defer { state_stack_set(evalState, stack_checkpoint); };

  for (usize i = 0; i < arg_count; i++)
    state_setVal(evalState, vla_params[0][i].name, value_acess(eval_args[i]));

  bool rflag = false;
  // var_ bb = (astBBlock){.stmts = f->block};

  value res = eval(evalState, nullptr, f->block[0], &rflag);
  coerce_value(evalState, &res, &ret_type);

  if_tu_is (var_ rt, runtime, value_acess(res).t) {
    if (TypeSize > 0) {
      var_ src = state_fromVal(evalState, res);
      var_ dst = state_fromV(evalState, ret_rtvalue);
      memcpy(dst, src, TypeSize);
      value_acess(res).rtvalue = ret_rtvalue;
    }
  }

  return res;
}
value state_getId(EState *s, astId id) {
  foreach (var_ i, range(mList_len(s->scopes), 0))
    if_decl (var_ v, msHmap_get(mList_arr(s->scopes)[i - 1], id))
      return value_of(v);

  // if (fptr_eq(id, "@noexpand")) return /*   */ (value){0, (Type)tu_of(builtin, builtin_$noExpand)};
  // if (fptr_eq(id, "@expand")) return /*  */ (value){0, (Type)tu_of(builtin, builtin_$expand)};

  if (fptr_eq(id, "@qot")) return /*   */ (value){0, (Type)tu_of(builtin, builtin_$noExpand)};
  if (fptr_eq(id, "@uqot")) return /*  */ (value){0, (Type)tu_of(builtin, builtin_$expand)};
  if (fptr_eq(id, "struct")) return /* */ (value){0, (Type)tu_of(builtin, builtin_struct)};
  if (fptr_eq(id, "@while")) return /* */ (value){0, (Type)tu_of(builtin, builtin_$while)};
  if (fptr_eq(id, "@if")) return /*    */ (value){0, (Type)tu_of(builtin, builtin_$if)};
  if (fptr_eq(id, "@ast")) return /*   */ (value){0, (Type)tu_of(builtin, builtin_$ast)};
  if (fptr_eq(id, "@int")) return /*   */ (value){0, (Type)tu_of(builtin, builtin_$int)};
  if (fptr_eq(id, "@uint")) return /*  */ (value){0, (Type)tu_of(builtin, builtin_$uint)};
  if (fptr_eq(id, "@sint")) return /*  */ (value){0, (Type)tu_of(builtin, builtin_$sint)};
  if (fptr_eq(id, "@any")) return /*   */ (value){0, (Type)tu_of(builtin, builtin_$any)};
  if (fptr_eq(id, "@type")) return /*  */ (value){0, (Type)tu_of(builtin, builtin_$type)};
  if (fptr_eq(id, "@typeof")) return /**/ (value){0, (Type)tu_of(builtin, builtin_$typeof)};
  if (fptr_eq(id, "return")) return /* */ (value){0, (Type)tu_of(builtin, builtin_return)};
  if (fptr_eq(id, "fn")) return /*     */ (value){0, (Type)tu_of(builtin, builtin_fn)};
  if (fptr_eq(id, "struct")) return /* */ (value){0, (Type)tu_of(builtin, builtin_struct)};
  error_ast((ast)tu_of(astId, id), snprint(stdAlloc, "unidentified symbol '{slice(c8)}'", id));
}
value eval_call(
    EState *evalState,
    value left,
    ast args,
    bool retflag[1]
) {
  assertMessage(retflag, "must have return flag");
  assertMessage(!*retflag, "return flag is already set?");
  var_ allocator = state_last_allocator(evalState);
  // {builtins
  if_tu_is (var_ builtin, builtin, value_acess(left).t) {
    switch (builtin) {
      case builtin_$while: {
        var_ argsList =
            tu_catch(
                astPBlock,
                args,
                error_ast(
                    args,
                    snprint(stdAlloc, "expected parenthesis with 2 statements")
                )
            )
                .stmts;
        if (sentList_length(argsList, sizeof(argsList[0])) != 2)
          error_ast(args, snprint(stdAlloc, "expected 2 args"));

        ast cond = argsList[0][0];
        ast body = argsList[1][0];

        // println("expect {Type} for {ast}", ctype, &cond);

        var_ place = state_stack_current(evalState[0]);

        var_ loop_arena = arena_new_ext(allocator, 128);
        defer { arena_cleanup(loop_arena); };

        while (true) {
          state_push(evalState);
          defer { state_pop(evalState); };
          arena_clear(loop_arena);

          state_stack_set(evalState, place);
          bool rf = false;

          var_ cond_val = eval(evalState, &typeof_int, cond, &rf);
          if (rf) {
            *retflag = true;
            return cond_val;
          }

          var_ cond_int = value_toI(loop_arena, evalState, cond_val);
          if (!cond_int)
            error_ast(cond, snprint(stdAlloc, "condition must evaluate to an integer")); // TODO actual types
          var_ cmp = bigint_cmp(cond_int, nullptr);
          if (!cmp) break;

          value last_val = eval(evalState, &typeof_any, body, &rf);
          if (rf) {
            *retflag = true;
            return last_val;
          }
        }
        return (value){0, typeof_void};
      }
      case builtin_$if: {
        var_ argsList =
            tu_catch(
                astPBlock,
                args,
                error_ast(
                    args,
                    snprint(stdAlloc, "expected parenthesis with 2 statements")
                )
            )
                .stmts;
        if (sentList_length(argsList, sizeof(argsList[0])) != 2)
          error_ast(args, snprint(stdAlloc, "expected 2 args"));

        ast cond = argsList[0][0];
        ast body = argsList[1][0];

        // println("expect {Type} for {ast}", ctype, &cond);

        var_ place = state_stack_current(evalState[0]);

        var_ loop_arena = arena_new_ext(allocator, 128);
        defer { arena_cleanup(loop_arena); };

        var_ temp_scope = msHmap_init(loop_arena, rvalue);
        mList_push(evalState->scopes, temp_scope);
        defer { mList_pop(evalState->scopes); };

        state_stack_set(evalState, place);
        bool rf = false;

        var_ cond_val = eval(evalState, &typeof_int, cond, &rf);
        if (rf) {
          *retflag = true;
          return cond_val;
        }

        var_ cond_int = value_toI(loop_arena, evalState, cond_val);
        if (!cond_int)
          error_ast(cond, snprint(stdAlloc, "condition must evaluate to an integer"));

        if (bigint_cmp(cond_int, nullptr)) {

          value last_val = eval(evalState, &typeof_any, body, &rf);
          if (rf) {
            *retflag = true;
            return last_val;
          }
        }

        return (value){0, typeof_void};
      }

      case builtin_$typeof: {
        bool r2 = false;
        value res = eval(evalState, nullptr, args, &r2);
        assertMessage(!r2, "returned inside a typeof ");
        return (value){0, Typeof_value(evalState, res)};
      }
      case builtin_$noExpand: {
        var_ pb = tu_catch(
            astPBlock, args, error_ast(args, snprint(stdAlloc, "expected parenthesis for @qot"))
        );
        var_ vla = sentList_vla(pb.stmts);
        if (countof(vla[0]) != 1)
          error_ast(args, snprint(stdAlloc, "@qot expects exactly 1 argument"));

        ast *ast_ptr = aValue(allocator, vla[0][0][0]);
        return (value){0, Type_of(comptime, c_ast, ast_ptr)};
      }
      case builtin_$expand: {
        bool rf = false;
        var_ evaled_arg = eval(evalState, &typeof_ast, args, &rf);
        if (rf) {
          *retflag = true;
          return evaled_arg;
        }

        var_ val = value_acess(evaled_arg);
        if_tu_is (var_ c_val, comptime, val.t)
          if_tu_is (var_ ast_ptr, c_ast, c_val) {
            if (!ast_ptr) error_ast(args, snprint(stdAlloc, "null ast passed to @uqot"));
            return eval(evalState, nullptr, *ast_ptr, retflag);
          }

        error_ast(
            args,
            snprint(
                stdAlloc,
                "error expanding {Type}",
                val.t,
            )
        );
      }
      case builtin_$ast:
        return (value){0, typeof_ast};
      case builtin_$int:
        return (value){0, typeof_int};
      case builtin_$sint:
      case builtin_$uint: {
        var_ mat = ast_match(allocator, args, fp("Pn"));
        defer { msList_deInit(allocator, mat); };

        if (!mat) error_ast(args, snprint(stdAlloc, "expected a number"));

        var_ resn = astStr_ton(allocator, tu_assert(astNumber, mat[0][0]));
        defer { msList_deInit(allocator, resn); };
        if (msList_len(resn) < sizeof(u16) / sizeof(*resn) + 1)
          bigint_expand(allocator, &resn, sizeof(u16) / sizeof(*resn) + 1);

        u16 res = 0;
        memcpy(&res, resn, sizeof(res));

        if (builtin == builtin_$sint) {
          var_ av = aValue(allocator, Type_of(runtime, r_sint, res));
          defer { aFree(allocator, av, sizeof(*av)); };
          return (value){0, Type__(evalState, Type_of(comptime, c_Type, av))};
        } else {
          var_ av = aValue(allocator, Type_of(runtime, r_uint, res));
          defer { aFree(allocator, av, sizeof(*av)); };
          return (value){0, Type__(evalState, Type_of(comptime, c_Type, av))};
        }

        assertMessage(false);
      }
      case builtin_$type:
        return (value){0, typeof_type};
      case builtin_$any:
        return (value){0, typeof_any};
      case builtin_return: {
        bool r2 = false;
        var_ lc = state_last_callfn(evalState);
        value res;
        if (lc) {
          var_ rts = lc->args;
          var_ sl = sentList_length(lc->args, sizeof(*lc->args));
          res = eval(
              evalState,
              sl ? &rts[sl - 1].t : nullptr,
              args,
              &r2
          );
        } else res = eval(evalState, nullptr, args, &r2);

        assertMessage(!r2, "returned inside a return ");
        *retflag = true;
        return res;
      }
      case builtin_fn: {
        AllocatorV arena = arena_new_ext(allocator, 128);
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

          var_ av = aValue(allocator, rfunc);
          defer { aFree(allocator, av, sizeof(*av)); };
          var_ rt = Type__(evalState, Type_of(comptime, c_funct, av));
          // println("made function type : {Type}", rt);
          return (value){0, rt};
        } else error_ast(args, snprint(stdAlloc, "expected parenthesized args"));
      }
      default:
        assertMessage(false, "unhandled builtin %i", builtin);
    }
  }
  // }

  if_tu_is (var_ c_val, comptime, value_acess(left).t) {
    tu_match(
        c_val,
        case (c_funct, f, {
          if (!f->block) {
            var_ rfunc = *f;
            rfunc.block = aValue(allocator, args);
            var_ av = aValue(allocator, rfunc);
            defer { aFree(allocator, av, sizeof(*av)); };

            return (value){0, Type__(evalState, Type_of(comptime, c_funct, av))};
            error_ast(args, snprint(stdAlloc, "expected block for function body"));
          } else
            return eval_fncall(evalState, f, args);
          goto fail;
        }),
        case (c_Type, t, {
          assertMessage(t);
          var_ td = (value){.rvalue = {.t = t[0]}};
          return eval_call(evalState, td, args, retflag);
        }),
        default(goto fail;),
    );
  }
  //
  // (void)something
  //
  if_tu_is (var_ r_val, runtime, value_acess(left).t) {
    if_tu_is (var_ r_val, r_void, r_val) {
      bool rf = false;
      eval(evalState, nullptr, args, &rf);
      if (rf) error_ast(args, snprint(stdAlloc, "returned from discarded result"));

      var_ ty = typeof_void;
      return (value){0, ty};
    }
  }

  // if_tu_is (var_ c_val, comptime, value_acess(left).t) {
  //   if_tu_is (var_ fn, c_funct, c_val) {
  //     if (!fn) assertMessage(false, "fn is null");
  //     if (fn->block) assertMessage(false, "fn is a value");
  //     assertMessage(false, "expected failure");
  //   } else assertMessage(false, "???");
  // } else assertMessage(false, "???");

  {fail : } assertMessage(
      false,
      "unhandled call: \n%s",
      snprint(
          stdAlloc,
          "{Type}\n{ast}\n",
          value_acess(left).t,
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

  // TODO switch to the actual operation as the name
  var_ all_operations = &(struct {char* name ; astOp_union un; }[]){
      {"call", {"()"}},
      {"dot", {"."}},
      {"mul", {"*"}},
      {"div", {"/"}},
      {"mod", {"%"}},
      {"add", {"+"}},
      {"sub", {"-"}},
      {"and", {"and"}},
      {"not", {"not"}},
      {"or", {"or"}},
      {"band", {"band"}},
      {"bnot", {"bnot"}},
      {"bor", {"bor"}},
      {"shr", {"shr"}},
      {"shl", {"shl"}},
      {"les", {"<"}},
      {"mor", {">"}},
      {"leq", {"<="}},
      {"moq", {">="}},
      {"eq", {"=="}},
      {"neq", {"!="}},
      {"pipe", {"|>"}},
      {"set", {"="}},

  };
  assertMessage(retflag, "must have return flag");
  assertMessage(!*retflag, "return flag is already set?");
  var_ allocator = state_last_allocator(evalState);

  if (op == setOp.num) {
    var_ rgh = eval(evalState, &value_acess(*left).t, right, retflag);
    if (*retflag) return rgh;
    if (value_acess(*left).isConst) error_ast(right, snprint(stdAlloc, "assigning to a const "));
    state_overwrite(evalState, value_ap(left), rgh);
    state_destroy(evalState, rgh);
    return *left;
  } else if (op == callOp.num) {
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
    var_ rv = eval(evalState, &value_acess(*left).t, right, retflag);
    if (*retflag) return rv;

    var_ arena = arena_new_ext(allocator, 128);
    defer { arena_cleanup(arena); };

    var_ lb = (bigint)value_toI(arena, evalState, lv);
    var_ rb = (bigint)value_toI(arena, evalState, rv);
    BInt.trim(&lb);
    BInt.trim(&rb);
    print("{bigint} {cstr} {bigint}  = ", lb, (char *)&op, rb);
    if (!lb || !rb) error_ast(right, snprint(stdAlloc, "could'nt convert these to integers"));
    if (
        msList_len(lb) <= sizeof(umax) / sizeof(*lb) &&
        msList_len(rb) <= sizeof(umax) / sizeof(*lb)
    ) {
      BInt.expand(arena, &lb, sizeof(umax) / sizeof(*lb));
      BInt.expand(arena, &rb, sizeof(umax) / sizeof(*lb));
      if (op == addOp.num)
        *(umax *)lb = *(umax *)lb + *(umax *)rb;
      else if (op == subOp.num)
        *(umax *)lb = *(umax *)lb - *(umax *)rb;
      else if (op == mulOp.num)
        lb = BInt.mul(arena, lb, rb);
      else if (op == divOp.num)
        *(umax *)lb = *(umax *)lb / *(umax *)rb;
      else if (op == modOp.num)
        *(umax *)lb = *(umax *)lb % *(umax *)rb;
      else assertMessage(false, "unreachable?");
    } else {
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
    }
    println("{bigint}", lb);

    return state_create(evalState, Type_of(comptime, c_int, lb), false);

  } else if (EQUAL_ANY(op, notOp.num, bnotOp.num, )) {
    assertMessage(!left, "not and bnot dont expect left");
    var_ rv = eval(evalState, &typeof_int, right, retflag);
    if (*retflag) return rv;

    var_ arena = arena_new_ext(allocator, 128);
    defer { arena_cleanup(arena); };

    var_ rb = (bigint)value_toI(arena, evalState, rv);
    var_ mem = msList_vla(rb);
    if (op == bnotOp.num) {
      foreach (var_ l, range(mem[0], mem[1]))
        *l = ~*l;
      return state_create(evalState, Type_of(comptime, c_int, rb), false); // TODO dont return a bigint from this
    } else {
      var_ rv = bigint_from(allocator, !bigint_cmp(rb, 0));
      defer { msList_deInit(allocator, rv); };
      return state_create(evalState, Type_of(comptime, c_int, rv), false); // TODO same here
    }
  } else if (op == dotOp.num) {
    assertMessage(left);
    astId prop = tu_catch(astString, right, error_ast(right, snprint(stdAlloc, "dot operator only takes left string")));
    prop = slice_split(prop, (1, prop.len - 1))[0];
    // {fake ast operations
    if_tu_is (var_ ctv, comptime, value_ap(left)->t)
      if_tu_is (var_ astv, c_ast, ctv) {
        if (!astv) error_ast(right, snprint(stdAlloc, "no value, is this a type?"));
        var_ aval = *astv;
        usize opIdx = 0;
        var_ isgeneralOp = fptr_eq(prop, "op");
        if (!isgeneralOp)
          for (; opIdx < countof(*all_operations); opIdx++) {
            var_ op = all_operations[0][opIdx];
            var_ fp = fptr_CS(op.name);
            if (fptr_eq(fp, prop))
              break;
          }
        if (isgeneralOp || opIdx < countof(*all_operations)) {
          var_ operation = tu_catch(
              astOp,
              aval,
              return state_create(evalState, Type_of(runtime, r_void, {}), false);
          );
          if (!isgeneralOp)
            if (operation.number_rep != all_operations[0][opIdx].un.num)
              return state_create(evalState, Type_of(runtime, r_void, {}), false);

          return (value){
              0,
              Type__(
                  evalState,
                  Type_of(
                      comptime,
                      c_ast,
                      aValue(allocator, (ast)tu_of(astOp, operation)) // TODO
                  )
              ),
          };
        } else if (EQUAL_ANY(1, fptr_eq(prop, "left"), fptr_eq(prop, "right"))) {
          var_ operation = tu_catch(
              astOp,
              aval,
              return state_create(evalState, Type_of(runtime, r_void, {}), false);
          );
          var_ wik =
              fptr_eq(prop, "left")
                  ? operation.left
                  : operation.right;
          assertMessage(wik);
          return (value){
              0,
              Type__(
                  evalState,
                  Type_of(comptime, c_ast, wik)
              ),
          };
        } else {
          println("it's something else?");
        }
      }
    // }
    assertMessage(false, "unreachable op?%s", &op);
  } else if (op == eqOp.num || op == neqOp.num) {
    assertMessage(left);
    var_ rv = eval(evalState, nullptr, right, retflag);
    if (*retflag) return rv;

    var_ alloc = state_last_allocator(evalState);
    bool is_eq = false;

    Type lt = Typeof_value(evalState, *left);
    Type rt = Typeof_value(evalState, rv);

    if (value_isType(*left) && value_isType(rv)) {
      is_eq = Type_eq(value_toType(evalState, right, *left), value_toType(evalState, right, rv));
    } else {
      var_ arena = arena_new_ext(alloc, 128);
      defer { arena_cleanup(arena); };
      var_ lb = value_toI(arena, evalState, *left);
      var_ rb = value_toI(arena, evalState, rv);
      if (lb && rb) {
        is_eq = (bigint_cmp(lb, rb) == 0);
      } else if (Type_eq(Typeof_value(evalState, *left), Typeof_value(evalState, rv)) && tu_is(runtime, value_acess(*left).t)) {
        is_eq = !memcmp(state_fromVal(evalState, *left), state_fromVal(evalState, rv), Type_sizeof(value_acess(*left).t));
      }
    }

    var_ ir = bigint_from(alloc, op == eqOp.num ? is_eq : !is_eq);
    defer { msList_deInit(alloc, ir); };
    return state_create(evalState, Type_of(comptime, c_int, ir), false);
  }
  assertMessage(false, "unreachable op %s", &op);
}
value eval(
    EState *evalState,
    Type *_Nullable expected,
    ast part,
    bool retflag[1]
) {
  // println("eval({ptr:Type},{ast})", expected, &part);
  assertMessage(retflag, "must have return flag");
  assertMessage(!*retflag, "return flag is already set?");
  // println("eval({ptr:Type},{astTag})", expected, part.tag);
  var_ allocator = state_last_allocator(evalState);

  // auto-qot
  if (expected && Type_eq(*expected, typeof_ast)) {
    ast *av = aValue(allocator, part);
    return (value){0, Type__(evalState, Type_of(comptime, c_ast, av))};
  }

  var_ result = (value){};
  tu_match(
      part,
      //
      // trivial
      //
      case (astEmptyStatement, _, {}),
      case (astId, id, result = state_getId(evalState, id);),
      case (astNumber, n, {
        var_ in = astStr_ton(allocator, n);
        result = state_create(evalState, Type_of(comptime, c_int, in), false);
      }),
      case (astString, s, {
        usize l = astStr_len(s);
        var_ arrt = aValue(allocator, ((ptrstype(r_arr)){runtime_of(r_sint, 8), l})); // TODO: intern
        result = state_create(evalState, Type_of(runtime, r_arr, arrt), false);
        var_ to = (fptr){l, state_fromV(evalState, value_acess(result).rtvalue)};
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
          if (*retflag) return left;
          result = eval_op(evalState, &left, op.right[0], opn, retflag);
        } else
          result = eval_op(evalState, nullptr, op.right[0], opn, retflag);
        if (*retflag) return result;
      }),
      case (astDecl, decl, {
        var_ type = (Type){};
        if (decl.type) {
          var_ t = eval(evalState, &typeof_type, decl.type[0], retflag);
          if (*retflag) return t;
          type = value_toType(evalState, decl.type[0], t);
        }

        var_ val = (value){};
        if (decl.value) {
          var_ v = eval(evalState, decl.type ? &type : nullptr, decl.value[0], retflag);
          if (*retflag) return v;
          val = v;
        } else if (decl.type)
          val = state_create(evalState, type, false);

        value_acess(val).isConst = decl.is_const;
        state_setVal(evalState, tu_assert(astId, decl.name[0]), value_acess(val));
        result = val;
      }),
      case (astBBlock, bb, {
        if (expected && !Type_eq(expected[0], typeof_any)) {
          var_ left = Type_toValue(evalState, part, expected[0]);
          result = eval_op(evalState, &left, part, callOp.num, retflag);
        } else {
          var_ vla = sentList_vla(bb.stmts);
          state_push(evalState);
          defer { state_pop(evalState); };
          result = (value){};
          foreach (var_ st, vlap(vla)) {
            if (st)
              result = eval(evalState, expected, *st, retflag);
            if (*retflag) return result;
          }
        }
      }),
      default({
        assertMessage(false, "%s", snprint(stdAlloc, "{astTag}", part.tag).ptr);
      }),

  );

  // auto-uqot
  if (expected && !Type_eq(*expected, typeof_ast)) {
    while (Type_eq(value_acess(result).t, typeof_ast)) {
      var_ ap = tu_assert(c_ast, tu_assert(comptime, value_acess(result).t));
      if (!ap) break;
      bool rf = false;
      result = eval(evalState, expected, *ap, &rf);
      if (rf) {
        *retflag = true;
        return result;
      }
    }
  }

  coerce_value(evalState, &result, expected);
  return result;
}

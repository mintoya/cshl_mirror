#if !defined(ITEM_TYPES_H)
  #define ITEM_TYPES_H (1)
  #include "ast.h"
  #include <wheels/allocator.h>
  #include <wheels/assertMessage.h>
  #include <wheels/fptr.h>
  #include <wheels/mytypes.h>
  #include <wheels/tu_macros.h>

typedef struct itype_t itype_t;
tu_def(
    (itype_t, u8),
    (it_void, nothing_t),
    (it_uint, u16),
    (it_sint, u16),
    // (it_fnum, typeof(enum floatKind{FNUM_SMALL, FNUM_MEDIUM, FNUM_LARGE})),
    (it_ptr, itype_t *),
    (it_arr, struct { itype_t * type ; usize length; }),
    (it_fun, sentList_t(itype_t *)),
    (it_struct, sentList_t(struct {itype_t * type; usize offset ; })),

    (it_type, itype_t *),
    (it_ast, nothing_t),
);

typedef struct itype {
  usize alignment;
  bool constness;
  itype_t *type;
} itype;
static usize itype_alignment(const itype *t) {
  if (t->alignment) return t->alignment;
  tu_match(
      t->type[0],

      case (it_void, _, { return 0; }),
      case (it_fun, _, { return 0; }),
      case (it_type, _, { return 0; }),
      case (it_ast, _, { return 0; }),

      case (it_uint, bits, { return (bits + 7) / 8; }),
      case (it_sint, bits, { return (bits + 7) / 8; }),

      case (it_ptr, _, {
        var_ t = (itype_t)tu_of(it_uint, 64);
        itype temp = (itype){.type = &t};
        return itype_alignment(&temp);
      }),
      case (it_arr, arry, {
        itype temp = (itype){.type = arry.type};
        return itype_alignment(&temp);
      }),
      case (it_struct, bits, {
        usize max = 1;
        var_ vla = sentList_vla(bits);
        foreach (var_ t, vla(*vla)) {
          itype temp = (itype){.type = t.type};
          usize alig = itype_alignment(&temp);
          max = max > alig ? max : alig;
        }
        return max;
      }),
      default({ assertMessage(false, "unhandled type"); }),
  );
}
static usize itype_size(const itype *t) {
  tu_match(
      t->type[0],

      case (it_void, _, { return 0; }),
      case (it_fun, _, { return 0; }),
      case (it_type, _, { return 0; }),
      case (it_ast, _, { return 0; }),

      case (it_uint, bits, { return (bits + 7) / 8; }),
      case (it_sint, bits, { return (bits + 7) / 8; }),

      case (it_ptr, _, {
        var_ t = (itype_t)tu_of(it_uint, 64);
        itype temp = (itype){.type = &t};
        return itype_size(&temp);
      }),
      case (it_arr, arry, {
        itype temp = (itype){.type = arry.type};
        return itype_size(&temp) * arry.length;
      }),
      case (it_struct, bits, {
        usize max = 0;
        usize alig = itype_alignment(t);

        var_ vla = sentList_vla(bits);
        foreach (var_ t, vla(*vla)) {
          itype temp = (itype){.type = t.type};
          usize size = itype_size(&temp);
          max = max > size + t.offset ? max : size + t.offset;
        }
        return lineup(max, alig);
      }),
      default({ assertMessage(false, "unhandled type"); }),
  );
}
static usize itype_alignment_cache(itype *t) {
  if (!t->alignment) t->alignment = itype_alignment(t);
  return t->alignment;
}
#endif

#include <__stddef_unreachable.h>
#include <wheels/mytypes.h>
#include <wheels/tu_macros.h>

typedef struct itype_t itype_t;
tu_def(
    (itype_t, u8),
    (it_void, nothing_t),
    (it_uint, u16),
    (it_sint, u16),
    // (it_fnum, typeof(enum {FNUM_SMALL, FNUM_MEDIUM, FNUM_LARGE})),
    (it_ptr, itype_t *),
    (it_arr, struct { itype_t * type ; usize length; }),
    (it_fun, sentList_t(itype_t *)),
    (it_struct, sentList_t(struct {itype_t * type; usize offset })),
);

typedef struct itype {
  usize alignment;
  itype_t type;
} itype;

usize itype_alignment(const itype *t) {
  tu_match(
      t->type,

      case (it_void, _, { return 0; }),
      case (it_fun, _, { return 0; }),

      case (it_uint, bits, { return (bits + 7) / 8; }),
      case (it_sint, bits, { return (bits + 7) / 8; }),

      case (it_ptr, _, {
        itype temp = (itype){.type = (itype_t)tu_of(it_uint, 64)};
        return itype_alignment(&temp);
      }),
      case (it_arr, arry, {
        itype temp = (itype){.type = *arry.type};
        return itype_alignment(&temp);
      }),
      case (it_struct, bits, {}),
      default({ unreachable(); }),
  );
}
usize itype_size(const itype *t) {}
usize itype_alignment_cache(const itype *t) {}
usize itype_size_cache(const itype *t) {}

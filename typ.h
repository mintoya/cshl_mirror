#include <wheels/mytypes.h>
#include <wheels/tu_macros.h>

typedef struct itype_t itype_t;
tu_def(
    (itype_t, u8),
    (it_void, nothing_t),
    (it_uint, u16),
    // (it_fnum, typeof(enum {FNUM_SMALL, FNUM_MEDIUM, FNUM_LARGE})),
    (it_sint, u16),
    (it_ptr, itype_t *),
    (it_arr, struct { itype_t * type ; usize length; }),
    (it_fun, sentList_t(itype_t *)),
    (it_struct, sentList_t(struct {itype_t * type; usize offset })),
);

typedef struct itype {
  usize alignment;
  itype_t type;
} itype;

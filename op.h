#include "ast.h"
#include "wheels/macros.h"
#include "wheels/mytypes.h"
#include "wheels/sList.h"
#include <assert.h>
#include <wheels/mylist.h>
static_assert(
    sizeof(itypeof(astOp, symLiteral)) <=
    sizeof(u32)
);
tu_def(
    // r   : right
    // l   : left
    // d   : destination/result
    (OCode, u8),
    (OSet /*    */, struct { u32 r, d; }),
    // s   : arguments list
    (OCall /*   */, struct { u32 r, d; msList(u32) s; }),
    // o   : operation (as char literals all fit in a u32)
    (OOp /*     */, struct { u32 o, r, l, d; }),
    (OPushS /*  */, nothing_t),
    (OPopS /*   */, nothing_t),
    // dir : jump direction
    (OJmp /*    */, struct { i32 dir; }),
    (OJmpZ /*   */, struct { i32 dir, r; }),
    // (JmpNZ, ),
);
usize tu_code_example(OCode o) {
  return tu_match(
      o,
      (OSet, s, sizeof(s)),
      (OCall, c, sizeof(c) + sizeof(*msList_vla(c.s))),
      (OOp, o, sizeof(o)),
      (OPushS, _, sizeof(_)),
      (OPopS, _, sizeof(_)),
      (OJmp, _, sizeof(_)),
      (OJmpZ, _, sizeof(_)),
      (default, unreachable(); (usize)0),
  );
}

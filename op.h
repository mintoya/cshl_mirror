#include "ast.h"
#include "wheels/macros.h"
#include "wheels/mytypes.h"
#include <assert.h>
#include <wheels/mylist.h>
static_assert(
    sizeof(itypeof(astOp, symLiteral)) <=
    sizeof(u32)
);

#include "type.h"
tu_def(
    (OCode, u8),
    (OBuiltin /**/, ast),       // thing to return
    (ORet /*    */, u32),       // thing to return
    (OPush /*   */, nothing_t), //
    (OPop /*    */, u32),       // thing to remain
    (ODot /*    */, struct { fptr s; u32 r; }),
    (OCall /*   */, struct { u32 r, l; }),
    (OOp /*     */, struct { u32 r, l, result, op; }),
);
typedef arrstype(c_func) funct;
typedef struct {
  mList(funct) functions;
} program;
int i = sizeof(OCode);
OCode emit() {}

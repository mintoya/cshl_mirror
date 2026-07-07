#pragma once
#include "ast.h"
#include "wheels/bigint.h"
#include "wheels/macros.h"
#include "wheels/mytypes.h"
#include "wheels/sList.h"
#include <assert.h>
#include <string.h>
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
    (ONew /*   */, struct { u32 type; }),

    (OStr /*   */, fptr),
    (ONum /*   */, bigint),

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
REGISTER_PRINTER(
    OCode,
    {
      tu_match(
          in,
          (ONew, n, PUTS("new\t"); USETYPEPRINTER(usize, n.type)),
          (OStr, s, PUTS("str\t"); USENAMEDPRINTER("slice(c8)", s)),
          (ONum, n, PUTS("num\t"); USENAMEDPRINTER("bigint", n)),
          (OSet, s, PUTS("set\t"); USETYPEPRINTER(usize, s.d); PUTS(",\t"); USETYPEPRINTER(usize, s.r)),
          (OPushS, o, PUTS("push")),
          (OPopS, o, PUTS("pop")),
          (OJmp, j, PUTS("jmp\t"); USETYPEPRINTER(isize, (isize)j.dir)),
          (OCall, c, {
            PUTS("call\t");
            USETYPEPRINTER(usize, c.r);
            PUTS(",[");
            foreach (var_ it, span(c.s, msList_len(c.s))) {
              if (it != c.s) PUTS(",");
              USETYPEPRINTER(usize, *it);
            }
            PUTS("],");
            USETYPEPRINTER(usize, c.d);
          }),
          (OOp, o, {
            PUTS("op\t");
            USETYPEPRINTER(usize, o.l);
            PUTS(",\t");
            {
              char x[sizeof(u32) + 1];
              memcpy(x, &o.o, ((char *)&o.o)[sizeof(u32) - 1] ? sizeof(u32) : strlen((char *)&o.o));
              USETYPEPRINTER(cstr, (char *)x);
            }
            PUTS(", ");
            USETYPEPRINTER(usize, o.r);
            PUTS(", ");
            USETYPEPRINTER(usize, o.d);
          }),
          (OJmpZ, j, {
            PUTS("jmpz\t");
            USETYPEPRINTER(isize, (isize)j.dir);
            PUTS(",\t");
            USETYPEPRINTER(usize, (usize)j.r);
          }),
      );
    }
);

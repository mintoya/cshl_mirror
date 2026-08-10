#pragma once
#include "../type.h"
#include <wheels/mytypes.h>

//{op_tv wrapper type
// generic function
// stores full ast
struct scope;
typedef struct {
  struct {
    astPBlock *p;
    ast *r;
  } type;
  ast *body;
  moxmap(r_func, struct scope * /*r_func_cmp*/) spec;
} gfn;
static i8 r_func_cmp(const void *a, const void *b) {
  let af = *(r_func *)a;
  let bf = *(r_func *)b;
  return Type_cmp((Type)tu_of(r_func, af), (Type)tu_of(r_func, bf));
}
tu_def((op_tv, u8), (tv, typed_value), (gfn, gfn));
typePrinter(op_tv) {
  tu_match(
      in,
      (tv, x, USETYPEPRINTER(typed_value, x)),
      (gfn, y, {
        PUTS("gfn{\n" P_INDENT_IN);

        PUTS("type: { p: ");
        if (y.type.p)
          USENAMEDPRINTER("ast", (&(ast)tu_of(astPBlock, *y.type.p)));
        else PUTS("nil");
        PUTS(", r: ");
        if (y.type.r)
          USENAMEDPRINTER("ast", y.type.r);
        else PUTS("nil");
        PUTS(" }\n");

        PUTS("body: ");
        // if (y.body)
        //   USENAMEDPRINTER("ast", y.body);
        // else PUTS("nil");
        PUTS("\n");
        PUTS("spec: {");
        if (y.spec) {
          PUTS("\n" P_INDENT_IN);
          foreach (let kv, moxmap_iter(y.spec, r_func, struct scope *)) {
            Type t = (Type)tu_of(r_func, kv.key);
            USETYPEPRINTER(Type, t);
            PUTS(" -> <scopeptr>\n");
          }
          PUTS(P_INDENT_OUT);
        }
        PUTS("}\n");

        PUTS(P_INDENT_OUT "}");
      }),
  );
}
#define Op_tv(x) match_type(                                   \
    x,                                                         \
    (tv, _t, (op_tv)tu_of(tv, _t)),                            \
    (gfn, _t, (op_tv)tu_of(gfn, _t)),                          \
    (r_void, _, (op_tv)tu_of(tv, {tu_of(r_void, {})})),        \
    (c_any, _, (op_tv)tu_of(tv, {tu_of(c_any, {})})),          \
    (ast *, a, (op_tv)tu_of(tv, {tu_of(c_ast, {}), {a}})),     \
    (Type *, _t, (op_tv)tu_of(tv, {tu_of(c_type, {}), {_t}})), \
)
#define Op_tv_is(first, second, value) (tu_is(first, value) && tu_is(second, tu_catch(first, value).t))
//}

// {opcodes
// 0 in any location is null
tu_def(
    (opcode, u8),
    (onone, nothing_t),
    // create an integer
    // creates
    (oint, fptr),
    // create a builtin
    // creates
    (obin, builtin_id),
    // create an []i8
    // creates
    (ostr, fptr),
    // create value of ast
    // creates
    (oast, /*  */ ast *),
    // creates
    // evaluate whatever is inside scope then return that
    (oscop, struct scope *),
    // create value of tpye l
    // l is nullable
    // creates
    (onew, /*  */ u32),
    // load element named  name in l into new reg
    // creates
    (olelem, struct {u32 l ; fptr name; }),
    // load n'th argument of some function into new reg
    // creates
    (oarg, u32),
    // set item in l to const
    (oconst, /**/ u32),
    // jump to d if l
    (ojif, /*  */ struct { u32 l, d; }),
    (ojmp, /*  */ u32),
    (obreak, nothing_t),
    // call l with args starting at offset off and len len and store in d
    (ocall, /* */ struct { u32 l, off, len, d; }),
    // apply o to l and r and store in d
    (oop, /*   */ struct { u32 l, r, d, o; }),
    // return thing stored in l
    (oret, /*  */ u32),
    // move thing in l to r
    (omov, /*   */ struct { u32 l, r; }),
    // copy thing pointed to by in l to thing pointed to by r
    (ocpy, /*   */ struct { u32 l, r; }),
    // store element named name in l into d
    (oselem, struct {u32 l , d ; fptr name; }),
);
#define ocode_of (opcode) tu_of
bool op_creates(opcode o) {
  return tu_match_exp(
      o,
      (oscop, _, true),
      (obin, _, true),
      (ostr, _, true),
      (oint, _, true),
      (onew, _, true),
      (oarg, _, true),
      (oast, _, true),
      (olelem, _, true),
      (default, false),

  );
}
// }

// {state
typedef struct scope scope;
struct scope {
  u32 start_reg;
  u32 next_reg;
  msList(opcode) code;
  scope *parent;
  msList(struct { op_tv value ; u32 cidx; } *) values;
  moxmap(fptr, struct { u32 idx:31; bool isconst:1; }) locals;
};
typedef struct IrBuilder IrBuilder;
struct IrBuilder {
  allocfn alloc;
  scope *env;
  moxmap(Type, Type *) types;
  moxmap(void *, usize) pointers;
  r_func *fnctx;
  Type *typectx;
};

void IrBuilder_deinit(IrBuilder *);
Type *IrBuilder_getType(IrBuilder *, Type t);

// add an operation
u32 env_addOp(IrBuilder *, opcode o);
// give a name to a value
void env_bind(IrBuilder *, fptr id, u32 reg);
// push a nested env
u32 env_push(IrBuilder *);
// destroy and free a scope
void env_destroy(IrBuilder *, scope *s);
// pop a nested env
void env_pop(IrBuilder *);
// find
u32 env_lookup(IrBuilder *, fptr id);
struct env_findOwner_result {
  u32 idx;
  scope *scope;
};
// starting at scope sp find the origional scope and opcode that created a value
struct env_findOwner_result env_findOwner(IrBuilder *, u32 idx);
scope *env_replace(IrBuilder *b, struct env_findOwner_result);

// {printers
typePrinter(scope) {
  PUTS("scope [start_reg: ");
  USETYPEPRINTER(u32, in.start_reg);
  PUTS(", next_reg: ");
  USETYPEPRINTER(u32, in.next_reg);
  PUTS("]\n" P_INDENT_IN);

  u32 current_reg = in.start_reg;

  foreach (let o, vlap(msList_vla(in.code))) {
    if (op_creates(o)) {
      current_reg++;
      USETYPEPRINTER(u32, current_reg);
      PUTS(" = ");
    }

    tu_match_void(
        o,
        (onone, _, ),
        (obreak, bre, PUTS("obreak\n")),
        (oarg, idx, {
          PUTS("oarg ");
          USETYPEPRINTER(u32, idx);
          PUTS("\n");
        }),
        (oint, str, {
          PUTS("oint '");
          USENAMEDPRINTER("slice(c8)", str);
          PUTS("'\n");
        }),
        (obin, bin, {
          PUTS("obin ");
          USETYPEPRINTER(builtin_id, bin);
          PUTS("\n");
        }),
        (ostr, str, {
          PUTS("ostr \"");
          USENAMEDPRINTER("slice(c8)", str);
          PUTS("\"\n");
        }),
        (oast, astp, {
          PUTS("oast\n" P_INDENT_IN);
#if defined TV_EXPAND_VALUE && TV_EXPAND_VALUE == 1
          USENAMEDPRINTER("ast", astp);
#else
       USENAMEDPRINTER("astTag", (astp->tag));
#endif
          PUTS(P_INDENT_OUT);
        }),
        (oscop, scop, {
          PUTS("oscop\n" P_INDENT_IN);
          USENAMEDPRINTER("scope", *scop);
          PUTS(P_INDENT_OUT);
        }),
        (onew, val, {
          PUTS("onew l:");
          USETYPEPRINTER(u32, val);
          PUTS("\n");
        }),
        (olelem, val, {
          PUTS("olelem l:");
          USETYPEPRINTER(u32, val.l);
          PUTS(" name:'");
          USENAMEDPRINTER("slice(c8)", val.name);
          PUTS("'\n");
        }),
        (oconst, val, {
          PUTS("oconst l:");
          USETYPEPRINTER(u32, val);
          PUTS("\n");
        }),
        (ojmp, val, {
          PUTS("ojmp ");
          USETYPEPRINTER(u32, val);
          PUTS("\n");
        }),
        (ojif, val, {
          PUTS("ojif l:");
          USETYPEPRINTER(u32, val.l);
          PUTS(" d:");
          USETYPEPRINTER(u32, val.d);
          PUTS("\n");
        }),
        (ocall, val, {
          PUTS("ocall l:");
          USETYPEPRINTER(u32, val.l);
          PUTS(" off:");
          USETYPEPRINTER(u32, val.off);
          PUTS(" len:");
          USETYPEPRINTER(u32, val.len);
          PUTS(" d:");
          USETYPEPRINTER(u32, val.d);
          PUTS("\n");
        }),
        (oop, val, {
          PUTS("oop l:");
          USETYPEPRINTER(u32, val.l);
          PUTS(" r:");
          USETYPEPRINTER(u32, val.r);
          PUTS(" d:");
          USETYPEPRINTER(u32, val.d);
          PUTS(" o:");
          USETYPEPRINTER(u32, val.o);
          PUTS("\n");
        }),
        (oret, val, {
          PUTS("oret l:");
          USETYPEPRINTER(u32, val);
          PUTS("\n");
        }),
        (omov, val, {
          PUTS("omov l:");
          USETYPEPRINTER(u32, val.l);
          PUTS(" r:");
          USETYPEPRINTER(u32, val.r);
          PUTS("\n");
        }),
        (ocpy, val, {
          PUTS("ocpy l:");
          USETYPEPRINTER(u32, val.l);
          PUTS(" r:");
          USETYPEPRINTER(u32, val.r);
          PUTS("\n");
        }),
        (oselem, val, {
          PUTS("oselem l:");
          USETYPEPRINTER(u32, val.l);
          PUTS(" d:");
          USETYPEPRINTER(u32, val.d);
          PUTS(" name:'");
          USENAMEDPRINTER("slice(c8)", val.name);
          PUTS("'\n");
        })
    );
  }
  PUTS("values{\n" P_INDENT_IN);
  foreach (let v, span(in.values, msList_len(in.values)))
    USETYPEPRINTER(op_tv, (*v)->value), PUTS("\n");
  PUTS("named{\n" P_INDENT_IN);
  foreach (let kv, moxmap_iter(in.locals, fptr, moxmap_vt(in.locals))) {
    USENAMEDPRINTER("slice(c8)", kv.key);
    PUTS(":");

    if (kv.val->idx > in.start_reg) {
      u32 ridx = kv.val->idx - in.start_reg - 1;
      if (ridx < msList_len(in.values))
        USETYPEPRINTER(u32, kv.val->idx), PUTS(":"), USETYPEPRINTER(op_tv, in.values[ridx]->value);
      else PUTS("<unevaluated?>");

    } else
      PUTS("<parent_reg:"), USETYPEPRINTER(u32, kv.val->idx), PUTS(">");

    PUTS("\n");
  }
  PUTS(P_INDENT_OUT "}\n");
  PUTS(P_INDENT_OUT "}\n");

  PUTS(P_INDENT_OUT);
}
typePrinter(IrBuilder) {
  PUTS("IrBuilder {\n" P_INDENT_IN);
  USETYPEPRINTER(scope, *in.env);
  PUTS(P_INDENT_OUT "}\n");
}
// }

// }

// {general helpers

// interpret a value as a  type , otherwise crash
Type value_asType(typed_value t);
// return the type of a value
Type typeof_Value(typed_value t);
// make a type into a value
typed_value Type_asValue(Type *t);
// convert a string that represents an itneger into a value (c_int)
typed_value convertInt(IrBuilder *b, fptr in);
// convert a string that represents a string into a value ([~in.len] i8)
typed_value convertString(IrBuilder *b, fptr in);

// }

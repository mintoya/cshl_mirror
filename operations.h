#include "ast.h"
#include "type.h"
#include <wheels/bigint.h>
#include <wheels/macros.h>
#include <wheels/sList.h>
typedef struct scope *scopeptr;
// 0 in any location is null
tu_def(
    (opcode, u8),
    // create an integer
    // creates
    (oint, bigint),
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
    (oscop, scopeptr),
    // create value of tpye l
    // l is nullable
    // creates
    (onew, /*  */ struct { u32 l; }),
    // set item in l to const
    (oconst, /**/ struct { u32 l; }),
    // jump to d if l
    (ojif, /*  */ struct { u32 l, d; }),
    // call l with args starting at offset off and len len and store in d
    (ocall, /* */ struct { u32 l, off, len, d; }),
    // apply o to l and r and store in d
    (oop, /*   */ struct { u32 l, r, d, o; }),
    // return thing stored in l
    (oret, /*  */ struct { u32 l; }),
    // move thing in l to r
    (omov, /*   */ struct { u32 l, r; }),
    // copy thing pointed to by in l to thing pointed to by r
    (ocpy, /*   */ struct { u32 l, r; }),
    // load element named  name in l into new reg
    // creates
    (olelem, struct {u32 l ; fptr name; }),
    // store element named name in l into d
    (oselem, struct {u32 l , d ; fptr name; }),
);
#define ocode_of(type, ...) (opcode) tu_of(type, __VA_ARGS__)

//
// whatever
//

#include <wheels/mylist.h>

typedef struct scope scope;
sliceDef(scopeptr);

struct scope {
  msList(scopeptr) relatives;
  msxmap(u32) locals;
};
typedef struct {
  AllocatorV alloc;
  mList(opcode) code;
  u32 next_reg;
  scope *env;
} IrBuilder;

u32 env_addOp(IrBuilder *b, opcode o);
void env_bind(IrBuilder *b, fptr id, u32 reg);
void env_push(IrBuilder *b);

IrBuilder IrBuilder_init(AllocatorV allocator) {
  var_ res = (IrBuilder){
      .alloc = allocator,
      .code = mList_init(allocator, opcode),
      .next_reg = 0,
      .env = nullptr,
  };
  // TODO();
  env_push(&res);
  struct {
    char *name;
    builtin_id id;
  } builtins[] = {
      {"@if", builtin_if},
      {"@while", builtin_while},
      {"@ast", builtin_ast},
      {"@any", builtin_any},
      {"@int", builtin_int},
      {"@uint", builtin_uint},
      {"@type", builtin_type},
      {"@typeof", builtin_typeof},
      {"@break", builtin_break},
      {"@qot", builtin_qot},
      {"@uqot", builtin_uqot},
      {"return", builtin_return},
      {"fn", builtin_fn},
  };
  foreach (var_ it, vla(builtins))
    env_bind(&res, fp(it.name), env_addOp(&res, ocode_of(obin, it.id)));

  return res;
}
void IrBuilder_deinit(IrBuilder *b) { TODO("not super important"); }

#include <wheels/mylist.h>

typedef struct scope scope;
typedef struct scope *scopeptr;
sliceDef(scopeptr);

struct scope {
  msList(scopeptr) relatives;
  msxmap(u32) locals;
};

AllocatorV scope_allocator(scope *s) { return ((hxmap *)s->locals)->allocator; }
scopeptr scope_parent(scope *s) {
  if (msList_len(s->relatives))
    return s->relatives[0];
  else return nullptr;
}
slice(scopeptr) scope_children(scope *s) {
  assertMessage(msList_len(s->relatives));
  return (slice(scopeptr)){
      msList_len(s->relatives) - 1,
      s->relatives + 1
  };
}

void env_bind(IrBuilder *b, fptr id, u32 reg) {
  if (msxmap_get(b->env->locals, id))
    error_at(id.ptr, "item with same name exists in scope");
  msxmap_set(b->env->locals, id, reg);
}
// u32 env_addreg(IrBuilder *b) { return ++b->next_reg; }

u32 lookup_variable_register(IrBuilder *b, fptr id) {
  for (scope *s = b->env; s; s = scope_parent(s))
    if_decl (u32 * reg_ptr, msxmap_get(s->locals, id))
      return *reg_ptr;

  error_at(id.ptr, "unknown variable");
}

void env_push(IrBuilder *b) {

  scope *new_scope = aValue(
      b->alloc,
      ((scope){
          .relatives = P$(msList_init(b->alloc, scopeptr), ({ msList_push(b->alloc, $, b->env);$; })),
          .locals = msxmap_init(b->alloc, u32)
      })
  );
  if (b->env)
    msList_push(b->alloc, b->env->relatives, new_scope);
  b->env = new_scope;
}

void env_pop(IrBuilder *b) { b->env = scope_parent(b->env); }
u32 env_addOp(IrBuilder *b, opcode o) {
  mList_push(b->code, o);
  return tu_match_exp(
      o,
      (oscop, _, TODO(push the thing); ++b->next_reg),
      (obin, _, ++b->next_reg),
      (ostr, _, ++b->next_reg),
      (oint, _, ; ++b->next_reg),
      (onew, _, ++b->next_reg),
      (oast, _, ; ++b->next_reg),
      (olelem, _, ++b->next_reg),
      (default, (u32)0),

  );
}

u32 emit_ir(IrBuilder *b, ast *node, u32 ctx) {
  assertMessage(node);
  return tu_match_exp(
      (*node),
      (astDecl, decl, ({
         u32 val_reg = 0;

         if (decl.value || decl.type) {
           val_reg = env_addOp(b, ocode_of(onew, {decl.type ? emit_ir(b, decl.type, 0) : 0}));
           if (decl.value) {
             u32 rhs_reg = 0;
             rhs_reg = env_addOp(b, ocode_of(oast, decl.value));
             env_addOp(b, ocode_of(omov, {.l = rhs_reg, .r = val_reg}));
           }
         } else val_reg = env_addOp(b, ocode_of(onew, {.l = 0}));

         if (decl.is_const) env_addOp(b, ocode_of(oconst, {val_reg}));
         if (decl.name) env_bind(b, tu_catch(astId, *decl.name), val_reg);

         val_reg;
       })),
      (astNumber, num, ({
         u8 base = 10;
         if (num.len > 0 && num.ptr[0] == '0') {
           if (num.len > 1 && num.ptr[1] == 'x') base = 16;
           else if (num.len > 1 && num.ptr[1] == 'b') base = 2;
         } else base = 10;
         env_addOp(
             b,
             ocode_of(
                 oint,
                 BInt.from.fptr(
                     scope_allocator(b->env),
                     base,
                     base != 10 ? slice_split(num, (2, -1))[0] : num
                 )
             )
         );
       })),
      (astString, str, ({
         // TODO(string includes "" and escapes, remove those);
         env_addOp(b, ocode_of(ostr, str));
       })),
      (astId, id, lookup_variable_register(b, id)),
      (astOp, op, ({
         u32 irc = 0;
         if (op.number_rep == opNumbers.call) {
           var_ left = emit_ir(b, op.left, 0);

           var_ list = b->code;
           var_ offset = mList_len(list);
           var_ len = 0;
           tu_match_void(
               *op.right,
               (astPBlock, bb, {
                 len = sentList_len(bb.stmts);
                 foreach (ast *it, vlap(sentList_vla(bb.stmts)))
                   env_addOp(b, ocode_of(oast, it));
               }),
               (default, mList_push(list, ocode_of(oast, op.right)); len = 1;),
           );
           irc = env_addOp(
               b,
               ocode_of(
                   ocall,
                   {
                       .d = env_addOp(b, ocode_of(onew, {})),
                       .len = len,
                       .off = offset,
                       .l = left,
                   }
               )
           );
         } else if (op.number_rep == opNumbers.dot) {
           irc = env_addOp(
               b,
               ocode_of(
                   olelem,
                   {
                       .l = op.left ? emit_ir(b, op.left, 0) : ctx,
                       .name = tu_catch(astId, op.right[0]),
                   }
               )
           );
         } else {
           irc = env_addOp(
               b,
               ocode_of(
                   oop,
                   {
                       .r = emit_ir(b, op.right, 0),
                       .l = emit_ir(b, op.left, 0),
                       .o = op.number_rep,
                       .d = env_addOp(b, ocode_of(onew, {})),
                   }
               )
           );
         }
         irc;
       })),
      (astPBlock, pb, ({
         if (sentList_len(pb.stmts) != 1)
           error_ast(node[0], snprint(stdAlloc, "there are no tuples in this language"));
         emit_ir(b, pb.stmts[0], ctx);
       })),
      (astBBlock, bb, ({
         u32 last_reg = 0;
         env_push(b);
         foreach (var_ stmnt, vlap(sentList_vla(bb.stmts)))
           last_reg = emit_ir(b, stmnt, ctx);
         env_pop(b);
         last_reg;
       })),
      (astABlock, ab, env_addOp(b, ocode_of(oast, node))),
      (astEmptyStatement, _, (u32)0),

  );
}

//
// opcode printer {
typePrinter("opcodeTag", itypeof(opcode, tag)) {
  tu_match_void(
      ((opcode){.tag = in}),
      (obin, _, PUTS("obin");),
      (oint, _, PUTS("oint");),
      (ostr, _, PUTS("ostr");),
      (oast, _, PUTS("oast");),
      (onew, _, PUTS("onew");),
      (oconst, _, PUTS("oconst");),
      (ojif, _, PUTS("ojif");),
      (ocall, _, PUTS("ocall");),
      (oop, _, PUTS("oop");),
      (oret, _, PUTS("oret");),
      (omov, _, PUTS("omov");),
      (ocpy, _, PUTS("ocpy");),
      (olelem, _, PUTS("olelem");),
      (oselem, _, PUTS("oselem");),
  );
}

static void print_reg(u32 r, outputFunction put, void *_arb, fptr args) {
  if (!r) PUTS("null");
  else {
    PUTS("%");
    USETYPEPRINTER(u32, r);
  }
}

static void print_opcode(opcode o, outputFunction put, void *_arb, fptr args) {
  USENAMEDPRINTER("opcodeTag", o.tag);
  PUTS(" ");
  tu_match_void(
      o,
      (oint, v, USETYPEPRINTER(bigint, v);),
      (ostr, v, USENAMEDPRINTER("slice(c8)", v);),
      (oast, v, USENAMEDPRINTER("ast", v);),
      (obin, b, USETYPEPRINTER(builtin_id, b);),
      (onew, v, {
        PUTS("l=");
        print_reg(v.l, put, _arb, args);
      }),
      (oconst, v, {
        PUTS("l=");
        print_reg(v.l, put, _arb, args);
      }),
      (ojif, v, {
        PUTS("l=");
        print_reg(v.l, put, _arb, args);
        PUTS(" d=");
        USETYPEPRINTER(u32, v.d);
      }),
      (ocall, v, {
        PUTS("l=");
        print_reg(v.l, put, _arb, args);
        PUTS(" off=");
        USETYPEPRINTER(u32, v.off);
        PUTS(" len=");
        USETYPEPRINTER(u32, v.len);
        PUTS(" d=");
        print_reg(v.d, put, _arb, args);
      }),
      (oop, v, {
        char sym[sizeof(u32) + 1] = {};
        memcpy(sym, &v.o, sizeof(v.o));
        PUTS("l=");
        print_reg(v.l, put, _arb, args);
        PUTS(" r=");
        print_reg(v.r, put, _arb, args);
        PUTS(" o='");
        USENAMEDPRINTER("cstr", (char *)sym);
        PUTS("' d=");
        print_reg(v.d, put, _arb, args);
      }),
      (oret, v, {
        PUTS("l=");
        print_reg(v.l, put, _arb, args);
      }),
      (omov, v, {
        PUTS("l=");
        print_reg(v.l, put, _arb, args);
        PUTS(" r=");
        print_reg(v.r, put, _arb, args);
      }),
      (ocpy, v, {
        PUTS("l=");
        print_reg(v.l, put, _arb, args);
        PUTS(" r=");
        print_reg(v.r, put, _arb, args);
      }),
      (olelem, v, {
        PUTS("l=");
        print_reg(v.l, put, _arb, args);
        PUTS(" name='");
        USENAMEDPRINTER("slice(c8)", v.name);
        PUTS("'");
      }),
      (oselem, v, {
        PUTS("l=");
        print_reg(v.l, put, _arb, args);
        PUTS(" name='");
        USENAMEDPRINTER("slice(c8)", v.name);
        PUTS("' d=");
        print_reg(v.d, put, _arb, args);
      }),
  );
}
typePrinter("opcode", opcode) { print_opcode(in, put, _arb, args); }

typePrinter("ir", mList(opcode)) {
  for (usize i = 0; i < mList_len(in); i++) {
    USETYPEPRINTER(u32, (u32)i);
    PUTS(": ");
    USENAMEDPRINTER("opcode", mList_arr(in)[i]);
    PUTC((char)'\n');
  }
}
typePrinter(IrBuilder) {
  PUTS("IrBuilder { next_reg: ");
  USETYPEPRINTER(u32, in.next_reg);
  PUTS(", code:\n");
  USENAMEDPRINTER("ir", in.code);
  PUTS("}\n");
}
// }
//

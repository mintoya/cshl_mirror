#include "ast.h"
#include "type.h"
#include <wheels/allocators/fbaAllocator.h>
#include <wheels/bigint.h>
#include <wheels/macros.h>
#include <wheels/mylist.h>
#include <wheels/sList.h>
typedef struct scope *scopeptr;
// 0 in any location is null
tu_def(
    (opcode, u8),
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
    (oscop, scopeptr),
    // create value of tpye l
    // l is nullable
    // creates
    (onew, /*  */ struct { u32 l; }),
    // load element named  name in l into new reg
    // creates
    (olelem, struct {u32 l ; fptr name; }),
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
    // store element named name in l into d
    (oselem, struct {u32 l , d ; fptr name; }),
);
#define ocode_of(type, ...) (opcode) tu_of(type, __VA_ARGS__)

typedef struct scope scope;
sliceDef(scopeptr);

struct scope {
  u32 start_reg;
  u32 next_reg;
  msList(opcode) code;
  msList(scopeptr) relatives;
  msList(typed_value) values;
  msxmap(struct { u32 idx ; bool isconst; }) locals;
};
typedef struct {
  AllocatorV alloc;
  scope *env;
  mxmap(Type, Type *) types;
} IrBuilder;

//
//
//

u32 env_addOp(IrBuilder *b, opcode o);
void env_bind(IrBuilder *b, fptr id, u32 reg);
void env_push(IrBuilder *b);

IrBuilder IrBuilder_init(AllocatorV allocator) {
  var_ res = (IrBuilder){
      .alloc = allocator,
      .env = nullptr,
      .types = mxmap_init(
          allocator,
          Type,
          Type *,
          3,
          Type_hash_hash,
          Type_cmp_hash
      )
  };
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
AllocatorV IrBuilder_allocator(IrBuilder *b) { return ((hxmap *)b->types)->allocator; }

AllocatorV scope_allocator(scope *s) { return ((hxmap *)s->locals)->allocator; }
Type *IrBuilder_getType(IrBuilder *b, Type t) {
  if_decl (let t, mxmap_get(b->types, t)) return *t;
  let allocator = IrBuilder_allocator(b);
  let pt = aCreate(allocator, Type);
  mxmap_set(b->types, t, pt);
  *pt = tu_match_exp(
      t,
      (builtin, b, (Type)tu_of(builtin, b)),
      (r_void, _, (Type)tu_of(r_void, {})),
      (r_uint, u, (Type)tu_of(r_uint, u)),
      (r_sint, s, (Type)tu_of(r_sint, s)),
      (r_tag, tag, (Type)tu_of(r_tag, {.name = tag.name, .type = IrBuilder_getType(b, *tag.type)})),
      (r_ptr, p, (Type)tu_of(r_ptr, IrBuilder_getType(b, *p))),
      (r_arr, arr, (Type)tu_of(r_arr, {.type = IrBuilder_getType(b, *arr.type), .count = arr.count})),
      (r_struct,
       items,
       (Type)tu_of(
           r_struct,
           mList_map(allocator, items, ({ // TODO: intern the other things also
                       $.init.t = *IrBuilder_getType(b, $.init.t);
                       $;
                     }))
       )),
      (r_bitfield,
       items,

       (Type)tu_of(
           r_bitfield,
           mList_map(allocator, items, ({ // TODO: intern the other things also
                       $.init.t = *IrBuilder_getType(b, $.init.t);
                       $;
                     }))
       )),
      (
          r_func,
          params,
          (Type)tu_of(
              r_func,
              mList_map(
                  allocator,
                  params,
                  ({ // TODO: intern the other things also
                    $ = *IrBuilder_getType(b, $);
                    $;
                  })
              )

          )
      ),
      (default, t),
  );
  return *mxmap_set(b->types, *pt, pt);
}
scopeptr scope_parent(scope *s) {
  if (msList_len(s->relatives))
    return s->relatives[0];
  else return nullptr;
}
slice(scopeptr) scope_children(scope *s) {
  if_decl (var_ len, msList_len(s->relatives))
    return (slice(scopeptr)){len - 1, s->relatives + 1};
  return (slice(scopeptr)){};
}

void env_bind(IrBuilder *b, fptr id, u32 reg) {
  if (msxmap_get(b->env->locals, id))
    error_at(id.ptr, "item with same name exists in scope");
  msxmap_set(b->env->locals, id, reg);
}
// u32 env_addreg(IrBuilder *b) { return ++b->next_reg; }

u32 env_lookup(IrBuilder *b, fptr id) {
  for (scope *s = b->env; s; s = scope_parent(s))
    if_decl (var_ reg_ptr, msxmap_get(s->locals, id))
      if (reg_ptr->idx >= s->start_reg) return reg_ptr->idx;
  error_at(id.ptr, snprint(stdAlloc, "unknown variable {slice(c8)}", id));
}

void env_push(IrBuilder *b) {
  scope *new_scope = aValue(
      b->alloc,
      ((scope){
          .relatives = P$(msList_init(b->alloc, scopeptr), ({ msList_push(b->alloc, $, b->env);$; })),
          .locals = msxmap_init(b->alloc, msxmap_iType((itypeof(scope, locals))0)),
          .values = msList_init(b->alloc, typed_value),
          .code = msList_init(b->alloc, opcode),
      })

  );

  if (b->env) {
    new_scope->start_reg = b->env->next_reg;
    new_scope->next_reg = b->env->next_reg;
    msList_push(b->alloc, b->env->relatives, new_scope);
  }
  b->env = new_scope;
}

void env_pop(IrBuilder *b) { b->env = scope_parent(b->env); }
bool op_creates(opcode o) {
  return tu_match_exp(
      o,
      (oscop, _, true),
      (obin, _, true),
      (ostr, _, true),
      (oint, _, true),
      (onew, _, true),
      (oast, _, true),
      (olelem, _, true),
      (default, false),

  );
}
u32 env_addOp(IrBuilder *b, opcode o) {
  msList_push(((hxmap *)b->env->locals)->allocator, b->env->code, o);
  return tu_match_exp(
      o,
      (oscop, _, assertMessage(false, "pushing scope by itself not allowed"), (u32)0),
      (obin, _, ++b->env->next_reg),
      (ostr, _, ++b->env->next_reg),
      (oint, _, ; ++b->env->next_reg),
      (onew, _, ++b->env->next_reg),
      (oast, _, ; ++b->env->next_reg),
      (olelem, _, ++b->env->next_reg),
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
         env_addOp(b, ocode_of(oint, num));
       })),
      (astString, str, env_addOp(b, ocode_of(ostr, str))),
      (astId, id, env_lookup(b, id)),
      (astOp, op, ({
         u32 irc = 0;
         if (op.number_rep == opNumbers.call) {
           var_ left = emit_ir(b, op.left, 0);

           var_ offset = b->env->next_reg;
           u32 len = 0;
           tu_match_void(
               *op.right,
               (astPBlock, bb, {
                 var_ lp = sentList_vla(bb.stmts);
                 len = countof(*lp);
                 foreach (ast *it, vlap(lp))
                   env_addOp(b, ocode_of(oast, it));
               }),
               (default, len = 1; env_addOp(b, ocode_of(oast, op.right));),
           );
           env_addOp(
               b,
               ocode_of(
                   ocall,
                   {
                       .d = irc = env_addOp(b, ocode_of(onew, {})),
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
           env_addOp(
               b,
               ocode_of(
                   oop,
                   {
                       .r = emit_ir(b, op.right, 0),
                       .l = emit_ir(b, op.left, 0),
                       .o = op.number_rep,
                       .d = irc = env_addOp(b, ocode_of(onew, {})),
                   }
               )
           );
         }
         irc;
       })),
      (astPBlock, pb, ({
         env_push(b);
         defer { env_pop(b); };
         if (sentList_len(pb.stmts) != 1)
           error_ast(node[0], snprint(stdAlloc, "there are no tuples in this language"));
         emit_ir(b, pb.stmts[0], ctx);
       })),
      (astBBlock, bb, ({
         u32 last_reg = 0;
         env_push(b);
         defer { env_pop(b); };
         foreach (var_ stmnt, vlap(sentList_vla(bb.stmts)))
           last_reg = emit_ir(b, stmnt, ctx);
         last_reg;
       })),
      (astABlock, ab, env_addOp(b, ocode_of(oast, node))),
      (astEmptyStatement, _, (u32)0),

  );
}
typed_value Type_asValue(Type *t) {
  return (typed_value){tu_of(c_type, {}), {.c_type = t}};
}
Type value_asType(typed_value t) {
  if (tu_is(c_type, t.t))
    return t.v.c_type[0];
  else assertMessage(false, "thing is not a type");
}
Type typeof_Value(typed_value t) { return t.t; }
typed_value convertString(IrBuilder *b, fptr in) { TODO(); }
typed_value convertInt(IrBuilder *b, fptr in) { TODO(); }

void ir_av1(IrBuilder *b) {
  let code = b->env->code;
  let values = &b->env->values;
  let allocator = IrBuilder_allocator(b);
  foreach (let i, vlap(msList_vla(code))) {
    tu_match_void(
        i,
        (oint, str, msList_push(allocator, *values, convertInt(b, str))),
        (ostr, str, msList_push(allocator, *values, convertString(b, str))),
        (obin, bin, msList_push(allocator, *values, {tu_of(builtin, bin)})),
        (oast, ats, msList_push(allocator, *values, {tu_of(c_ast, {}), ats})),
        (onew, nit, {
          if_decl (let x, nit.l)
            msList_push(allocator, *values, {value_asType((*values)[nit.l]), {}});
          else msList_push(allocator, *values, {tu_of(builtin, builtin_anyType)});
        }),
        //
        (oconst, _, ),
        (ojif, _, ),
    );
  }
}

//{ printer
static inline void print_ir_indent(int indent, printerfunction_context _ctx) {
  for (int i = 0; i < indent; i++)
    PUTS(" ");
}

typePrinter(scope) {
  let indent = fptr_toi(PRINTARGS());

  let b = (fba_buffer(myAlign[2])){};
  let ndepth = i_tofptr(fba_initBuffer(b), indent + 1);

  print_ir_indent(indent, _ctx);
  PUTS("scope [start_reg: ");
  USETYPEPRINTER(u32, in.start_reg);
  PUTS(", next_reg: ");
  USETYPEPRINTER(u32, in.next_reg);
  PUTS("]\n");

  u32 current_reg = in.start_reg;

  foreach (var_ o, vlap(msList_vla(in.code))) {
    print_ir_indent(indent + 1, _ctx);

    if (op_creates(o)) {
      current_reg++;
      PUTS("r");
      USETYPEPRINTER(u32, current_reg);
      PUTS(" = ");
    } else {
      PUTS("       ");
    }

    tu_match_void(
        o,
        (oint, str, {
          PUTS("oint '");
          USENAMEDPRINTER("slice(c8)", str);
          PUTS("'\n");
        }),
        (obin, bin, {
          PUTS("obin ");
          USETYPEPRINTER(u32, bin);
          PUTS("\n");
        }),
        (ostr, str, {
          PUTS("ostr \"");
          USENAMEDPRINTER("slice(c8)", str);
          PUTS("\"\n");
        }),
        (oast, astp, {
          PUTS("oast\n");
          PRINTARGS_PUSH(ndepth) {
            print_ir_indent(indent + 2, _ctx);
            // PRINTARGS_PUSH("");
            USENAMEDPRINTER("ast", astp);
          }
        }),
        (oscop, scop, {
          PUTS("oscop\n");
          PRINTARGS_PUSH(ndepth) {
            USENAMEDPRINTER("scope", scop);
          }
        }),
        (onew, val, {
          PUTS("onew l:r");
          USETYPEPRINTER(u32, val.l);
          PUTS("\n");
        }),
        (olelem, val, {
          PUTS("olelem l:r");
          USETYPEPRINTER(u32, val.l);
          PUTS(" name:'");
          USENAMEDPRINTER("slice(c8)", val.name);
          PUTS("'\n");
        }),
        (oconst, val, {
          PUTS("oconst l:r");
          USETYPEPRINTER(u32, val.l);
          PUTS("\n");
        }),
        (ojif, val, {
          PUTS("ojif l:r");
          USETYPEPRINTER(u32, val.l);
          PUTS(" d:");
          USETYPEPRINTER(u32, val.d);
          PUTS("\n");
        }),
        (ocall, val, {
          PUTS("ocall l:r");
          USETYPEPRINTER(u32, val.l);
          PUTS(" off:");
          USETYPEPRINTER(u32, val.off);
          PUTS(" len:");
          USETYPEPRINTER(u32, val.len);
          PUTS(" d:r");
          USETYPEPRINTER(u32, val.d);
          PUTS("\n");
        }),
        (oop, val, {
          PUTS("oop l:r");
          USETYPEPRINTER(u32, val.l);
          PUTS(" r:r");
          USETYPEPRINTER(u32, val.r);
          PUTS(" d:r");
          USETYPEPRINTER(u32, val.d);
          PUTS(" o:");
          USETYPEPRINTER(u32, val.o);
          PUTS("\n");
        }),
        (oret, val, {
          PUTS("oret l:r");
          USETYPEPRINTER(u32, val.l);
          PUTS("\n");
        }),
        (omov, val, {
          PUTS("omov l:r");
          USETYPEPRINTER(u32, val.l);
          PUTS(" r:r");
          USETYPEPRINTER(u32, val.r);
          PUTS("\n");
        }),
        (ocpy, val, {
          PUTS("ocpy l:r");
          USETYPEPRINTER(u32, val.l);
          PUTS(" r:r");
          USETYPEPRINTER(u32, val.r);
          PUTS("\n");
        }),
        (oselem, val, {
          PUTS("oselem l:r");
          USETYPEPRINTER(u32, val.l);
          PUTS(" d:r");
          USETYPEPRINTER(u32, val.d);
          PUTS(" name:'");
          USENAMEDPRINTER("slice(c8)", val.name);
          PUTS("'\n");
        })
    );
  }
}

typePrinter(IrBuilder) {
  let indent = fptr_toi(PRINTARGS());

  let b = (fba_buffer(myAlign[2])){};
  let alloc = fba_initBuffer(b);

  let ndepth = i_tofptr(alloc, indent);
  let n2depth = i_tofptr(alloc, indent + 1);

  print_ir_indent(indent, _ctx);
  PUTS("IrBuilder {\n");

  PRINTARGS_PUSH(ndepth)
  USETYPEPRINTER(scope, *in.env);

  foreach (let sc, vlap(slice_vla(scope_children(in.env))))
    PRINTARGS_PUSH(n2depth)
  USETYPEPRINTER(scope, *sc);

  print_ir_indent(indent, _ctx);
  PUTS("}\n");
}
//}
//
//

test_fn(type_interns) {
  let irb = IrBuilder_init(allocator);

  let v = (Type)tu_of(
      r_struct,
      mList_init(allocator, r_struct_item)
  );
  mList_push(
      tu_catch(r_struct, v),
      {
          .alignment = 8,
          .isconst = false,
          .name = fp("self"),
          .offset = 0,
          .init = (typed_value){tu_of(r_ptr, &v)},
      }
  );
  println("{Type}", v);

  let p1 = IrBuilder_getType(&irb, v);
  let p2 = IrBuilder_getType(&irb, *p1);
  let p3 = IrBuilder_getType(&irb, *p2);
  test_assert(p1 && p2 && p3);
  test_assert(!Type_cmp(*p1, *p2));
  test_assert(!Type_cmp(*p1, *p3));
  test_assert(p1 == p2);
  test_assert(p1 == p3);
}
test_fn(type_typeof) {
  u64 mem = 0xF00;
  u64 mem2 = ~(u64)0;
  let foo = (typed_value){
      tu_of(r_uint, 64), {.value = &mem}
  };
  let bar = (typed_value){
      tu_of(r_uint, 64), {.value = &mem2}
  };
  println("values : {typed_value} , {typed_value} ", foo, bar);
  let typeof_foo = typeof_Value(foo);
  let typeof_bar = typeof_Value(bar);
  test_assert(!Type_cmp(typeof_bar, typeof_foo));

  let typetype = typeof_Value(Type_asValue(&typeof_foo));

  test_assert(Type_cmp(typeof_Value(bar), typetype));
  println("{Type}", typetype);
  test_assert(!Type_cmp(typeof_Value(Type_asValue(&typeof_bar)), typetype));
  let typeof_typeof_bar = typeof_Value(Type_asValue(&typeof_bar));
  test_assert(!Type_cmp(typeof_Value(Type_asValue(&typeof_typeof_bar)), typetype));
  println("t\t: {}", ((fptr){sizeof(typeof_foo), (u8 *)&typeof_foo}));
  println("t(t)\t: {}", ((fptr){sizeof(typetype), (u8 *)&typetype}));
  println("t(t(t))\t: {}", ((fptr){sizeof(typeof_typeof_bar), (u8 *)&typeof_typeof_bar}));
}

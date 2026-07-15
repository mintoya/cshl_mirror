#include "ast.h"
#include "wheels/macros.h"
#include <wheels/bigint.h>
#include <wheels/sList.h>
// 0 in any location is null
tu_def(
    (opcode, u8),
    // create an integer
    // creates
    (oint, bigint),
    // create an []i8
    // creates
    (ostr, fptr),
    // create value of ast
    // creates
    (oast, /*  */ ast *),
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
#define ocode_of(...) match_type(          \
    (__VA_ARGS__),                         \
    (ostr, y, (opcode)tu_of(ostr, y)),     \
    (oast, y, (opcode)tu_of(oast, y)),     \
    (oint, y, (opcode)tu_of(oint, y)),     \
    (onew, y, (opcode)tu_of(onew, y)),     \
    (oconst, y, (opcode)tu_of(oconst, y)), \
    (ojif, y, (opcode)tu_of(ojif, y)),     \
    (ocall, y, (opcode)tu_of(ocall, y)),   \
    (oop, y, (opcode)tu_of(oop, y)),       \
    (oret, y, (opcode)tu_of(oret, y)),     \
    (omov, y, (opcode)tu_of(omov, y)),     \
    (ocpy, y, (opcode)tu_of(ocpy, y)),     \
    (olelem, y, (opcode)tu_of(olelem, y)), \
    (oselem, y, (opcode)tu_of(oselem, y)), \
)

//
// whatever
//

#include <wheels/mylist.h>

typedef struct scope scope;
typedef struct scope *scopeptr;
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
  return tu_match(
      o,
      (ostr, _, ++b->next_reg),
      (oint, _, TODO(copy the int?);++b->next_reg),
      (onew, _, ++b->next_reg),
      (oast, _, TODO(copy the ast?);++b->next_reg),
      (olelem, _, ++b->next_reg),
      (default, (u32)0),

  );
}

u32 emit_ir(IrBuilder *b, ast *node, u32 ctx) {
  assertMessage(node);
  return tu_match(
      (*node),
      (astDecl, decl, ({
         u32 val_reg = 0;

         if (decl.value || decl.type) {
           val_reg = env_addOp(b, ocode_of((onew){decl.type ? emit_ir(b, decl.type, 0) : 0}));
           env_addOp(b, ocode_of((omov){.l = emit_ir(b, decl.value, ctx), .r = val_reg}));
         } else val_reg = env_addOp(b, ocode_of((onew){.l = 0}));

         if (decl.is_const) env_addOp(b, ocode_of((oconst){val_reg}));
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
                 BInt.from.fptr(
                     scope_allocator(b->env),
                     base,
                     base != 10 ? slice_split(num, (2, -1))[0] : num
                 )
             )
         );
       })),
      (astString, str, ({
         TODO(string includes "" and escapes, remove those);
         env_addOp(b, ocode_of(str));
       })),
      (astId, id, lookup_variable_register(b, id)),
      (astOp, op, ({
         u32 irc = 0;
         if (op.number_rep == opNumbers.call) {
           var_ left = emit_ir(b, op.left, 0);

           var_ list = b->code;
           var_ offset = mList_len(list);
           var_ len = 0;
           tu_match(
               *op.right,
               (astPBlock, bb, {
                 len = sentList_len(bb.stmts);
                 foreach (ast *it, vlap(sentList_vla(bb.stmts)))
                   env_addOp(b, ocode_of(it));
               }),
               (default, mList_push(list, ocode_of(op.right)); len = 1;),
           );
           irc = env_addOp(
               b,
               ocode_of((ocall){
                   .d = env_addOp(b, ocode_of((onew){})),
                   .len = len,
                   .off = offset,
                   .l = left,
               })
           );
         } else if (op.number_rep == opNumbers.dot) {
           irc = env_addOp(
               b,
               ocode_of((olelem){
                   .l = op.left ? emit_ir(b, op.left, 0) : ctx,
                   .name = tu_catch(astId, op.right[0]),
               })
           );
         } else {
           irc = env_addOp(
               b,
               ocode_of((oop){
                   .r = emit_ir(b, op.right, 0),
                   .l = emit_ir(b, op.left, 0),
                   .o = op.number_rep,
                   .d = env_addOp(b, ocode_of((onew){})),
               })
           );
         }
         irc;
       })),
      (astPBlock, pb, ({
         if (sentList_len(pb.stmts) > 1)
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
      (astABlock, ab, env_addOp(b, ocode_of(node))),
      (astEmptyStatement, _, (u32)0),

  );
}

#pragma once
#include "ast.h"
#include "op.h"
#include "wheels/macros.h"
#include "wheels/mylist.h"
#include "wheels/mytypes.h"
#include "wheels/sList.h"
#define sentList_len(l) sentList_length(l, sizeof(*l))

typedef struct Codegen {
  AllocatorV a;
  mList(OCode) code;
  mList(msxmap(u32)) scopes; // stack of frames, name -> register
} Codegen;

static u32 emit(Codegen *cg, OCode op) {
  mList_push(cg->code, op);
  return (u32)mList_len(cg->code); // 1-based; 0 == null register
}

static void scope_push(Codegen *cg) {
  mList_push(cg->scopes, msxmap_init(cg->a, u32));
}

static void scope_pop(Codegen *cg) {
  mList_pop(cg->scopes);
}

static void scope_bind(Codegen *cg, fptr name, u32 reg) {
  msxmap_set(mList_last(cg->scopes), name, reg);
}

static u32 scope_lookup(Codegen *cg, fptr name) {
  for (isize i = (isize)mList_len(cg->scopes) - 1; i >= 0; i--) {
    u32 *r = msxmap_get(mList_arr(cg->scopes)[i], name);
    if (r) return *r;
  }
  assert(0 && "unbound identifier during codegen");
  return 0;
}

static u32 lower(Codegen *cg, ast node);

// PBlock/BBlock share this: run each stmt in order in a fresh scope,
// bracketed by push/pop so temporaries die with the block.
static u32 lower_stmts(Codegen *cg, sentList_t(ast *) stmts) {
  scope_push(cg);
  emit(cg, (OCode)tu_of(OPushS, {}));
  u32 last = 0;
  foreach (var_ it, span(stmts, sentList_length(stmts, sizeof(*stmts))))
    last = lower(cg, **it);
  emit(cg, (OCode)tu_of(OPopS, {}));
  scope_pop(cg);
  return last;
}

static u32 lower(Codegen *cg, ast node) {
  return tu_match(
      node,
      (astId, id, scope_lookup(cg, id)),

      (astEmptyStatement, _, (u32)0),

      (astNumber, text, ({
         u8 base = 10;
         fptr digits = text;
         if (text.len >= 2 && text.ptr[0] == '0' && (text.ptr[1] | 32) == 'x') {
           base = 16;
           digits = (fptr){text.len - 2, text.ptr + 2};
         } else if (text.len >= 2 && text.ptr[0] == '0' && (text.ptr[1] | 32) == 'b') {
           base = 2;
           digits = (fptr){text.len - 2, text.ptr + 2};
         }
         emit(cg, (OCode)tu_of(ONum, bigint_fptr(cg->a, base, digits)));
       })),

      (astString, text, emit(cg, (OCode)tu_of(OStr, text))),

      (astDecl, d, ({
         u32 v = d.value ? lower(cg, *d.value) : 0;
         if (d.name) scope_bind(cg, tu_catch(astId, *d.name), v);
         v;
       })),

      (astOp, o, ({
         u32 result;
         if_tu_is (astPBlock pb, astPBlock, o.right[0]) {
           u32 callee = lower(cg, *o.left);
           msList(u32) args = msList_init(cg->a, u32);
           foreach (var_ it, span(pb.stmts, sentList_len(pb.stmts)))
             msList_push(cg->a, args, lower(cg, **it));
           result = emit(cg, (OCode)tu_of(OCall, {.r = callee, .s = args}));
         } else if_tu_is (astBBlock bb, astBBlock, o.right[0]) {
           u32 callee = lower(cg, *o.left);
           msList(u32) args = msList_init(cg->a, u32);
           foreach (var_ it, span(bb.stmts, sentList_len(bb.stmts)))
             msList_push(cg->a, args, lower(cg, **it));
           result = emit(cg, (OCode)tu_of(OCall, {.r = callee, .s = args}));
         } else if (o.symLiteral[0] == '.' && o.symLiteral[1] == 0) {
           u32 l = lower(cg, *o.left);
           fptr name = tu_catch(astString, *o.right);
           result = emit(cg, (OCode)tu_of(ODot, {.l = l, .name = name}));
         } else {
           u32 l = o.left ? lower(cg, *o.left) : 0;
           u32 r = lower(cg, *o.right);
           result = emit(cg, (OCode)tu_of(OOp, {.o = o.number_rep, .l = l, .r = r}));
         }
         result;
       })),

      (astPBlock, b, lower_stmts(cg, b.stmts)),
      (astBBlock, b, lower_stmts(cg, b.stmts)),

      (astABlock, _, ({ assertMessage (false ,  "array literal lowering: opcode not decided yet"); (u32)0; })),
  );
}

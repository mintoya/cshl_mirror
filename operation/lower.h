#include "common2.h"
#include "wheels/macros.h"

void lower(superscope env, scope *s, astptr ast) {
  if (!ast) return;
  return tu_match_exp(
      *ast,
      (astDecl, decl, )
  );
}

#pragma once
#include "ast.h"
#include "type.h"
typedef struct Env {
  mList(msList(value)) values;
  msxmap(u32) vnames;
} Env;

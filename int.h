#include "ast.h"
#include "tok1.h"
#include "typ.h"
#include "wheels/funct.h"
#include "wheels/macros.h"
#include "wheels/mytypes.h"
#include "wheels/smallstream.h"
#include "wheels/tu_macros.h"
#include <stdalign.h>
#include <string.h>
#include <wheels/hhmap.h>
#include <wheels/sList.h>
#include <wheels/shmap.h>

typedef itypeof(astOp, symLiteral) astOp_sym;
typedef itypeof(astOp, number_rep) astOp_num;
typedef union astOp_union {
  astOp_sym sym;
  astOp_num num;
} astOp_union;

var_ callOp = /*  */ (astOp_union){"()"};
var_ acallOp = /* */ (astOp_union){"( )"};
var_ dotOp = /* */ (astOp_union){"."};
var_ mulOp = /* */ (astOp_union){"*"};
var_ divOp = /* */ (astOp_union){"/"};
var_ modOp = /* */ (astOp_union){"%"};
var_ addOp = /* */ (astOp_union){"+"};
var_ subOp = /* */ (astOp_union){"-"};
var_ asnOp = /* */ (astOp_union){"and"};
var_ notOp = /* */ (astOp_union){"not"};
var_ orOp = /*  */ (astOp_union){"or"};
var_ bandOp = /**/ (astOp_union){"band"};
var_ bnotOp = /**/ (astOp_union){"bnot"};
var_ borOp = /* */ (astOp_union){"bor"};
var_ shrOp = /* */ (astOp_union){"shr"};
var_ shlOp = /* */ (astOp_union){"shl"};
var_ lesOp = /* */ (astOp_union){"<"};
var_ morOp = /* */ (astOp_union){">"};
var_ leqOp = /* */ (astOp_union){"<="};
var_ moqOp = /* */ (astOp_union){">="};
var_ eqOp = /*  */ (astOp_union){"=="};
var_ neqOp = /* */ (astOp_union){"!="};
var_ pipeOp = /**/ (astOp_union){"|>"};
var_ setOp = /* */ (astOp_union){"="};

itype_t *getType(itype_t t) {
  tu_match(
      t,
      case (it_void, v, {
        static var_ t_void = (itype_t)tu_of(it_void, {});
        return &t_void;
      }),
      case (it_uint, bits, {
        static mHmap(u16, itype_t *) map = nullptr;
        if (!map) map = mHmap_init(stdAlloc, u16, itype_t *);
        if (HMap_load((void *)map) > 75) HMap_manage((HMap **)&map, stdAlloc, HMap_getMetaSize((HMap *)map) * 5);

        itype_t **cached = mHmap_get(map, bits);
        if (cached) return *cached;

        var_ new_t = aValue(stdAlloc, (itype_t)tu_of(it_uint, bits));
        mHmap_set(map, bits, new_t);
        return new_t;
      }),
      case (it_sint, bits, {
        static mHmap(u16, itype_t *) map = nullptr;
        if (!map) map = mHmap_init(stdAlloc, u16, itype_t *);
        if (HMap_load((void *)map) > 75) HMap_manage((HMap **)&map, stdAlloc, HMap_getMetaSize((HMap *)map) * 5);

        itype_t **cached = mHmap_get(map, bits);
        if (cached) return *cached;

        var_ new_t = aValue(stdAlloc, (itype_t)tu_of(it_sint, bits));
        mHmap_set(map, bits, new_t);
        return new_t;
      }),
      case (it_ptr, ptr, {
        var_ saved = getType(ptr[0]);
        static mHmap(itype_t *, itype_t *) map = nullptr;
        if (!map) map = mHmap_init(stdAlloc, itype_t *, itype_t *);
        if (HMap_load((void *)map) > 75) HMap_manage((HMap **)&map, stdAlloc, HMap_getMetaSize((HMap *)map) * 5);

        itype_t **cached = mHmap_get(map, saved);
        if (cached) return *cached;

        var_ new_t = aValue(stdAlloc, (itype_t)tu_of(it_ptr, saved));
        mHmap_set(map, saved, new_t);
        return new_t;
      }),
      case (it_arr, _, {}),
      case (it_fun, _, {}),
      case (it_struct, _, {}),
      case (it_type, _, {
        static var_ t_type = (itype_t)tu_of(it_type, {});
        return &t_type;
      }),
      case (it_ast, _, {
        static var_ t_ast = (itype_t)tu_of(it_ast, {});
        return &t_ast;
      })
  );
  return nullptr;
}

i64 parse_number(fptr str_number) {
  u8 *ptr = str_number.ptr;
  usize len = str_number.len;
  bool neg = ptr[0] == '-';
  if (neg) {
    ptr++;
    len--;
  }
  u64 val = 0;
  if (len > 2 && ptr[1] == 'x')
    for (usize i = 2; i < len; i++)
      val = val * 16 + (ptr[i] >= 'a' ? ptr[i] - 'a' + 10 : ptr[i] - '0');
  else if (len > 2 && ptr[1] == 'b')
    for (usize i = 2; i < len; i++)
      val = val * 2 + (ptr[i] - '0');
  else
    for (usize i = 0; i < len; i++)
      val = val * 10 + (ptr[i] - '0');
  return neg ? -val : val;
}

typedef struct rtval {
  itype type;
  union {
    void *runtime_value;
    struct {
      // same functions
      fptr *names;
      void *values;
      ast *ast;
    } function_value;
    itype_t *type_value;
    ast *ast_value;
  } cvalue;
  alignas(myAlign) u8 rvalue[];
} *value;
typedef msHmap(value) scope;


value eval(mList(scope) scope_list, value *expected_value, ast *ast_part) {
  assertMessage(scope_list && mList_len(scope_list));
  var_ thisAllocator = msHmap_allocator(mList_last(scope_list));
  value result = nullptr;
  tu_match(
      (ast_part[0]),
      case (astEmptyStatement, _, {}),
      case (astString, str, {}),
      case (astNumber, str_number, {}),
      case (astId, id, {}),
  );
  return result;
}

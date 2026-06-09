#include "ast.h"
#include "tok1.h"
#include "typ.h"
#include "wheels/macros.h"
#include <stdalign.h>
#include <string.h>
#include <wheels/hhmap.h>
#include <wheels/sList.h>
#include <wheels/shmap.h>

typedef struct rtval {
  itype type;
  alignas(myAlign) u8 value[];
} *rtval;

typedef msHmap(rtval) scope;
typedef itypeof(astOp, symLiteral) astOp_sym;
typedef itypeof(astOp, number_rep) astOp_num;
typedef union astOp_union {
  astOp_sym sym;
  astOp_num num;
} astOp_union;

var_ call = /*  */ (astOp_union){"()"};
var_ rcall = /* */ (astOp_union){"( )"};
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
var_ upOp = /*  */ (astOp_union){"up"};
var_ inOp = /*  */ (astOp_union){"in"};
var_ setOp = /* */ (astOp_union){"="};
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
rtval eval(mList(scope) scope_list, rtval *expected_value, ast *ast_part) {
  assertMessage(scope_list && mList_len(scope_list));
  var_ thisAllocator = msHmap_allocator(mList_last(scope_list));
  rtval result = nullptr;

  tu_match(
      (ast_part[0]),
      case (astEmptyStatement, _, {}),
      case (astString, str, {
        var_ res = (rtval)aAlloc(thisAllocator, sizeof(struct rtval) + str.len + 1);
        memcpy(res->value, str.ptr, str.len);
        res->value[str.len] = 0;
        result = res;
      }),
      case (astNumber, str_number, {
        var_ res = (rtval)aAlloc(thisAllocator, sizeof(struct rtval) + sizeof(i64));
        *(i64 *)res->value = (i64)parse_number(str_number);
        res->type = (itype){.type = aValue(thisAllocator, (itype_t)tu_of(it_sint, 64))};
        result = res;
      }),
      case (astId, id, {
        for (; 0; 0)
          ;
        foreach (var_ i, range(msList_len(scope_list), 0)) {
          var_ s = msHmap_get(mList_arr(scope_list)[i - 1], id);
          if (s) {
            result = *s;
            break;
          }
        }
        if (!result) error_at(id.ptr, snprint(stdAlloc, "couldnt find symbol {slice(c8)}", id));
      }),
  );
  return result;
}

#if !defined(TOKENIZER_H)
  #define TOKENIZER_H (1)
  #include "ast.h"
  #include "wheels/macros.h"
  #include "wheels/shmap.h"
  #include <wheels/fptr.h>
  #include <wheels/print.h>
  #include <wheels/sList.h>

void error_at(c8 *location, slice(c8) message);

static inline bool isNum(u8 i) { return i >= '0' && i <= '9'; };
static inline bool isAlphaNum(u8 i) {
  switch (i) {
    case '0' ... '9':
      return true;
    case 'a' ... 'z':
      return true;
    case 'A' ... 'Z':
      return true;
    case '@':
      return true;
    case '_':
      return true;
    default:
      return false;
  }
};

static inline msList(Token) tokenize(AllocatorV allocator, fptr input) {
  var_ res = msList_init(allocator, Token);

  var_ tmap = msHmap_init(allocator, token);
  defer { msHmap_deinit(tmap); };

  foreach (var_ pun, vla(punctuations))
    msHmap_set(tmap, pun, t_punctuation);
  foreach (var_ pun, vla(operations))
    msHmap_set(tmap, pun.name, t_operator);

  msHmap_set(tmap, "[]", t_identifier);
  msHmap_set(tmap, "[*]", t_identifier);
  msHmap_set(tmap, "$", t_identifier);

  for (usize i = 0; i < input.len;) {
    switch (input.ptr[i]) {
      case '"': {
        usize j = i + 1;
        while (j < input.len) {
          if (input.ptr[j] == '\\') j += 2;
          else if (input.ptr[j] == '"') {
            j++;
            break;
          } else j++;
        }
        if (input.ptr[j - 1] != '"') {
          error_at(input.ptr + j - 1, snprint(stdAlloc, "expected closing \""));
        } else {
          msList_push(allocator, res, ((Token){.kind = t_string, .s = {j - i, input.ptr + i}}));
          i = j;
          continue;
        }
      } break;
      case '/': {
        if (i + 1 < input.len && input.ptr[i + 1] == '*') {
          usize j = i + 2;
          while (j + 1 < input.len && !(input.ptr[j] == '*' && input.ptr[j + 1] == '/'))
            j++;
          i = j + 2;
        } else if (i + 1 < input.len && input.ptr[i + 1] == '/') {
          while (input.ptr[i] != '\n')
            i++;
        } else error_at(input.ptr + i, snprint(stdAlloc, "/ by itself"));
        continue;
      } break;

      case '0': {
        if (i + 1 < input.len && input.ptr[i + 1] == 'x') {
          usize j = i + 2;
          while (j < input.len && ((input.ptr[j] >= '0' && input.ptr[j] <= '9') ||
                                   (input.ptr[j] >= 'a' && input.ptr[j] <= 'f')))
            j++;
          if (isAlphaNum(input.ptr[j])) error_at(input.ptr + j, snprint(stdAlloc, " {c8} is not part of hex", input.ptr[j]));
          msList_push(allocator, res, ((Token){.kind = t_number, .s = {j - i, input.ptr + i}}));
          i = j;
          continue;
        } else if (i + 1 < input.len && input.ptr[i + 1] == 'b') {
          usize j = i + 2;
          while (j < input.len && (input.ptr[j] == '0' || input.ptr[j] == '1'))
            j++;
          if (isAlphaNum(input.ptr[j])) error_at(input.ptr + j, snprint(stdAlloc, " {c8} is not part of bin", input.ptr[j]));
          msList_push(allocator, res, ((Token){.kind = t_number, .s = {j - i, input.ptr + i}}));
          i = j;
          continue;
        }
      } // FALLTHROUGH
      case '1' ... '9': {
        usize j = i + 1;
        while (j < input.len && isNum(input.ptr[j]))
          j++;
        msList_push(allocator, res, ((Token){.kind = t_number, .s = {j - i, input.ptr + i}}));
        i = j;
        continue;
      } break;

      case 0 ... ' ':
        i++;
        continue;

      default: {

        if (isAlphaNum(input.ptr[i])) {
          usize j = i + 1;
          while (j < input.len && isAlphaNum(input.ptr[j]))
            j++;
          fptr word = {j - i, input.ptr + i};
          token *wkind = msHmap_get(tmap, word);
          msList_push(allocator, res, ((Token){.kind = wkind ? *wkind : t_identifier, .s = word}));
          i = j;
          continue;
        } else {
          bool matched = false;
          for (
              usize diff = input.len - i > 8 ? 8 : input.len - i;
              diff > 0;
              diff--) {
            token *kind = msHmap_get(tmap, ((fptr){diff, input.ptr + i}));
            if (kind) {
              msList_push(allocator, res, ((Token){.kind = *kind, .s = {diff, input.ptr + i}}));
              i += diff;
              matched = true;
              break;
            }
          }
          if (matched) continue;
        }
        error_at(input.ptr + i, snprint(stdAlloc, "unexpected character {c8}", input.ptr[i]));
      } break;
    }
  }

  return res;
}
#endif

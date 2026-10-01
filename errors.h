#pragma once
#include "type.h"
typedef struct Errors Errors;
struct Errors {
  typeenv tenv;
  Type a;
  Type b;
  struct srcpos_links {
    struct srcpos_links *last;
    srcpos pos;
  } locs;
};

static inline Errors *Errors_new(typeenv tenv, Type a, Type b) {
  return avalue(stdAlloc, ((Errors){.tenv = tenv, .a = a, .b = b}));
}
static inline void Errors_add_location(Errors *error, srcpos pos) {
  if (!pos) return;
  struct srcpos_links *last = error->locs.pos
      ? avalue(stdAlloc, error->locs) : nullptr;
  error->locs = (struct srcpos_links){.last = last, .pos = pos};
}
static inline void Errors_deInit(Errors *error) {
  if (!error) return;
  struct srcpos_links *link = error->locs.last;
  while (link) {
    let last = link->last;
    adestroy(stdAlloc, link);
    link = last;
  }
  adestroy(stdAlloc, error);
}
static void Errors_print_locations(const struct srcpos_links *link, typeenv tenv,
                                   printerfunction_context _ctx) {
  if (link->last) Errors_print_locations(link->last, tenv, _ctx);
  if (link->pos)
    PUTS(":"), USETYPEPRINTER(srcpos_full, ((srcpos_full){link->pos, tenv.plist, tenv.flist}));
}
typePrinter(Errors) {
  Errors_print_locations(&in.locs, in.tenv, _ctx);
  if (!tu_is(r_null, in.a.t))
    PUTS("\ntype a: "), USETYPEPRINTER(Type_full, ((Type_full){in.a, in.tenv}));
  if (!tu_is(r_null, in.b.t))
    PUTS("\ntype b: "), USETYPEPRINTER(Type_full, ((Type_full){in.b, in.tenv}));
}
static char *Errors_print(void *userdata) {
  print_wfO(fileprint, stderr, "{Errors}", *(Errors *)userdata);
  return "";
}
static inline Errors *Errors_get(err_t error) {
  return error.fn == Errors_print ? error.fnd : nullptr;
}
static inline err_t Errors_with_location(err_t error, typeenv tenv, srcpos pos) {
  if (!error.err_code) return error;
  if (error.fn && error.fn != Errors_print) return error;
  if (!error.fnd) {
    error.fnd = Errors_new(tenv, (Type){}, (Type){});
    error.fn = Errors_print;
  }
  Errors_add_location(error.fnd, pos);
  return error;
}
static inline void Errors_release(err_t *error) {
  Errors_deInit(Errors_get(*error));
  if (error->fn == Errors_print) *error = (err_t){};
}
#define return_type_err(code, tenv, a, b, ...) \
  pass_err(((err_t){.err_code = code, .file = __FILE__, .function = __FUNCTION__, \
                    .line = __LINE__, .fn = Errors_print, \
                    .fnd = Errors_new(tenv, a, b)}) __VA_OPT__(, ) __VA_ARGS__)

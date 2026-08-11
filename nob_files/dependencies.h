#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined CC
  #define CC "clang"
#endif
#if defined(_WIN32)
  #define EXTENSION ".exe"
#else
  #define EXTENSION ""
#endif

char *strcats(char *c, ...) {
  va_list l;
  va_start(l, c);
  char *nc = 0;
  while ((nc = va_arg(l, char *)))
    strcat(c, nc);
  va_end(l);
  return c;
}
#define strcats(cp, ...) strcats(cp __VA_OPT__(, __VA_ARGS__, (char *)0))
int clone(char *url) {
  return system(strcats((char[1024]){}, "git clone ", url));
}
int main(int argc, char **argv) {
  puts("Missing dependencies. Cloning...\n");
  if (
      clone("https://github.com/tsoding/nob.h deps/nob.h") ||
      clone("https://codeberg.org/mintoya/wheels deps/wheels") ||
      clone("https://github.com/tree-sitter/tree-sitter deps/tree-sitter")
  ) {
    puts("Failed to clone dependencies.\n");
    return 1;
  }

  if (system(CC " -o nob" EXTENSION " nob.c")) {
    puts("Failed to recompile self\n");
    return 1;
  }
  char cmd[1024] = "./nob" EXTENSION;
  for (int i = 1; i < argc; ++i)
    strcats(cmd, " ", argv[i]);
  return system(cmd);
}

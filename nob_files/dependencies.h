#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined CC
  #define CC "clang"
#endif
#if defined(_WIN32)
  #define PREFIX ""
  #define EXTENSION ".exe"
#else
  #define PREFIX "./"
  #define EXTENSION ""
#endif
#if __has_include(<unistd.h> )
  #include <unistd.h>
#endif
#if __has_include(<process.h> )
  #include <direct.h>
  #include <process.h>
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
char *strcats_space(char *c, ...) {
  va_list l;
  va_start(l, c);
  char *nc = 0;
  while ((nc = va_arg(l, char *)))
    strcat(strcat(c, nc), " ");
  va_end(l);
  return c;
}
#define strcats_space(cp, ...) strcats_space(cp __VA_OPT__(, __VA_ARGS__, (char *)0))
int clone(char *url) {
  return system(strcats((char[256]){}, "git clone ", url));
}
int main(int argc, char **argv) {
  puts("Missing dependencies. Cloning...\n");
  if (
      clone("https://github.com/tsoding/nob.h deps/nob.h") ||
      clone("https://codeberg.org/mintoya/wheels deps/wheels") ||
      clone("https://github.com/tree-sitter/tree-sitter deps/tree-sitter")
  ) return puts("Failed to clone dependencies.\n"), 1;

  if (rename("nob" EXTENSION, "nob.old" EXTENSION))
    return puts("Failed to rename self\n"), 1;

  if (system(strcats_space(((char[256]){}), CC, "-o", "nob" EXTENSION, "nob.c", "-w")))
    return puts("Failed to recompile self\n"), 1;

  if (chdir("deps/wheels"))
    return puts("chdir failed idk"), 1;
  if (system(strcats_space(((char[256]){}), CC, "-o", "nob" EXTENSION, "nob.c", "-w")))
    return puts("failed to run nob for wheels dependency"), 1;
  if (system(PREFIX "nob" EXTENSION)) return 1;
  if (chdir("../.."))
    return puts("chdir failed idk"), 1;

  char cmd[100] = {};
  strcats_space(cmd, PREFIX "nob" EXTENSION);
  for (int i = 1; i < argc; i++)
    strcats_space(cmd, argv[i]);
  printf("done cloning dependencies , rerunning");
  return system(cmd);
}

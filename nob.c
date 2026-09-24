#if !defined CC
  #define CC "gcc"
#endif

#if defined(_WIN32)
  #define EXTENSION ".exe"
#else
  #define EXTENSION ""
#endif

#if !defined INPUT_FLAGS
  #define INPUT_FLAGS "-fsanitize=address", "-fsanitize=undefined", "-O0"
#endif

#if __has_include("deps/nob.h/nob.h")
  #include "nob_files/main.h"
int main(int argc, char **argv) {
  NOB_GO_REBUILD_URSELF_PLUS(argc, argv, "nob_files/main.h", "nob_files/dependencies.h");
  return actualmain(argc, argv);
}
#else
  #include "nob_files/dependencies.h"
#endif

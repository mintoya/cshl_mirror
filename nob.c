#if !defined CC
  #define CC "clang"
#endif
#if defined(_WIN32)
  #undef CC
  #define CC "clang"
#endif

#if defined(_WIN32)
  #define EXTENSION ".exe"
#else
  #define EXTENSION ""
#endif

#if !defined INPUT_FLAGS
  #define INPUT_FLAGS  ""
#endif

#if __has_include("deps/nob.h/nob.h")
  #include "nob_files/main.h"
int main(int argc, char **argv) {
  NOB_GO_REBUILD_URSELF(argc, argv);
  actualmain(argc , argv);
}
#else
  #include "nob_files/dependencies.h"
#endif

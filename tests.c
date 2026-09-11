#define MY_TEST_FRAMEWORK_C (1)
#define TESTS_SUBPROCESSES (1)
#include <wheels/tests.h>

// so output survives a crash mid-test
__attribute__((constructor)) static void unbuffer_test_output(void) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  setvbuf(stderr, nullptr, _IONBF, 0);
}

//
#include "indentprinter.h"
#include "test_compile.c"


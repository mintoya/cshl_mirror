#define TESTS_SUBPROCESSES (1)
#define MY_TEST_FRAMEWORK_C (1)
#include <wheels/tests.h>
//
#include "indentprinter.h"
#include "ir/interpret.h"
#include "ir/lower.h"

fptr *filectx = nullptr;
__attribute__((noreturn)) void(error_at)(u8 *location, fptr message) {
  fflush(stdout); // otherwise the error text is lost when the trap kills the process
  assertMessage(false);
}

#include "test_compile.c"

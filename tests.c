#define MY_TEST_FRAMEWORK_C (1)
#include <wheels/tests.h>
#include "ir/lower.h"
#include "ir/interpret.h"
#include "indentprinter.h"

fptr *filectx = nullptr;
__attribute__((noreturn)) void(error_at)(u8 *location, fptr message) {
  assertMessage(false);
}


#define MY_TEST_FRAMEWORK_C (1)
#include <wheels/tests.h>

#include "operations.h"
#include "type.h"
fptr *filectx = nullptr;
__attribute__((noreturn)) void(error_at)(u8 *location, fptr message) {
  assertMessage(false);
}

#define MY_TEST_FRAMEWORK_C (1)
#include <wheels/tests.h>
// #include "top.h"

fptr *filectx = nullptr;
__attribute__((noreturn)) void error_at(u8 *location, slice(c8) message) {
  assertMessage(false);
}

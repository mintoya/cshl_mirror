#define PRINT_NDEF
#include <filesystem>
#include <print>
namespace fs = std::filesystem;
#include "deps/wheels/macros.h"
#include "deps/wheels/print.h"
#include "limits.h"
int main(void) {
  let p = fs::path("");
  p = p / "helo";
  std::println("{}", p);
}
#include "deps/wheels/wheels.h"

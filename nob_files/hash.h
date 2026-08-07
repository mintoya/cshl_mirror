#include <stdcountof.h>
#include <stdint.h>
#include <stdio.h>

uint64_t hashfile(FILE *f) {
  uint64_t hr = 5381;
  char h[1024];
  uint64_t l;
  while ((l = fread(h, sizeof(h[0]), countof(h), f)))
    for (int i = 0; i < l; i++) {
      hr ^= hr >> 3;
      hr = hr * 65;
      hr ^= (h[i]);
    }
  return hr;
}

int main(void) { printf("%zu", hashfile(stdin)); }

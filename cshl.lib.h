// Runtime support for bitstructs, used by the interpreter and included at the top of the C that
// cshl emits.
#ifndef CSHL_LIB_H
#define CSHL_LIB_H

#include <stdint.h>

// Where a bitstruct member lives: `ptr` addresses its first byte, and
// `bit_offset` selects its first bit within that byte (0–7).
// A member is not byte addressable, so this pair is what stands in for the address of one.
typedef struct {
  char *ptr;
  uint8_t bit_offset;
} double_offset;

// Bits are numbered from the least significant one of each byte, so bit `k` is bit `k % 8` of
// byte `k / 8` and a member starts where the members before it ended.

// Copies the `width` bits at `o` into `bits`, least significant bit first, and zero fills the bits
// above `width`. `bits` needs room for (width + 7) / 8 bytes.
static inline void cshl_bits_load(double_offset o, uint32_t width, unsigned char *bits) {
  uint32_t bytes = (width + 7) / 8;
  for (uint32_t i = 0; i < bytes; i++)
    bits[i] = 0;
  for (uint32_t i = 0; i < width; i++) {
    uint32_t bit = o.bit_offset + i;
    unsigned char byte = (unsigned char)o.ptr[bit / 8];
    bits[i / 8] = (unsigned char)(bits[i / 8] | (((byte >> (bit % 8)) & 1) << (i % 8)));
  }
}

// Copies the `width` bits of `bits` (least significant bit first) to `o`, leaving the bits after
// the member alone.
static inline void cshl_bits_store(double_offset o, uint32_t width, const unsigned char *bits) {
  for (uint32_t i = 0; i < width; i++) {
    uint32_t bit = o.bit_offset + i;
    unsigned char mask = (unsigned char)(1u << (bit % 8));
    unsigned char *byte = (unsigned char *)&o.ptr[bit / 8];
    if ((bits[i / 8] >> (i % 8)) & 1) *byte = (unsigned char)(*byte | mask);
    else *byte = (unsigned char)(*byte & (unsigned char)~mask);
  }
}

#endif // CSHL_LIB_H

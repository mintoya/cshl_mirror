#ifndef CSHL_LIB_H
#define CSHL_LIB_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

typedef struct {
} void_t;

typedef struct {
  unsigned char *ptr;
  unsigned char bit_offset; // :4
  unsigned int width;
} double_offset;

#define double_offset_t(T)                                                                      \
  union {                                                                                       \
    /*dont use this, it's here for type info, and pointer sized cause that's garunteed to fit*/ \
    T *data_type;                                                                               \
    double_offset data;                                                                         \
  }

static inline int cshl_bits_load(double_offset o, unsigned char *dest) {
  unsigned int bytes = (o.width + 7) / 8;
  for (unsigned int i = 0; i < bytes; i++)
    dest[i] = 0;
  for (unsigned int i = 0; i < o.width; i++) {
    unsigned int bit = o.bit_offset + i;
    unsigned char byte = (unsigned char)o.ptr[bit / 8];
    dest[i / 8] = (unsigned char)(dest[i / 8] | (((byte >> (bit % 8)) & 1) << (i % 8)));
  }
  return 0;
};
static inline int cshl_bits_store(double_offset o, const unsigned char *src) {
  for (unsigned int i = 0; i < o.width; i++) {
    unsigned int bit = o.bit_offset + i;
    unsigned char mask = (unsigned char)(1u << (bit % 8));
    unsigned char *byte = (unsigned char *)&o.ptr[bit / 8];
    if ((src[i / 8] >> (i % 8)) & 1) *byte = (unsigned char)(*byte | mask);
    else *byte = (unsigned char)(*byte & (unsigned char)~mask);
  }
  return 0;
};

// Byte copies also support references stored in unaligned structure members.
static inline double_offset cshl_place(unsigned char *slot, unsigned kind) {
  double_offset p = {slot, 0, 0};
  if (kind == 1) memcpy(&p.ptr, slot, sizeof(p.ptr));
  if (kind == 2) memcpy(&p, slot, sizeof(p));
  return p;
}
static inline int cshl_read(double_offset p, unsigned char *dest, size_t capacity, size_t size, unsigned sign_bits) {
  unsigned char *bytes = dest;
  memset(bytes, 0, capacity);
  if (p.width) cshl_bits_load(p, bytes);
  else if (size) memcpy(bytes, p.ptr, size);
  unsigned bits = p.width ? p.width : sign_bits;
  if (sign_bits && bits && (bytes[(bits - 1) / 8] & (1u << ((bits - 1) % 8)))) {
    if (bits % 8) bytes[bits / 8] |= (unsigned char)(0xffu << (bits % 8));
    size_t used = (bits + 7) / 8;
    if (capacity > used) memset(bytes + used, 0xff, capacity - used);
  }
  return 0;
}
static inline uint64_t cshl_load(double_offset p, unsigned sign_bits) {
  uint64_t value = 0;
  cshl_read(p, (unsigned char *)&value, sizeof(value), sizeof(value), sign_bits);
  return value;
}
static inline int cshl_write(double_offset p, const unsigned char *src, size_t size) {
  if (p.width) cshl_bits_store(p, src);
  else if (size) memcpy(p.ptr, src, size);
  return 0;
}
static inline int cshl_copy(double_offset dest, double_offset src, size_t size, unsigned sign_bits) {
  unsigned char value[size ? size : 1];
  cshl_read(src, value, size, size, sign_bits);
  cshl_write(dest, value, size);
  return 0;
}

#endif // CSHL_LIB_H

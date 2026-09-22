#ifndef CSHL_LIB_H
#define CSHL_LIB_H

typedef struct {
  unsigned char *ptr;
  unsigned char bit_offset; // :4
  unsigned int width;
} double_offset;

static inline void cshl_bits_load(double_offset o, unsigned char *dest) {
  unsigned int bytes = (o.width + 7) / 8;
  for (unsigned int i = 0; i < bytes; i++)
    dest[i] = 0;
  for (unsigned int i = 0; i < o.width; i++) {
    unsigned int bit = o.bit_offset + i;
    unsigned char byte = (unsigned char)o.ptr[bit / 8];
    dest[i / 8] = (unsigned char)(dest[i / 8] | (((byte >> (bit % 8)) & 1) << (i % 8)));
  }
}
static inline void cshl_bits_store(double_offset o, const unsigned char *src) {
  for (unsigned int i = 0; i < o.width; i++) {
    unsigned int bit = o.bit_offset + i;
    unsigned char mask = (unsigned char)(1u << (bit % 8));
    unsigned char *byte = (unsigned char *)&o.ptr[bit / 8];
    if ((src[i / 8] >> (i % 8)) & 1) *byte = (unsigned char)(*byte | mask);
    else *byte = (unsigned char)(*byte & (unsigned char)~mask);
  }
}

#endif // CSHL_LIB_H

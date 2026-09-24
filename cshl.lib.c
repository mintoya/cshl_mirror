#ifndef CSHL_LIB_H
#define CSHL_LIB_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <limits.h>

// Placeholder for zero-sized values; omitted from generated struct members.
typedef struct {
  char cshl_unused;
} void_t;

typedef struct {
  unsigned char *ptr;
  unsigned char bit_offset;
  unsigned int width;
} double_offset;

#define double_offset_t(T)                                                                      \
  union {                                                                                       \
    /* Metadata only; pointer-sized to fit any supported type. */ \
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
}
static inline int cshl_bits_store(double_offset o, const unsigned char *src) {
  for (unsigned int i = 0; i < o.width; i++) {
    unsigned int bit = o.bit_offset + i;
    unsigned char mask = (unsigned char)(1u << (bit % 8));
    unsigned char *byte = (unsigned char *)&o.ptr[bit / 8];
    if ((src[i / 8] >> (i % 8)) & 1) *byte = (unsigned char)(*byte | mask);
    else *byte = (unsigned char)(*byte & (unsigned char)~mask);
  }
  return 0;
}

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

// Little-endian byte buffers support arbitrary widths without _BitInt.
enum cshl_int_op {
  CSHL_INT_ADD,
  CSHL_INT_SUB,
  CSHL_INT_MUL,
  CSHL_INT_DIV,
  CSHL_INT_MOD,
  CSHL_INT_AND,
  CSHL_INT_OR,
  CSHL_INT_XOR,
  CSHL_INT_SHL,
  CSHL_INT_SHR,
};

static inline void cshl_int_normalize(unsigned char *value, size_t size, size_t bits, int is_signed) {
  (void)is_signed;
  if (!size) return;
  size_t used = (bits + CHAR_BIT - 1) / CHAR_BIT;
  if (used < size) memset(value + used, 0, size - used);
  if (used < size && is_signed && (value[(bits - 1) / 8] & (1u << ((bits - 1) % CHAR_BIT)))) {
    if (bits % CHAR_BIT) value[bits / CHAR_BIT] |= (unsigned char)(0xffu << (bits % CHAR_BIT));
    if (used < size) memset(value + used, 0xff, size - used);
  } else if (used) {
    unsigned unused = (unsigned)(used * CHAR_BIT - bits);
    value[used - 1] &= (unsigned char)~((1u << unused) - 1u);
  }
}

static inline int cshl_int_negative(const unsigned char *value, size_t size, size_t bits) {
  return size && (value[(bits - 1) / 8] & (1u << ((bits - 1) % CHAR_BIT))) != 0;
}

static inline void cshl_int_negate(unsigned char *value, size_t size) {
  unsigned carry = 1;
  size_t i;
  for (i = 0; i < size; i++) {
    unsigned inverted = (unsigned char)~value[i];
    unsigned next = inverted + carry;
    carry = next > 0xffu;
    value[i] = (unsigned char)next;
  }
}

static inline int cshl_int_compare_magnitude(const unsigned char *a, const unsigned char *b, size_t size) {
  size_t i = size;
  while (i) {
    i--;
    if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
  }
  return 0;
}

static inline int cshl_int_compare(
    const unsigned char *a,
    const unsigned char *b,
    size_t size,
    size_t bits,
    int is_signed
) {
  if (!is_signed) return cshl_int_compare_magnitude(a, b, size);
  int an = cshl_int_negative(a, size, bits), bn = cshl_int_negative(b, size, bits);
  if (an != bn) return an ? -1 : 1;
  int cmp = cshl_int_compare_magnitude(a, b, size);
  return an ? -cmp : cmp;
}

static inline void cshl_int_decimal(
    unsigned char *out,
    size_t size,
    size_t bits,
    int is_signed,
    const char *text
) {
  unsigned char magnitude[sizeof(uintmax_t) * CHAR_BIT / CHAR_BIT > 64 ? 128 : 64];
  size_t magnitude_size = (size > sizeof(magnitude)) ? sizeof(magnitude) : size;
  if (magnitude_size > size) magnitude_size = size;
  memset(out, 0, size);
  memset(magnitude, 0, magnitude_size);
  int negative = *text == '-';
  int nonzero = 0;
  size_t scan;
  if (negative || *text == '+') text++;
  for (; *text; text++) {
    if (*text < '0' || *text > '9') continue;      unsigned carry = (unsigned)(*text - '0');
      size_t i;
      nonzero = 1;

    for (i = 0; i < magnitude_size; i++) {
      unsigned product = (unsigned)magnitude[i] * 10u + carry;
      magnitude[i] = (unsigned char)product;
      carry = product >> CHAR_BIT;
    }
  }
  memcpy(out, magnitude, magnitude_size);
  if (magnitude_size < size) memset(out + magnitude_size, 0, size - magnitude_size);
  cshl_int_normalize(out, size, bits, 0);
  for (scan = 0; scan < size; scan++) nonzero |= out[scan] != 0;
  if (negative && nonzero) cshl_int_negate(out, size);
}

static inline void cshl_int_from_i64(
    unsigned char *out,
    size_t size,
    size_t bits,
    int is_signed,
    int64_t input
) {
  uint64_t value = (uint64_t)input;
  memset(out, 0, size);
  size_t i;
  for (i = 0; i < size && i < sizeof(value); i++) {
    out[i] = (unsigned char)value;
    value >>= CHAR_BIT;
  }
  cshl_int_normalize(out, size, bits, is_signed);
}

static inline int64_t cshl_int_to_i64(const unsigned char *value, size_t size, size_t bits, int is_signed) {
  uint64_t result = 0;
  size_t i;
  for (i = 0; i < size && i < sizeof(result); i++) result |= (uint64_t)value[i] << (i * CHAR_BIT);
  if (is_signed && cshl_int_negative(value, size, bits)) {
    uint64_t magnitude = (~result) + 1u;
    if (magnitude == ((uint64_t)1 << 63)) return INT64_MIN;
    return -(int64_t)magnitude;
  }
  return (int64_t)result;
}

static inline void cshl_int_cast(
    unsigned char *out,
    size_t out_size,
    size_t out_bits,
    int out_signed,
    const unsigned char *in,
    size_t in_size,
    size_t in_bits,
    int in_signed
) {
  size_t copied = out_size < in_size ? out_size : in_size;
  memset(out, 0, out_size);
  memcpy(out, in, copied);
  if (out_size > copied && in_signed && cshl_int_negative(in, in_size, in_bits))
    memset(out + copied, 0xff, out_size - copied);
  cshl_int_normalize(out, out_size, out_bits, out_signed);
}

static inline void cshl_int_shift(
    unsigned char *out,
    const unsigned char *in,
    size_t size,
    size_t bits,
    int is_signed,
    int64_t amount,
    int left
) {
  int original_left = left;
  if (amount < 0) {
    amount = -amount;
    left = !left;
  }
  (void)original_left;
  memset(out, 0, size);
  if ((uint64_t)amount >= (uint64_t)bits) {
    if (!left && is_signed && cshl_int_negative(in, size, bits)) memset(out, 0xff, size);
    cshl_int_normalize(out, size, bits, is_signed);
    return;
  }
  size_t amount_bytes = (size_t)amount / CHAR_BIT;
  unsigned amount_bits = (unsigned)amount % CHAR_BIT;
  size_t i;
  if (left) {
    for (i = size; i-- > amount_bytes;) {
      unsigned value = in[i - amount_bytes] << amount_bits;
      if (amount_bits && i > amount_bytes) value |= in[i - amount_bytes - 1] >> (CHAR_BIT - amount_bits);
      out[i] = (unsigned char)value;
    }
  } else {
    for (i = 0; i + amount_bytes < size; i++) {
      unsigned value = in[i + amount_bytes] >> amount_bits;
      if (amount_bits && i + amount_bytes + 1 < size)
        value |= in[i + amount_bytes + 1] << (CHAR_BIT - amount_bits);
      out[i] = (unsigned char)value;
    }
    if (is_signed && cshl_int_negative(in, size, bits) && amount) {
      size_t fill_from = bits - (size_t)amount;
      for (i = fill_from; i < bits; i++) out[i / CHAR_BIT] |= (unsigned char)(1u << (i % CHAR_BIT));
    }
  }
  cshl_int_normalize(out, size, bits, is_signed);
}

static inline void cshl_int_binary(
    unsigned char *out,
    const unsigned char *a,
    const unsigned char *b,
    size_t size,
    size_t bits,
    int is_signed,
    enum cshl_int_op operation
) {
  size_t work_size = size ? size : 1;
  unsigned char remainder[work_size];
  size_t i;
  memset(out, 0, size);
  if (operation == CSHL_INT_AND || operation == CSHL_INT_OR || operation == CSHL_INT_XOR) {
    for (i = 0; i < size; i++) {
      if (operation == CSHL_INT_AND) out[i] = (unsigned char)(a[i] & b[i]);
      else if (operation == CSHL_INT_OR) out[i] = (unsigned char)(a[i] | b[i]);
      else out[i] = (unsigned char)(a[i] ^ b[i]);
    }
  } else if (operation == CSHL_INT_ADD || operation == CSHL_INT_SUB) {
    int is_add = operation == CSHL_INT_ADD;
    unsigned carry = 0;
    for (i = 0; i < size; i++) {
      unsigned left = (unsigned)a[i] + carry;
      unsigned right = (unsigned)b[i];
      unsigned result = is_add ? left + right : left - right;
      carry = is_add ? (result > 0xffu) : (left < right || (carry && left == right));
      out[i] = (unsigned char)result;
    }
  } else if (operation == CSHL_INT_MUL) {
    memset(remainder, 0, work_size);
    for (i = 0; i < size; i++) {
      unsigned carry = 0;
      size_t j;
      for (j = 0; i + j < work_size; j++) {
        unsigned current = (unsigned)out[i + j] + (unsigned)a[i] * (unsigned)b[j] + carry;
        out[i + j] = (unsigned char)current;
        carry = current >> CHAR_BIT;
      }
    }
  } else if (operation == CSHL_INT_DIV || operation == CSHL_INT_MOD) {
    unsigned char divisor[work_size];
    unsigned char dividend[work_size];
    int negative = is_signed && (cshl_int_negative(a, size, bits) != cshl_int_negative(b, size, bits));
    int divisor_negative = is_signed && cshl_int_negative(b, size, bits);
    memset(remainder, 0, work_size);
    memset(out, 0, size);
    memcpy(divisor, b, work_size);
    memcpy(dividend, a, work_size);
    if (divisor_negative) cshl_int_negate(divisor, work_size);
    if (is_signed && cshl_int_negative(a, size, bits)) cshl_int_negate(dividend, work_size);
    i = bits;
    while (i--) {
      size_t j;
      unsigned carry = (dividend[i / CHAR_BIT] >> (i % CHAR_BIT)) & 1u;
      for (j = 0; j < work_size; j++) {
        unsigned next_carry = remainder[j] >> (CHAR_BIT - 1);
        remainder[j] = (unsigned char)((remainder[j] << 1) | carry);
        carry = next_carry;
      }
      if (cshl_int_compare_magnitude(remainder, divisor, work_size) >= 0) {
        unsigned borrow = 0;
        for (j = 0; j < size; j++) {
          unsigned left = remainder[j];
          unsigned right = divisor[j];
          unsigned result = left - right - borrow;
          borrow = left < right || (borrow && left == right);
          remainder[j] = (unsigned char)result;
        }
        if (operation == CSHL_INT_DIV && i / CHAR_BIT < size) out[i / CHAR_BIT] |= (unsigned char)(1u << (i % CHAR_BIT));
      }
    }
    if (operation == CSHL_INT_MOD) {
      memcpy(out, remainder, size);
      if (is_signed && cshl_int_negative(a, size, bits)) cshl_int_negate(out, size);
    } else if (negative) {
      cshl_int_negate(out, size);
    }
    cshl_int_normalize(out, size, bits, is_signed);
  }
  cshl_int_normalize(out, size, bits, is_signed);
}

#define CSHL_DEFINE_INTEGER(NAME, BITS)                                                    \
  typedef struct NAME {                                                                     \
    unsigned char bytes[(BITS + CHAR_BIT - 1) / CHAR_BIT];                                  \
  } NAME;                                                                                   \
  static inline NAME NAME##_from_decimal(const char *text) {                             \
    NAME result;                                                                            \
    cshl_int_decimal(result.bytes, sizeof(result.bytes), BITS, 1, text);                   \
    return result;                                                                          \
  }                                                                                         \
  static inline NAME NAME##_from_i64(int64_t value) {                                      \
    NAME result;                                                                            \
    cshl_int_from_i64(result.bytes, sizeof(result.bytes), BITS, 1, value);                 \
    return result;                                                                          \
  }                                                                                         \
  static inline NAME NAME##_binary_value(                                                   \
      const unsigned char *a, const unsigned char *b, enum cshl_int_op operation,          \
      int is_signed) {                                                                       \
    NAME result;                                                                            \
    cshl_int_binary(result.bytes, a, b, sizeof(result.bytes), BITS, is_signed, operation);  \
    return result;                                                                          \
  }                                                                                         \
  static inline NAME NAME##_shift_value(                                                    \
      const unsigned char *a, int64_t amount, int left, int is_signed) {                    \
    NAME result;                                                                            \
    cshl_int_shift(result.bytes, a, sizeof(result.bytes), BITS, is_signed, amount, left);   \
    return result;                                                                          \
  }                                                                                         \
  static inline NAME NAME##_cast_value(                                                     \
      const unsigned char *a, size_t in_size, size_t in_bits, int in_signed,               \
      int out_signed) {                                                                      \
    NAME result;                                                                            \
    cshl_int_cast(result.bytes, sizeof(result.bytes), BITS, out_signed, a, in_size,         \
                  in_bits, in_signed);                                                      \
    return result;                                                                          \
  }

#endif // CSHL_LIB_H

/* SPDX-License-Identifier: MIT */
#ifndef P25CAI_INTERNAL_H
#define P25CAI_INTERNAL_H
#include "p25cai.h"
#include <string.h>

void cai_wire_write(uint8_t *air, size_t logical, unsigned bits, uint64_t value);
void cai_block_write(uint8_t *air, unsigned block, unsigned rate, const uint8_t *data);
int cai_block_read(const uint8_t *air, unsigned block, unsigned rate,
                    unsigned budget, uint8_t *data);

static inline unsigned cai_weight(uint64_t value)
{
    unsigned result = 0;
    for (; value; value >>= 1) result += (unsigned)(value & 1);
    return result;
}
static inline unsigned cai_bit(const uint8_t *bytes, size_t bit)
{
    return (bytes[bit / 8] >> (7 - bit % 8)) & 1;
}
static inline void cai_set(uint8_t *bytes, size_t bit, unsigned value)
{
    uint8_t mask = (uint8_t)(0x80u >> (bit % 8));
    bytes[bit / 8] = (uint8_t)((bytes[bit / 8] & ~mask) | (value ? mask : 0));
}
static inline uint64_t cai_read(const uint8_t *bytes, size_t bit, unsigned count)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < count; ++i) value = (value << 1) | cai_bit(bytes, bit + i);
    return value;
}
static inline void cai_write(uint8_t *bytes, size_t bit, unsigned count, uint64_t value)
{
    for (unsigned i = 0; i < count; ++i)
        cai_set(bytes, bit + i, (unsigned)((value >> (count - 1 - i)) & 1));
}
#endif

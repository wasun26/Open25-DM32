/* SPDX-License-Identifier: MIT
 * BAAA-B 5.2-5.4.2,6,7.2: checksums and shared trellis-block wire access. */
#include "internal.h"

static uint32_t crc_bit(uint32_t raw, unsigned bit, unsigned width, uint32_t polynomial)
{
    unsigned feedback = ((raw >> (width - 1)) & 1) ^ bit;
    raw <<= 1;
    if (feedback) raw ^= polynomial;
    return width == 32 ? raw : raw & ((UINT32_C(1) << width) - 1);
}

int p25cai_crc16(const uint8_t *data, size_t bytes, uint16_t *crc)
{
    if (!crc || (!data && bytes)) return P25CAI_INVALID;
    uint32_t raw = 0;
    for (size_t i = 0; i < bytes; ++i)
        for (unsigned bit = 0; bit < 8; ++bit)
            raw = crc_bit(raw,(data[i] >> (7-bit)) & 1,16,0x1021);
    *crc = (uint16_t)(raw ^ 0xffff);
    return 0;
}

int p25cai_crc32_update(uint32_t *raw, const uint8_t *data, size_t bytes)
{
    if (!raw || (!data && bytes)) return P25CAI_INVALID;
    uint32_t value = *raw;
    for (size_t i = 0; i < bytes; ++i)
        for (unsigned bit = 0; bit < 8; ++bit)
            value = crc_bit(value,(data[i] >> (7-bit)) & 1,32,UINT32_C(0x04c11db7));
    *raw = value;
    return 0;
}

int p25cai_crc32(const uint8_t *data, size_t bytes, uint32_t *crc)
{
    if (!crc || (!data && bytes)) return P25CAI_INVALID;
    uint32_t raw = 0;
    p25cai_crc32_update(&raw,data,bytes);
    *crc = raw ^ UINT32_C(0xffffffff);
    return 0;
}

int p25cai_crc9(uint8_t serial, const uint8_t data[16], uint16_t *crc)
{
    if (!data || !crc || serial > 127) return P25CAI_INVALID;
    uint32_t raw = 0;
    for (unsigned i = 0; i < 7; ++i)
        raw = crc_bit(raw,(serial >> (6-i)) & 1,9,0x59);
    for (unsigned i = 0; i < 128; ++i) raw = crc_bit(raw,cai_bit(data,i),9,0x59);
    *crc = (uint16_t)(raw ^ 0x1ff);
    return 0;
}

void cai_wire_write(uint8_t *air, size_t logical, unsigned bits, uint64_t value)
{
    for (unsigned i = 0; i < bits; ++i) {
        size_t position = logical+i;
        cai_set(air,position+2*(position/70),(unsigned)((value >> (bits-1-i)) & 1));
    }
}

void cai_block_write(uint8_t *air, unsigned block, unsigned rate, const uint8_t *data)
{
    uint8_t coded[25];
    p25cai_trellis_encode(data,rate,coded);
    for (unsigned i = 0; i < 196; ++i) cai_wire_write(air,112+block*196+i,1,cai_bit(coded,i));
}

int cai_block_read(const uint8_t *air, unsigned block, unsigned rate,
                              unsigned budget, uint8_t *data)
{
    uint8_t coded[25] = {0};
    for (unsigned i = 0; i < 196; ++i) {
        size_t logical = 112+block*196+i;
        cai_set(coded,i,cai_bit(air,logical+2*(logical/70)));
    }
    return p25cai_trellis_decode(coded,rate,budget,data);
}

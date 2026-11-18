/* SPDX-License-Identifier: MIT
 * TIA-102.BAAA-B 4.3, Table 1. Mathematical bit permutation, no codec. */
#include "internal.h"

/* The serialized codewords fill six rows of 24 bits. On air, traverse
 * columns, swapping adjacent rows in odd columns. This expresses the
 * mandatory Table 1 permutation without a copied interleave lookup table. */
static unsigned source_bit(unsigned air_bit)
{
    unsigned column = air_bit / 6;
    unsigned row = air_bit % 6;
    return (row ^ (column & 1)) * 24 + column;
}

int p25cai_voice_encode_serial(const uint16_t u[8], uint8_t coded[18])
{
    if (!u || !coded) return P25CAI_INVALID;
    for (unsigned i = 0; i < 8; ++i)
        if (u[i] >= (i < 4 ? 4096 : i < 7 ? 2048 : 128)) return P25CAI_INVALID;
    uint8_t serial[18] = {0};
    uint16_t random = (uint16_t)(u[0] << 4);
    unsigned offset = 0;
    for (unsigned i = 0; i < 8; ++i) {
        unsigned width = i < 4 ? 23 : i < 7 ? 15 : 7;
        uint32_t word = u[i];
        if (i < 4) p25cai_golay_encode(u[i], 23, &word);
        else if (i < 7) {
            uint16_t hamming;
            p25cai_hamming_encode(u[i], 15, &hamming);
            word = hamming;
        }
        for (unsigned bit = 0; bit < width; ++bit) {
            unsigned value = (word >> (width - 1 - bit)) & 1;
            if (i > 0 && i < 7) {
                random = (uint16_t)(173u * random + 13849u);
                value ^= random >> 15;
            }
            cai_set(serial, offset++, value);
        }
    }
    memcpy(coded, serial, sizeof serial);
    return 0;
}

int p25cai_voice_encode(const uint16_t u[8], uint8_t air[18])
{
    if (!air || !u) return P25CAI_INVALID;
    uint8_t serial[18], encoded[18] = {0};
    int result = p25cai_voice_encode_serial(u,serial);
    if (result < 0) return result;
    for (unsigned bit = 0; bit < 144; ++bit)
        cai_set(encoded,bit,cai_bit(serial,source_bit(bit)));
    memcpy(air,encoded,sizeof encoded);
    return 0;
}

int p25cai_voice_decode_serial(const uint8_t serial[18], uint16_t u[8], uint8_t *u0_errors)
{
    if (!serial || !u) return P25CAI_INVALID;
    uint16_t decoded[8], random = 0;
    unsigned offset = 0;
    int corrected = 0;
    uint8_t first_errors = 0;
    for (unsigned i = 0; i < 8; ++i) {
        unsigned width = i < 4 ? 23 : i < 7 ? 15 : 7;
        uint32_t word = 0;
        for (unsigned bit = 0; bit < width; ++bit) {
            unsigned value = cai_bit(serial, offset++);
            if (i > 0 && i < 7) {
                random = (uint16_t)(173u * random + 13849u);
                value ^= random >> 15;
            }
            word = (word << 1) | value;
        }
        int result = 0;
        if (i < 4) result = p25cai_golay_decode(word, 23, &decoded[i]);
        else if (i < 7) result = p25cai_hamming_decode((uint16_t)word, 15, &decoded[i]);
        else decoded[i] = (uint16_t)word;
        if (result < 0) return result;
        corrected += result;
        if (i == 0) {
            random = (uint16_t)(decoded[0] << 4);
            first_errors = (uint8_t)result;
        }
    }
    memcpy(u, decoded, sizeof decoded);
    if (u0_errors) *u0_errors = first_errors;
    return corrected;
}

int p25cai_voice_decode_ex(const uint8_t air[18], uint16_t u[8], uint8_t *u0_errors)
{
    if (!air || !u) return P25CAI_INVALID;
    uint8_t serial[18] = {0};
    for (unsigned bit = 0; bit < 144; ++bit)
        cai_set(serial,source_bit(bit),cai_bit(air,bit));
    return p25cai_voice_decode_serial(serial,u,u0_errors);
}

int p25cai_voice_decode(const uint8_t air[18], uint16_t u[8])
{
    return p25cai_voice_decode_ex(air,u,NULL);
}

/* SPDX-License-Identifier: MIT
 * AACE-A 6.1-6.4; FIPS 197 4,5.1,5.2. AES-128 forward operation only.
 * Byte substitution is derived from field inversion and the affine map. */
#include "internal.h"

static void erase_bytes(void *memory, size_t bytes)
{
    volatile uint8_t *p = memory;
    while (bytes--) *p++ = 0;
}

static uint8_t xtime(uint8_t value)
{
    return (uint8_t)((value << 1) ^ (0x1b & (0u-(value >> 7))));
}

static uint8_t multiply_byte(uint8_t a, uint8_t b)
{
    uint8_t product = 0;
    for (unsigned i = 0; i < 8; ++i) {
        product ^= (uint8_t)(a & (0u-(b&1)));
        a = xtime(a); b >>= 1;
    }
    return product;
}

static uint8_t rotate_byte(uint8_t value, unsigned bits)
{
    return (uint8_t)((value << bits) | (value >> (8-bits)));
}

static uint8_t substitute(uint8_t value)
{
    uint8_t power = multiply_byte(value,value), inverse = power;
    for (unsigned i = 0; i < 6; ++i) {
        power = multiply_byte(power,power);
        inverse = multiply_byte(inverse,power);
    }
    return (uint8_t)(inverse ^ rotate_byte(inverse,1) ^ rotate_byte(inverse,2) ^
        rotate_byte(inverse,3) ^ rotate_byte(inverse,4) ^ 0x63);
}

static void aes128_forward(const uint8_t key[16], const uint8_t input[16], uint8_t output[16])
{
    uint8_t expanded[176], state[16], shifted[16], word[4], rcon = 1;
    memcpy(expanded,key,16);
    for (unsigned start = 16; start < sizeof expanded; start += 4) {
        memcpy(word,expanded+start-4,4);
        if (start%16 == 0) {
            uint8_t first = word[0];
            word[0] = (uint8_t)(substitute(word[1]) ^ rcon);
            word[1] = substitute(word[2]); word[2] = substitute(word[3]); word[3] = substitute(first);
            rcon = xtime(rcon);
        }
        for (unsigned i = 0; i < 4; ++i) expanded[start+i] = expanded[start-16+i] ^ word[i];
    }
    for (unsigned i = 0; i < 16; ++i) state[i] = input[i] ^ expanded[i];
    for (unsigned round = 1; round <= 10; ++round) {
        for (unsigned column = 0; column < 4; ++column)
            for (unsigned row = 0; row < 4; ++row)
                shifted[4*column+row] = substitute(state[4*((column+row)%4)+row]);
        if (round < 10)
            for (unsigned column = 0; column < 4; ++column) {
                uint8_t *s = shifted+4*column;
                uint8_t a = s[0], b = s[1], c = s[2], d = s[3];
                s[0] = (uint8_t)(xtime(a) ^ xtime(b) ^ b ^ c ^ d);
                s[1] = (uint8_t)(a ^ xtime(b) ^ xtime(c) ^ c ^ d);
                s[2] = (uint8_t)(a ^ b ^ xtime(c) ^ xtime(d) ^ d);
                s[3] = (uint8_t)(xtime(a) ^ a ^ b ^ c ^ xtime(d));
            }
        for (unsigned i = 0; i < 16; ++i) state[i] = shifted[i] ^ expanded[16*round+i];
    }
    memcpy(output,state,16);
    erase_bytes(expanded,sizeof expanded); erase_bytes(state,sizeof state);
    erase_bytes(shifted,sizeof shifted); erase_bytes(word,sizeof word);
}

int p25cai_auth_derive(const uint8_t key[16], const uint8_t seed[10],
                      unsigned mutual, uint8_t session[16])
{
    if (!key || !seed || !session || mutual > 1) return P25CAI_INVALID;
    uint8_t block[16] = {0};
    memcpy(block,seed,10);
    if (mutual) for (unsigned i = 0; i < 16; ++i) block[i] ^= 0xff;
    aes128_forward(key,block,session);
    erase_bytes(block,sizeof block);
    return 0;
}

int p25cai_auth_response(const uint8_t session[16], const uint8_t challenge[5], uint8_t response[4])
{
    if (!session || !challenge || !response) return P25CAI_INVALID;
    uint8_t input[16] = {0}, output[16];
    memcpy(input,challenge,5);
    aes128_forward(session,input,output);
    memcpy(response,output,4);
    erase_bytes(input,sizeof input); erase_bytes(output,sizeof output);
    return 0;
}

int p25cai_auth_equal(const uint8_t expected[4], const uint8_t received[4])
{
    if (!expected || !received) return 0;
    unsigned difference = 0;
    for (unsigned i = 0; i < 4; ++i) difference |= expected[i] ^ received[i];
    return difference == 0;
}

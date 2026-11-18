/* SPDX-License-Identifier: MIT
 * TIA-102.BAAA-B 6.1-6.2 Tables 12-14, 8.2 Table 20.
 * Hard-bit Viterbi decoder is an independent implementation choice. */
#include "internal.h"

static unsigned transition(unsigned rate, unsigned state, unsigned input)
{
    /* Mandatory state-machine outputs (constellation point numbers). */
    static const uint8_t half[4][4] = {
        {0,15,12,3}, {4,11,8,7}, {13,2,1,14}, {9,6,5,10}
    };
    static const uint8_t three_quarter[8][8] = {
        {0,8,4,12,2,10,6,14}, {4,12,2,10,6,14,0,8},
        {1,9,5,13,3,11,7,15}, {5,13,3,11,7,15,1,9},
        {3,11,7,15,1,9,5,13}, {7,15,1,9,5,13,3,11},
        {2,10,6,14,0,8,4,12}, {6,14,0,8,4,12,2,10}
    };
    static const uint8_t dibit_pair[16] = {2,10,7,15,14,6,11,3,13,5,8,0,1,9,4,12};
    unsigned point = rate == 2 ? half[state][input] : three_quarter[state][input];
    return dibit_pair[point];
}

static void interleave(const uint8_t *input, uint8_t *output, int reverse)
{
    unsigned cursor = 0;
    memset(output, 0, 25);
    /* Four lanes of dibit pairs, lane zero has the final flush pair. */
    for (unsigned lane = 0; lane < 4; ++lane)
        for (unsigned pair = 2 * lane; pair < 98; pair += 8)
            for (unsigned bit = 0; bit < 4; ++bit) {
                unsigned original = 2 * pair + bit;
                if (reverse) cai_set(output, original, cai_bit(input, cursor++));
                else cai_set(output, cursor++, cai_bit(input, original));
            }
}

int p25cai_trellis_encode(const uint8_t *data, unsigned rate, uint8_t air[25])
{
    if (!data || !air || (rate != 2 && rate != 3)) return P25CAI_INVALID;
    uint8_t serial[25] = {0}, encoded[25];
    unsigned state = 0;
    for (unsigned step = 0; step < 49; ++step) {
        unsigned input = step < 48 ? (unsigned)cai_read(data, step * rate, rate) : 0;
        cai_write(serial, step * 4, 4, transition(rate, state, input));
        state = input;
    }
    interleave(serial, encoded, 0);
    memcpy(air, encoded, sizeof encoded);
    return 0;
}

int p25cai_trellis_decode(const uint8_t air[25], unsigned rate,
                         unsigned max_distance, uint8_t *data)
{
    if (!air || !data || (rate != 2 && rate != 3) || max_distance > 196)
        return P25CAI_INVALID;
    if (air[24] & 15) return P25CAI_INVALID;
    uint8_t serial[25], predecessor[49][8] = {{0}};
    interleave(air, serial, 1);
    unsigned states = 1u << rate;
    unsigned metric[8], next[8];
    uint8_t ambiguous[8] = {0}, next_ambiguous[8];
    for (unsigned state = 0; state < states; ++state) metric[state] = state ? 1000 : 0;
    for (unsigned step = 0; step < 49; ++step) {
        unsigned observed = (unsigned)cai_read(serial, step * 4, 4);
        for (unsigned state = 0; state < states; ++state) {
            next[state] = 1000;
            next_ambiguous[state] = 0;
        }
        unsigned choices = step == 48 ? 1 : states;
        for (unsigned previous = 0; previous < states; ++previous) {
            if (metric[previous] >= 1000) continue;
            for (unsigned input = 0; input < choices; ++input) {
                unsigned cost = metric[previous] + cai_weight(observed ^ transition(rate, previous, input));
                if (cost < next[input]) {
                    next[input] = cost;
                    predecessor[step][input] = (uint8_t)previous;
                    next_ambiguous[input] = ambiguous[previous];
                } else if (cost == next[input]) next_ambiguous[input] = 1;
            }
        }
        memcpy(metric, next, states * sizeof metric[0]);
        memcpy(ambiguous, next_ambiguous, states);
    }
    if (metric[0] > max_distance || ambiguous[0]) return P25CAI_UNCORRECTABLE;
    uint8_t decoded[18] = {0};
    unsigned state = 0;
    for (unsigned step = 49; step-- > 0;) {
        if (step < 48) cai_write(decoded, step * rate, rate, state);
        state = predecessor[step][state];
    }
    memcpy(data, decoded, 6 * rate);
    return (int)metric[0];
}

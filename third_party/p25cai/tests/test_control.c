/* SPDX-License-Identifier: MIT */
#include "p25cai.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); exit(1); } } while (0)
static unsigned dibit(const uint8_t *p, unsigned n) { return (p[n/4] >> (6-2*(n%4))) & 3; }

int main(void)
{
    for (unsigned mode = 0; mode < 2; ++mode)
        for (unsigned count = mode?2:1; count <= (mode?4u:3u); ++count) {
            p25cai_control input = {0}, decoded, saved;
            input.nac = 0x38b;
            input.duid = mode ? P25CAI_PDU : P25CAI_TSDU;
            input.count = (uint8_t)count;
            for (unsigned block = 0; block < count; ++block)
                for (unsigned i = 0; i < 12; ++i) input.block[block][i] = (uint8_t)(block*37+i*11);
            if (mode) { input.block[0][0] = 0x37; input.block[0][1] = 0xfd; }
            else for (unsigned i = 0; i < count; ++i) {
                input.block[i][0] = (uint8_t)i; input.block[i][1] = 0;
            }
            uint8_t air[117];
            size_t bytes = p25cai_control_size(input.duid,count);
            CHECK(bytes == (mode ? (count==2?72:count==3?90:117) : count==1?45:count==2?72:90));
            CHECK(p25cai_control_encode(&input,2,air,sizeof air) == (int)bytes);
            CHECK(p25cai_control_decode(air,bytes,input.nac,0,&decoded) == 0);
            CHECK(decoded.nac == input.nac && decoded.duid == input.duid && decoded.count == count);
            if (!mode)
                for (unsigned i = 0; i < count; ++i) {
                    CHECK((decoded.block[i][0]&0x3f) == i);
                    CHECK((decoded.block[i][0]>>7) == (i+1 == count));
                    CHECK(memcmp(decoded.block[i]+1,input.block[i]+1,9) == 0);
                }
            else {
                CHECK((decoded.block[0][6]&127) == count-1);
                CHECK(memcmp(decoded.block[1],input.block[1],8) == 0);
            }
            saved = decoded;
            CHECK(p25cai_control_decode(air,bytes,input.nac^1,0,&decoded) == P25CAI_INVALID);
            CHECK(memcmp(&saved,&decoded,sizeof saved) == 0);
            unsigned logical_end = 112+196*count;
            size_t minimum = (logical_end+2*(logical_end/70)+7)/8;
            CHECK(p25cai_control_decode(air,minimum-1,input.nac,0,&decoded) == P25CAI_LENGTH);
            CHECK(p25cai_control_decode(air,minimum,input.nac,0,&decoded) == 0);
            for (unsigned polarity = 0; polarity < 2; ++polarity) {
                p25cai_control_receiver rx;
                CHECK(p25cai_control_receiver_init(&rx,input.nac,0) == 0);
                for (unsigned i = 0; i < 31; ++i) CHECK(p25cai_control_receiver_push(&rx,(uint8_t)(i%4),&decoded) == 0);
                for (unsigned symbol = 0; symbol < bytes*4; ++symbol) {
                    int result = p25cai_control_receiver_push(&rx,(uint8_t)(dibit(air,symbol)^(polarity?2:0)),&decoded);
                    CHECK(result == (symbol+1 == bytes*4));
                }
                CHECK(decoded.count == count && memcmp(decoded.block,saved.block,sizeof saved.block) == 0);
                p25cai_control_receiver_gap(&rx);
                CHECK(rx.symbols == 0 && rx.search_symbols == 0);
                /* Actual control ISP bursts may end before null fill. */
                for (unsigned symbol = 0; symbol < minimum*4; ++symbol) {
                    int result = p25cai_control_receiver_push(&rx,(uint8_t)dibit(air,symbol),&decoded);
                    CHECK(result == (minimum == bytes && symbol+1 == minimum*4));
                }
                CHECK(p25cai_control_receiver_finish(&rx,&decoded) == (minimum < bytes));
                /* A missing coded dibit cannot be supplied by byte rounding,
                 * even when error correction could infer its value. */
                CHECK(p25cai_control_receiver_init(&rx,input.nac,16) == 0);
                unsigned coded_symbols = (logical_end+2*(logical_end/70))/2;
                for (unsigned symbol = 0; symbol+1 < coded_symbols; ++symbol)
                    CHECK(p25cai_control_receiver_push(&rx,(uint8_t)dibit(air,symbol),&decoded) == 0);
                decoded = saved;
                CHECK(p25cai_control_receiver_finish(&rx,&decoded) == 0);
                CHECK(memcmp(&decoded,&saved,sizeof saved) == 0);
                CHECK(decoded.count == count);
            }
            /* Single coded-bit damage is corrected only when budget permits. */
            air[18] ^= 0x80;
            CHECK(p25cai_control_decode(air,bytes,input.nac,0,&decoded) == P25CAI_UNCORRECTABLE);
            CHECK(p25cai_control_decode(air,bytes,input.nac,1,&decoded) == 0);
        }
    CHECK(p25cai_control_size(7,0) == 0 && p25cai_control_size(7,4) == 0);
    CHECK(p25cai_control_size(12,1) == 0 && p25cai_control_size(12,5) == 0);
    printf("%u control transport assertions passed\n",checks);
    return 0;
}

/* SPDX-License-Identifier: MIT */
#include "p25.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); exit(1); } } while (0)
static void conventional_flow(void)
{
    CHECK(sizeof(p25_nid) == 4 && sizeof(p25_call) == 12 && sizeof(p25_rx) == 264);
    CHECK(offsetof(p25_rx,frame) == 36 && offsetof(p25_rx,nid) == 252);
    CHECK(sizeof(p25_decoded) == 200);
    p25_call call = {0x293,0x1234,0x123456,1,0x87,0};
    uint8_t air[216];
    p25_decoded decoded;
    CHECK(p25_encode_hdu(&call,air) == 0);
    CHECK(p25_decode_frame(air,99,&decoded) == 0 && decoded.metadata_valid && decoded.clear_voice);
    CHECK(decoded.talkgroup == call.talkgroup && decoded.manufacturer_id == 1);
    uint16_t voice[9][8] = {{0}};
    uint8_t lsd[2] = {0x41,0x7e};
    CHECK(p25_encode_ldu(&call,5,(const uint16_t (*)[8])voice,lsd,air) == 0);
    CHECK(p25_decode_frame(air,216,&decoded) == 0 && decoded.source_id == call.source_id);
    for (unsigned i = 0; i < 9; ++i) CHECK(decoded.imbe[i][7] == (i&1));
    CHECK(memcmp(decoded.low_speed_data,lsd,2) == 0);
    p25_rx rx;
    p25_rx_init(&rx,call.nac,0);
    for (unsigned symbol = 0; symbol < 864; ++symbol) {
        uint8_t dibit = (uint8_t)((air[symbol/4] >> (6-2*(symbol%4)))&3);
        CHECK(p25_rx_push_dibit(&rx,dibit) == (symbol == 863));
    }
    CHECK(rx.nid.duid == 5 && rx.nid.nac == call.nac && rx.complete_frames == 1);
    CHECK(memcmp(rx.frame,air,216) == 0);
}

static unsigned symbol_at(const uint8_t *air, unsigned symbol)
{
    return (air[symbol/4] >> (6-2*(symbol%4))) & 3;
}

static void sync_policy(void)
{
    p25_call call = {0x293,0x1234,0x123456,1,0,0};
    uint8_t air[216], original[216];
    p25_decoded decoded;
    CHECK(p25_encode_hdu(&call,original) == 0);
    for (unsigned errors = 0; errors <= 5; ++errors) {
        memcpy(air,original,99);
        for (unsigned bit = 0; bit < errors; ++bit) air[0] ^= (uint8_t)(0x80u >> bit);
        CHECK((p25_decode_frame(air,99,&decoded) == 0) == (errors <= 4));
        p25cai_frame frame;
        p25cai_quality quality;
        CHECK((p25cai_frame_decode(air,99,&frame,&quality) == 0) == (errors <= 2));
        for (unsigned tolerance = 0; tolerance <= 4; ++tolerance)
            CHECK((p25cai_frame_decode_ex(air,99,tolerance,&frame,&quality) == 0) == (errors <= tolerance));
        memset(&frame,0x5a,sizeof frame); memset(&quality,0x5a,sizeof quality);
        p25cai_frame before = frame;
        p25cai_quality before_quality = quality;
        CHECK(p25cai_frame_decode_ex(air,99,5,&frame,&quality) == P25CAI_INVALID);
        CHECK(memcmp(&frame,&before,sizeof frame) == 0 && memcmp(&quality,&before_quality,sizeof quality) == 0);
        if (errors > 4) continue;
        for (unsigned inverted = 0; inverted < 2; ++inverted) {
            p25_rx rx;
            p25_rx_init(&rx,call.nac,4);
            for (unsigned symbol = 0; symbol < 99*4; ++symbol)
                CHECK(p25_rx_push_dibit(&rx,(uint8_t)(symbol_at(air,symbol)^(inverted?2:0))) == (symbol+1 == 99*4));
            CHECK(p25_decode_frame(rx.frame,99,&decoded) == 0 && decoded.metadata_valid);
        }
    }
}

static void invalid_dibit(void)
{
    p25_call call = {0x293,0x1234,0x123456,1,0,0};
    uint8_t air[18];
    CHECK(p25_encode_tdu(&call,air) == 0);
    p25_rx rx;
    p25_rx_init(&rx,call.nac,0);
    for (unsigned symbol = 0; symbol < 28; ++symbol)
        CHECK(p25_rx_push_dibit(&rx,(uint8_t)symbol_at(air,symbol)) == 0);
    CHECK(rx.symbols == 28);
    CHECK(p25_rx_push_dibit(&rx,4) == -1 && rx.symbols == 0);
    for (unsigned symbol = 28; symbol < 72; ++symbol)
        CHECK(p25_rx_push_dibit(&rx,(uint8_t)symbol_at(air,symbol)) == 0);
    CHECK(rx.complete_frames == 0);
    for (unsigned symbol = 0; symbol < 72; ++symbol)
        CHECK(p25_rx_push_dibit(&rx,(uint8_t)symbol_at(air,symbol)) == (symbol == 71));
    CHECK(rx.complete_frames == 1);
    p25_decoded decoded;
    CHECK(p25_decode_frame(rx.frame,18,&decoded) == 0 && decoded.metadata_valid && !decoded.clear_voice);
    uint16_t words[8] = {0}; uint8_t coded[18];
    CHECK(p25_imbe_encode(words,coded) == 0);
    CHECK(p25_imbe_decode(coded,words,NULL) == -1);
    CHECK(p25cai_voice_decode_serial(coded,words,NULL) >= 0);
}

static void decoded_metadata_policy(void)
{
    /* Existing Classic callers own admission; preserve their raw fields and
     * ALGID/service decisions while retaining stricter standalone helpers. */
    p25cai_frame input = {0};
    input.nac = 0x293; input.duid = P25CAI_HDU;
    const uint8_t header[15] = {1,2,3,4,5,6,7,8,9,1,0x80,0x12,0x34,0xab,0xcd};
    memcpy(input.metadata,header,sizeof header);
    p25cai_header strict;
    CHECK(p25cai_header_decode(header,&strict) == 0 && !p25cai_header_clear(&strict));
    uint8_t air[216]; p25_decoded decoded;
    for (unsigned manufacturer = 0; manufacturer < 256; ++manufacturer) {
        input.metadata[9] = (uint8_t)manufacturer;
        CHECK(p25cai_frame_encode(&input,air,sizeof air) == 99);
        CHECK(p25_decode_frame(air,99,&decoded) == 0 && decoded.metadata_valid && decoded.clear_voice);
        CHECK(decoded.manufacturer_id == manufacturer && decoded.algorithm_id == 0x80);
        CHECK(decoded.key_id == 0x1234 && decoded.talkgroup == 0xabcd);
        CHECK(memcmp(decoded.message_indicator,header,9) == 0);
    }
    input.duid = P25CAI_LDU2;
    memset(input.metadata,0,sizeof input.metadata);
    input.metadata[0] = 0x5a; input.metadata[9] = 0x80;
    input.metadata[10] = 0x12; input.metadata[11] = 0x34;
    CHECK(p25cai_frame_encode(&input,air,sizeof air) == 216);
    CHECK(p25_decode_frame(air,216,&decoded) == 0 && decoded.metadata_valid && decoded.clear_voice);
    CHECK(decoded.message_indicator[0] == 0x5a && decoded.key_id == 0x1234 && decoded.algorithm_id == 0x80);
    input.metadata[9] = 0x84;
    CHECK(p25cai_frame_encode(&input,air,sizeof air) == 216);
    CHECK(p25_decode_frame(air,216,&decoded) == 0 && decoded.metadata_valid && !decoded.clear_voice);
    input.duid = P25CAI_LDU1;
    const uint8_t lc[9] = {0,1,0,0x81,0xab,0xcd,0x12,0x34,0x56};
    memcpy(input.metadata,lc,sizeof lc);
    for (unsigned service = 0; service < 256; ++service) {
        input.metadata[2] = (uint8_t)service;
        CHECK(p25cai_frame_encode(&input,air,sizeof air) == 216);
        CHECK(p25_decode_frame(air,216,&decoded) == 0 && decoded.metadata_valid);
        CHECK(decoded.service_options == service && decoded.clear_voice == !(service&0x40));
        CHECK(decoded.link_control_format == 0 && decoded.manufacturer_id == 1 && decoded.algorithm_id == 0);
        CHECK(decoded.talkgroup == 0xabcd && decoded.source_id == 0x123456);
    }
    input.metadata[0] = 3;
    CHECK(p25cai_frame_encode(&input,air,sizeof air) == 216);
    CHECK(p25_decode_frame(air,216,&decoded) == 0 && !decoded.metadata_valid);
    CHECK(decoded.link_control_format == 3 && decoded.manufacturer_id == 1 && decoded.service_options == 255);
    CHECK(!decoded.clear_voice && !decoded.talkgroup && !decoded.source_id);
    input.metadata[0] = 0; input.metadata[1] = 2;
    CHECK(p25cai_frame_encode(&input,air,sizeof air) == 216);
    CHECK(p25_decode_frame(air,216,&decoded) == 0 && !decoded.metadata_valid);
    CHECK(decoded.manufacturer_id == 2 && decoded.service_options == 255 && !decoded.source_id);
}

int main(void)
{
    conventional_flow(); sync_policy(); invalid_dibit(); decoded_metadata_policy();
    puts("Open25 interface, ABI, acquisition and caller policies passed");
    return 0;
}

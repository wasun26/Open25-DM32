/* SPDX-License-Identifier: MIT */
#include "p25cai.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); exit(1); } } while (0)
static unsigned bit(const uint8_t *p, unsigned n) { return (p[n/8] >> (7-n%8)) & 1; }
static void set(uint8_t *p, unsigned n, unsigned value)
{
    unsigned mask = 0x80u >> (n%8);
    p[n/8] = (uint8_t)((p[n/8] & ~mask) | (value ? mask : 0));
}

static void block_read(const uint8_t *air, unsigned index, unsigned rate, uint8_t *data)
{
    uint8_t coded[25] = {0};
    for (unsigned i = 0; i < 196; ++i) {
        unsigned pos = 112+196*index+i;
        set(coded,i,bit(air,pos+2*(pos/70)));
    }
    CHECK(p25cai_trellis_decode(coded,rate,0,data) == 0);
}

static void block_write(uint8_t *air, unsigned index, unsigned rate, const uint8_t *data)
{
    uint8_t coded[25];
    CHECK(p25cai_trellis_encode(data,rate,coded) == 0);
    for (unsigned i = 0; i < 196; ++i) {
        unsigned pos = 112+196*index+i;
        set(air,pos+2*(pos/70),bit(coded,i));
    }
}

static void fields(void)
{
    p25cai_link_control lc = {0}, decoded;
    lc.source = 0x123456; lc.group = 0xabcd; lc.manufacturer = 1; lc.service = 0x87;
    uint8_t bytes[15];
    static const uint8_t group[9] = {0,1,0x87,0,0xab,0xcd,0x12,0x34,0x56};
    CHECK(p25cai_lc_encode(&lc,bytes) == 0 && memcmp(bytes,group,9) == 0);
    CHECK(p25cai_lc_decode(bytes,&decoded) == 0);
    CHECK(decoded.source == lc.source && decoded.group == lc.group && decoded.service == 0x87);
    lc.format = 3; lc.target = 0x987654;
    static const uint8_t individual[9] = {3,1,0x87,0x98,0x76,0x54,0x12,0x34,0x56};
    CHECK(p25cai_lc_encode(&lc,bytes) == 0 && memcmp(bytes,individual,9) == 0);
    CHECK(p25cai_lc_decode(bytes,&decoded) == 0 && decoded.target == lc.target);
    bytes[0] |= 0x80;
    CHECK(p25cai_lc_decode(bytes,&decoded) == P25CAI_UNSUPPORTED);
    p25cai_header h = {0}, d;
    h.algorithm = 0x80; h.group = 0x1234;
    CHECK(p25cai_header_clear(&h));
    CHECK(p25cai_header_encode(&h,bytes) == 0);
    CHECK(bytes[10] == 0x80 && bytes[13] == 0x12 && bytes[14] == 0x34);
    CHECK(p25cai_header_decode(bytes,&d) == 0 && p25cai_header_clear(&d));
    d.indicator[8] = 1; CHECK(!p25cai_header_clear(&d));
    d.indicator[8] = 0; d.key = 1; CHECK(!p25cai_header_clear(&d));
    d.key = 0; d.algorithm = 0x84; CHECK(!p25cai_header_clear(&d));
}

static void crcs(void)
{
    uint8_t zero[16] = {0};
    uint16_t crc16, crc9;
    uint32_t crc32, raw = 0;
    CHECK(p25cai_crc16(zero,10,&crc16) == 0 && crc16 == 0xffff);
    CHECK(p25cai_crc32(NULL,0,&crc32) == 0 && crc32 == UINT32_C(0xffffffff));
    CHECK(p25cai_crc9(0,zero,&crc9) == 0 && crc9 == 0x1ff);
    /* Independent common polynomial checks for the chosen init/xor conventions. */
    const uint8_t text[] = "123456789";
    CHECK(p25cai_crc16(text,9,&crc16) == 0 && crc16 == 0xce3c);
    CHECK(p25cai_crc32(text,9,&crc32) == 0 && crc32 == UINT32_C(0x765e7680));
    CHECK(p25cai_crc32_update(&raw,text,4) == 0);
    CHECK(p25cai_crc32_update(&raw,text+4,5) == 0 && ~raw == crc32);
    raw = 0x12345678;
    CHECK(p25cai_crc32_update(&raw,NULL,1) == P25CAI_INVALID && raw == 0x12345678);
    CHECK(p25cai_crc9(128,zero,&crc9) == P25CAI_INVALID);
}

static void packets(void)
{
    const unsigned lengths[] = {0,1,8,9,11,12,13,15,16,17,20,31,63,64,65,211,212,213,1519,1520,2027,2028};
    uint8_t data[2028], decoded[2028], air[3240], damaged[3240], saved[2028];
    for (unsigned i = 0; i < sizeof data; ++i) data[i] = (uint8_t)(i*67+i/3);
    for (unsigned confirmed = 0; confirmed <= 1; ++confirmed)
        for (unsigned index = 0; index < sizeof lengths/sizeof lengths[0]; ++index) {
            unsigned length = lengths[index];
            if (length > (confirmed ? 2028u : 1520u)) continue;
            p25cai_packet packet = {0}, result;
            packet.nac = 0x713; packet.link_id = 0x2468ac; packet.sap = 4;
            packet.confirmed = (uint8_t)confirmed; packet.fragment = 8;
            packet.outbound = (uint8_t)(index%2); packet.sequence = (uint8_t)(index%8);
            packet.offset = length > 2 ? 2 : 0;
            size_t size = p25cai_packet_size(length,confirmed);
            CHECK(size >= 72 && size <= sizeof air && size%9 == 0);
            CHECK(p25cai_packet_encode(&packet,2,data,length,air,sizeof air) == (int)size);
            CHECK(p25cai_packet_decode(air,size,packet.nac,0,&result,decoded,sizeof decoded) == (int)length);
            CHECK(memcmp(decoded,data,length) == 0);
            CHECK(result.link_id == packet.link_id && result.sap == 4 && result.offset == packet.offset);
            CHECK(result.confirmed == confirmed && result.outbound == packet.outbound);
            if (confirmed) CHECK(result.sequence == packet.sequence && result.fragment == 8);
            CHECK(p25cai_packet_decode(air,size,packet.nac^1,0,&result,decoded,sizeof decoded) == P25CAI_INVALID);
            CHECK(p25cai_packet_decode(air,size-1,packet.nac,0,&result,decoded,sizeof decoded) == P25CAI_LENGTH);
            if (length) CHECK(p25cai_packet_decode(air,size,packet.nac,0,&result,decoded,length-1) == P25CAI_LENGTH);
            uint8_t header[12]; block_read(air,0,2,header);
            CHECK(header[0] == (unsigned)((confirmed?0x56:0x15)|(packet.outbound<<5)));
            unsigned block_count = (length+4+(confirmed?15:11))/(confirmed?16:12);
            CHECK(header[6] == (0x80 | block_count));
            for (unsigned position = 70; position < size*8; position += 72)
                CHECK(bit(air,position) == 1 && bit(air,position+1) == 0);
            /* Preserve both outputs on packet CRC failure after valid block FEC. */
            memcpy(damaged,air,size);
            uint8_t content[18]; block_read(damaged,1,confirmed?3:2,content);
            content[confirmed?2:0] ^= 0x80;
            if (confirmed) {
                uint16_t crc; p25cai_crc9(0,content+2,&crc);
                content[0] = (uint8_t)(crc >> 8); content[1] = (uint8_t)crc;
            }
            block_write(damaged,1,confirmed?3:2,content);
            memset(decoded,0x5a,sizeof decoded); memcpy(saved,decoded,sizeof decoded);
            p25cai_packet before; memset(&result,0xa5,sizeof result); memcpy(&before,&result,sizeof result);
            CHECK(p25cai_packet_decode(damaged,size,P25CAI_ANY_NAC,0,&result,decoded,sizeof decoded) == P25CAI_UNCORRECTABLE);
            CHECK(memcmp(decoded,saved,sizeof decoded) == 0 && memcmp(&result,&before,sizeof result) == 0);
        }
    CHECK(p25cai_packet_size(1521,0) == 0 && p25cai_packet_size(2029,1) == 0);
    p25cai_packet p = {0}; p.sap = 31;
    CHECK(p25cai_packet_encode(&p,0,data,1,air,sizeof air) == P25CAI_INVALID);
    for (unsigned size = 0; size < 72; ++size)
        CHECK(p25cai_packet_decode(air,size,P25CAI_ANY_NAC,0,&p,decoded,sizeof decoded) == P25CAI_LENGTH);
}

int main(void)
{
    fields(); crcs(); packets();
    printf("%u packet/field assertions passed\n",checks);
    return 0;
}

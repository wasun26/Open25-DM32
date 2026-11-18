/* SPDX-License-Identifier: MIT */
#include "p25cai.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); exit(1); } } while (0)
static void put(uint8_t *p, unsigned start, unsigned count, uint32_t value)
{
    for (unsigned i = 0; i < count; ++i) {
        unsigned pos = start+i, mask = 0x80u >> (pos%8);
        p[pos/8] = (uint8_t)((p[pos/8]&~mask)|(((value>>(count-1-i))&1)?mask:0));
    }
}
static void checksum(uint8_t *block)
{
    uint16_t crc;
    CHECK(p25cai_crc16(block,10,&crc) == 0);
    put(block,80,16,crc);
}
static void plans_and_grants(void)
{
    uint8_t block[12] = {0xb4,0,0x35};
    put(block,24,14,0x2000+360); put(block,38,10,100); put(block,48,32,90000000);
    checksum(block);
    p25cai_cc_message message;
    CHECK(p25cai_cc_decode(block,&message) == 0);
    CHECK(message.identifier == 3 && message.plan.base_hz == 450000000);
    CHECK(message.plan.spacing_hz == 12500 && message.plan.offset_hz == 4500000);
    CHECK(message.plan.bandwidth_hz == 12500 && !message.plan.explicit_tx);
    p25cai_bandplan plans[16] = {{0}};
    plans[3] = message.plan;
    CHECK(p25cai_channel_hz(plans,0x3002,0) == 450025000);
    CHECK(p25cai_channel_hz(plans,0x3002,1) == 454525000);
    put(block,24,14,360); checksum(block);
    CHECK(p25cai_cc_decode(block,&message) == 0 && message.plan.offset_hz == -4500000);
    put(block,24,14,0x2000); checksum(block);
    CHECK(p25cai_cc_decode(block,&message) == 0 && message.plan.explicit_tx);
    plans[3] = message.plan;
    CHECK(p25cai_channel_hz(plans,0x3002,1) == 0);
    block[2] = 0x36; checksum(block);
    CHECK(p25cai_cc_decode(block,&message) == P25CAI_UNSUPPORTED);
    memset(block,0,sizeof block); block[0] = 0xbd;
    put(block,16,4,4); put(block,20,9,100); put(block,29,9,180);
    put(block,38,10,100); put(block,48,32,170200000); checksum(block);
    CHECK(p25cai_cc_decode(block,&message) == 0);
    CHECK(message.identifier == 4 && message.plan.base_hz == 851000000 && message.plan.offset_hz == -45000000);
    plans[4] = message.plan;
    CHECK(p25cai_channel_hz(plans,0x4001,1) == 806012500);
    memset(block,0,sizeof block); block[0] = 0x80; block[2] = 0x87;
    put(block,24,16,0x3002); put(block,40,16,0x2468); put(block,56,24,0x123456); checksum(block);
    CHECK(p25cai_cc_decode(block,&message) == 0 && message.opcode == 0 && message.count == 1);
    CHECK(message.group[0] == 0x2468 && message.channel[0] == 0x3002 && message.source == 0x123456 && message.service == 0x87);
    block[0] = 0x84; put(block,16,16,0x3456); put(block,32,24,0x654321); checksum(block);
    CHECK(p25cai_cc_decode(block,&message) == 0 && message.target == 0x654321 && message.channel[0] == 0x3456);
    block[0] = 0x86; checksum(block); CHECK(p25cai_cc_decode(block,&message) == 0);
    memset(block,0,sizeof block); block[0] = 0x82;
    put(block,16,16,0x3001); put(block,32,16,10); put(block,48,16,0x4002); put(block,64,16,20); checksum(block);
    CHECK(p25cai_cc_decode(block,&message) == 0 && message.count == 2);
    CHECK(message.group[0] == 10 && message.group[1] == 20 && message.channel[1] == 0x4002);
    block[0] = 0x83; block[2] = 0x80; put(block,32,16,0x3001); put(block,48,16,0xffff); put(block,64,16,20); checksum(block);
    CHECK(p25cai_cc_decode(block,&message) == 0 && !message.explicit_channels);
    put(block,48,16,0x4002); checksum(block);
    CHECK(p25cai_cc_decode(block,&message) == 0 && message.explicit_channels && message.uplink == 0x4002);
    memset(block,0,sizeof block); block[0] = 0xbb; block[2] = 7;
    put(block,24,20,0xabcde); put(block,44,12,0x123); put(block,56,16,0x3002); checksum(block);
    CHECK(p25cai_cc_decode(block,&message) == 0 && message.network == 0xabcde && message.system == 0x123 && message.lra == 7);
    memset(block,0,sizeof block); block[0] = 0xba; block[2] = 7; block[3] = 0x31; block[4] = 0x23;
    block[5] = 9; block[6] = 10; put(block,56,16,0x3002); checksum(block);
    CHECK(p25cai_cc_decode(block,&message) == 0 && message.system == 0x123 && message.rfss == 9 && message.site == 10);
    p25cai_cc_message before = message; block[8] ^= 1;
    CHECK(p25cai_cc_decode(block,&message) == P25CAI_UNCORRECTABLE && memcmp(&before,&message,sizeof before) == 0);
}

static void signaling(void)
{
    static const uint8_t isp[] = {0,4,5,0x18,0x19,0x1a,0x1c,0x1f,0x20,0x23,0x27,0x28,0x2b,0x2c,0x2d,0x38};
    static const uint8_t osp[] = {5,0x18,0x1a,0x1c,0x1f,0x20,0x21,0x27,0x28,0x2b,0x2c,0x2d,0x32};
    for (unsigned direction = 0; direction < 2; ++direction)
        for (unsigned i = 0; i < (direction?sizeof osp:sizeof isp); ++i) {
            p25cai_signal signal = {0}, decoded;
            signal.outbound = (uint8_t)direction; signal.opcode = direction?osp[i]:isp[i];
            signal.source = 0x123456; signal.target = 0x654321; signal.group = 0x2468;
            signal.value = signal.opcode == 5 ? 0x20 : 0xbeef;
            signal.service = 0x27; signal.network = 0xabcde; signal.system = 0x789;
            signal.capabilities = 0x83; signal.rfss = 7; signal.site = 9; signal.lra = 11; signal.result = 2;
            memcpy(signal.response,"ABCD",4);
            if (signal.opcode == 0x38) signal.flags = 1;
            if ((direction && (signal.opcode == 0x20 || signal.opcode == 0x21 || signal.opcode == 0x27)) ||
                (!direction && signal.opcode == 0x23)) { signal.flags = 0x80; signal.additional = 0x808182; }
            uint8_t block[12], again[12];
            CHECK(p25cai_signal_encode(&signal,block) == 0);
            CHECK(p25cai_signal_decode(block,direction,&decoded) == 0);
            CHECK(p25cai_signal_encode(&decoded,again) == 0 && memcmp(block,again,12) == 0);
            if (signal.opcode == 0x1c || signal.opcode == 0x18 || signal.opcode == 0x19) {
                const uint8_t expected[10] = {(uint8_t)(0x80|signal.opcode),0,0xbe,0xef,0x65,0x43,0x21,0x12,0x34,0x56};
                CHECK(memcmp(block,expected,10) == 0);
            }
            if (!direction && signal.opcode == 0x2c) {
                const uint8_t expected[10] = {0xac,0,0x83,0xab,0xcd,0xe7,0x89,0x12,0x34,0x56};
                CHECK(memcmp(block,expected,10) == 0);
            }
            if (signal.opcode == 0x27) {
                if (direction) CHECK(decoded.target == signal.target && decoded.additional == signal.additional && !decoded.source);
                else CHECK(decoded.source == signal.source && decoded.group == signal.group && !decoded.target);
            }
            p25cai_signal before = decoded;
            block[7] ^= 1;
            CHECK(p25cai_signal_decode(block,direction,&decoded) == P25CAI_UNCORRECTABLE && memcmp(&before,&decoded,sizeof before) == 0);
        }
    p25cai_signal signal = {0}; uint8_t block[12];
    signal.opcode = 0x20; signal.outbound = 1; signal.additional = 1;
    CHECK(p25cai_signal_encode(&signal,block) == P25CAI_INVALID);
    signal.additional = 0; signal.opcode = 0x3f;
    CHECK(p25cai_signal_encode(&signal,block) == P25CAI_UNSUPPORTED);
}

static void authentication_format(void)
{
    p25cai_control input = {0}, decoded;
    input.nac = 0x293; input.duid = 12; input.count = 3;
    uint8_t *h = input.block[0], *a = input.block[1], *b = input.block[2];
    h[0] = 0x37; h[1] = 0xfd; h[7] = 0x31;
    put(h,24,24,0x123456); h[8] = 0xab; h[9] = 0xcd;
    a[0] = 0xe7; a[1] = 0x89; put(a,16,24,0x654321);
    for (unsigned i = 0; i < 7; ++i) a[5+i] = (uint8_t)i;
    b[0] = 7; b[1] = 8; b[2] = 9;
    memcpy(b+3,"HELLO",5);
    uint8_t air[117];
    CHECK(p25cai_control_encode(&input,2,air,sizeof air) == 90);
    CHECK(p25cai_control_decode(air,90,0x293,0,&decoded) == 0);
    p25cai_auth_demand demand;
    CHECK(p25cai_auth_demand_decode(&decoded,&demand) == 0);
    CHECK(demand.target_address == 0x123456 && demand.network == 0xabcde && demand.system == 0x789 && demand.unit == 0x654321);
    for (unsigned i = 0; i < 10; ++i) CHECK(demand.seed[i] == i);
    CHECK(memcmp(demand.challenge,"HELLO",5) == 0);
    p25cai_auth_demand before = demand; decoded.block[1][5] ^= 1;
    CHECK(p25cai_auth_demand_decode(&decoded,&demand) == P25CAI_UNCORRECTABLE && memcmp(&before,&demand,sizeof demand) == 0);
}
int main(void)
{
    plans_and_grants(); signaling(); authentication_format();
    printf("%u control message assertions passed\n",checks);
    return 0;
}

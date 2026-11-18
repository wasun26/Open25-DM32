/* SPDX-License-Identifier: MIT
 * Independent expected values: BAAA-B Tables 2-4,7-9,19; SOURCES.md.
 * Generated payloads are synthetic, not copied conformance vectors. */
#include "p25cai.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { ++checks; if (!(expr)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); } } while (0)
static unsigned long checks;
static uint32_t random_state = 0xc4f025;
static uint32_t random_value(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static unsigned bit(const uint8_t *p, unsigned n) { return (p[n/8] >> (7-n%8)) & 1; }
static void flip(uint8_t *p, unsigned n) { p[n/8] ^= (uint8_t)(0x80u >> (n%8)); }
static unsigned air_position(unsigned logical) { return logical + 2 * (logical / 70); }

static void binary_codes(void)
{
    /* Table 3, ordered least-significant input bit first. */
    static const unsigned golay_parity[12] = {
        04353,04476,05227,06706,01547,03315,06631,01732,03664,07550,03073,06165
    };
    for (unsigned data = 0; data < 4096; ++data) {
        unsigned parity = 0;
        for (unsigned i = 0; i < 12; ++i) if ((data >> i) & 1) parity ^= golay_parity[i];
        uint32_t encoded;
        uint16_t decoded;
        CHECK(p25cai_golay_encode((uint16_t)data, 24, &encoded) == 0);
        CHECK(encoded == ((data << 12) | parity));
        CHECK(p25cai_golay_decode(encoded, 24, &decoded) == 0 && decoded == data);
        CHECK(p25cai_golay_encode((uint16_t)data, 23, &encoded) == 0);
        for (unsigned b = 0; b < 23; ++b)
            CHECK(p25cai_golay_decode(encoded ^ (1u << b), 23, &decoded) == 1 && decoded == data);
    }
    /* Every error mask of weight <=3, on both a full and shortened word. */
    for (unsigned bits = 18; bits <= 24; ++bits) {
        if (bits != 18 && bits != 23 && bits != 24) continue;
        uint16_t data = bits == 18 ? 0x2d : 0x9a3, decoded;
        uint32_t encoded;
        CHECK(p25cai_golay_encode(data, bits, &encoded) == 0);
        for (unsigned a = 0; a < bits; ++a) {
            CHECK(p25cai_golay_decode(encoded ^ (1u << a), bits, &decoded) == 1 && decoded == data);
            for (unsigned b = a+1; b < bits; ++b) {
                CHECK(p25cai_golay_decode(encoded ^ (1u << a) ^ (1u << b), bits, &decoded) == 2 && decoded == data);
                for (unsigned c = b+1; c < bits; ++c)
                    CHECK(p25cai_golay_decode(encoded ^ (1u << a) ^ (1u << b) ^ (1u << c), bits, &decoded) == 3 && decoded == data);
            }
        }
    }
    static const unsigned hamming_parity[11] = {3,5,6,7,9,10,11,12,13,14,15};
    static const unsigned short_parity[6] = {12,3,7,11,13,14};
    for (unsigned bits = 10; bits <= 15; bits += 5)
        for (unsigned data = 0; data < (1u << (bits-4)); ++data) {
            unsigned expected = data << 4;
            for (unsigned i = 0; i < bits-4; ++i)
                if ((data >> i) & 1) expected ^= bits == 10 ? short_parity[i] : hamming_parity[i];
            uint16_t encoded, decoded;
            CHECK(p25cai_hamming_encode((uint16_t)data, bits, &encoded) == 0 && encoded == expected);
            for (unsigned b = 0; b < bits; ++b)
                CHECK(p25cai_hamming_decode(encoded ^ (1u << b), bits, &decoded) == 1 && decoded == data);
        }
    /* Table 2's explicit ASCII A example. */
    uint16_t encoded;
    CHECK(p25cai_lsd_encode(0x41, &encoded) == 0 && encoded == 0x411e);
    for (unsigned data = 0; data < 256; ++data) {
        uint8_t decoded;
        p25cai_lsd_encode((uint8_t)data, &encoded);
        CHECK(p25cai_lsd_decode(encoded, &decoded) == 0 && decoded == data);
        for (unsigned a = 0; a < 16; ++a)
            for (unsigned b = a+1; b < 16; ++b)
                CHECK(p25cai_lsd_decode(encoded ^ (1u << a) ^ (1u << b), &decoded) == 2 && decoded == data);
    }
}

/* Bit-serial field multiplication is intentionally independent of the
 * production logarithm tables. Used to verify all codeword check equations. */
static unsigned gf_multiply(unsigned a, unsigned b)
{
    unsigned result = 0;
    while (b) {
        if (b & 1) result ^= a;
        b >>= 1;
        a <<= 1;
        if (a & 64) a ^= 0x43;
    }
    return result;
}

static void reed_solomon(void)
{
    static const unsigned ns[3] = {36,24,24}, ks[3] = {20,12,16};
    /* First rows of Tables 9,7,8: synthetic single-symbol basis messages. */
    static const uint8_t expected[3][16] = {
        {074,037,034,006,002,007,044,064,026,014,026,044,054,013,077,005},
        {062,044,003,025,014,016,027,003,053,004,036,047},
        {051,045,067,015,064,067,052,012}
    };
    for (unsigned mode = 0; mode < 3; ++mode) {
        unsigned n = ns[mode], k = ks[mode], parity = n-k;
        uint8_t data[20] = {1}, encoded[36], damaged[36];
        CHECK(p25cai_rs_encode(data, n, k, encoded) == 0);
        CHECK(memcmp(encoded+k, expected[mode], parity) == 0);
        for (unsigned trial = 0; trial < 500; ++trial) {
            for (unsigned i = 0; i < k; ++i) data[i] = (uint8_t)(random_value() & 63);
            CHECK(p25cai_rs_encode(data, n, k, encoded) == 0);
            unsigned root = 1;
            for (unsigned r = 1; r <= parity; ++r) {
                root = gf_multiply(root, 2);
                unsigned syndrome = 0;
                for (unsigned i = 0; i < n; ++i) syndrome = gf_multiply(syndrome, root) ^ encoded[i];
                CHECK(syndrome == 0);
            }
            for (unsigned errors = 0; errors <= parity/2; ++errors) {
                memcpy(damaged, encoded, n);
                /* Distinct cyclic positions including parity symbols. */
                unsigned start = random_value() % n;
                for (unsigned i = 0; i < errors; ++i)
                    damaged[(start+i)%n] ^= (uint8_t)(1 + random_value()%63);
                CHECK(p25cai_rs_decode(damaged, n, k) == (int)errors);
                CHECK(memcmp(damaged, encoded, n) == 0);
            }
        }
        /* Search deterministic beyond-radius samples for a rejected word,
         * then verify rejection does not partially modify the input. */
        int rejected = 0;
        for (unsigned attempt = 0; attempt < 100 && !rejected; ++attempt) {
            for (unsigned i = 0; i < n; ++i) damaged[i] = (uint8_t)(random_value() & 63);
            uint8_t before[36];
            memcpy(before, damaged, n);
            if (p25cai_rs_decode(damaged,n,k) == P25CAI_UNCORRECTABLE) {
                CHECK(memcmp(before,damaged,n) == 0);
                rejected = 1;
            }
        }
        CHECK(rejected);
    }
}

/* Existing Next public helpers admit arbitrary shortened words, including
 * n=63. Verify roots with direct polynomial arithmetic, not codec tables. */
static void reed_solomon_shortening(void)
{
    for(unsigned parity=8;parity<=16;parity+=4)
        for(unsigned n=parity+1;n<=63;n++) {
            unsigned k=n-parity;
            uint8_t data[63],encoded[63],damaged[63];
            for(unsigned i=0;i<k;i++)data[i]=(uint8_t)(random_value()&63);
            CHECK(p25cai_rs_encode(data,n,k,encoded)==0);
            unsigned root=1;
            for(unsigned r=0;r<parity;r++) {
                root=gf_multiply(root,2);unsigned value=0;
                for(unsigned i=0;i<n;i++)value=gf_multiply(value,root)^encoded[i];
                CHECK(value==0);
            }
            for(unsigned errors=0;errors<=parity/2;errors++) {
                memcpy(damaged,encoded,n);
                for(unsigned i=0;i<errors;i++)damaged[n-1-i]^=(uint8_t)(i+1);
                CHECK(p25cai_rs_decode(damaged,n,k)==(int)errors);
                CHECK(!memcmp(damaged,encoded,n));
            }
        }
    uint8_t data[64]={0},before[64];memcpy(before,data,sizeof data);
    CHECK(p25cai_rs_encode(data,64,56,data)==P25CAI_INVALID);
    CHECK(p25cai_rs_decode(data,64,56)==P25CAI_INVALID);
    CHECK(!memcmp(data,before,sizeof data));
}

static void network_id(void)
{
    /* Table 19 parity rows, MSB-first input. No production polynomial here. */
    static const uint64_t parity[16] = {
        UINT64_C(06331141367235452), UINT64_C(05265521614723276),
        UINT64_C(04603711461164164), UINT64_C(02301744630472072),
        UINT64_C(07271623073000466), UINT64_C(05605650752635660),
        UINT64_C(02702724365316730), UINT64_C(01341352172547354),
        UINT64_C(00560565075263566), UINT64_C(06141333751704220),
        UINT64_C(03060555764742110), UINT64_C(01430266772361044),
        UINT64_C(00614133375170422), UINT64_C(06037114611641642),
        UINT64_C(05326507063515373), UINT64_C(04662302756473127)
    };
    static const uint8_t types[6] = {0,3,5,10,12,15};
    for (unsigned nac = 0; nac < 4096; ++nac)
        for (unsigned type = 0; type < 6; ++type) {
            uint16_t information = (uint16_t)((nac << 4) | types[type]);
            uint64_t expected = (uint64_t)information << 48, encoded;
            for (unsigned row = 0; row < 16; ++row)
                if (information & (0x8000u >> row)) expected ^= parity[row];
            CHECK(p25cai_nid_encode((uint16_t)nac,types[type],&encoded) == 0 && encoded == expected);
            uint16_t decoded_nac;
            uint8_t decoded_type;
            CHECK(p25cai_nid_decode(encoded,&decoded_nac,&decoded_type) == 0);
            CHECK(decoded_nac == nac && decoded_type == types[type]);
        }
    for (unsigned trial = 0; trial < 500; ++trial) {
        uint16_t nac = (uint16_t)(random_value() & 4095), decoded_nac;
        uint8_t type = types[trial%6], decoded_type;
        uint64_t encoded;
        p25cai_nid_encode(nac,type,&encoded);
        for (unsigned errors = 1; errors <= 11; ++errors) {
            uint64_t damaged = encoded;
            unsigned start = random_value()%64;
            for (unsigned i = 0; i < errors; ++i) damaged ^= UINT64_C(1) << ((start+7*i)%64);
            CHECK(p25cai_nid_decode(damaged,&decoded_nac,&decoded_type) == (int)errors);
            CHECK(decoded_nac == nac && decoded_type == type);
        }
    }
}

/* Literal Table 1 relationships are independent of the production grid
 * formula. Each entry is {codeword, bit index, codeword, bit index}. */
static const uint8_t voice_schedule[72][4] = {
    {0,22,1,21},{2,20,3,19},{4,10,5,1},{1,20,0,21},{3,18,2,19},{5,0,4,9},
    {0,20,1,19},{2,18,3,17},{4,8,6,14},{1,18,0,19},{3,16,2,17},{6,13,4,7},
    {0,18,1,17},{2,16,3,15},{4,6,6,12},{1,16,0,17},{3,14,2,15},{6,11,4,5},
    {0,16,1,15},{2,14,3,13},{4,4,6,10},{1,14,0,15},{3,12,2,13},{6,9,4,3},
    {0,14,1,13},{2,12,3,11},{4,2,6,8},{1,12,0,13},{3,10,2,11},{6,7,4,1},
    {0,12,1,11},{2,10,3,9},{4,0,6,6},{1,10,0,11},{3,8,2,9},{6,5,5,14},
    {0,10,1,9},{2,8,3,7},{5,13,6,4},{1,8,0,9},{3,6,2,7},{6,3,5,12},
    {0,8,1,7},{2,6,3,5},{5,11,6,2},{1,6,0,7},{3,4,2,5},{6,1,5,10},
    {0,6,1,5},{2,4,3,3},{5,9,6,0},{1,4,0,5},{3,2,2,3},{7,6,5,8},
    {0,4,1,3},{2,2,3,1},{5,7,7,5},{1,2,0,3},{3,0,2,1},{7,4,5,6},
    {0,2,1,1},{2,0,4,14},{5,5,7,3},{1,0,0,1},{4,13,3,22},{7,2,5,4},
    {0,0,2,22},{3,21,4,12},{5,3,7,1},{2,21,1,22},{4,11,3,20},{7,0,5,2}
};

static void voice_words(void)
{
    for (unsigned trial = 0; trial < 1000; ++trial) {
        uint16_t input[8], decoded[8];
        uint32_t words[8];
        for (unsigned i = 0; i < 8; ++i) {
            input[i] = (uint16_t)(random_value() & (i<4?4095:i<7?2047:127));
            words[i] = input[i];
            if (i < 4) CHECK(p25cai_golay_encode(input[i],23,&words[i]) == 0);
            else if (i < 7) {
                uint16_t encoded;
                CHECK(p25cai_hamming_encode(input[i],15,&encoded) == 0);
                words[i] = encoded;
            }
        }
        unsigned state = input[0] * 16;
        for (unsigned i = 1; i < 7; ++i) {
            unsigned mask = 0, width = i<4?23:15;
            for (unsigned j = 0; j < width; ++j) {
                state = (173 * state + 13849) % 65536;
                mask = (mask << 1) | (state / 32768);
            }
            words[i] ^= mask;
        }
        uint8_t air[18];
        CHECK(p25cai_voice_encode(input,air) == 0);
        for (unsigned dibit = 0; dibit < 72; ++dibit)
            for (unsigned b = 0; b < 2; ++b) {
                unsigned code = voice_schedule[dibit][b*2];
                unsigned position = voice_schedule[dibit][b*2+1];
                CHECK(bit(air,dibit*2+b) == ((words[code] >> position)&1));
            }
        CHECK(p25cai_voice_decode(air,decoded) == 0 && memcmp(input,decoded,sizeof input) == 0);
    }
}

static void frames_and_stream(void)
{
    static const unsigned types[5] = {0,3,5,10,15};
    for (unsigned trial = 0; trial < 300; ++trial) {
        p25cai_frame input = {0}, decoded;
        p25cai_quality quality;
        input.nac = (uint16_t)(random_value()&4095);
        input.duid = (uint8_t)types[trial%5];
        input.status = (uint8_t)(trial%4);
        for (unsigned i = 0; i < 15; ++i) input.metadata[i] = (uint8_t)random_value();
        for (unsigned i = 0; i < 9; ++i)
            for (unsigned j = 0; j < 8; ++j)
                input.voice[i][j] = (uint16_t)(random_value() & (j<4?4095:j<7?2047:127));
        input.low_speed[0] = (uint8_t)random_value();
        input.low_speed[1] = (uint8_t)random_value();
        uint8_t air[216], assembled[216];
        size_t length = p25cai_frame_size(input.duid), received = 999;
        CHECK(p25cai_frame_encode(&input,air,sizeof air) == (int)length);
        for (unsigned pos = 70; pos < length*8; pos+=72)
            CHECK(((bit(air,pos)<<1)|bit(air,pos+1)) == input.status);
        CHECK(p25cai_frame_decode(air,length,&decoded,&quality) == 0);
        CHECK(decoded.nac == input.nac && decoded.duid == input.duid && decoded.status == input.status);
        if (input.duid != P25CAI_TDU) {
            unsigned meta_length = input.duid==P25CAI_HDU?15:input.duid==P25CAI_LDU2?12:9;
            CHECK(quality.metadata_valid && quality.metadata_corrected == 0);
            CHECK(memcmp(input.metadata,decoded.metadata,meta_length) == 0);
        }
        if (input.duid == P25CAI_LDU1 || input.duid == P25CAI_LDU2) {
            CHECK(quality.voice_valid == 511 && quality.low_speed_valid == 3);
            CHECK(memcmp(input.voice,decoded.voice,sizeof input.voice) == 0);
            CHECK(memcmp(input.low_speed,decoded.low_speed,2) == 0);
        }
        CHECK(p25cai_frame_decode(air,length-1,&decoded,&quality) == P25CAI_LENGTH);
        for (unsigned polarity = 0; polarity < 2; ++polarity) {
            p25cai_receiver rx;
            CHECK(p25cai_receiver_init(&rx,input.nac) == 0);
            for (unsigned i = 0; i < 37; ++i)
                CHECK(p25cai_receiver_push(&rx,(uint8_t)(i%4),assembled,&received) == 0);
            for (unsigned symbol = 0; symbol < length*4; ++symbol) {
                uint8_t dibit = (uint8_t)((bit(air,symbol*2)<<1)|bit(air,symbol*2+1));
                int result = p25cai_receiver_push(&rx,dibit ^ (polarity?2:0),assembled,&received);
                CHECK(result == (symbol+1 == length*4));
            }
            CHECK(received == length && memcmp(air,assembled,length) == 0);
            CHECK(p25cai_receiver_init(&rx,(uint16_t)(input.nac ^ 1)) == 0);
            for (unsigned symbol = 0; symbol < length*4; ++symbol) {
                uint8_t dibit = (uint8_t)((bit(air,symbol*2)<<1)|bit(air,symbol*2+1));
                CHECK(p25cai_receiver_push(&rx,dibit,assembled,&received) == 0);
            }
        }
        if (input.duid == P25CAI_HDU) {
            /* Four flipped bits in each of eight different short Golay words:
             * outer RS must repair their fallback systematic symbols. */
            for (unsigned symbol = 0; symbol < 8; ++symbol)
                for (unsigned b = 0; b < 4; ++b)
                    flip(air,air_position(112+18*symbol+b));
            CHECK(p25cai_frame_decode(air,length,&decoded,&quality) == 0);
            CHECK(quality.metadata_valid && memcmp(input.metadata,decoded.metadata,15) == 0);
        }
    }
}

static void trellis(void)
{
    for (unsigned rate = 2; rate <= 3; ++rate) {
        uint8_t input[18] = {0}, air[25], decoded[18];
        CHECK(p25cai_trellis_encode(input,rate,air) == 0);
        /* A zero stream stays in state zero at point 0: (+1,-1),
         * mapped by Table 20 to dibit pair (00,10). Interleaving preserves
         * the identical pairs, giving 49 nibbles of 0x2. */
        for (unsigned i = 0; i < 24; ++i) CHECK(air[i] == 0x22);
        CHECK(air[24] == 0x20);
        for (unsigned trial = 0; trial < 400; ++trial) {
            for (unsigned i = 0; i < 6*rate; ++i) input[i] = (uint8_t)random_value();
            CHECK(p25cai_trellis_encode(input,rate,air) == 0);
            CHECK(p25cai_trellis_decode(air,rate,0,decoded) == 0);
            CHECK(memcmp(input,decoded,6*rate) == 0);
            flip(air,random_value()%196);
            CHECK(p25cai_trellis_decode(air,rate,0,decoded) == P25CAI_UNCORRECTABLE);
            CHECK(p25cai_trellis_decode(air,rate,1,decoded) == 1);
            CHECK(memcmp(input,decoded,6*rate) == 0);
        }
    }
}

static void hostile_inputs(void)
{
    uint32_t word = 0xaabbccdd;
    uint16_t small = 0xdead;
    uint64_t nid = UINT64_C(0xdeadbeefdeadbeef);
    CHECK(p25cai_golay_encode(4096,23,&word) == P25CAI_INVALID && word == 0xaabbccdd);
    CHECK(p25cai_golay_decode(1u<<24,24,&small) == P25CAI_INVALID && small == 0xdead);
    CHECK(p25cai_hamming_encode(64,10,&small) == P25CAI_INVALID && small == 0xdead);
    CHECK(p25cai_nid_encode(4096,0,&nid) == P25CAI_INVALID && nid == UINT64_C(0xdeadbeefdeadbeef));
    uint8_t data[36] = {64}, before[36];
    memcpy(before,data,sizeof data);
    CHECK(p25cai_rs_decode(data,36,20) == P25CAI_INVALID && memcmp(data,before,sizeof data) == 0);
    CHECK(p25cai_rs_encode(data,36,20,data) == P25CAI_INVALID && memcmp(data,before,sizeof data) == 0);
    p25cai_frame frame = {0}, original;
    p25cai_quality quality, original_quality;
    frame.duid = P25CAI_LDU1;
    frame.voice[8][7] = 128;
    uint8_t air[216], saved[216];
    memset(air,0xa5,sizeof air);
    memcpy(saved,air,sizeof air);
    CHECK(p25cai_frame_encode(&frame,air,sizeof air) == P25CAI_INVALID && memcmp(air,saved,sizeof air) == 0);
    frame.duid = P25CAI_HDU;
    CHECK(p25cai_frame_encode(&frame,air,98) == P25CAI_LENGTH && memcmp(air,saved,sizeof air) == 0);
    memset(&frame,0xa5,sizeof frame);
    memset(&quality,0x5a,sizeof quality);
    memcpy(&original,&frame,sizeof frame);
    memcpy(&original_quality,&quality,sizeof quality);
    CHECK(p25cai_frame_decode(air,216,&frame,&quality) < 0);
    CHECK(memcmp(&frame,&original,sizeof frame) == 0 && memcmp(&quality,&original_quality,sizeof quality) == 0);
    p25cai_receiver rx;
    CHECK(p25cai_receiver_init(&rx,P25CAI_ANY_NAC) == 0);
    size_t bytes = 0;
    CHECK(p25cai_receiver_push(&rx,4,air,&bytes) == P25CAI_INVALID);
    for (unsigned trial = 0; trial < 30000; ++trial) {
        for (unsigned i = 0; i < sizeof air; ++i) air[i] = (uint8_t)random_value();
        size_t length = random_value()%217;
        (void)p25cai_frame_decode(air,length,&frame,&quality);
        (void)p25cai_nid_decode(((uint64_t)random_value()<<32)|random_value(),&small,data);
        (void)p25cai_receiver_push(&rx,(uint8_t)(random_value()&3),saved,&bytes);
        if (trial%31 == 0) {
            air[24] &= 0xf0;
            (void)p25cai_trellis_decode(air,2+(trial&1),8,data);
        }
    }
}

static void damaged_frames(void)
{
    p25cai_frame input = {0}, decoded;
    p25cai_quality quality;
    uint8_t air[216], damaged[216], out[216];
    input.nac = 0x789;
    input.duid = P25CAI_LDU1;
    input.metadata[4] = 0x37;
    input.metadata[8] = 0xb9;
    CHECK(p25cai_frame_encode(&input,air,sizeof air) == 216);
    /* Body mutations retain a valid preamble, reaching payload decoders;
     * random-byte tests alone would mostly stop at the sync check. */
    for (unsigned trial = 0; trial < 2000; ++trial) {
        memcpy(damaged,air,sizeof air);
        for (unsigned flips = 0; flips < trial%39; ++flips)
            flip(damaged,114 + random_value()%(1728-114));
        CHECK(p25cai_frame_decode(damaged,sizeof damaged,&decoded,&quality) == 0);
        CHECK(decoded.nac == input.nac && decoded.duid == input.duid);
        if (!quality.metadata_valid)
            for (unsigned i = 0; i < 15; ++i) CHECK(decoded.metadata[i] == 0);
    }
    for (unsigned errors = 0; errors <= 3; ++errors) {
        memcpy(damaged,air,sizeof air);
        for (unsigned i = 0; i < errors; ++i) flip(damaged,7*i);
        int result = p25cai_frame_decode(damaged,sizeof damaged,&decoded,&quality);
        CHECK(errors <= 2 ? result == 0 : result == P25CAI_INVALID);
    }
    /* A payload containing an exact sync must not truncate a locked frame. */
    p25cai_receiver rx;
    CHECK(p25cai_receiver_init(&rx,P25CAI_ANY_NAC) == 0);
    memcpy(damaged,air,sizeof air);
    memcpy(damaged+40,air,6);
    size_t bytes = 0;
    for (unsigned symbol = 0; symbol < 864; ++symbol) {
        uint8_t dibit = (uint8_t)((bit(damaged,2*symbol)<<1)|bit(damaged,2*symbol+1));
        CHECK(p25cai_receiver_push(&rx,dibit,out,&bytes) == (symbol == 863));
    }
    CHECK(bytes == 216 && memcmp(damaged,out,216) == 0);
    /* Dropped symbols force a length-based discard/reacquisition. A later
     * complete frame must still be received; no mid-body recovery claim. */
    CHECK(p25cai_receiver_init(&rx,input.nac) == 0);
    for (unsigned symbol = 0; symbol < 200; ++symbol) {
        uint8_t dibit = (uint8_t)((bit(air,2*symbol)<<1)|bit(air,2*symbol+1));
        (void)p25cai_receiver_push(&rx,dibit,out,&bytes);
    }
    unsigned complete = 0;
    for (unsigned repeat = 0; repeat < 3; ++repeat)
        for (unsigned symbol = 0; symbol < 864; ++symbol) {
            uint8_t dibit = (uint8_t)((bit(air,2*symbol)<<1)|bit(air,2*symbol+1));
            int result = p25cai_receiver_push(&rx,dibit,out,&bytes);
            if (result == 1 && bytes == 216 && memcmp(out,air,216) == 0) ++complete;
        }
    CHECK(complete >= 1);
}

static void qualified_channel_status(void)
{
    p25cai_frame input = {0};
    input.nac = 0x293; input.duid = P25CAI_TDU;
    for (unsigned status = 0; status < 4; ++status) {
        input.status = (uint8_t)status;
        uint8_t air[216], assembled[216];
        CHECK(p25cai_frame_encode(&input,air,sizeof air) == 18);
        for (unsigned inverted = 0; inverted < 2; ++inverted) {
            p25cai_receiver rx;
            size_t bytes = 0;
            CHECK(p25cai_receiver_init_ex(&rx,input.nac,0,1) == 0);
            CHECK(!rx.status_valid && rx.status_symbol == 2);
            for (unsigned i = 0; i < 72; ++i) {
                uint8_t symbol = (uint8_t)((air[i/4] >> (6-2*(i%4)))&3);
                CHECK(p25cai_receiver_push(&rx,symbol ^ (inverted?2:0),assembled,&bytes) == (i == 71));
                if (i < 71) CHECK(!rx.status_valid);
            }
            CHECK(rx.status_valid && rx.status_symbol == status && rx.status_updates == 2);
            CHECK(bytes == 18 && memcmp(air,assembled,18) == 0);
            uint32_t seen = rx.symbols_seen, complete = rx.complete_frames;
            p25cai_receiver_gap(&rx);
            CHECK(!rx.status_valid && rx.status_symbol == 2 && rx.status_updates == 3);
            CHECK(!rx.symbols && !rx.target_symbols && !rx.sync_shift);
            CHECK(rx.expected_nac == input.nac && rx.symbols_seen == seen && rx.complete_frames == complete);
            CHECK(p25cai_receiver_init_ex(&rx,input.nac ^ 1,0,1) == 0);
            for (unsigned i = 0; i < 72; ++i) {
                uint8_t symbol = (uint8_t)((air[i/4] >> (6-2*(i%4)))&3);
                CHECK(p25cai_receiver_push(&rx,symbol ^ (inverted?2:0),assembled,&bytes) == 0);
                CHECK(!rx.status_valid);
            }
            CHECK(rx.bad_nids == 1);
            CHECK(p25cai_receiver_push(&rx,4,assembled,&bytes) == P25CAI_INVALID);
            CHECK(!rx.status_valid && !rx.symbols);
        }
    }
}

int main(void)
{
    qualified_channel_status();
    reed_solomon_shortening();
    binary_codes(); puts("PASS binary FEC: normative values, exhaustive data, correction masks");
    reed_solomon(); puts("PASS RS: normative basis vectors, independent syndromes, all correction counts");
    network_id(); puts("PASS NID: all 24576 supported values versus Table 19, 1..11 bit errors");
    voice_words(); puts("PASS voice: all 144 Table 1 positions, PN masks, unshifted codec boundary");
    frames_and_stream(); puts("PASS frames: five formats, status placement, polarity, NAC filtering, outer repair");
    trellis(); puts("PASS trellis: normative zero vector, both rates, bounded Viterbi correction");
    hostile_inputs(); puts("PASS malformed input and 30000 deterministic hostile cases");
    damaged_frames(); puts("PASS valid-preamble damage, sync tolerance, payload sync, stream recovery");
    printf("%lu assertions passed\n", checks);
    return 0;
}

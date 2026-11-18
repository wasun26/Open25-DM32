/* SPDX-License-Identifier: MIT */
#include "p25cai.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x); exit(1); } } while (0)
int main(void)
{
    uint8_t key[16] = {0}, seed[10] = {0}, challenge[5] = {0}, session[16], response[4];
    const uint8_t zero_cipher[16] = {0x66,0xe9,0x4b,0xd4,0xef,0x8a,0x2c,0x3b,0x88,0x4c,0xfa,0x59,0xca,0x34,0x2b,0x2e};
    CHECK(p25cai_auth_derive(key,seed,0,session) == 0 && memcmp(session,zero_cipher,16) == 0);
    CHECK(p25cai_auth_response(key,challenge,response) == 0 && memcmp(response,zero_cipher,4) == 0);
    const uint8_t example_seed[10] = {0x38,0xae,0xc8,0x29,0x33,0xb1,0x7f,0x80,0x24,0x9d};
    const uint8_t example_session[16] = {0x05,0x24,0x30,0xbd,0xaf,0x39,0xe8,0x2f,0xd0,0xdd,0xd6,0x98,0xc0,0x2f,0xb0,0x36};
    for (unsigned i = 0; i < 16; ++i) key[i] = (uint8_t)i;
    CHECK(p25cai_auth_derive(key,example_seed,0,session) == 0 && memcmp(session,example_session,16) == 0);
    uint8_t mutual[16];
    /* AACE-A 6.6, PDF 47 / printed 39, AM1 through AM4. */
    const uint8_t example_mutual[16] = {0x69,0xd5,0xdc,0x08,0x02,0x3c,0x46,0x52,0xcc,0x71,0xd5,0xcd,0x1e,0x74,0xe1,0x04};
    const uint8_t challenge1[5] = {0x4d,0x92,0x5a,0xf6,0x08}, challenge2[5] = {0x6e,0x78,0x4f,0x75,0xbd};
    const uint8_t response1[4] = {0x3e,0x00,0xfa,0xa8}, response2[4] = {0xb3,0xad,0x16,0xe1};
    CHECK(p25cai_auth_derive(key,example_seed,1,mutual) == 0 && memcmp(example_mutual,mutual,16) == 0);
    CHECK(p25cai_auth_response(session,challenge1,response) == 0 && memcmp(response,response1,4) == 0);
    CHECK(p25cai_auth_response(mutual,challenge2,response) == 0 && memcmp(response,response2,4) == 0);
    CHECK(p25cai_auth_equal(response,response));
    uint8_t different[4]; memcpy(different,response,4);
    for (unsigned i = 0; i < 4; ++i) {
        different[i] ^= 1; CHECK(!p25cai_auth_equal(response,different)); different[i] ^= 1;
    }
    CHECK(!p25cai_auth_equal(NULL,response));
    uint8_t before[16]; memcpy(before,session,16);
    CHECK(p25cai_auth_derive(key,seed,2,session) == P25CAI_INVALID && memcmp(before,session,16) == 0);
    CHECK(p25cai_auth_response(NULL,challenge,response) == P25CAI_INVALID);
    puts("Authentication mathematical vectors and input checks passed");
    return 0;
}

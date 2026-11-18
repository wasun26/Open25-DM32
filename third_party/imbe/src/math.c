/* SPDX-License-Identifier: MIT
 * Mathematical fixed-point support, generated from log2 and exp2 definitions.
 * Tables contain round(log2(1+i/64)*2048) and round(2^(i/64)*16384).
 * No codec implementation source or normative dataset is imported. */
#include "imbe_model.h"
#include <limits.h>
static const uint16_t log_fraction[65] = {
    0, 46, 91, 135, 179, 222, 265, 307, 348, 389,
    429, 469, 508, 546, 585, 622, 659, 696, 732, 768,
    803, 838, 873, 907, 941, 974, 1007, 1040, 1072, 1104,
    1136, 1167, 1198, 1229, 1259, 1289, 1319, 1348, 1377, 1406,
    1435, 1463, 1491, 1519, 1546, 1573, 1600, 1627, 1653, 1680,
    1706, 1732, 1757, 1783, 1808, 1833, 1857, 1882, 1906, 1930,
    1954, 1978, 2001, 2025, 2048
};
static const uint16_t exp_fraction[65] = {
    16384, 16562, 16743, 16925, 17109, 17296, 17484, 17674, 17867, 18061,
    18258, 18457, 18658, 18861, 19066, 19274, 19484, 19696, 19911, 20127,
    20347, 20568, 20792, 21019, 21247, 21479, 21713, 21949, 22188, 22430,
    22674, 22921, 23170, 23423, 23678, 23936, 24196, 24460, 24726, 24995,
    25268, 25543, 25821, 26102, 26386, 26674, 26964, 27258, 27554, 27855,
    28158, 28464, 28774, 29088, 29405, 29725, 30048, 30376, 30706, 31041,
    31379, 31720, 32066, 32415, 32768
};
int32_t imbe_log2_u32_q11(uint32_t value)
{
    unsigned n, index, fraction;
    uint32_t normalized;
    if(!value)return -32768;
    n=31u-(unsigned)__builtin_clz(value);
    normalized=value<<(31u-n);
    index=(normalized>>25)&63u;
    fraction=(normalized&0x1ffffffu)>>9;
    return (int32_t)(n*2048u+log_fraction[index])+
      (int32_t)(((uint32_t)(log_fraction[index+1]-log_fraction[index])*fraction+32768u)>>16);
}
uint32_t imbe_exp2_q11(int32_t value)
{
    int n=value>>11, shift;
    unsigned f=(unsigned)value&2047u, index=f>>5, fraction=f&31u;
    uint32_t base=exp_fraction[index]+(((uint32_t)(exp_fraction[index+1]-exp_fraction[index])*fraction+16u)>>5);
    /* base is Q14; result is linear amplitude Q12. */
    shift=n-2;
    if(shift>=0){
        if(shift>=32 || base>(UINT32_MAX>>(unsigned)shift))return UINT32_MAX;
        return base<<(unsigned)shift;
    }
    shift=-shift;
    if(shift>=32)return 0;
    return (base+(1u<<((unsigned)shift-1u)))>>(unsigned)shift;
}
uint32_t imbe_isqrt(uint32_t value)
{
    uint32_t root=0,bit=1u<<30;
    while(bit>value)bit>>=2;
    while(bit){
        if(value>=root+bit){value-=root+bit;root=(root>>1)+bit;}
        else root>>=1;
        bit>>=2;
    }
    return root;
}

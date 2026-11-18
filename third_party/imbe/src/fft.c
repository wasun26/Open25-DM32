/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Open25 contributors
 *
 * Independently expressed real-DFT mathematics for full-rate IMBE analysis.
 * The transform definition is the negative-exponential DFT used by
 * TIA-102.BABA-A section 5.1.5, equation 29. This implementation is a packed
 * 128-point complex radix-2 FFT followed by the standard even/odd real-DFT
 * identity; its control flow and numeric representation are original here.
 *
 * The sine table is mathematical: full_sine[k] = round(32768*sin(2*pi*k/256)),
 * k=0..255. It is generated from that formula, not imported from a codec,
 * reference binary or a published coefficient dataset. The value 32768 is
 * stored in32-bit entries so the exact unit twiddle is representable.
 *
 * All products and sums are signed 32-bit. Before a twiddled stage, a scan
 * scales every component to abs(v)<=13571. Since abs(c)+abs(s)<=46342 for
 * these Q15 twiddles, a rotated component is at most
 * ceil(13571*46342/32768)=19194. Adding the unchanged component remains below
 * 32768. Dot products are below 2*13571*32768 and fit int32_t.
 *
 * Real unpacking uses pairwise sums/differences bounded by 27142; its rotated
 * products sum to less than 2*27142*32768 < INT32_MAX. The final halving keeps
 * each result in int16_t. Every normalization shift applies to the entire
 * complex block and is returned as scale credit. In-place pair processing
 * reads both operands before either is overwritten. No clipping is required.
 */
#include "imbe_model.h"
#include <limits.h>
#include <stddef.h>

_Static_assert(INT16_MIN == -32768 && INT16_MAX == 32767, "16-bit two's complement required");
_Static_assert(INT32_MAX == 2147483647, "32-bit signed arithmetic required");
_Static_assert((-1 >> 1) == -1, "arithmetic signed right shift required");

/* Formula-generated full-cycle expansion; same exact Q15 values. */
/* CK803S integrations copy these measured hot sections at startup. Other
 * targets retain ordinary read-only tables and code placement. */
#if defined(__csky__) && defined(IMBE_SINE_RAM)
#define IMBE_SINE_DATA __attribute__((section(".ramdata.imbe_sine")))
#define IMBE_SINE_CODE __attribute__((section(".ramfunc.imbe_sine_q15")))
#else
#define IMBE_SINE_DATA
#define IMBE_SINE_CODE
#endif

static const int32_t full_sine[256] IMBE_SINE_DATA = {
    0, 804, 1608, 2411, 3212, 4011, 4808, 5602,
    6393, 7180, 7962, 8740, 9512, 10279, 11039, 11793,
    12540, 13279, 14010, 14733, 15447, 16151, 16846, 17531,
    18205, 18868, 19520, 20160, 20788, 21403, 22006, 22595,
    23170, 23732, 24279, 24812, 25330, 25833, 26320, 26791,
    27246, 27684, 28106, 28511, 28899, 29269, 29622, 29957,
    30274, 30572, 30853, 31114, 31357, 31581, 31786, 31972,
    32138, 32286, 32413, 32522, 32610, 32679, 32729, 32758,
    32768, 32758, 32729, 32679, 32610, 32522, 32413, 32286,
    32138, 31972, 31786, 31581, 31357, 31114, 30853, 30572,
    30274, 29957, 29622, 29269, 28899, 28511, 28106, 27684,
    27246, 26791, 26320, 25833, 25330, 24812, 24279, 23732,
    23170, 22595, 22006, 21403, 20788, 20160, 19520, 18868,
    18205, 17531, 16846, 16151, 15447, 14733, 14010, 13279,
    12540, 11793, 11039, 10279, 9512, 8740, 7962, 7180,
    6393, 5602, 4808, 4011, 3212, 2411, 1608, 804,
    0, -804, -1608, -2411, -3212, -4011, -4808, -5602,
    -6393, -7180, -7962, -8740, -9512, -10279, -11039, -11793,
    -12540, -13279, -14010, -14733, -15447, -16151, -16846, -17531,
    -18205, -18868, -19520, -20160, -20788, -21403, -22006, -22595,
    -23170, -23732, -24279, -24812, -25330, -25833, -26320, -26791,
    -27246, -27684, -28106, -28511, -28899, -29269, -29622, -29957,
    -30274, -30572, -30853, -31114, -31357, -31581, -31786, -31972,
    -32138, -32286, -32413, -32522, -32610, -32679, -32729, -32758,
    -32768, -32758, -32729, -32679, -32610, -32522, -32413, -32286,
    -32138, -31972, -31786, -31581, -31357, -31114, -30853, -30572,
    -30274, -29957, -29622, -29269, -28899, -28511, -28106, -27684,
    -27246, -26791, -26320, -25833, -25330, -24812, -24279, -23732,
    -23170, -22595, -22006, -21403, -20788, -20160, -19520, -18868,
    -18205, -17531, -16846, -16151, -15447, -14733, -14010, -13279,
    -12540, -11793, -11039, -10279, -9512, -8740, -7962, -7180,
    -6393, -5602, -4808, -4011, -3212, -2411, -1608, -804,
};

static inline int32_t sine_q15(unsigned phase)
{
    return full_sine[phase & 255u];
}

/* Symmetric nearest rounding, ties away from zero. Proven product bounds
 * exclude INT32_MIN and leave room for the rounding term, so both negation
 * and addition are defined. Symmetry avoids a repeated positive half-LSB
 * bias in the FFT and its block normalization. */
static inline int32_t q15_round(int32_t x)
{
    if (x < 0) return -((-x + 16384) >> 15);
    return (x + 16384) >> 15;
}

static int normalize(int16_t *v, unsigned count, int limit)
{
    int32_t peak = 0;
    unsigned shift = 0;
    unsigned i;
    for (i = 0; i < count; ++i) {
        int32_t a = v[i];
        if (a < 0) a = -a;
        if (a > peak) peak = a;
    }
    while (peak > limit) {
        peak = (peak + 1) >> 1;
        ++shift;
    }
    if (shift != 0u) {
        const int32_t half = 1 << (shift - 1u);
        for (i = 0; i < count; ++i) {
            const int32_t x = v[i];
            v[i] = (int16_t)(x < 0 ? -((-x + half) >> shift)
                                      : ((x + half) >> shift));
        }
    }
    return (int)shift;
}

int imbe_cfft(int16_t *buf, unsigned log2n)
{
    unsigned i, j, width, phase_step;
    unsigned n;
    int scale = 0;
    if (log2n < 2u || log2n > 8u) return -1;
    n = 1u << log2n;
    /* Binary index reversal places input values in iterative
     * decimation-in-time order. */
    j = 0;
    for (i = 1; i < n; ++i) {
        unsigned bit = n >> 1;
        while ((j & bit) != 0u) { j ^= bit; bit >>= 1; }
        j ^= bit;
        if (i < j) {
            int16_t r = buf[2u*i], im = buf[2u*i + 1u];
            buf[2u*i] = buf[2u*j]; buf[2u*i + 1u] = buf[2u*j + 1u];
            buf[2u*j] = r; buf[2u*j + 1u] = im;
        }
    }
    for (width = 2u, phase_step = 128u; width <= n;
         width <<= 1, phase_step >>= 1) {
        const unsigned half = width >> 1;
        unsigned base;
        /* First two passes have only unit/quarter-turn rotations. */
        scale += normalize(buf, 2u*n, width <= 4u ? 16383 : 13571);
        for (base = 0; base < n; base += width) {
            for (j = 0; j < half; ++j) {
                const unsigned a = 2u*(base + j);
                const unsigned b = 2u*(base + j + half);
                const int32_t ar = buf[a], ai = buf[a + 1u];
                const int32_t br = buf[b], bi = buf[b + 1u];
                int32_t tr, ti;
                if (j == 0u) {
                    tr = br; ti = bi;
                } else if (j*2u == half) {
                    tr = bi; ti = -br;
                } else {
                    const unsigned phase = j*phase_step;
                    const int32_t c = sine_q15(phase + 64u);
                    const int32_t s = sine_q15(phase);
                    tr = q15_round(c*br + s*bi);
                    ti = q15_round(c*bi - s*br);
                }
                buf[a] = (int16_t)(ar + tr); buf[a + 1u] = (int16_t)(ai + ti);
                buf[b] = (int16_t)(ar - tr); buf[b + 1u] = (int16_t)(ai - ti);
            }
        }
    }
    return scale;
}

int imbe_rfft256(int16_t buf[258])
{
    unsigned i;
    int scale = imbe_cfft(buf, 7u);
    scale += normalize(buf, 256u, 13571);
    {
        const int32_t zr = buf[0], zi = buf[1];
        buf[0] = (int16_t)(zr + zi); buf[1] = 0;
        buf[256] = (int16_t)(zr - zi); buf[257] = 0;
    }
    /* E=(Z[k]+conj(Z[128-k]))/2,
     * O=(Z[k]-conj(Z[128-k]))/(2j), X[k]=E+exp(-j*pi*k/128)*O.
     * Process the conjugate partner together so no separate buffer is needed. */
    for (i = 1u; i < 64u; ++i) {
        const unsigned a = 2u*i, b = 2u*(128u-i);
        const int32_t ar = buf[a], ai = buf[a + 1u];
        const int32_t br = buf[b], bi = buf[b + 1u];
        const int32_t sum = ar + br, diff = ai - bi;
        const int32_t odd_r = ai + bi, odd_i = br - ar;
        const int32_t c = sine_q15(i + 64u), s = sine_q15(i);
        const int32_t r = q15_round(c*odd_r + s*odd_i);
        const int32_t im = q15_round(c*odd_i - s*odd_r);
        buf[a] = (int16_t)((sum + r) >> 1);
        buf[a + 1u] = (int16_t)((diff + im) >> 1);
        buf[b] = (int16_t)((sum - r) >> 1);
        buf[b + 1u] = (int16_t)((im - diff) >> 1);
    }
    buf[129] = (int16_t)-buf[129];
    return scale;
}

/* Algebraic inverse of the packed even/odd real-DFT identity. All paired
 * operands are read before overwrite, and normalization keeps their Q15
 * products below INT32_MAX. The final complex inverse uses conjugation and
 * accounts for the required 1/128 by exponent credit rather than a lossy pass. */
int imbe_irfft256(int16_t buf[258])
{
    unsigned i;
    int scale = normalize(buf, 258u, 13571);
    {
        int32_t dc = buf[0], nyquist = buf[256];
        buf[0] = (int16_t)((dc + nyquist) >> 1);
        buf[1] = (int16_t)((dc - nyquist) >> 1);
    }
    for (i = 1; i < 64; ++i) {
        unsigned a = 2*i, b = 2*(128-i);
        int32_t ar=buf[a], ai=buf[a+1], br=buf[b], bi=buf[b+1];
        int32_t sum=ar+br, diff=ai-bi, tr=ar-br, ti=ai+bi;
        int32_t c=sine_q15(i+64), si=sine_q15(i);
        int32_t odd_r=q15_round(c*tr-si*ti);
        int32_t odd_i=q15_round(c*ti+si*tr);
        buf[a]=(int16_t)((sum-odd_i)>>1);
        buf[a+1]=(int16_t)((diff+odd_r)>>1);
        buf[b]=(int16_t)((sum+odd_i)>>1);
        buf[b+1]=(int16_t)((odd_r-diff)>>1);
    }
    buf[129]=(int16_t)-buf[129];
    for(i=1;i<256;i+=2)buf[i]=(int16_t)-buf[i];
    scale += imbe_cfft(buf,7u);
    for(i=1;i<256;i+=2)buf[i]=(int16_t)-buf[i];
    return scale-7;
}

/* Full-cycle Q32 phase, linearly interpolated full-cycle mathematical sine. */
IMBE_SINE_CODE
int16_t imbe_sine_q15(uint32_t phase)
{
    unsigned at=phase>>24;
    int32_t a=sine_q15(at),b=sine_q15(at+1);
    int32_t value=a+(((b-a)*(int32_t)((phase>>8)&65535u)+32768)>>16);
    if(value>32767)value=32767;
    return (int16_t)value;
}

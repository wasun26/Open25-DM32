/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Open25 contributors
 * Independently expressed full-rate IMBE parameter quantization from the
 * mathematical definitions in TIA-102.BABA-A sections 6 and 7.1.
 * Numeric datasets are separately identified in quant_tables.inc.
 * No source implementation or recovered codec table layout was imported.
 */
#include "quant.h"
#include <limits.h>
#include <stddef.h>

_Static_assert(INT16_MIN == -32768 && INT16_MAX == 32767, "16-bit two's complement required");
_Static_assert(INT32_MAX == 2147483647, "32-bit signed integer required");
#include "quant_tables.inc"

const uint8_t imbe_word_widths[8] = {12,12,12,12,11,11,11,7};
/* Table 3's decimal factors, in hundredths; zero means no allocation. */
static const uint8_t step_centi[11] = {0,120,85,65,40,28,15,8,4,2,1};
/* All 240 Annex F steps equal sigma_milli[i]*step_centi[B]/100000.
 * Extraction verifies that identity against every published step. */
static const uint16_t gain_sigma_milli[5] = {310,201,168,145,132};
static const uint16_t hoc_sigma_milli[9] = {307,241,207,190,179,173,165,170,170};

static int16_t clamp_log(int32_t x)
{
    if (x > INT16_MAX) return INT16_MAX;
    if (x < INT16_MIN) return INT16_MIN;
    return (int16_t)x;
}

/* Nearest, with exact ties away from zero. Bounds below exclude INT32_MIN
 * and leave enough room for half the positive denominator. */
static int32_t nearest_div(int32_t n, int32_t d)
{
    return n < 0 ? -((-n + d/2)/d) : (n + d/2)/d;
}

static unsigned nibble(const uint8_t *p, unsigned index)
{
    return (p[index/2u] >> (4u*(index&1u))) & 15u;
}

static unsigned block_length(unsigned L, unsigned block)
{
    return L/6u + (block >= 6u-L%6u);
}

static void allocation(unsigned L, uint8_t bits[59])
{
    unsigned i;
    for (i=0;i<59u;++i) bits[i]=0;
    bits[0]=8; bits[1]=(uint8_t)(L<=36u ? (L+2u)/3u : 12u); bits[2]=6;
    for (i=0;i<5u;++i) bits[3u+i]=(uint8_t)nibble(gain_alloc_nibbles,5u*(L-9u)+i);
    const unsigned offset=(L-9u)*(L-4u)/2u;
    for (i=0;i<L-6u;++i) bits[8u+i]=(uint8_t)nibble(hoc_alloc_nibbles,offset+i);
    bits[L+2u]=1;
}

void imbe_quant_init(imbe_quant_state *state)
{
    if (!state) return;
    state->previous_L=30;
    for (unsigned i=0;i<IMBE_MAX_HARMONICS;++i) state->previous_log_amplitude[i]=0;
}

int imbe_pitch_decode(unsigned code, imbe_model *model)
{
    if (!model || code>=208u) return 0;
    const unsigned denominator=2u*code+79u;
    /* round(2^34/denominator), decomposed to avoid a 64-bit divide. */
    const uint32_t q=UINT32_MAX/denominator, r=UINT32_MAX%denominator;
    model->frequency_q32=4u*q+(4u*(r+1u)+denominator/2u)/denominator;
    model->pitch_code=(uint8_t)code;
    model->L=(uint8_t)(9254u*((2u*code+81u)/8u)/10000u);
    model->K=(uint8_t)(model->L<=36u ? (model->L+2u)/3u : 12u);
    return 1;
}

unsigned imbe_voicing_group(unsigned harmonic_zero_based, unsigned L)
{
    if (L<9u || L>56u || harmonic_zero_based>=L) return 0;
    const unsigned group=harmonic_zero_based/3u;
    return group<12u ? group : 11u;
}

/* One-based interpolated previous harmonic. The DC amplitude is 1 (log=0),
 * and the high-frequency tail repeats the last harmonic, equations 56–57. */
static int32_t interpolate_previous(const imbe_quant_state *state, unsigned L, unsigned l)
{
    const unsigned position=state->previous_L*l;
    const unsigned k=position/L, fraction=position%L;
    const unsigned last=state->previous_L;
    const int32_t a=k==0u ? 0 : state->previous_log_amplitude[(k<=last ? k : last)-1u];
    const int32_t b=state->previous_log_amplitude[(k<last ? k+1u : last)-1u];
    return nearest_div((int32_t)(L-fraction)*a+(int32_t)fraction*b,(int32_t)L);
}

static int32_t prediction_mean(const imbe_quant_state *state, unsigned L)
{
    int32_t sum=0;
    for (unsigned l=1;l<=L;++l) sum+=interpolate_previous(state,L,l);
    return nearest_div(sum,(int32_t)L);
}

static int32_t prediction(const imbe_quant_state *state, unsigned L, unsigned l, int32_t mean)
{
    const unsigned rho_centi=L<=15u ? 40u : L<=24u ? 3u*L-5u : 70u;
    return nearest_div((interpolate_previous(state,L,l)-mean)*(int32_t)rho_centi,100);
}

/* cos(pi*k*(j+1/2)/n). Phase wraps in unsigned arithmetic by design.
 * Q14 keeps a residual*coefficient product inside int32_t: with log inputs
 * confined to int16 Q11 and rho<=0.7, abs(residual)<=78644. Thus even a unit
 * Q14 cosine gives abs(product)<1.29e9. Accumulation occurs after rescaling. */
static int32_t dct_cos(unsigned n, unsigned k, unsigned j)
{
    const uint32_t phase=k*(2u*j+1u)*(UINT32_C(0x40000000)/n);
    return nearest_div(imbe_sine_q15(phase+UINT32_C(0x40000000)),2);
}

static void forward_dct(int32_t *v, unsigned n)
{
    int32_t input[10];
    for (unsigned j=0;j<n;++j) input[j]=v[j];
    for (unsigned k=0;k<n;++k) {
        int32_t sum=0;
        for (unsigned j=0;j<n;++j)
            sum+=k==0u ? input[j] : nearest_div(input[j]*dct_cos(n,k,j),16384);
        v[k]=nearest_div(sum,(int32_t)n);
    }
}

static void inverse_dct(int32_t *v, unsigned n)
{
    int32_t input[10];
    for (unsigned k=0;k<n;++k) input[k]=v[k];
    for (unsigned j=0;j<n;++j) {
        int32_t sum=input[0];
        for (unsigned k=1;k<n;++k) sum+=2*nearest_div(input[k]*dct_cos(n,k,j),16384);
        v[j]=sum;
    }
}

/* Exact decimal steps are represented by a numerator in units of 1e-5.
 * Conversion ratio Q11/step is therefore 3125/(64*step_numerator). */
static unsigned scalar_encode(int32_t value, unsigned bits, unsigned step_numerator)
{
    if (bits==0u) return 0;
    const int32_t denominator=(int32_t)(64u*step_numerator);
    const int32_t numerator=value*3125;
    const int32_t q=numerator<0 ? -((-numerator+denominator-1)/denominator) : numerator/denominator;
    const int32_t index=q+(int32_t)(1u<<(bits-1u));
    if (index<0) return 0;
    const unsigned last=(1u<<bits)-1u;
    return (uint32_t)index>last ? last : (unsigned)index;
}

static int32_t scalar_decode(unsigned code, unsigned bits, unsigned step_numerator)
{
    if (bits==0u) return 0;
    const int32_t twice=(int32_t)(2u*code+1u)-(int32_t)(1u<<bits);
    /* For legal bit allocations and their associated steps this product is
     * below 11 million. Even combining independent maxima remains <1.22e9. */
    return nearest_div((int32_t)step_numerator*twice*32,3125);
}

static unsigned step_for_gain(unsigned index, unsigned bits)
{
    return gain_sigma_milli[index]*step_centi[bits];
}

static unsigned step_for_hoc(unsigned order, unsigned bits)
{
    return hoc_sigma_milli[order-1u]*step_centi[bits];
}

static void stream_write(uint16_t words[8], unsigned *position, unsigned bit)
{
    unsigned pos=*position, word=0;
    while (pos>=imbe_word_widths[word]) { pos-=imbe_word_widths[word]; ++word; }
    words[word]|=(uint16_t)((bit&1u)<<(imbe_word_widths[word]-1u-pos));
    ++*position;
}

static unsigned stream_read(const uint16_t words[8], unsigned *position)
{
    unsigned pos=*position, word=0;
    while (pos>=imbe_word_widths[word]) { pos-=imbe_word_widths[word]; ++word; }
    ++*position;
    return (words[word]>>(imbe_word_widths[word]-1u-pos))&1u;
}

/* Section 7.1: spectral quantizer bits descend by bit plane, then ascend by
 * parameter number. Exactly 39 precede the voicing/gain insertion at bit 48. */
static void pack_parameters(const uint16_t parameters[59], const uint8_t bits[59], unsigned L, unsigned sync, uint16_t words[8])
{
    unsigned position=0, scanned=0, highest=0;
    for (unsigned w=0;w<8u;++w) words[w]=0;
    for (unsigned k=0;k<6u;++k) stream_write(words,&position,parameters[0]>>(7u-k));
    for (unsigned k=0;k<3u;++k) stream_write(words,&position,parameters[2]>>(5u-k));
    for (unsigned m=3;m<=L+1u;++m) if (bits[m]>highest) highest=bits[m];
    for (unsigned plane=highest;plane-- > 0u;) for (unsigned m=3;m<=L+1u;++m) {
        if (bits[m]<=plane) continue;
        if (scanned==39u) {
            for (unsigned k=bits[1];k-- > 0u;) stream_write(words,&position,parameters[1]>>k);
            stream_write(words,&position,parameters[2]>>2);
            stream_write(words,&position,parameters[2]>>1);
        }
        stream_write(words,&position,parameters[m]>>plane);
        ++scanned;
    }
    stream_write(words,&position,parameters[2]);
    stream_write(words,&position,parameters[0]>>1);
    stream_write(words,&position,parameters[0]);
    stream_write(words,&position,sync);
}

static void unpack_parameters(const uint16_t words[8], const uint8_t bits[59], unsigned L, uint16_t parameters[59])
{
    unsigned position=0, scanned=0, highest=0;
    for (unsigned m=0;m<59u;++m) parameters[m]=0;
    for (unsigned k=0;k<6u;++k) parameters[0]|=(uint16_t)(stream_read(words,&position)<<(7u-k));
    for (unsigned k=0;k<3u;++k) parameters[2]|=(uint16_t)(stream_read(words,&position)<<(5u-k));
    for (unsigned m=3;m<=L+1u;++m) if (bits[m]>highest) highest=bits[m];
    for (unsigned plane=highest;plane-- > 0u;) for (unsigned m=3;m<=L+1u;++m) {
        if (bits[m]<=plane) continue;
        if (scanned==39u) {
            for (unsigned k=bits[1];k-- > 0u;) parameters[1]|=(uint16_t)(stream_read(words,&position)<<k);
            parameters[2]|=(uint16_t)(stream_read(words,&position)<<2);
            parameters[2]|=(uint16_t)(stream_read(words,&position)<<1);
        }
        parameters[m]|=(uint16_t)(stream_read(words,&position)<<plane);
        ++scanned;
    }
    parameters[2]|=(uint16_t)stream_read(words,&position);
    parameters[0]|=(uint16_t)(stream_read(words,&position)<<1);
    parameters[0]|=(uint16_t)stream_read(words,&position);
    parameters[L+2u]=(uint16_t)stream_read(words,&position);
}

int imbe_quant_decode(imbe_quant_state *state, const uint16_t words[8], imbe_model *model)
{
    if (!state || !words || !model || state->previous_L<9u || state->previous_L>56u) return 0;
    for (unsigned w=0;w<8u;++w) if (words[w]>=(1u<<imbe_word_widths[w])) return 0;
    const unsigned pitch=(((unsigned)words[0]>>6)<<2)|(((unsigned)words[7]>>1)&3u);
    imbe_model result={0};
    if (!imbe_pitch_decode(pitch,&result)) return 0;
    const unsigned L=result.L;
    uint8_t bits[59]; uint16_t parameters[59]; int32_t gain[6], residual[56];
    allocation(L,bits); unpack_parameters(words,bits,L,parameters);
    for (unsigned k=0;k<result.K;++k) result.voiced|=(uint16_t)(((parameters[1]>>(result.K-1u-k))&1u)<<k);
    gain[0]=gain_q11[parameters[2]];
    for (unsigned g=0;g<5u;++g) gain[g+1u]=scalar_decode(parameters[g+3u],bits[g+3u],step_for_gain(g,bits[g+3u]));
    /* Equation 69's printed denominator J_i is inconsistent with its six-point
     * transform Eq61. The mathematical inverse uses n=6 for every gain entry. */
    inverse_dct(gain,6u);
    unsigned offset=0, m=8;
    for (unsigned b=0;b<6u;++b) {
        const unsigned n=block_length(L,b);
        residual[offset]=gain[b];
        for (unsigned k=1;k<n;++k,++m) residual[offset+k]=scalar_decode(parameters[m],bits[m],step_for_hoc(k,bits[m]));
        inverse_dct(residual+offset,n); offset+=n;
    }
    const int32_t mean=prediction_mean(state,L);
    for (unsigned l=1;l<=L;++l) result.log_amplitude[l-1u]=clamp_log(residual[l-1u]+prediction(state,L,l,mean));
    state->previous_L=(uint8_t)L;
    for (unsigned l=0;l<IMBE_MAX_HARMONICS;++l) state->previous_log_amplitude[l]=result.log_amplitude[l];
    *model=result;
    return 1;
}

int imbe_quant_encode(imbe_quant_state *state, imbe_model *model, unsigned sync, uint16_t words[8])
{
    if (!state || !model || !words || sync>1u || state->previous_L<9u || state->previous_L>56u) return 0;
    imbe_model pitch={0};
    if (!imbe_pitch_decode(model->pitch_code,&pitch)) return 0;
    const unsigned L=pitch.L;
    uint8_t bits[59]; uint16_t parameters[59]={0}; int32_t residual[56], gain[6];
    allocation(L,bits); parameters[0]=model->pitch_code;
    for (unsigned k=0;k<pitch.K;++k) parameters[1]=(uint16_t)(((unsigned)parameters[1]<<1)|(((unsigned)model->voiced>>k)&1u));
    const int32_t mean=prediction_mean(state,L);
    for (unsigned l=1;l<=L;++l) residual[l-1u]=(int32_t)model->log_amplitude[l-1u]-prediction(state,L,l,mean);
    unsigned offset=0;
    for (unsigned b=0;b<6u;++b) {
        const unsigned n=block_length(L,b);
        forward_dct(residual+offset,n); gain[b]=residual[offset]; offset+=n;
    }
    forward_dct(gain,6u);
    unsigned best=0; int32_t best_distance=INT32_MAX;
    for (unsigned g=0;g<64u;++g) {
        int32_t d=gain[0]-gain_q11[g]; if (d<0) d=-d;
        if (d<best_distance) { best=g; best_distance=d; }
    }
    parameters[2]=(uint16_t)best;
    for (unsigned g=0;g<5u;++g) parameters[g+3u]=(uint16_t)scalar_encode(gain[g+1u],bits[g+3u],step_for_gain(g,bits[g+3u]));
    offset=0; unsigned m=8;
    for (unsigned b=0;b<6u;++b) {
        const unsigned n=block_length(L,b);
        for (unsigned k=1;k<n;++k,++m) parameters[m]=(uint16_t)scalar_encode(residual[offset+k],bits[m],step_for_hoc(k,bits[m]));
        offset+=n;
    }
    pack_parameters(parameters,bits,L,sync,words);
    /* Decoder reconstruction is authoritative for the predictor in both
     * directions. Reuse it so encoded frames cannot develop predictor drift. */
    return imbe_quant_decode(state,words,model);
}

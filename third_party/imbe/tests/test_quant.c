/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Open25 contributors
 * Host-only parameter checks from mathematical equations and functional bit
 * layout facts. No external codec source, reference binary, PDF or audio. */
#include "imbe_model.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Compile-time data view: the owner remains the sole numerical dataset owner.
 * Independent double arithmetic below does not call its fixed-point mapper. */
#include "../src/quant_tables.inc"
#define PI 3.1415926535897932384626433832795
static uint32_t seed=0x761b8831u;
static uint32_t random_word(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static unsigned at_nibble(const uint8_t *p,unsigned at){return (p[at/2]>>(4*(at%2)))&15u;}
static unsigned lengths(unsigned L,unsigned block){return L/6+(block>=6-L%6);}
static void bits_for(unsigned L,unsigned bits[59])
{
    memset(bits,0,59*sizeof *bits);bits[0]=8;bits[1]=(L+2)/3;if(bits[1]>12)bits[1]=12;bits[2]=6;
    for(unsigned i=0;i<5;++i)bits[i+3]=at_nibble(gain_alloc_nibbles,5*(L-9)+i);
    unsigned start=(L-9)*(L-4)/2;
    for(unsigned i=0;i<L-6;++i)bits[i+8]=at_nibble(hoc_alloc_nibbles,start+i);
    bits[L+2]=1;
}
struct bit_position{unsigned parameter,bit;};
static int order_bits(const void *a,const void *b)
{
    const struct bit_position *x=a,*y=b;
    if(x->bit!=y->bit)return x->bit>y->bit?-1:1;
    return x->parameter<y->parameter?-1:x->parameter>y->parameter;
}
static void reference_pack(unsigned L,const unsigned p[59],uint16_t words[8])
{
    unsigned widths[59],sequence[88],used=0,count=0;
    struct bit_position positions[73];bits_for(L,widths);
    for(unsigned m=3;m<=L+1;++m)for(unsigned b=0;b<widths[m];++b)positions[count++]=(struct bit_position){m,b};
    qsort(positions,count,sizeof positions[0],order_bits);
    for(unsigned b=8;b>2;--b)sequence[used++]=(p[0]>>(b-1))&1u;
    for(unsigned b=6;b>3;--b)sequence[used++]=(p[2]>>(b-1))&1u;
    for(unsigned i=0;i<count;++i){
        if(i==39){
            for(unsigned b=widths[1];b>0;--b)sequence[used++]=(p[1]>>(b-1))&1u;
            sequence[used++]=(p[2]>>2)&1u;sequence[used++]=(p[2]>>1)&1u;
        }
        sequence[used++]=(p[positions[i].parameter]>>positions[i].bit)&1u;
    }
    sequence[used++]=p[2]&1u;sequence[used++]=(p[0]>>1)&1u;
    sequence[used++]=p[0]&1u;sequence[used++]=p[L+2]&1u;
    assert(used==88);used=0;
    for(unsigned w=0;w<8;++w){unsigned value=0;for(unsigned b=0;b<imbe_word_widths[w];++b)value=2*value+sequence[used++];words[w]=(uint16_t)value;}
}
static void inverse(double *v,unsigned n)
{
    double input[10];memcpy(input,v,n*sizeof *v);
    for(unsigned j=0;j<n;++j){v[j]=input[0];for(unsigned k=1;k<n;++k)v[j]+=2*input[k]*cos(PI*k*(j+.5)/n);}
}
static const double gain_sigma[5]={.310,.201,.168,.145,.132};
static const double sigma[9]={.307,.241,.207,.190,.179,.173,.165,.170,.170};
static const double factor[11]={0,1.20,.85,.65,.40,.28,.15,.08,.04,.02,.01};
static double previous[56];static unsigned previous_L=30;
static void reference_reset(void){memset(previous,0,sizeof previous);previous_L=30;}
static void reference_decode(unsigned L,const unsigned p[59],double result[56])
{
    unsigned widths[59],offset=0,m=8;double gain[6],predicted[56],mean=0;
    bits_for(L,widths);gain[0]=gain_q11[p[2]]/2048.0;
    for(unsigned i=0;i<5;++i)gain[i+1]=gain_sigma[i]*factor[widths[i+3]]*((double)p[i+3]-(1u<<(widths[i+3]-1))+.5);
    inverse(gain,6);
    for(unsigned block=0;block<6;++block){
        unsigned n=lengths(L,block);result[offset]=gain[block];
        for(unsigned k=1;k<n;++k,++m)result[offset+k]=widths[m]?sigma[k-1]*factor[widths[m]]*((double)p[m]-(1u<<(widths[m]-1))+.5):0;
        inverse(result+offset,n);offset+=n;
    }
    for(unsigned l=1;l<=L;++l){
        double position=(double)previous_L*l/L,fraction=position-floor(position);unsigned k=(unsigned)position;
        double a=k?previous[(k<previous_L?k:previous_L)-1]:0;
        double b=previous[(k+1<previous_L?k+1:previous_L)-1];
        predicted[l-1]=(1-fraction)*a+fraction*b;mean+=predicted[l-1]/L;
    }
    double rho=L<=15?.4:L<=24?.03*L-.05:.7;
    for(unsigned l=0;l<L;++l){
        result[l]+=rho*(predicted[l]-mean);
        if(result[l]<-16)result[l]=-16;
        if(result[l]>32767/2048.0)result[l]=32767/2048.0;
        previous[l]=result[l];
    }
    previous_L=L;
}
static void worked_layout(void)
{
    /* Section10 Tables8–10, conditional L16 layout. Use valid pitch code32:
     * the example's stated35.125-sample pitch is inconsistent with Eq47.
     * The single duplicated Table10 bit is b16:0, not b16:1 already at u5:2. */
    static const uint8_t pairs[88][2]={
      {0,7},{0,6},{0,5},{0,4},{0,3},{0,2},{2,5},{2,4},{2,3},{3,5},{4,5},{5,5},
      {8,5},{9,5},{3,4},{4,4},{5,4},{6,4},{7,4},{8,4},{9,4},{10,4},{3,3},{4,3},
      {5,3},{6,3},{7,3},{8,3},{9,3},{10,3},{11,3},{12,3},{3,2},{4,2},{5,2},{6,2},
      {7,2},{8,2},{9,2},{10,2},{11,2},{12,2},{13,2},{14,2},{15,2},{16,2},{3,1},{4,1},
      {1,5},{1,4},{1,3},{1,2},{1,1},{1,0},{2,2},{2,1},{5,1},{6,1},{7,1},
      {8,1},{9,1},{10,1},{11,1},{12,1},{13,1},{14,1},{15,1},{16,1},{17,1},{3,0},
      {4,0},{5,0},{6,0},{7,0},{8,0},{9,0},{10,0},{11,0},{12,0},{13,0},{14,0},
      {15,0},{16,0},{17,0},{2,0},{0,1},{0,0},{18,0}};
    for(unsigned bit=0;bit<88;++bit){
        unsigned p[59]={0};uint16_t words[8];p[pairs[bit][0]]=1u<<pairs[bit][1];reference_pack(16,p,words);
        unsigned at=0;for(unsigned w=0;w<8;++w)for(unsigned b=imbe_word_widths[w];b>0;--b,++at)assert(((words[w]>>(b-1))&1u)==(at==bit));
    }
    const unsigned expected_gain_bits[5]={6,6,6,5,5},expected_hoc_bits[10]={6,6,5,4,4,3,3,3,3,2};unsigned b[59];bits_for(16,b);
    for(unsigned i=0;i<5;++i)assert(b[i+3]==expected_gain_bits[i]);
    for(unsigned i=0;i<10;++i)assert(b[i+8]==expected_hoc_bits[i]);
    /* End-to-end encode has an independent model at reconstruction midpoints,
     * comfortably away from quantizer thresholds. This pins outbound ordering. */
    for(unsigned trial=0;trial<128;++trial){
        unsigned p[59]={0};uint16_t expected_words[8],got[8];double values[56];imbe_model model={0};imbe_quant_state state;
        p[0]=32+trial%4;p[1]=random_word()&63u;p[2]=32;
        for(unsigned m=3;m<=17;++m)p[m]=(1u<<(b[m]-1))+(random_word()%(b[m]<=2?2u:3u));
        p[18]=trial&1u;reference_pack(16,p,expected_words);reference_reset();reference_decode(16,p,values);
        assert(imbe_pitch_decode(p[0],&model));
        for(unsigned k=0;k<6;++k)model.voiced|=(uint16_t)(((p[1]>>(5-k))&1u)<<k);
        for(unsigned l=0;l<16;++l)model.log_amplitude[l]=(int16_t)lround(values[l]*2048);
        imbe_quant_init(&state);assert(imbe_quant_encode(&state,&model,p[18],got));assert(memcmp(got,expected_words,sizeof got)==0);
    }
}
int main(void)
{
    worked_layout();imbe_model model={0};
    for(unsigned code=0;code<208;++code){
        assert(imbe_pitch_decode(code,&model));unsigned inner=(2*code+81)/8;
        assert(model.L==9254*inner/10000);assert(model.K==(model.L>36?12:(model.L+2)/3));
        uint64_t den=2*code+79;assert(model.frequency_q32==((UINT64_C(1)<<34)+den/2)/den);
        for(unsigned l=0;l<model.L;++l)assert(imbe_voicing_group(l,model.L)==(l/3<12?l/3:11));
    }
    for(unsigned code=208;code<256;++code){imbe_model before=model;assert(!imbe_pitch_decode(code,&model));assert(!memcmp(&before,&model,sizeof model));}
    imbe_quant_state state;imbe_quant_init(&state);reference_reset();double worst=0;
    for(unsigned trial=0;trial<3000;++trial){
        unsigned p[59]={0},bits[59];uint16_t words[8];double expected[56];assert(imbe_pitch_decode(random_word()%208,&model));bits_for(model.L,bits);p[0]=model.pitch_code;
        for(unsigned m=1;m<=model.L+2;++m)p[m]=random_word()&((1u<<bits[m])-1u);
        unsigned L=model.L;reference_pack(L,p,words);reference_decode(L,p,expected);assert(imbe_quant_decode(&state,words,&model));
        for(unsigned l=0;l<L;++l){double error=fabs(model.log_amplitude[l]/2048.0-expected[l]);if(error>worst)worst=error;assert(error<.02);}
    }
    imbe_quant_state encoder={0},decoder={0};imbe_quant_init(&encoder);imbe_quant_init(&decoder);
    for(unsigned trial=0;trial<3000;++trial){
        imbe_model m={0},out={0};uint16_t words[8];assert(imbe_pitch_decode(random_word()%208,&m));m.voiced=(uint16_t)(random_word()&((1u<<m.K)-1u));
        for(unsigned l=0;l<m.L;++l){int32_t x=(int32_t)(random_word()&65535u);if(x>32767)x-=65536;m.log_amplitude[l]=(int16_t)x;}
        assert(imbe_quant_encode(&encoder,&m,trial&1u,words));assert(imbe_quant_decode(&decoder,words,&out));
        assert(m.pitch_code==out.pitch_code && m.voiced==out.voiced && m.frequency_q32==out.frequency_q32);
        assert(!memcmp(m.log_amplitude,out.log_amplitude,sizeof m.log_amplitude));
        assert(encoder.previous_L==decoder.previous_L && !memcmp(encoder.previous_log_amplitude,decoder.previous_log_amplitude,sizeof encoder.previous_log_amplitude));
    }
    for(unsigned trial=0;trial<10000;++trial){
        uint16_t words[8];imbe_model before=model;imbe_quant_state saved=state;
        for(unsigned w=0;w<8;++w)words[w]=(uint16_t)(random_word()&((1u<<imbe_word_widths[w])-1u));
        if(trial%4==0)words[trial%8]|=(uint16_t)(1u<<imbe_word_widths[trial%8]);
        if(!imbe_quant_decode(&state,words,&model)){assert(!memcmp(&model,&before,sizeof model));assert(state.previous_L==saved.previous_L && !memcmp(state.previous_log_amplitude,saved.previous_log_amplitude,sizeof state.previous_log_amplitude));}
    }
    printf("quant:208 pitch codes,88 worked positions,128 encode vectors,3000 math frames,3000 predictor pairs,10000 hostile; max log2 error %.7f\n",worst);
    return 0;
}

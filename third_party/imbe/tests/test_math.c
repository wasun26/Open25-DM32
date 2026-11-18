/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Open25 contributors
 * Host-only mathematical references; no recorded audio or codec oracle. */
#include "imbe_model.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
static uint32_t seed=0x251102u;
static uint32_t rnd(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
int main(void){
 assert(imbe_log2_u32_q11(0)==-32768);
 assert(imbe_isqrt(0)==0 && imbe_isqrt(UINT32_MAX)==65535);
 assert(imbe_exp2_q11(INT32_MAX)==UINT32_MAX && imbe_exp2_q11(INT32_MIN)==0);
 for(unsigned test=0;test<10000;++test){uint32_t phase=rnd();double expected=32768*sin(6.2831853071795864769*phase/4294967296.0);assert(fabs(imbe_sine_q15(phase)-expected)<5);}
 double maxlog=0,maxexp=0,maxfft=0,maxifft=0;
 unsigned i,k,n;
 for(i=0;i<100000;++i){uint32_t x=rnd()|1u;double e=fabs(imbe_log2_u32_q11(x)-2048*log2((double)x));if(e>maxlog)maxlog=e;assert(e<1.6);uint32_t r=imbe_isqrt(x);assert((uint64_t)r*r<=x && (uint64_t)(r+1)*(r+1)>x);}
 for(int q=-32768;q<40000;++q){double expected=4096*exp2(q/2048.0);double e=fabs(imbe_exp2_q11(q)-expected);if(e/(1+expected)>maxexp)maxexp=e/(1+expected);assert(e<1+expected*.0001);}
 for(unsigned test=0;test<80;++test){int16_t b[258],input[256];for(i=0;i<256;++i)input[i]=b[i]=(int16_t)(rnd()>>16);b[256]=b[257]=0;int shift=imbe_rfft256(b);
 for(k=0;k<129;++k){double re=0,im=0;for(n=0;n<256;++n){double a=6.2831853071795864769*k*n/256;re+=input[n]*cos(a);im-=input[n]*sin(a);}double err=hypot(ldexp(b[2*k],shift)-re,ldexp(b[2*k+1],shift)-im)/ldexp(1,shift);if(err>maxfft)maxfft=err;assert(err<30);}
 int inverse=imbe_irfft256(b);for(n=0;n<256;++n){double err=fabs(ldexp(b[n],shift+inverse)-input[n]);if(err>maxifft)maxifft=err;assert(err<250);}
 }
 for(unsigned lg=2;lg<=8;++lg){unsigned count=1u<<lg;int16_t b[512],input[512];for(i=0;i<2*count;++i)input[i]=b[i]=(int16_t)(rnd()>>16);int shift=imbe_cfft(b,lg);for(k=0;k<count;++k){double re=0,im=0;for(n=0;n<count;++n){double a=6.2831853071795864769*k*n/count;re+=input[2*n]*cos(a)+input[2*n+1]*sin(a);im+=input[2*n+1]*cos(a)-input[2*n]*sin(a);}double e=hypot(ldexp(b[2*k],shift)-re,ldexp(b[2*k+1],shift)-im)/ldexp(1,shift);assert(e<30);}}
 printf("math: logQ11 max %.4f; exp error/(1+Q12) %.8f; FFT mantissa %.4f; roundtrip raw %.4f\n",maxlog,maxexp,maxfft,maxifft);
 return 0;
}

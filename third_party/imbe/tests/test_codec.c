/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Open25 contributors
 * Synthesized input exercises the public C ABI; no recorded audio or oracle. */
#include "dm32_imbe.h"
#include "encoder.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define GUARD UINT64_C(0xa5a5a5a5a5a5a5a5)
struct guarded {uint64_t before[2];dm32_imbe_context context;uint64_t after[2];};
static uint32_t seed=0x10225u;
static uint32_t random_word(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static void initialize(struct guarded *p)
{
    memset(p,0xa5,sizeof *p);dm32_imbe_init(&p->context);
}
static void guards(const struct guarded *p)
{
    for(unsigned i=0;i<2;++i)assert(p->before[i]==GUARD && p->after[i]==GUARD);
}
static void input_frame(int16_t pcm[160],unsigned frame)
{
    for(unsigned i=0;i<160;++i){double t=(frame*160+i)/8000.0;pcm[i]=(int16_t)lround(4000*sin(2*3.14159265358979323846*220*t)*(.75+.25*sin(2*3.14159265358979323846*3.1*t)));}
}
static unsigned word_width(unsigned w){return w<4?12:w<7?11:7;}
static void pack_tests(void)
{
    for(unsigned one=0;one<88;++one){
        uint16_t words[8]={0},back[8];uint8_t bytes[11],unaligned[12];unsigned at=0;
        for(unsigned w=0;w<8;++w)for(unsigned bit=word_width(w);bit>0;--bit,++at)if(at==one)words[w]=(uint16_t)(1u<<(bit-1));
        assert(dm32_imbe_pack(words,bytes));
        for(unsigned b=0;b<11;++b)assert(bytes[b]==(b==one/8?(1u<<(7-one%8)):0u));
        memcpy(unaligned+1,bytes,11);dm32_imbe_unpack(unaligned+1,back);assert(!memcmp(words,back,sizeof words));
    }
    for(unsigned trial=0;trial<10000;++trial){
        uint16_t words[8],back[8];uint8_t bytes[11];
        for(unsigned w=0;w<8;++w)words[w]=(uint16_t)(random_word()&((1u<<word_width(w))-1u));
        assert(dm32_imbe_pack(words,bytes));dm32_imbe_unpack(bytes,back);assert(!memcmp(words,back,sizeof words));
    }
    for(unsigned w=0;w<8;++w){uint16_t words[8]={0};uint8_t bytes[11];memset(bytes,0x5a,sizeof bytes);words[w]=(uint16_t)(1u<<word_width(w));assert(!dm32_imbe_pack(words,bytes));for(unsigned i=0;i<11;++i)assert(bytes[i]==0x5a);}
}
int main(void)
{
    assert(dm32_imbe_state_bytes()==DM32_IMBE_CONTEXT_BYTES);
    assert(DM32_IMBE_ENCODER_FLUSH_FRAMES==IMBE_ENCODER_TAIL_FRAMES);
    pack_tests();struct guarded encoder,decoder;initialize(&encoder);initialize(&decoder);
    uint16_t valid[8]={0};int16_t pcm[160],saved[160],audio[160];unsigned active=0;
    for(unsigned frame=0;frame<500+DM32_IMBE_ENCODER_FLUSH_FRAMES;++frame){
        if(frame<500)input_frame(pcm,frame);else memset(pcm,0,sizeof pcm);
        memcpy(saved,pcm,sizeof saved);assert(dm32_imbe_encode(&encoder.context,pcm,valid));assert(!memcmp(pcm,saved,sizeof pcm));
        assert((valid[7]&1u)==(frame&1u));
        for(unsigned w=0;w<8;++w)assert(valid[w]<(1u<<word_width(w)));
        assert((((valid[0]>>4)&252u)|((valid[7]>>1)&3u))<208u);
        dm32_imbe_context copy=decoder.context;int16_t duplicate[160];
        assert(dm32_imbe_decode(&decoder.context,valid,audio));assert(dm32_imbe_decode(&copy,valid,duplicate));
        assert(!memcmp(audio,duplicate,sizeof audio));assert(!memcmp(&decoder.context,&copy,sizeof copy));
        for(unsigned i=0;i<160;++i)if(audio[i]!=0){++active;break;}
        guards(&encoder);guards(&decoder);
    }
    assert(active>450);
    for(unsigned trial=0;trial<1000;++trial){
        uint16_t bad[8];memcpy(bad,valid,sizeof bad);
        if(trial%2)bad[trial%8]=(uint16_t)(1u<<word_width(trial%8));
        else{unsigned pitch=208+trial%48;bad[0]=(uint16_t)((bad[0]&~0xfc0u)|((pitch&252u)<<4));bad[7]=(uint16_t)((bad[7]&~6u)|((pitch&3u)<<1));}
        dm32_imbe_context before=decoder.context;memset(audio,0x5a,sizeof audio);
        assert(!dm32_imbe_decode(&decoder.context,bad,audio));assert(!memcmp(&before,&decoder.context,sizeof before));for(unsigned i=0;i<160;++i)assert(audio[i]==0);
        int16_t other[160];assert(dm32_imbe_decode(&decoder.context,valid,audio));assert(dm32_imbe_decode(&before,valid,other));assert(!memcmp(audio,other,sizeof audio));
    }
    /* Role reset produces the same first encoded frame as a fresh context. */
    struct guarded fresh;initialize(&fresh);uint16_t expected[8],actual[8];input_frame(pcm,0);
    assert(dm32_imbe_encode(&fresh.context,pcm,expected));assert(dm32_imbe_decode(&encoder.context,valid,audio));assert(dm32_imbe_encode(&encoder.context,pcm,actual));assert(!memcmp(actual,expected,sizeof actual));
    for(unsigned frame=0;frame<1000;++frame){
        for(unsigned i=0;i<160;++i){int32_t x=(int32_t)(random_word()&65535u);if(x>32767)x-=65536;pcm[i]=(int16_t)x;}
        assert(dm32_imbe_encode(&encoder.context,pcm,valid));assert(dm32_imbe_decode(&decoder.context,valid,audio));guards(&encoder);guards(&decoder);
    }
    assert(!dm32_imbe_encode(NULL,pcm,valid));assert(!dm32_imbe_decode(NULL,valid,audio));assert(!dm32_imbe_pack(NULL,(uint8_t *)audio));
    puts("codec:88 packing positions,10000 pack roundtrips,stream/drain,clones,1000 invalid-state checks,role reset,1000 full-range frames");
    return 0;
}

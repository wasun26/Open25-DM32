/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Open25 contributors
 * Synthesized-model functional, range, cloning and guard checks. */
#include "decoder.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t random_state=0x25c0d3u;
static uint32_t random_word(void)
{
    uint32_t x=random_state;
    x^=x<<13;x^=x>>17;x^=x<<5;random_state=x;return x;
}

int main(void)
{
    struct {uint32_t before[4];imbe_decoder decoder;uint32_t after[4];} guarded;
    imbe_decoder copied;
    imbe_model model,saved;
    int16_t pcm[160],duplicate[160];
    unsigned code,frame,i,frames=0,active=0;
    uint64_t total_power=0;
    memset(&guarded,0xa5,sizeof guarded);
    imbe_decoder_init(&guarded.decoder);
    assert(_Alignof(imbe_decoder)==4);
    /* Independent host-width Eq117/141 oracle verifies the n=1..56 index
     * mapping and the phase formula without importing a codec source. */
    assert(imbe_pitch_decode(207,&model));model.voiced=0;
    for(i=0;i<model.L;++i)model.log_amplitude[i]=6*2048;
    imbe_synthesize(&guarded.decoder,&model,pcm);
    {
        uint32_t noise=3147;
        for(unsigned draw=1;draw<=161;++draw) {
            noise=(171u*noise+11213u)%53125u;
            if(draw>=106u) {
                unsigned harmonic=draw-106u;
                uint32_t phase_noise=(uint32_t)(((uint64_t)noise<<32)/53125u)-0x80000000u;
                uint32_t expected=guarded.decoder.fundamental_phase*(harmonic+1u);
                if(harmonic+1u>model.L/4u)expected+=phase_noise;
                assert(guarded.decoder.phase[harmonic]==expected);
            }
        }
    }
    imbe_decoder_init(&guarded.decoder);
    for(code=0;code<208;++code) {
        assert(imbe_pitch_decode(code,&model));
        assert(model.L>=9 && model.L<=56 && model.K>=3 && model.K<=12);
        for(frame=0;frame<24;++frame) {
            uint64_t sum=0,power=0;
            model.voiced=(uint16_t)(frame<8?(1u<<model.K)-1u:frame<16?0:random_word()&((1u<<model.K)-1u));
            for(i=0;i<model.L;++i) {
                /* Low amplitudes, ordinary speech-like envelopes and hostile
                 * high levels exercise all fixed-point boundary decisions. */
                int level=frame%6==0?-8:frame%6==1?-2:frame%6==2?0:frame%6==3?6:frame%6==4?10:15;
                model.log_amplitude[i]=(int16_t)(level*2048-(int)(i*41));
            }
            saved=model;
            copied=guarded.decoder;
            imbe_synthesize(&guarded.decoder,&model,pcm);
            imbe_synthesize(&copied,&model,duplicate);
            assert(!memcmp(pcm,duplicate,sizeof pcm));
            assert(!memcmp(&guarded.decoder,&copied,sizeof copied));
            assert(!memcmp(&model,&saved,sizeof model));
            for(i=0;i<4;++i)assert(guarded.before[i]==0xa5a5a5a5u && guarded.after[i]==0xa5a5a5a5u);
            for(i=0;i<56;++i)sum+=guarded.decoder.amplitude_q12[i];
            assert(sum<=20482ull*4096);
            for(i=0;i<160;++i)power+=(uint64_t)((int32_t)pcm[i]*pcm[i]);
            total_power+=power;active+=power!=0;++frames;
        }
    }
    assert(active>frames*9/10);
    memset(pcm,0x5a,sizeof pcm);model.L=0;saved=model;copied=guarded.decoder;
    imbe_synthesize(&guarded.decoder,&model,pcm);
    for(i=0;i<160;++i)assert(pcm[i]==0);
    assert(!memcmp(&guarded.decoder,&copied,sizeof copied));
    assert(!memcmp(&model,&saved,sizeof model));
    printf("DECODER_LOCAL frames=%u active=%u state_bytes=%zu total_power=%llu\n",frames,active,sizeof(imbe_decoder),(unsigned long long)total_power);
    return 0;
}

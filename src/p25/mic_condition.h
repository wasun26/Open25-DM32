/* SPDX-License-Identifier: MIT */
#ifndef P25_MIC_CONDITION_H
#define P25_MIC_CONDITION_H
#include <stdint.h>

/* Conventional microphone conditioning is owned by Classic; refresh Next's
 * identical copy when this file changes. No codec or synthetic-tail filtering. */
typedef struct {
    int32_t low_q8;
    uint16_t fade;
    int16_t last;
} p25_mic_condition;

static inline void p25_mic_condition_init(p25_mic_condition *s)
{
    *s=(p25_mic_condition){INT32_MIN,224,0};
}

static inline void p25_mic_condition_block(p25_mic_condition *s,int16_t pcm[160],int keyed)
{
    /* Captured startup settles in 20 ms. RF switching disturbs the containing
     * block and the next 10 ms. Fade in over 8 ms; bridge RF fade-out over 4 ms
     * using the preceding output, so the disturbance cannot enter the bridge. */
    if(keyed)s->fade=304;
    if(s->low_q8==INT32_MIN)s->low_q8=pcm[159]*256;
    for(unsigned i=0;i<160;i++) {
        s->low_q8+=(pcm[i]*256-s->low_q8)>>(s->fade>64?3:6);
        int32_t sample=pcm[i]-(s->low_q8>>8);
        if(sample>INT16_MAX)sample=INT16_MAX;
        if(sample<INT16_MIN)sample=INT16_MIN;
        if(s->fade) {
            if(keyed && i<32)sample=(s->last*(31-(int)i))>>5;
            else if(s->fade>64)sample=0;
            else sample=(sample*(64-s->fade))>>6;
            s->fade--;
        }
        pcm[i]=(int16_t)sample;
    }
    s->last=pcm[159];
}
#endif

/* SPDX-License-Identifier: MIT */
/* Real live caller + voice/CAI/codec: a one-block microphone release must
 * drain the codec before minimal LDU padding, rather than pad at both tiers. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "p25_lab.c"
static uint32_t test_clock;
static unsigned critical,blocks,go_calls,stops;
static int finished_tx;
static uint8_t transmitted[32][36];
void audio_test_enter(void){critical++;}
void audio_test_exit(void){assert(critical);critical--;}
uint32_t hal_millis(void){return test_clock;}
uint32_t hal_micros(void){return test_clock*1000u;}
void vTaskDelay(TickType_t ms){test_clock+=ms;assert(test_clock<1000);}
bool input_ptt_down(void){return !live_released;}
static bool tail_audio_start(audio_block_cb_t cb,void *ctx)
{
    int16_t mic[160],speaker[160];
    for(unsigned i=0;i<160;i++)mic[i]=(int16_t)((i%40)*400-8000);
    cb(mic,speaker,ctx);live_released=1;return true;
}
static void tail_audio_stop(void){stops++;}
static void mic_route(bool on){(void)on;}
static const hal_audio_t test_audio={.start=tail_audio_start,.stop=tail_audio_stop};
const board_t board={.audio=&test_audio,.native_mic_route=mic_route};
int native_lab_live_poll(uint32_t sid,uint8_t *state,uint8_t *reason,uint8_t *queued)
{
    assert(sid==42);*queued=0;
    *state=finished_tx?NT_STOPPED:go_calls?NT_TX:NT_ARMED;
    *reason=finished_tx?NT_TX_COMPLETE:NT_OK;return 1;
}
int native_lab_live_block(uint32_t sid,const uint8_t block[36],int last)
{
    assert(sid==42&&blocks<32&&!finished_tx);
    memcpy(transmitted[blocks++],block,36);finished_tx=last;return 1;
}
int native_lab_live_go(uint32_t sid){assert(sid==42);go_calls++;return 1;}
static uint32_t release_at;
void native_lab_voice_stop(uint32_t sid){assert(sid==42);release_at=test_clock+600;}
int native_lab_voice_release(uint32_t sid){assert(sid==42);return (int32_t)(test_clock-release_at)>=0;}

static void check_mic_conditioning(void)
{
    struct { uint32_t before; p25_mic_condition state; uint32_t after; } guard;
    int16_t pcm[160],speaker[160];
    for(int bias=-32768;bias<=32767;bias+=65535) {
        guard.before=guard.after=0x12345678;
        p25_mic_condition_init(&guard.state);
        for(unsigned frame=0;frame<20;frame++) {
            for(unsigned i=0;i<160;i++)pcm[i]=(int16_t)bias;
            p25_mic_condition_block(&guard.state,pcm,frame==10);
            for(unsigned i=0;i<160;i++)assert(!pcm[i]);
        }
        assert(guard.before==0x12345678 && guard.after==0x12345678);
    }
    p25_mic_condition_init(&guard.state);
    for(unsigned frame=0;frame<100;frame++) {
        for(unsigned i=0;i<160;i++)pcm[i]=i&1?INT16_MIN:INT16_MAX;
        p25_mic_condition_block(&guard.state,pcm,frame%7==0);
    }
    /* The IRQ marks the next captured block even if earlier blocks remain
     * queued. Raw input/peak diagnostics and speaker silence stay unchanged. */
    p25_audio_fifo_reset(&fifo);live_capturing=1;live_released=stop_requested=0;
    live_mic_frames=0;live_key_frame=UINT32_MAX;
    for(unsigned i=0;i<160;i++)pcm[i]=2496;
    mic_block(pcm,speaker,NULL);p25_lab_live_key();mic_block(pcm,speaker,NULL);
    assert(live_key_frame==1 && live_mic_frames==2 && !stop_requested);
    for(unsigned i=0;i<160;i++)assert(!speaker[i] && pcm[i]==2496);
    assert(p25_audio_fifo_take(&fifo,pcm) && p25_audio_fifo_take(&fifo,pcm));
    live_capturing=0;p25_lab_live_key();assert(live_key_frame==1);
    p25_audio_fifo_reset(&fifo);
}

int main(void)
{
    check_mic_conditioning();
    const p25_call call={.nac=0x293,.talkgroup=42,.source_id=123456};
    p25_voice_init(&voice,call.nac,call.talkgroup,NULL,NULL);
    live_released=stop_requested=0;
    assert(p25_lab_set_channel(0xF7E,call.nac,call.talkgroup,call.source_id,1));
    live_sid=42;assert(live_run());
    assert(finished_tx&&go_calls==1&&stops==2&&!critical);
    assert(test_clock>=release_at);
    assert(live_mic_frames==1&&live_encoded==1&&!live_overflows&&!voice.fault);
    /* Three carrier lead records,333 CAI bytes in ten records, one seal.
     * Caller-side LDU padding would instead produce a second voice LDU. */
    assert(blocks==14);
    for(unsigned i=0;i<36;i++)assert(transmitted[0][i]==0x77&&transmitted[13][i]==0x77);
    const uint8_t *air=&transmitted[3][0];p25_decoded decoded;
    assert(!p25_decode_frame(air,99,&decoded)&&decoded.nid.duid==P25_DUID_HDU);
    assert(decoded.nid.nac==call.nac && channel_rx_nac==0xF7E);
    assert(!p25_decode_frame(air+99,216,&decoded)&&decoded.nid.duid==P25_DUID_LDU1);
    assert(!p25_decode_frame(air+315,18,&decoded)&&decoded.nid.duid==P25_DUID_TDU);
    puts("PASS live caller: one microphone block, one codec tail, one LDU, final TDU before native seal");
    return 0;
}

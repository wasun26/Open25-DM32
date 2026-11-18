/* SPDX-License-Identifier: MIT */
/* Generated upstream PCM driver + real native controller/IRQ port. Only the
 * RTOS, register window and modem interrupt events are host doubles. */
#include "config.h"
#include "hrc_native_port.h"
#include "platform.h"
#include "FreeRTOS.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define PLATFORM_HRC7000_H
#define HRC_DMR_GLUE_H
#define pdFALSE 0
#define portYIELD_FROM_ISR(x) ((void)(x))

hrc_irq_fn hrc_irq_table[64];
uint32_t mock_pic[2],mock_tmr[7],mock_pic_history;
static uint32_t now_us,irq_depth,periods[7],acks[7];
static unsigned key_requests,pa_keys,rf_offs;
uint32_t hrc_irq_save(void) { return irq_depth++; }
void hrc_irq_restore(uint32_t prior) { assert(irq_depth==prior+1);irq_depth=prior; }
void hrc_irq_enable(uint8_t n,bool on)
{ if(on)mock_pic[n/32]&=~(1u<<(n&31));else mock_pic[n/32]|=1u<<(n&31); }
void hrc_irq_register(uint8_t n,hrc_irq_fn fn)
{ hrc_irq_enable(n,false);hrc_irq_table[n]=fn; }
void hrc_timer_ack(unsigned n) { assert(n<7);acks[n]++; }
void hrc_timer_stop(unsigned n) { assert(n<7);mock_tmr[n]=0; }
void hrc_timer_periodic(unsigned n,uint32_t us)
{ assert(n<7);periods[n]=us;mock_tmr[n]=3; }
uint32_t hrc_micros(void) { return now_us; }
static void hrc_barrier(void) { }
static bool hrc_dmr_tx_active(void) { return false; }
void hrc7000_set_crit(hrc7000_t *m,uint32_t (*enter)(void),void (*leave)(uint32_t))
{ m->crit_enter=enter;m->crit_exit=leave; }
void hrc7000_audio_bits(hrc7000_t *m,uint32_t set,uint32_t clear)
{ m->audio_ctl=(m->audio_ctl&~clear)|set; }
void vTaskNotifyGiveFromISR(TaskHandle_t task,BaseType_t *woken) { (void)task;(void)woken; }
BaseType_t xTaskCreate(void (*fn)(void *),const char *name,unsigned words,void *arg,unsigned priority,TaskHandle_t *task)
{ (void)fn;(void)name;(void)words;(void)arg;(void)priority;*task=(void *)1;return pdPASS; }
uint32_t ulTaskNotifyTake(BaseType_t clear,TickType_t wait) { (void)clear;(void)wait;return 0; }
void xTaskNotifyGive(TaskHandle_t task) { (void)task; }
#include "../../build/radiofoundry-p25/platform/hrc7000/audio_pcm.c"

static hn_chip chip;
static void previous_irq(void) { assert(0); }
/* Native modem MMIO is covered separately by test_port; these deliver the
 * same controller events through the real installed interrupt handlers. */
void hn_sys(hn_chip *d,uint32_t us) { nt_boundary(d->test,NT_IRQ_PHY,us); }
void hn_slot(hn_chip *d,uint8_t source,uint32_t us) { nt_boundary(d->test,source,us); }
void hn_rftx(hn_chip *d,uint32_t us) { nt_rf_tx(d->test,us); }
static int native_start(void *ctx,nt_test *t)
{ hn_chip *d=ctx;d->test=t;return hn_port_on(d,t); }
static int write_raw(void *ctx,const uint8_t raw[NT_BYTES]) { (void)ctx;(void)raw;return 1; }
static void cancel_raw(void *ctx) { (void)ctx; }
static void request_pa(void *ctx) { (void)ctx;key_requests++;mock_tmr[TIMER_PA]=1; }
static void key_pa(void *ctx) { (void)ctx;pa_keys++; }
static void native_quiet(void *ctx) { (void)ctx;rf_offs++;hn_port_quiet(); }
static int native_restore(void *ctx) { (void)ctx;hn_port_restore();return 1; }
static uint32_t native_lock(void *ctx) { (void)ctx;return hrc_irq_save(); }
static void native_unlock(void *ctx,uint32_t s) { (void)ctx;hrc_irq_restore(s); }
static void microphone(const int16_t *mic,int16_t *spk,void *ctx)
{ (void)mic;(void)ctx;memset(spk,0,AUDIO_BLOCK_SAMPLES*sizeof *spk); }
static void tick(unsigned timer,uint32_t us)
{
    assert(mock_tmr[timer]&1u);
    assert(!(mock_pic[timer/32]&(1u<<(timer&31))));
    now_us=us;hrc_irq_table[timer]();
}
static void load_tx(nt_test *t)
{
    uint8_t raw[NT_BYTES]={0};
    assert(nt_arm_live(t,145500000));
    for(unsigned i=0;i<3;i++)assert(nt_load(t,t->session,i,raw));
}
static void assert_pcm(hrc_irq_fn refill)
{
    assert(mock_tmr[TIMER_PCM] && periods[TIMER_PCM]==2000);
    assert(hrc_irq_table[TIMER_PCM]==refill);
    unsigned before=acks[TIMER_PCM];
    tick(TIMER_PCM,now_us+2000);
    assert(acks[TIMER_PCM]==before+1);
}
int main(void)
{
    assert(TIMER_PCM==6 && HN_GUARD_TIMER==4);
    uint32_t regs[1024]={0};uint8_t buf[4096]={0};
    hrc7000_t modem_window={.regs=regs,.buf=buf};
    for(unsigned i=0;i<64;i++)hrc_irq_table[i]=previous_irq;
    hrc_audio_pcm_attach(&modem_window,NULL);
    const nt_ops ops={.ctx=&chip,.start=native_start,.write=write_raw,.cancel_send=cancel_raw,
        .request_key=request_pa,.pa_on=key_pa,.quiet=native_quiet,.restore=native_restore,
        .lock=native_lock,.unlock=native_unlock};
    nt_test t;nt_init(&t,&ops);

    /* Physical PTT starts microphone capture before native PHY setup. The
     * published r12 guard on timer 6 refuses this exact startup ordering. */
    assert(hrc_audio_pcm.start(microphone,NULL));
    hrc_irq_fn refill=hrc_irq_table[TIMER_PCM];
    assert_pcm(refill);
    load_tx(&t);assert(nt_begin(&t,t.session,now_us));
    assert(t.state==NT_TX && periods[HN_GUARD_TIMER]==1000);
    tick(TIMER_PCM,now_us+2000);
    now_us+=1000;hrc_irq_table[IRQ_MODEM_RF_TX]();
    assert(key_requests==1 && !pa_keys);
    now_us+=28000;hrc_irq_table[IRQ_TIMER3]();
    assert(pa_keys==1 && t.keyed && !mock_tmr[TIMER_PA]);

    /* Stop/restart PCM while a P25 call owns its guard. The guard must keep
     * running, retain its handler and independently shut RF off on expiry. */
    hrc_irq_fn guard_irq=hrc_irq_table[HN_GUARD_TIMER];
    hrc_audio_pcm.stop();
    assert(!mock_tmr[TIMER_PCM] && mock_tmr[HN_GUARD_TIMER]);
    assert(hrc_audio_pcm.start(microphone,NULL));
    assert(hrc_irq_table[HN_GUARD_TIMER]==guard_irq);
    assert_pcm(refill);
    tick(HN_GUARD_TIMER,t.ping_us+500000);
    assert(t.state==NT_FAULT && t.reason==NT_LEASE && rf_offs==1);
    assert(nt_cleanup(&t) && !t.owned && !mock_tmr[HN_GUARD_TIMER]);
    assert_pcm(refill); /* native cleanup cannot stop/replace PCM's timer */

    /* Re-key after cleanup; a failed first call cannot poison the next PTT. */
    assert(nt_reset(&t));load_tx(&t);
    assert(nt_begin(&t,t.session,now_us) && t.state==NT_TX);
    assert(nt_stop(&t,t.session,NT_USER_STOP) && nt_cleanup(&t));
    assert_pcm(refill);hrc_audio_pcm.stop();

    /* Receive owns its guard before speaker playback starts, the opposite
     * ordering. PCM startup and shutdown cannot replace or stop that guard. */
    assert(nt_reset(&t) && nt_arm_live_rx(&t,145500000));
    assert(nt_begin(&t,t.session,now_us) && t.state==NT_RX);
    guard_irq=hrc_irq_table[HN_GUARD_TIMER];
    assert(hrc_audio_pcm.start(microphone,NULL));
    assert(hrc_irq_table[HN_GUARD_TIMER]==guard_irq);
    assert_pcm(refill);
    hrc_audio_pcm.stop();assert(mock_tmr[HN_GUARD_TIMER]);
    assert(hrc_audio_pcm.start(microphone,NULL));
    assert(nt_stop(&t,t.session,NT_USER_STOP) && nt_cleanup(&t));
    assert_pcm(refill);hrc_audio_pcm.stop();
    assert(!mock_tmr[HN_GUARD_TIMER] && !mock_tmr[TIMER_PCM] && !irq_depth);
    puts("PASS real PCM/native coexistence: mic-before-TX, PA key, independent lease expiry, PCM restart, cleanup/re-key and RX-before-speaker");
    return 0;
}

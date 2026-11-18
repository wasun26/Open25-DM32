/* SPDX-License-Identifier: MIT */
/* Audio-task route callbacks at every hardware boundary of real TX setup. */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include "hal_radio.h"
#define CONFIG_FIRSTBOOT 0
#define FIRSTBOOT_FM_TX 0
#define CONFIG_OPERATOR_UI 0
#define CONFIG_P25_CODEPLUG 0
#define CONFIG_P25_DEMO 0
#define CONFIG_DEV_TX 0
#define PIN_MIC_PATH 0
#define PIN_SPK_SOURCE 1
#define UHF_FROM_HZ 280000000u
#define FD6818_BW_DMR 0
#define FD6818_STATE_RX 0
#define FD6818_STATE_TX 1
#define FD6818_TONE_NONE 0
#define C7_AUDIO_FM_PLAY 64
static bool rf_b,rf_unlocked;
static bool transmitting,have_rx,tx_uhf,tx_wide,dmr_keyed,pcm_on,rx_audio,aprs_listen,pins[2];
static uint32_t tx_hz,tx_drive,rx_hz;
static radio_mode_t mode;
static unsigned step,inject;
static void speaker_source(void);
static void hardware_step(void){if(++step==inject)speaker_source();}
static bool board_in_band(uint32_t hz,bool tx){(void)tx;return hz>=136000000&&hz<=174000000;}
static void hrc_pin_write(unsigned pin,bool on){pins[pin]=on;hardware_step();}
#define wake() hardware_step()
#define bf_cal_power_level(hz,power) ((void)(hz),(void)(power),hardware_step(),1u)
#define write_mod_bias(hz) ((void)(hz),hardware_step())
#define fd6818_set_freq(...) hardware_step()
#define hrc_dac_b_power(...) hardware_step()
#define lna(...) hardware_step()
#define fd6818_set_bandwidth(...) hardware_step()
#define fd6818_set_state(...) hardware_step()
#define fd6818_set_rx_tone(...) hardware_step()
#define fd6818_fm_tx_prepare(...) hardware_step()
#define fd6818_write(...) hardware_step()
#define bf_cal_valid() true
#define bf_cal_tx_byte(...) (hardware_step(),1u)
#define hal_delay_ms(...) hardware_step()
#define hrc7000_set_subtone(...) ((void)(tone),hardware_step())
#define hrc7000_fm_tx(...) hardware_step()
#define pa_key() hardware_step()
#define fm_bw_reg(...) 0
#define ring_play() hardware_step()
#define hrc_irq_save() 0u
#define hrc_irq_restore(s) ((void)(s))
#define hrc7000_audio_bits(...) ((void)play)


#include "../../build/audio-host/board_tx_route.inc"
int main(void){
 unsigned failed=0,cases=0;
 for(unsigned variant=0;variant<2;variant++)for(unsigned m=0;m<2;m++)for(unsigned at=0;at<32;at++){
  rf_b=variant!=0;rf_unlocked=false;
  mode=m?RADIO_MODE_DMR:RADIO_MODE_FM;transmitting=dmr_keyed=false;
  have_rx=true;rx_hz=146550000;pcm_on=rx_audio=aprs_listen=false;
  pins[PIN_MIC_PATH]=true;pins[PIN_SPK_SOURCE]=false;step=0;inject=at;
  bool ok=tx(146550000,RADIO_BW_12K5,1,NULL);cases++;
  if(!ok||!transmitting||pins[PIN_MIC_PATH]!=(mode==RADIO_MODE_FM)){
   printf("FAIL mode=%s callback_at=%u final_mic_analog=%u\n",m?"DMR":"FM",at,pins[PIN_MIC_PATH]);failed++;
  }
 }
 mode=RADIO_MODE_DMR;transmitting=false;pins[0]=true;step=0;inject=0;
 if(tx(100000000,RADIO_BW_12K5,1,NULL)||transmitting||!pins[0]||step)failed++;
 rf_unlocked=true;transmitting=false;step=0;
 if(tx(146550000,RADIO_BW_12K5,1,NULL)||transmitting||step)failed++;
 printf("cases=%u failures=%u\n",cases+2,failed);return failed?1:0;
}

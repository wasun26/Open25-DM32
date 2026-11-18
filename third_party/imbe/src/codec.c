/* SPDX-License-Identifier: MIT
 * Existing Open25 caller-owned C ABI for the standards-based full-rate core.
 * Caller-owned state, bounded fixed-point arithmetic and no dynamic allocation.
 */
#include "imbe_model.h"
#include "encoder.h"
#include "decoder.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>
/* The public union remains opaque to this implementation. */
typedef union dm32_imbe_context dm32_imbe_context;
typedef struct {
    uint32_t direction;
    imbe_quant_state quant;
    unsigned sync;
    union { imbe_encoder encoder; imbe_decoder decoder; } state;
} imbe_context;
static int valid(const void *p,unsigned alignment){return p && ((uintptr_t)p%alignment)==0;}
size_t dm32_imbe_state_bytes(void){return (sizeof(imbe_context)+7u)&~(size_t)7u;}
void dm32_imbe_init(dm32_imbe_context *p){if(valid(p,_Alignof(imbe_context)))memset(p,0,dm32_imbe_state_bytes());}
int dm32_imbe_encode(dm32_imbe_context *memory,int16_t pcm[160],uint16_t words[8]){
 imbe_context *s=(imbe_context *)memory;imbe_model model;
 if(!valid(s,_Alignof(imbe_context)) || !valid(pcm,2) || !valid(words,2))return 0;
 if(s->direction!=1){imbe_encoder_init(&s->state.encoder);imbe_quant_init(&s->quant);s->sync=0;s->direction=1;}
 if(!imbe_analyze(&s->state.encoder,pcm,&model))return 0;
 if(!imbe_quant_encode(&s->quant,&model,s->sync,words))return 0;
 s->sync^=1u;return 1;
}
int dm32_imbe_decode(dm32_imbe_context *memory,const uint16_t words[8],int16_t pcm[160]){
 imbe_context *s=(imbe_context *)memory;imbe_model model;
 if(!valid(s,_Alignof(imbe_context)) || !valid(pcm,2) || !valid(words,2))return 0;
 for(unsigned i=0;i<8;++i)if(words[i]>=(1u<<imbe_word_widths[i])){memset(pcm,0,320);return 0;}
 unsigned pitch=((words[0]>>4)&252u)|((words[7]>>1)&3u);
 if(pitch>207u){memset(pcm,0,320);return 0;}
 if(s->direction!=2){imbe_decoder_init(&s->state.decoder);imbe_quant_init(&s->quant);s->direction=2;}
 if(!imbe_quant_decode(&s->quant,words,&model)){memset(pcm,0,320);return 0;}
 imbe_synthesize(&s->state.decoder,&model,pcm);return 1;
}
int dm32_imbe_pack(const uint16_t words[8],uint8_t bytes[11]){
 unsigned at=0;if(!valid(words,2) || !bytes)return 0;
 for(unsigned i=0;i<8;++i)if(words[i]>=(1u<<imbe_word_widths[i]))return 0;
 memset(bytes,0,11);
 for(unsigned i=0;i<8;++i)for(unsigned bit=imbe_word_widths[i];bit;--bit){bytes[at>>3]|=(uint8_t)(((words[i]>>(bit-1))&1u)<<(7u-(at&7u)));++at;}
 return 1;
}
void dm32_imbe_unpack(const uint8_t bytes[11],uint16_t words[8]){
 unsigned at=0;if(!bytes || !valid(words,2))return;
 for(unsigned i=0;i<8;++i){unsigned value=0;for(unsigned bit=0;bit<imbe_word_widths[i];++bit){value=(value<<1)|((bytes[at>>3]>>(7u-(at&7u)))&1u);++at;}words[i]=(uint16_t)value;}
}

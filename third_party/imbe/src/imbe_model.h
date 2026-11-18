/* SPDX-License-Identifier: MIT
 * Original full-rate fixed-point IMBE model and mathematical interfaces. */
#ifndef OPEN25_IMBE_MODEL_H
#define OPEN25_IMBE_MODEL_H
#include <stdint.h>
#define IMBE_MAX_HARMONICS 56
extern const uint8_t imbe_word_widths[8];
/* Log amplitudes are log2(linear amplitude), Q11. Frequency is cycles/sample Q32.
 * The parameter mapper owns exact pitch->frequency/L/K and voicing-group rules. */
typedef struct {
    uint8_t pitch_code, L, K;
    uint16_t voiced; /* bit k is transmitted voicing group k, zero-based. */
    uint32_t frequency_q32;
    int16_t log_amplitude[IMBE_MAX_HARMONICS];
} imbe_model;
typedef struct {
    uint8_t previous_L;
    int16_t previous_log_amplitude[IMBE_MAX_HARMONICS];
} imbe_quant_state;
void imbe_quant_init(imbe_quant_state *state);
int imbe_pitch_decode(unsigned code, imbe_model *model);
unsigned imbe_voicing_group(unsigned harmonic_zero_based, unsigned L);
int imbe_quant_encode(imbe_quant_state *state, imbe_model *model, unsigned sync, uint16_t words[8]);
int imbe_quant_decode(imbe_quant_state *state, const uint16_t words[8], imbe_model *model);
/* Mathematical support is shared and independently generated. */
int32_t imbe_log2_u32_q11(uint32_t value);
/* exp2(log2-amplitude Q11), returned as unsigned raw-linear amplitude Q12. */
uint32_t imbe_exp2_q11(int32_t value);
uint32_t imbe_isqrt(uint32_t value);
int16_t imbe_sine_q15(uint32_t phase);
/* DFT=out*2^return; inverse mathematical (1/N) IFFT=out*2^return. */
int imbe_rfft256(int16_t buf[258]);
int imbe_irfft256(int16_t buf[258]);
int imbe_cfft(int16_t *buf, unsigned log2n);
#endif

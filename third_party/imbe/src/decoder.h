/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Open25 contributors
 * Independently expressed full-rate model synthesis. Caller-owned synthesis state. */
#ifndef OPEN25_IMBE_DECODER_H
#define OPEN25_IMBE_DECODER_H
#include "imbe_model.h"

typedef struct {
    uint32_t noise_state;
    uint32_t fundamental_phase;
    uint32_t previous_frequency_q32;
    uint32_t phase[IMBE_MAX_HARMONICS];
    uint32_t amplitude_q12[IMBE_MAX_HARMONICS];
    uint32_t previous_voiced[2];
    uint8_t previous_L;
    /* Successive 209-sample noise records overlap by49 samples. */
    int16_t noise_history[49];
    /* Previous shaped-noise samples n=0..104, in raw PCM Q8. */
    int32_t noise_tail_q8[105];
    /* Reused in-place for windowed noise, spectrum shaping and inverse FFT. */
    int16_t transform[258];
} imbe_decoder;

void imbe_decoder_init(imbe_decoder *state);
void imbe_synthesize(imbe_decoder *state, const imbe_model *model,
                     int16_t pcm[160]);
#endif

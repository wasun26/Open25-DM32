/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Open25 contributors
 */
#ifndef OPEN25_IMBE_ENCODER_H
#define OPEN25_IMBE_ENCODER_H
#include "imbe_model.h"
#define IMBE_PITCH_CANDIDATES 203
/* Analysis window right edge is370 samples behind the latest input sample.
 * ceil(370/160)=3 zero frames reach the last microphone sample.
 * tools/generate_header.py refreshes the public API constant from here. */
enum { IMBE_ENCODER_TAIL_FRAMES = 3 };
/* The800-sample DC-blocked history owns analysis continuity. Its analyzed
 * center is480 samples(60ms) behind the latest input sample. The first two
 * calls analyze startup zero history. All scratch is instance-owned and
 * reused only after its previous stage has consumed it. No heap/globals.
 */
typedef struct {
    int16_t history[800];
    uint16_t pitch_error[3][IMBE_PITCH_CANDIDATES];
    uint16_t previous_pitch[2], previous_error[2], previous_voiced;
    int16_t previous_input;
    int32_t dc_output, dc_remainder;
    uint32_t energy_peak, frames;
    union {
        struct { int16_t samples[301]; int32_t correlation[151]; } pitch;
        uint32_t future_cost[IMBE_PITCH_CANDIDATES];
        struct {
            int16_t fft[258];
            int16_t unvoiced_log[IMBE_MAX_HARMONICS];
            uint32_t group_energy[12], group_captured[12];
        } spectrum;
    } scratch;
} imbe_encoder;
void imbe_encoder_init(imbe_encoder *state);
/* pcm/model are160-sample input and one full-rate model output. State and
 * output require their ordinary4-byte alignment, input requires2-byte
 * alignment; none may overlap. Return1 on success,0 on a null pointer.
 */
int imbe_analyze(imbe_encoder *state, const int16_t pcm[160], imbe_model *model);
#endif

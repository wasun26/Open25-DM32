/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Open25 contributors
 *
 * Full-rate IMBE synthesis expressed from the mathematical model in
 * TIA-102.BABA-A sections8,9 and11. No reference-vocoder source or data table
 * is incorporated. The synthesis window is generated from its linear ramp
 * and plateau; phases and white noise are owned by each decoder instance.
 *
 * Quantizer prediction owns the unenhanced log amplitudes. This stage only
 * stores the enhanced, clean-frame-smoothed amplitudes used for synthesis.
 * The model has no channel-error estimates: section9's clean-frame branch
 * leaves voicing unchanged and limits the amplitude sum to20480 raw units.
 * Error concealment/admission remains with the caller, not an invented API.
 */
#include "decoder.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

_Static_assert(INT32_MAX == 2147483647 && (-1 >> 1) == -1,
               "synthesis requires32-bit arithmetic signed shifts");

enum { AMP_FRACTION = 12, SAMPLE_FRACTION = 8, FRAME = 160 };

/* log2(2*pi), log2(.96), log2(1.2), allQ11 from the mathematical constants. */
enum { LOG_TWO_PI = 5430, LOG_096 = -121, LOG_12 = 539 };
/* Eq121 evaluated using an analytically generated221-point Kaiser(beta6)
 * refinement window and the piecewise-linear synthesis window:
 * gamma=146.6432712244; only its log2Q11 scalar is needed in production.
 * No221-element reference table is imported. */
enum { LOG_UNVOICED_GAMMA = 14738 };

static int16_t clip16(int32_t value)
{
    if (value > 32767) return 32767;
    if (value < -32768) return -32768;
    return (int16_t)value;
}

/* The section11.2 recommended white-noise recurrence, Eq117. Its state
 * stays0..53124; the32-bit product is below9.1million. Subtracting26562
 * centers it without changing the arbitrary-mean noise model. The same
 * sequence supplies the phase perturbation from Eq141 at n=1..56. */
static int16_t white_noise(imbe_decoder *state)
{
    state->noise_state = (171u * state->noise_state + 11213u) % 53125u;
    return (int16_t)((int32_t)state->noise_state - 26562);
}

static int32_t noise_phase(uint32_t noise)
{
    /* floor(2^32*u/53125)-2^31 implements Eq141 in turnsQ32. Splitting
     * 2^32 into quotient/remainder leaves both products withinuint32:
     * noise<=53124 and remainder<53125. No64-bit multiply/divide. */
    const uint32_t quotient = UINT32_MAX / 53125u;
    const uint32_t remainder = UINT32_MAX % 53125u + 1u;
    return (int32_t)(noise * quotient
                    + (noise * remainder) / 53125u - 0x80000000u);
}

static unsigned synthesis_weight(int n)
{
    unsigned magnitude = (unsigned)(n < 0 ? -n : n);
    if (magnitude >= 105) return 0;
    return magnitude <= 55 ? 50u : 105u - magnitude;
}

static uint32_t scaled_amplitude(uint32_t value, uint32_t gain_q15)
{
    /* Exact split product with nearest rounding. Both component products
     * fituint32 for the bounded linear amplitudes and0..32768 gain. */
    return (value >> 15) * gain_q15
         + (((value & 32767u) * gain_q15 + 16384u) >> 15);
}

static int32_t amplitude_cos_q8(uint32_t amplitude_q12, int16_t cosine_q15)
{
    /* floor(amplitude*cos/2^16), split into products which fitint32. The
     * final shift gives rawPCM Q8; accumulated harmonic amplitudes are
     * bounded by the section9 clean-frame sum limit. */
    int32_t high = (int32_t)(amplitude_q12 >> 16) * cosine_q15;
    int32_t low = (int32_t)(amplitude_q12 & 65535u) * cosine_q15;
    return (high + (low >> 16) + 4) >> 3;
}

static int16_t cosine_q15(uint32_t phase)
{
    return imbe_sine_q15(phase + 0x40000000u);
}

/* Bring positive Q12 amplitudes to a peak in512..1023 for bounded32-bit
 * energy arithmetic. The shift may be negative for fractional quiet speech. */
static int amplitude_shift(const uint32_t *amplitude, unsigned count)
{
    uint32_t peak = 0;
    unsigned i;
    for (i = 0; i < count; ++i)
        if (amplitude[i] > peak) peak = amplitude[i];
    return peak ? 22 - (int)__builtin_clz(peak) : 0;
}

static uint32_t normalized_amplitude(uint32_t amplitude, int shift)
{
    return shift >= 0 ? amplitude >> (unsigned)shift
                      : amplitude << (unsigned)-shift;
}

static int32_t amplitude_energy_log(const uint32_t *amplitude, unsigned count)
{
    int shift = amplitude_shift(amplitude, count);
    uint32_t energy = 0;
    unsigned i;
    for (i = 0; i < count; ++i) {
        uint32_t a = normalized_amplitude(amplitude[i], shift);
        energy += a * a;
    }
    return imbe_log2_u32_q11(energy) + (2 * shift - 24) * 2048;
}

static unsigned harmonic_voiced(const imbe_model *model, unsigned harmonic)
{
    unsigned group = imbe_voicing_group(harmonic, model->L);
    return (model->voiced >> group) & 1u;
}

static unsigned previous_voiced(const imbe_decoder *state, unsigned harmonic)
{
    return (state->previous_voiced[harmonic >> 5] >> (harmonic & 31)) & 1u;
}

static void enhanced_amplitudes(const imbe_model *model, uint32_t amplitude[56])
{
    unsigned i, L = model->L;
    int shift;
    uint32_t energy = 0, reduced_energy = 0;
    int32_t correlation = 0, ratio_q15, ratio_square, denominator;
    int32_t original_log_energy, enhanced_log_energy, gamma_log;
    int32_t log_frequency = imbe_log2_u32_q11(model->frequency_q32)
                          - 32 * 2048 + LOG_TWO_PI;

    for (i = 0; i < L; ++i)
        amplitude[i] = imbe_exp2_q11(model->log_amplitude[i]);
    original_log_energy = amplitude_energy_log(amplitude, L);
    shift = amplitude_shift(amplitude, L);
    for (i = 0; i < L; ++i) {
        uint32_t a = normalized_amplitude(amplitude[i], shift);
        uint32_t square = a * a;
        uint32_t reduced = square >> 4;
        int32_t cosine = cosine_q15(model->frequency_q32 * (i + 1));
        energy += square;
        reduced_energy += reduced;
        /* reduced<=65408, so this signed product is insideint32. */
        correlation += ((int32_t)reduced * cosine) >> 15;
    }
    if (!energy) {
        for (i = 0; i < L; ++i) amplitude[i] = 0;
        return;
    }
    while (reduced_energy > 65535u) {
        reduced_energy >>= 1;
        correlation >>= 1;
    }
    ratio_q15 = reduced_energy ? correlation * 32768 / (int32_t)reduced_energy : 0;
    /* Keep the enhancement denominator away from singular fixed-point
     * endpoints; the unclipped spectrum is renormalized to its old energy. */
    if (ratio_q15 > 32700) ratio_q15 = 32700;
    if (ratio_q15 < -32700) ratio_q15 = -32700;
    ratio_square = (ratio_q15 * ratio_q15) >> 15;
    denominator = 32768 - ratio_square;

    for (i = 0; i < L; ++i) {
        int32_t weight = 0;
        if (8 * (i + 1) > L) {
            int32_t cosine = cosine_q15(model->frequency_q32 * (i + 1));
            int32_t numerator = 32768 + ratio_square
                              - ((ratio_q15 * cosine) >> 14);
            if (numerator < 1) numerator = 1;
            weight = model->log_amplitude[i] / 2
                   + (LOG_096 + imbe_log2_u32_q11((uint32_t)numerator)
                      - log_frequency - original_log_energy
                      - imbe_log2_u32_q11((uint32_t)denominator)) / 4;
            if (weight < -2048) weight = -2048;
            if (weight > LOG_12) weight = LOG_12;
        }
        amplitude[i] = imbe_exp2_q11(model->log_amplitude[i] + weight);
    }
    enhanced_log_energy = amplitude_energy_log(amplitude, L);
    gamma_log = (original_log_energy - enhanced_log_energy) / 2;
    reduced_energy = 0;
    for (i = 0; i < L; ++i) {
        int32_t log_amplitude = imbe_log2_u32_q11(amplitude[i]) - AMP_FRACTION * 2048;
        amplitude[i] = imbe_exp2_q11(log_amplitude + gamma_log);
        /* Sum inQ8 avoids overflow while retaining sub-sample amplitudes. */
        reduced_energy += (amplitude[i] + 8u) >> 4;
    }
    /* Clean channel branch of section9, Eq115/116. An integer-rounded raw
     * sum is sufficient for the fixed20480 threshold, without64-bit divide. */
    reduced_energy = (reduced_energy + 128u) >> 8;
    if (reduced_energy > 20480u) {
        uint32_t gain = (20480u * 32768u) / reduced_energy;
        for (i = 0; i < L; ++i)
            amplitude[i] = scaled_amplitude(amplitude[i], gain);
    }
}

static int32_t noise_sample_q8(int16_t sample, int exponent)
{
    /* Leave ample headroom for bounded overlap/add and the harmonic sum.
     * The clipping boundary is65536 raw units, beyond the16-bit DAC range. */
    int32_t value = sample;
    exponent += SAMPLE_FRACTION;
    if (exponent >= 0) {
        if (exponent > 24) return value < 0 ? -(1 << 24) : value ? 1 << 24 : 0;
        if (value > ((1 << 24) >> exponent)) return 1 << 24;
        if (value < -((1 << 24) >> exponent)) return -(1 << 24);
        return value * (int32_t)(1u << exponent);
    }
    return exponent <= -31 ? (value < 0 ? -1 : 0) : value >> (unsigned)-exponent;
}

static int shaped_noise(imbe_decoder *state, const imbe_model *model,
                        const uint32_t amplitude[56], uint32_t phase_noise[56])
{
    int16_t *spectrum = state->transform;
    uint32_t maximum = 0;
    unsigned i, harmonic;
    int spectrum_exponent;

    memset(spectrum, 0, sizeof state->transform);
    for (i = 0; i < 209; ++i) {
        int16_t white = i < 49 ? state->noise_history[i] : white_noise(state);
        int n = (int)i - 104;
        unsigned weight = synthesis_weight(n);
        unsigned destination = (unsigned)(n < 0 ? n + 256 : n);
        spectrum[destination] = (int16_t)(((int32_t)white * (int32_t)weight) / 50);
        if (i >= 160) state->noise_history[i - 160] = white;
        /* i=105..160 corresponds exactly to current-frame u(1)..u(56).
         * These are new sequence samples, never the retained49-sample tail. */
        if (i >= 105 && i <= 160)
            phase_noise[i - 105] = (uint32_t)noise_phase(state->noise_state);
    }
    for (i = 0; i < model->L; ++i)
        if (!harmonic_voiced(model, i) && amplitude[i] > maximum)
            maximum = amplitude[i];
    /* All sequence samples, the49-word history and u(1)..u(56) above
     * advance exactly as before, even if no unvoiced energy is present.
     * In that case every shaped bin and inverse-transform sample is zero;
     * skip both transforms and clear all258 words, including their tail.
     * The previous frame's retained noise still fades through overlap/add. */
    if (!maximum) {
        memset(spectrum, 0, sizeof state->transform);
        return 0;
    }
    (void)imbe_rfft256(spectrum);
    spectrum_exponent = 15 - (int)__builtin_clz(maximum);

    /* The exact pitch->L rule bounds(2L+1)*frequency below oneQ32 turn.
     * Band edges are therefore computed entirely inuint32 without overflow. */
    for (harmonic = 0; harmonic < model->L; ++harmonic) {
        unsigned l = harmonic + 1;
        unsigned first = (model->frequency_q32 * (2 * l - 1) + 0x1ffffffu) >> 25;
        unsigned end = (model->frequency_q32 * (2 * l + 1) + 0x1ffffffu) >> 25;
        uint32_t energy = 0, rms, gain;
        if (first > 128) first = 128;
        if (end > 128) end = 128;
        if (end <= first) continue;
        if (harmonic_voiced(model, harmonic) || !amplitude[harmonic]) {
            for (i = first; i < end; ++i) spectrum[2 * i] = spectrum[2 * i + 1] = 0;
            continue;
        }
        for (i = first; i < end; ++i) {
            int32_t re = spectrum[2 * i] >> 3, im = spectrum[2 * i + 1] >> 3;
            energy += (uint32_t)(re * re + im * im);
        }
        rms = imbe_isqrt(energy / (end - first));
        if (!rms) {
            for (i = first; i < end; ++i) spectrum[2 * i] = spectrum[2 * i + 1] = 0;
            continue;
        }
        {
            int32_t log_amplitude = imbe_log2_u32_q11(amplitude[harmonic]) - 12 * 2048;
            gain = imbe_exp2_q11(log_amplitude + LOG_UNVOICED_GAMMA
                                - spectrum_exponent * 2048) / rms;
        }
        for (i = first; i < end; ++i) {
            int32_t re = spectrum[2 * i] >> 3, im = spectrum[2 * i + 1] >> 3;
            /* rms includes every written component. Thus abs(component)/rms
             * is bounded by the square root of this band's bin count; the
             * coupled product is far belowINT32_MAX with the chosen exponent. */
            spectrum[2 * i] = clip16((re * (int32_t)gain + 2048) >> 12);
            spectrum[2 * i + 1] = clip16((im * (int32_t)gain + 2048) >> 12);
        }
    }
    {
        unsigned low = (model->frequency_q32 + 0x1ffffffu) >> 25;
        unsigned high = (model->frequency_q32 * (2u * model->L + 1u) + 0x1ffffffu) >> 25;
        for (i = 0; i < low && i <= 128; ++i) spectrum[2 * i] = spectrum[2 * i + 1] = 0;
        if (high > 128) high = 128;
        for (i = high; i <= 128; ++i) spectrum[2 * i] = spectrum[2 * i + 1] = 0;
    }
    spectrum[1] = spectrum[257] = 0;
    return spectrum_exponent + imbe_irfft256(spectrum);
}

static int32_t noise_overlap(const imbe_decoder *state, unsigned n, int exponent)
{
    int32_t previous = n < 105 ? state->noise_tail_q8[n] : 0;
    int32_t current = n >= 56 ? noise_sample_q8(state->transform[n + 96], exponent) : 0;
    if (n < 56) return previous;
    if (n > 104) return current;
    {
        int32_t a = (int32_t)synthesis_weight((int)n);
        int32_t b = (int32_t)synthesis_weight((int)n - FRAME);
        int32_t denominator = a * a + b * b;
        int32_t numerator = previous * a + current * b;
        /* Divide before the final*50, retaining the signed remainder. */
        return (numerator / denominator) * 50
             + ((numerator % denominator) * 50) / denominator;
    }
}

static int32_t signed_fraction_q15(int32_t value, unsigned fraction)
{
    int32_t high = (value >> 16) * (int32_t)fraction;
    uint32_t low = ((uint32_t)value & 65535u) * fraction;
    return high * 2 + (int32_t)(low >> 15);
}

void imbe_decoder_init(imbe_decoder *state)
{
    unsigned i;
    if (!state) return;
    memset(state, 0, sizeof *state);
    /* u(-105)=3147; history begins with u(-104)..u(-56). */
    state->noise_state = 3147;
    for (i = 0; i < 49; ++i)
        state->noise_history[i] = white_noise(state);
}

void imbe_synthesize(imbe_decoder *state, const imbe_model *model,
                     int16_t pcm[160])
{
    uint32_t amplitude[56], phase[56];
    unsigned i, n, first, count, L, previous_L, max_L, unvoiced = 0;
    uint32_t fundamental_phase, previous_frequency;
    int noise_exponent;

    if (!pcm) return;
    if (!state || !model || model->L < 9 || model->L > 56
        || !model->frequency_q32) {
        memset(pcm, 0, FRAME * sizeof *pcm);
        return;
    }
    L = model->L;
    previous_L = state->previous_L;
    max_L = previous_L > L ? previous_L : L;
    enhanced_amplitudes(model, amplitude);
    for (i = L; i < 56; ++i) amplitude[i] = 0;
    for (i = 0; i < L; ++i) unvoiced += !harmonic_voiced(model, i);
    previous_frequency = state->previous_frequency_q32;
    fundamental_phase = state->fundamental_phase
                      + (previous_frequency + model->frequency_q32) * 80u;
    /* The shaped-noise pass materializes current-frame u(1)..u(56), which
     * Eq140/141 use for the high-harmonic phase perturbations. */
    noise_exponent = shaped_noise(state, model, amplitude, phase);
    for (i = 0; i < 56; ++i) {
        int32_t jitter = (int32_t)phase[i];
        phase[i] = fundamental_phase * (i + 1);
        if (i + 1 > L / 4)
            phase[i] += (uint32_t)signed_fraction_q15(jitter, unvoiced * 32768u / L);
    }

    /* Whole-frame scratch avoids repeating harmonic setup across five blocks.
     * Three160-word accumulators retain wide sums until final DAC clipping;
     * no mutable global/state buffer is added. The coherent path below
     * preserves the earlier32-sample amplitude-restart rounding exactly. */
    for (first = 0; first < FRAME; first += FRAME) {
        int32_t mix[FRAME];
        int32_t old_mix[FRAME] = {0}, new_mix[FRAME] = {0};
        count = FRAME;
        for (n = 0; n < count; ++n)
            mix[n] = noise_overlap(state, first + n, noise_exponent);
        for (i = 0; i < max_L; ++i) {
            unsigned old_voiced = i < previous_L && previous_voiced(state, i);
            unsigned new_voiced = i < L && harmonic_voiced(model, i);
            uint32_t old_step = previous_frequency * (i + 1);
            uint32_t new_step = model->frequency_q32 * (i + 1);
            uint32_t old_phase, new_phase;
            uint32_t old_amplitude = old_voiced ? state->amplitude_q12[i] : 0;
            uint32_t new_amplitude = new_voiced ? amplitude[i] : 0;
            uint32_t difference = previous_frequency > model->frequency_q32
                                ? previous_frequency - model->frequency_q32
                                : model->frequency_q32 - previous_frequency;
            if (!old_voiced && !new_voiced) continue;
            if (old_voiced && new_voiced && i < 7
                && difference < model->frequency_q32 / 10) {
                int32_t phase_error = (int32_t)(phase[i] - state->phase[i]
                                      - (previous_frequency + model->frequency_q32) * (i + 1) * 80u);
                int32_t acceleration = (int32_t)(new_step - old_step) / FRAME;
                uint32_t step = old_step + (uint32_t)(phase_error / FRAME)
                              + (uint32_t)(acceleration / 2);
                int32_t delta = (int32_t)new_amplitude - (int32_t)old_amplitude;
                int32_t increment = delta / FRAME;
                int32_t remainder = delta % FRAME;
                int32_t running_amplitude = (int32_t)old_amplitude
                    + increment * (int32_t)first
                    + remainder * (int32_t)first / FRAME;
                old_phase = state->phase[i] + step * first
                          + (uint32_t)acceleration * (first * (first - (first != 0)) / 2);
                step += (uint32_t)acceleration * first;
                for (n = 0; n < count; ++n) {
                    /* Preserve the original 32-sample amplitude restart rounding. */
                    if (n && !(n & 31u))
                        running_amplitude += remainder * (int32_t)n / FRAME
                            - remainder * (int32_t)(n - 32u) / FRAME;
                    mix[n] += 2 * amplitude_cos_q8((uint32_t)running_amplitude, cosine_q15(old_phase));
                    old_phase += step;
                    step += (uint32_t)acceleration;
                    running_amplitude += increment;
                }
            } else {
                old_phase = state->phase[i] + old_step * first;
                new_phase = phase[i] + new_step * (first - FRAME);
                for (n = 0; n < count; ++n) {
                    if (old_voiced)
                        old_mix[n] += amplitude_cos_q8(old_amplitude, cosine_q15(old_phase));
                    if (new_voiced)
                        new_mix[n] += amplitude_cos_q8(new_amplitude, cosine_q15(new_phase));
                    old_phase += old_step;
                    new_phase += new_step;
                }
            }
        }
        for (n = 0; n < count; ++n) {
            /* The same synthesis windows multiply every noncoherent
             * harmonic. Factor them outside the harmonic sum. Since each
             * prior per-term division truncated less than oneQ8 unit,
             * moving the two divisions changes the wide mix by fewer than
             * 2*(56+56+2)=228Q8 units (<0.9 rawPCM). Final DAC outputs
             * therefore differ by at most one sample unit. Phase, noise
             * and all retained state are unaffected. Each oscillator sum
             * is bounded by the20480-amplitude clean-frame limit, so its
             * product with a0..50 window fitsint32. */
            int32_t old_weight = (int32_t)synthesis_weight((int)(first + n));
            int32_t new_weight = (int32_t)synthesis_weight((int)(first + n) - FRAME);
            mix[n] += 2 * ((old_mix[n] * old_weight) / 50
                        + (new_mix[n] * new_weight) / 50);
            pcm[first + n] = clip16((mix[n] + 128) >> SAMPLE_FRACTION);
        }
    }

    for (n = 0; n < 105; ++n)
        state->noise_tail_q8[n] = noise_sample_q8(state->transform[n], noise_exponent);
    state->previous_voiced[0] = state->previous_voiced[1] = 0;
    for (i = 0; i < L; ++i) {
        if (harmonic_voiced(model, i)) state->previous_voiced[i >> 5] |= 1u << (i & 31);
        state->amplitude_q12[i] = amplitude[i];
    }
    for (; i < 56; ++i) state->amplitude_q12[i] = 0;
    memcpy(state->phase, phase, sizeof phase);
    state->fundamental_phase = fundamental_phase;
    state->previous_frequency_q32 = model->frequency_q32;
    state->previous_L = (uint8_t)L;
}

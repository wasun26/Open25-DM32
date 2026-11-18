/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Open25 contributors
 *
 * Full-rate IMBE model analysis from the mathematical description in
 * TIA-102.BABA-A sections 5.1--5.3, independently expressed with explicit
 * fixed-point state.
 *
 * Implementation choices: generated Kaiser windows (beta5.25 at301 taps; beta6 at221 taps),
 * the21-point numeric LPF fromAnnexD (source documented atits table),
 * half-period autocorrelation with two future-frame continuity tracking,
 * quantized-pitch spectral refinement, and a shared centered256-point DFT.
 * Quantizer/parameter meanings are owned by imbe_model.h's parameter mapper.
 */
#include "encoder.h"
_Static_assert(IMBE_PITCH_CANDIDATES <= 256, "pitch queue indices must fit uint8_t");
#include <limits.h>
#include <stddef.h>
#include <string.h>
_Static_assert(INT32_MAX == 2147483647 && (-1 >> 1) == -1,
               "analysis requires32-bit arithmetic signed shifts");

static const int16_t refinement_window[111] = {
    487, 570, 659, 754, 855, 962, 1076, 1198, 1326, 1461, 1603, 1752,
    1910, 2074, 2247, 2427, 2615, 2811, 3015, 3227, 3447, 3676, 3912, 4157,
    4410, 4672, 4941, 5219, 5505, 5799, 6101, 6411, 6729, 7055, 7388, 7729,
    8077, 8433, 8795, 9165, 9541, 9924, 10313, 10708, 11109, 11515, 11927, 12344,
    12766, 13192, 13623, 14057, 14494, 14935, 15379, 15825, 16274, 16724, 17176, 17628,
    18081, 18535, 18988, 19441, 19892, 20343, 20791, 21237, 21681, 22121, 22558, 22991,
    23419, 23843, 24261, 24674, 25081, 25481, 25874, 26260, 26638, 27008, 27370, 27723,
    28066, 28400, 28724, 29038, 29340, 29632, 29913, 30182, 30439, 30683, 30916, 31135,
    31342, 31535, 31715, 31881, 32034, 32172, 32296, 32406, 32502, 32583, 32650, 32701,
    32738, 32761, 32767,
};

static const int16_t coarse_window_squared[151] = {
    28, 34, 40, 47, 55, 64, 74, 85, 97, 110, 125, 141,
    159, 178, 199, 222, 247, 274, 302, 333, 367, 403, 441, 483,
    527, 574, 624, 677, 734, 794, 858, 926, 997, 1073, 1153, 1237,
    1325, 1418, 1516, 1619, 1726, 1839, 1957, 2080, 2209, 2343, 2483, 2629,
    2781, 2938, 3102, 3273, 3449, 3632, 3822, 4018, 4220, 4429, 4645, 4868,
    5098, 5334, 5578, 5828, 6085, 6349, 6620, 6898, 7182, 7474, 7772, 8076,
    8387, 8705, 9029, 9360, 9696, 10039, 10387, 10742, 11102, 11467, 11838, 12213,
    12594, 12979, 13368, 13762, 14160, 14561, 14966, 15374, 15785, 16198, 16613, 17031,
    17450, 17871, 18292, 18715, 19137, 19560, 19982, 20403, 20823, 21242, 21659, 22074,
    22486, 22894, 23300, 23702, 24100, 24493, 24881, 25264, 25641, 26012, 26377, 26735,
    27086, 27429, 27764, 28091, 28409, 28719, 29019, 29310, 29591, 29861, 30121, 30370,
    30609, 30835, 31051, 31254, 31446, 31625, 31792, 31946, 32087, 32216, 32331, 32433,
    32521, 32597, 32658, 32706, 32741, 32761, 32767,
};

/* Numeric FIR coefficients from TIA-102.BABA-A2014 AnnexD, printedp83.
 * Values quantized byround(published_coefficient*32768); only the symmetric
 * half is stored. This published functional dataset is not claimed as
 * original project numerical data. The analysis expression is authored here. */
static const int16_t pitch_fir[11] = {
    -95, -93, 186, 544, 288, -883, -1835, -495, 3891, 9142, 11513,
};

static const int16_t window_response[417] = {
    4096, 4095, 4094, 4091, 4088, 4083, 4078, 4071, 4064, 4055, 4046, 4035,
    4024, 4011, 3998, 3983, 3968, 3952, 3935, 3917, 3898, 3878, 3857, 3835,
    3813, 3789, 3765, 3740, 3715, 3688, 3661, 3633, 3604, 3575, 3544, 3514,
    3482, 3450, 3417, 3384, 3350, 3315, 3280, 3245, 3209, 3172, 3135, 3097,
    3060, 3021, 2983, 2944, 2904, 2864, 2824, 2784, 2744, 2703, 2662, 2621,
    2580, 2538, 2497, 2455, 2413, 2372, 2330, 2288, 2246, 2204, 2163, 2121,
    2079, 2038, 1997, 1955, 1914, 1873, 1833, 1792, 1752, 1712, 1672, 1633,
    1593, 1554, 1516, 1478, 1440, 1402, 1365, 1328, 1292, 1256, 1221, 1186,
    1151, 1117, 1083, 1050, 1017, 985, 953, 922, 891, 861, 831, 802,
    774, 746, 718, 691, 665, 639, 613, 589, 564, 541, 518, 495,
    473, 452, 431, 411, 391, 372, 353, 335, 317, 300, 284, 268,
    252, 237, 223, 209, 195, 182, 170, 158, 146, 135, 125, 115,
    105, 96, 87, 78, 70, 63, 55, 49, 42, 36, 30, 25,
    20, 15, 10, 6, 2, -1, -4, -7, -10, -13, -15, -17,
    -19, -20, -22, -23, -24, -25, -25, -26, -26, -26, -26, -26,
    -26, -26, -25, -25, -24, -23, -23, -22, -21, -20, -19, -18,
    -17, -16, -15, -13, -12, -11, -10, -9, -7, -6, -5, -4,
    -3, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 8,
    9, 10, 11, 11, 12, 12, 13, 13, 14, 14, 14, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 14, 14,
    14, 14, 13, 13, 12, 12, 11, 11, 10, 10, 9, 9,
    8, 8, 7, 6, 6, 5, 4, 4, 3, 3, 2, 1,
    1, 0, -1, -1, -2, -2, -3, -4, -4, -5, -5, -6,
    -6, -7, -7, -7, -8, -8, -8, -9, -9, -9, -10, -10,
    -10, -10, -10, -10, -10, -11, -11, -11, -11, -11, -10, -10,
    -10, -10, -10, -10, -10, -9, -9, -9, -9, -8, -8, -8,
    -7, -7, -7, -6, -6, -6, -5, -5, -4, -4, -3, -3,
    -3, -2, -2, -1, -1, 0, 0, 0, 1, 1, 2, 2,
    3, 3, 3, 4, 4, 4, 5, 5, 5, 6, 6, 6,
    6, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 7,
    7, 7, 7, 7, 7, 6, 6, 6, 6, 5, 5, 5,
    5, 4, 4, 4, 3, 3, 3, 2, 2, 2, 1, 1,
    1, 0, 0, 0, -1, -1, -1, -2, -2, -2, -3, -3,
    -3, -3, -4, -4, -4, -4, -5, -5, -5,
};

#define COARSE_WINDOW_SQUARES_Q8 30195u
#define COARSE_WINDOW_RATIO_Q20 6407u
#define REFINEMENT_LOGSUM_Q11 13889

/* A normalized unsigned ratio with bounded32-bit numerator. All callers
 * supply ratios below16. Common scaling preserves at least15 denominator
 * bits for these ranges and never forms a wide multiplication. */
static uint32_t ratio(uint32_t numerator, uint32_t denominator, unsigned bits)
{
    unsigned top, shift = 0;
    if (!numerator) return 0;
    if (!denominator) return UINT32_MAX;
    top = 31u - (unsigned)__builtin_clz(numerator);
    if (top + bits > 30) shift = top + bits - 30;
    numerator >>= shift;
    denominator >>= shift;
    if (!denominator) denominator = 1;
    return (numerator << bits) / denominator;
}

static unsigned abs_sample(int32_t value)
{
    return (unsigned)(value < 0 ? -value : value);
}

static int16_t clip16(int32_t value)
{
    if (value > 32767) value = 32767;
    if (value < -32768) value = -32768;
    return (int16_t)value;
}

/* Center639 is the newest usable301-point window: its FIR endpoints lie
 * within479..799. The cached error rows advance by160 samples per call. */
static void coarse_errors(imbe_encoder *state, uint16_t error[IMBE_PITCH_CANDIDATES])
{
    int16_t *samples = state->scratch.pitch.samples;
    int32_t *correlation = state->scratch.pitch.correlation;
    unsigned peak = 0, filtered_peak = 0, i, lag, down = 0, up = 0;
    uint32_t energy = 0;
    for (i = 479; i < 800; ++i) {
        unsigned value = abs_sample(state->history[i]);
        if (value > peak) peak = value;
    }
    if (!peak) {
        for (i = 0; i < IMBE_PITCH_CANDIDATES; ++i) error[i] = 32768;
        return;
    }
    while (peak > 1023) { peak >>= 1; ++down; }
    while (peak < 512) { peak <<= 1; ++up; }
    for (i = 0; i < 301; ++i) {
        int32_t accumulator = 0;
        unsigned tap;
        /* The FIR's21 published coefficients are symmetric. Normalize each
         * source sample before pairing, preserving the original arithmetic
         * right-shift rounding. Then (a*h+b*h)=(a+b)*h exactly: normalized
         * source magnitudes are at most1024, paired products are at most
         * 2048*32768, and the complete sum is bounded by
         * 1024*sum(abs(Q15 taps))=47531008 < INT32_MAX. */
        for (tap = 0; tap < 10; ++tap) {
            int32_t a = state->history[479 + i + tap];
            int32_t b = state->history[499 + i - tap];
            a = (a * (int32_t)(1u << up)) >> down;
            b = (b * (int32_t)(1u << up)) >> down;
            accumulator += (a + b) * pitch_fir[tap];
        }
        {
            int32_t center = state->history[489 + i];
            center = (center * (int32_t)(1u << up)) >> down;
            accumulator += center * pitch_fir[10];
        }
        samples[i] = (int16_t)((accumulator + 16384) >> 15);
        if (abs_sample(samples[i]) > filtered_peak) filtered_peak = abs_sample(samples[i]);
    }
    down = filtered_peak > 1023 ? 1u : 0u;
    for (i = 0; i < 301; ++i) {
        int32_t sample = samples[i] >> down;
        int32_t weighted = (sample * coarse_window_squared[i <= 150 ? i : 300 - i] + 16384) >> 15;
        energy += (uint32_t)(sample * weighted);
        samples[i] = (int16_t)weighted;
    }
    energy >>= 4;
    if (!energy) {
        for (i = 0; i < IMBE_PITCH_CANDIDATES; ++i) error[i] = 32768;
        return;
    }
    for (lag = 0; lag <= 150; ++lag) {
        unsigned count = 301 - lag;
        int32_t s0 = 0, s1 = 0, s2 = 0, s3 = 0, sum;
        /* Independent accumulators remove three quarters of the loop
         * branches and the serial dependency between adjacent products.
         * Every component is at most1023 in magnitude. Each accumulator
         * has at most76 terms and the combined301-term dot product is
         * bounded by301*1023^2=315005229, so every regrouping is exact in
         * int32_t. Apply the original rounding shift only after summation. */
        for (i = 0; i + 4 <= count; i += 4) {
            s0 += (int32_t)samples[i] * samples[i + lag];
            s1 += (int32_t)samples[i + 1] * samples[i + lag + 1];
            s2 += (int32_t)samples[i + 2] * samples[i + lag + 2];
            s3 += (int32_t)samples[i + 3] * samples[i + lag + 3];
        }
        sum = s0 + s1 + s2 + s3;
        for (; i < count; ++i) sum += (int32_t)samples[i] * samples[i + lag];
        correlation[lag] = sum >> 4;
    }
    for (i = 0; i < IMBE_PITCH_CANDIDATES; ++i) {
        unsigned period2 = 42 + i, multiple, denominator;
        int32_t comb = correlation[0];
        uint32_t term, value;
        for (multiple = period2; multiple <= 300; multiple += period2) {
            unsigned index = multiple >> 1;
            int32_t value_r = correlation[index];
            if ((multiple & 1u) && index < 150)
                value_r = (value_r + correlation[index + 1]) >> 1;
            comb += value_r * 2;
        }
        term = comb <= 0 ? 0 : ratio((uint32_t)comb, energy, 12);
        term = (term * period2 * 128u) / COARSE_WINDOW_SQUARES_Q8;
        denominator = 4096u - ((period2 * COARSE_WINDOW_RATIO_Q20) >> 9);
        value = term >= 4096 ? 0 : ((4096u - term) * 32768u) / denominator;
        error[i] = (uint16_t)(value > 65535u ? 65535u : value);
    }
}

static void neighbours(unsigned period2, unsigned *lower, unsigned *upper)
{
    unsigned a = (4 * period2 + 4) / 5, b = 6 * period2 / 5;
    *lower = (a < 42 ? 42 : a) - 42;
    *upper = (b > 244 ? 244 : b) - 42;
}

static uint32_t forward_cost(const imbe_encoder *state, unsigned index)
{
    unsigned lower, upper, i;
    uint32_t best = UINT32_MAX;
    neighbours(42 + index, &lower, &upper);
    for (i = lower; i <= upper; ++i)
        if (state->scratch.future_cost[i] < best) best = state->scratch.future_cost[i];
    return best + state->pitch_error[0][index];
}

static unsigned tracked_pitch(imbe_encoder *state)
{
    unsigned i, lower, upper, backward = 0, forward = 0, selected;
    unsigned head = 0, tail = 0, next = 0;
    uint8_t minimum_queue[IMBE_PITCH_CANDIDATES];
    uint32_t backward_error = UINT32_MAX, forward_error = UINT32_MAX;
    /* Both neighbourhood endpoints are monotone. Each index enters and
     * leaves this queue at most once; values and tie-selected pitch remain
     * identical to exhaustive range scans. Indices0..202 fit uint8_t. */
    for (i = 0; i < IMBE_PITCH_CANDIDATES; ++i) {
        neighbours(42 + i, &lower, &upper);
        while (next <= upper) {
            while (tail > head && state->pitch_error[2][minimum_queue[tail-1]] >= state->pitch_error[2][next]) --tail;
            minimum_queue[tail++] = (uint8_t)next++;
        }
        while (minimum_queue[head] < lower) ++head;
        state->scratch.future_cost[i] = state->pitch_error[1][i] + state->pitch_error[2][minimum_queue[head]];
    }
    neighbours(state->previous_pitch[0], &lower, &upper);
    for (i = lower; i <= upper; ++i) {
        if (state->pitch_error[0][i] < backward_error) {
            backward_error = state->pitch_error[0][i]; backward = i;
        }
    }
    backward_error += state->previous_error[0] + state->previous_error[1];
    head = tail = next = 0;
    for (i = 0; i < IMBE_PITCH_CANDIDATES; ++i) {
        neighbours(42 + i, &lower, &upper);
        while (next <= upper) {
            while (tail > head && state->scratch.future_cost[minimum_queue[tail-1]] >= state->scratch.future_cost[next]) --tail;
            minimum_queue[tail++] = (uint8_t)next++;
        }
        while (minimum_queue[head] < lower) ++head;
        uint32_t value = state->scratch.future_cost[minimum_queue[head]] + state->pitch_error[0][i];
        if (value < forward_error) { forward_error = value; forward = i; }
    }
    /* The shortest acceptable submultiple suppresses period doubling. */
    for (i = (forward + 42) / 42; i >= 2; --i) {
        unsigned candidate = ((forward + 42 + i / 2) / i) - 42;
        uint32_t value = forward_cost(state, candidate);
        if ((value <= 27853 && value * 10 <= forward_error * 17)
            || (value <= 13107 && value * 2 <= forward_error * 7)
            || value <= 1638) {
            forward = candidate; forward_error = value; break;
        }
    }
    selected = (backward_error <= 15729 || backward_error <= forward_error) ? backward : forward;
    /* A short-window comb has tiny finite-window/half-lag errors even on a
     * perfectly periodic signal. Continuity can otherwise lock onto a
     * multiple whose discretization error happens to be closer to zero.
     * Prefer the shortest subperiod when all three frames jointly explain
     * at least95 percent of their windowed energy (sum errors <=.15).
     * This explicit estimator choice differs from the reference's ratio
     * tests, while preserving the transmitted pitch parameter meanings. */
    for (i = (selected + 42) / 42; i >= 2; --i) {
        unsigned candidate = ((selected + 42 + i / 2) / i) - 42;
        if (forward_cost(state, candidate) <= 4915) { selected = candidate; break; }
    }
    state->previous_pitch[1] = state->previous_pitch[0];
    state->previous_pitch[0] = (uint16_t)(selected + 42);
    state->previous_error[1] = state->previous_error[0];
    state->previous_error[0] = state->pitch_error[0][selected];
    return selected + 42;
}

static unsigned spectrum(imbe_encoder *state, int *normalization)
{
    int16_t *fft = state->scratch.spectrum.fft;
    unsigned peak = 0, i, shift;
    for (i = 209; i <= 429; ++i) {
        unsigned value = abs_sample(state->history[i]);
        if (value > 32767) value = 32767;
        if (value > peak) peak = value;
    }
    shift = peak ? (unsigned)__builtin_clz(peak) - 17 : 0;
    for (i = 0; i < 258; ++i) fft[i] = 0;
    for (i = 0; i < 221; ++i) {
        int32_t value = (int32_t)state->history[209 + i] * (int32_t)(1u << shift);
        unsigned coefficient = i <= 110 ? i : 220 - i;
        fft[(i + 146) & 255u] = (int16_t)((value * refinement_window[coefficient] + 16384) >> 15);
    }
    *normalization = (int)shift;
    return (unsigned)imbe_rfft256(fft);
}

typedef struct { uint32_t energy, denominator, captured; } harmonic_fit;

static harmonic_fit fit(const int16_t *fft, uint32_t frequency, unsigned harmonic)
{
    uint32_t bin_frequency = frequency >> 8, center = harmonic * bin_frequency;
    unsigned lower = ((2 * harmonic - 1) * bin_frequency + 131071u) >> 17;
    unsigned upper = ((2 * harmonic + 1) * bin_frequency + 131071u) >> 17;
    unsigned m;
    int32_t real = 0, imag = 0;
    harmonic_fit result = {0, 0, 0};
    if (lower < 1) lower = 1;
    if (upper > 129) upper = 129;
    for (m = lower; m < upper; ++m) {
        uint32_t position = m << 16;
        unsigned delta = ((position > center ? position - center : center - position) + 512) >> 10;
        int32_t w = delta <= 416 ? window_response[delta] : 0;
        int32_t sr = fft[2 * m] >> 4, si = fft[2 * m + 1] >> 4;
        real += sr * w; imag += si * w;
        result.denominator += (uint32_t)(w * w);
        result.energy += (uint32_t)(sr * sr + si * si);
    }
    if (result.denominator) {
        int32_t root = (int32_t)imbe_isqrt(result.denominator);
        int32_t a = real / root, b = imag / root;
        result.captured = (uint32_t)(a * a + b * b);
        if (result.captured > result.energy) result.captured = result.energy;
    }
    return result;
}

static unsigned refine_pitch(imbe_encoder *state, unsigned period2, imbe_model *model)
{
    /* Quantizing the ten odd-eighth offsets -9/8..+9/8 yields these six
     * adjacent half-sample codes, including code0 at the short-period edge. */
    int initial = (int)period2 - 39, first = initial - 3, last = initial + 2, code;
    unsigned selected = (unsigned)initial;
    uint32_t best = UINT32_MAX;
    if (first < 0) first = 0;
    if (last > 207) last = 207;
    for (code = first; code <= last; ++code) {
        unsigned h;
        uint32_t energy = 0, residual = 0;
        (void)imbe_pitch_decode((unsigned)code, model);
        for (h = 1; h <= model->L; ++h) {
            if (h * (model->frequency_q32 >> 8) < (48u << 16)) continue;
            harmonic_fit estimate = fit(state->scratch.spectrum.fft, model->frequency_q32, h);
            energy += estimate.energy;
            residual += estimate.energy - estimate.captured;
        }
        if (energy) {
            uint32_t score = ratio(residual, energy, 15);
            if (score < best || (score == best && code == initial)) {
                best = score; selected = (unsigned)code;
            }
        }
    }
    (void)imbe_pitch_decode(selected, model);
    return selected;
}

static unsigned voicing_threshold(const imbe_encoder *state, unsigned group, uint32_t frequency,
                                   uint32_t low, uint32_t high, uint32_t raw_energy)
{
    uint32_t multiplier, denominator, threshold, frequency_factor;
    if (raw_energy > 0x3fffffffu) raw_energy = 0x3fffffffu;
    /* The update itself is performed once by the caller; here only form M. */
    denominator = raw_energy + state->energy_peak / 100;
    multiplier = ratio(raw_energy + state->energy_peak / 400, denominator, 15);
    /* Independent reliability-aware analysis choice: the level-only energy
     * floor should not erase a reliably measured periodic spectrum. Blend
     * its multiplier toward the ordinary full-level threshold by confidence
     * r=max(0,1-E/.5), using the same0.5 periodicity bound already governing
     * the higher-band unvoiced rule. This only relaxes the absolute-level
     * penalty, leaves the LF/HF noise-shape factor below unchanged, and never
     * exceeds the normal-level voicing threshold. No corpus-specific level,
     * clip identifier or additional confidence cutoff is introduced. */
    if (state->previous_error[0] < 16384) {
        uint32_t reliability = 32768u - 2u * state->previous_error[0];
        multiplier += ((32768u - multiplier) * reliability) >> 15;
    }
    low >>= 3; high >>= 3;
    if (low < 5 * high && high) {
        uint32_t shape = ratio(low, 5 * high, 15);
        multiplier = (multiplier * imbe_isqrt(shape << 15)) >> 15;
    }
    if (state->previous_error[0] > 16384 && group >= 1) return 0;
    threshold = (state->previous_voiced & (1u << group)) ? 18432u : 14746u;
    frequency_factor = 32768u - ((group * (frequency >> 16) * 7968u) >> 13);
    threshold = (threshold * frequency_factor) >> 15;
    return (threshold * multiplier) >> 15;
}

void imbe_encoder_init(imbe_encoder *state)
{
    unsigned row, i;
    if (!state) return;
    memset(state, 0, sizeof(*state));
    for (row = 0; row < 3; ++row)
        for (i = 0; i < IMBE_PITCH_CANDIDATES; ++i) state->pitch_error[row][i] = 32768;
    state->previous_pitch[0] = state->previous_pitch[1] = 200;
    state->energy_peak = 20000;
}

int imbe_analyze(imbe_encoder *state, const int16_t pcm[160], imbe_model *model)
{
    unsigned i, period2, shifts, h, k;
    int normalization;
    uint32_t low = 0, high = 0, raw_energy, total;
    int32_t log_energy;
    if (!state || !pcm || !model) return 0;
    memmove(state->history, state->history + 160, 640 * sizeof(int16_t));
    for (i = 0; i < 160; ++i) {
        int32_t product = state->dc_output * 32440 + state->dc_remainder;
        int32_t filtered = product >> 15;
        state->dc_remainder = product - filtered * 32768;
        filtered += (int32_t)pcm[i] - state->previous_input;
        if (filtered > 65535) filtered = 65535;
        if (filtered < -65535) filtered = -65535;
        state->previous_input = pcm[i]; state->dc_output = filtered;
        state->history[640 + i] = clip16(filtered);
    }
    memmove(state->pitch_error[0], state->pitch_error[1], 2 * sizeof(state->pitch_error[0]));
    coarse_errors(state, state->pitch_error[2]);
    period2 = tracked_pitch(state);
    shifts = spectrum(state, &normalization);
    (void)refine_pitch(state, period2, model);
    for (k = 0; k < 12; ++k) {
        state->scratch.spectrum.group_energy[k] = 0;
        state->scratch.spectrum.group_captured[k] = 0;
    }
    for (i = 0; i <= 128; ++i) {
        int32_t a = state->scratch.spectrum.fft[2 * i] >> 4;
        int32_t b = state->scratch.spectrum.fft[2 * i + 1] >> 4;
        uint32_t power = (uint32_t)(a * a + b * b);
        if (i < 64) low += power; else high += power;
    }
    total = low + high;
    log_energy = imbe_log2_u32_q11(total) + 2 * ((int)shifts + 4 - normalization) * 2048
                 - 2 * REFINEMENT_LOGSUM_Q11;
    raw_energy = total ? imbe_exp2_q11(log_energy - 12 * 2048) : 0;
    if (raw_energy > 0x3fffffffu) raw_energy = 0x3fffffffu;
    if (raw_energy > state->energy_peak) state->energy_peak = (raw_energy + state->energy_peak) / 2;
    else state->energy_peak -= state->energy_peak / 100 - raw_energy / 100;
    if (state->energy_peak < 20000) state->energy_peak = 20000;
    for (h = 0; h < model->L; ++h) {
        harmonic_fit estimate = fit(state->scratch.spectrum.fft, model->frequency_q32, h + 1);
        uint32_t frequency = model->frequency_q32 >> 8;
        unsigned first = ((2 * h + 1) * frequency + 131071u) >> 17;
        unsigned last = ((2 * h + 3) * frequency + 131071u) >> 17;
        int32_t e = imbe_log2_u32_q11(estimate.energy);
        int32_t base = ((int)shifts + 4 - normalization) * 2048 - REFINEMENT_LOGSUM_Q11;
        int32_t voiced = (e - imbe_log2_u32_q11(estimate.denominator)) / 2 + 12 * 2048 + base;
        int32_t unvoiced = (e - imbe_log2_u32_q11(last > first ? last - first : 1)) / 2 + base;
        k = imbe_voicing_group(h, model->L);
        if (k > 11) return 0;
        state->scratch.spectrum.group_energy[k] += estimate.energy;
        state->scratch.spectrum.group_captured[k] += estimate.captured;
        model->log_amplitude[h] = estimate.energy ? clip16(voiced) : -32768;
        state->scratch.spectrum.unvoiced_log[h] = estimate.energy ? clip16(unvoiced) : -32768;
    }
    model->voiced = 0;
    for (k = 0; k < model->K; ++k) {
        uint32_t energy = state->scratch.spectrum.group_energy[k];
        uint32_t residual = energy - state->scratch.spectrum.group_captured[k];
        unsigned threshold = voicing_threshold(state, k, model->frequency_q32, low, high, raw_energy);
        if (energy && ratio(residual, energy, 15) < threshold) model->voiced |= (uint16_t)(1u << k);
    }
    for (h = 0; h < model->L; ++h)
        if (!(model->voiced & (1u << imbe_voicing_group(h, model->L))))
            model->log_amplitude[h] = state->scratch.spectrum.unvoiced_log[h];
    for (; h < IMBE_MAX_HARMONICS; ++h) model->log_amplitude[h] = -32768;
    state->previous_voiced = model->voiced;
    ++state->frames;
    return 1;
}

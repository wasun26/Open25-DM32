/* SPDX-License-Identifier: MIT */
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "dm32_imbe.h"
extern "C" {
#include "mbelib.h"
}

namespace {
constexpr double pi = 3.14159265358979323846;
using Clock = std::chrono::steady_clock;

void require(bool test, const char *message) {
    if (!test) {
        std::cerr << message << "\n";
        std::exit(1);
    }
}

std::vector<int16_t> generated_input(size_t frames) {
    std::vector<int16_t> samples(frames * 160);
    uint32_t rng = 0x12345678u;
    for (size_t i = 0; i < samples.size(); ++i) {
        const unsigned type = (i / (160 * 50)) % 6;
        rng = rng * 1664525u + 1013904223u;
        double value = 0;
        if (type == 1)
            value = 12000.0 * std::sin(2 * pi * 300 * i / 8000.0);
        else if (type == 2) {
            const double f = 85.0 + 115.0 * ((i % 8000) / 8000.0);
            value = 5000 * std::sin(2 * pi * f * i / 8000.0)
                  + 3500 * std::sin(2 * pi * 2 * f * i / 8000.0)
                  + 1800 * std::sin(2 * pi * 4 * f * i / 8000.0);
        } else if (type == 3)
            value = static_cast<int16_t>(rng >> 16) / 2;
        else if (type == 4)
            value = (i & 1) ? 32767 : -32768;
        else if (type == 5)
            value = (i % 97 == 0) ? 32767 : 0;
        samples[i] = static_cast<int16_t>(value);
    }
    return samples;
}

void write_samples(std::ofstream &out, const int16_t *samples, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        const uint16_t word = static_cast<uint16_t>(samples[i]);
        const char bytes[2] = {static_cast<char>(word & 255),
                               static_cast<char>(word >> 8)};
        out.write(bytes, 2);
    }
}

void print_timing(const char *name, std::vector<double> samples) {
    std::sort(samples.begin(), samples.end());
    double sum = 0;
    for (double x : samples) sum += x;
    std::cout << "  \"" << name << "_host_microseconds\": {\"min\": " << samples.front()
              << ", \"mean\": " << sum / samples.size()
              << ", \"median\": " << samples[samples.size()/2]
              << ", \"p99\": " << samples[(samples.size()-1)*99/100]
              << ", \"max\": " << samples.back() << "},\n";
}
}

int main(int argc, char **argv) {
    size_t frames = 2000;
    size_t fuzz_frames = 10000;
    std::string input_path;
    std::string out_prefix;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
            frames = std::stoul(argv[++i]);
        else if (std::strcmp(argv[i], "--fuzz") == 0 && i + 1 < argc)
            fuzz_frames = std::stoul(argv[++i]);
        else if (std::strcmp(argv[i], "--input") == 0 && i + 1 < argc)
            input_path = argv[++i];
        else if (std::strcmp(argv[i], "--out-prefix") == 0 && i + 1 < argc)
            out_prefix = argv[++i];
        else {
            std::cerr << "usage: codec_harness [--frames N] [--fuzz N] [--input mono-8k.s16le] [--out-prefix PATH]\n";
            return 2;
        }
    }
    require(frames > 0 && frames <= 100000, "frames outside 1..100000");
    require(fuzz_frames <= 1000000, "fuzz frames exceed 1000000");
    std::vector<int16_t> pcm;
    if (input_path.empty()) {
        pcm = generated_input(frames);
    } else {
        std::ifstream in(input_path, std::ios::binary);
        require(in.good(), "cannot open PCM input");
        char pair[2];
        while (in.read(pair, 2))
            pcm.push_back(static_cast<int16_t>(static_cast<unsigned char>(pair[0]) |
                          (static_cast<unsigned char>(pair[1]) << 8)));
        require(!pcm.empty() && in.gcount() == 0, "empty or odd-length PCM input");
        frames = (pcm.size() + 159) / 160;
        pcm.resize(frames * 160, 0);
    }

    dm32_imbe_context encoder, decoder, decoder_duplicate;
    dm32_imbe_init(&encoder);
    dm32_imbe_init(&decoder);
    dm32_imbe_init(&decoder_duplicate);
    mbe_parms current, previous, previous_enhanced;
    mbe_initMbeParms(&current, &previous, &previous_enhanced);
    mbe_parms shifted_current, shifted_previous, shifted_enhanced;
    mbe_initMbeParms(&shifted_current, &shifted_previous, &shifted_enhanced);

    size_t packing_checks = 0, parameter_checks = 0;
    size_t shifted_parameter_mismatches = 0, mbelib_rejected = 0, fixed_nonzero = 0, mbe_nonzero = 0;
    std::vector<double> encode_us, decode_us;
    std::ofstream encoded, original, fixed, reference;
    if (!out_prefix.empty()) {
        encoded.open(out_prefix + ".imbe88", std::ios::binary);
        original.open(out_prefix + ".input.s16le", std::ios::binary);
        fixed.open(out_prefix + ".fixed.s16le", std::ios::binary);
        reference.open(out_prefix + ".mbelib.s16le", std::ios::binary);
        require(encoded.good() && original.good() && fixed.good() && reference.good(), "cannot create outputs");
    }
    for (size_t f = 0; f < frames; ++f) {
        uint16_t u[8], roundtrip[8];
        uint8_t packed[11], shifted_packed[11];
        int16_t output[160], output_duplicate[160], reference_output[160];
        const auto t0 = Clock::now();
        require(dm32_imbe_encode(&encoder, &pcm[f * 160], u), "codec arithmetic fault while encoding");
        const auto t1 = Clock::now();
        require(dm32_imbe_pack(u, packed), "out-of-range encoder parameters");
        dm32_imbe_unpack(packed, roundtrip);
        require(std::memcmp(u, roundtrip, sizeof(u)) == 0, "codec payload packing mismatch");
        ++packing_checks;

        const auto d0 = Clock::now();
        require(dm32_imbe_decode(&decoder, u, output), "encoder produced invalid pitch");
        const auto d1 = Clock::now();
        require(dm32_imbe_decode(&decoder_duplicate, u, output_duplicate), "duplicate decoder rejected pitch");
        require(std::memcmp(output, output_duplicate, sizeof(output)) == 0, "per-context RNG or decoder state leaked");
        encode_us.push_back(std::chrono::duration<double, std::micro>(t1-t0).count());
        decode_us.push_back(std::chrono::duration<double, std::micro>(d1-d0).count());

        char bits[88];
        for (unsigned b = 0; b < 88; ++b)
            bits[b] = (packed[b / 8] >> (7-b%8)) & 1u;
        const int reject = mbe_decodeImbe4400Parms(bits, &current, &previous);
        require(reject == 0, "mbelib rejected codec-generated parameters");
        require(current.L >= 9 && current.L <= 56 && current.K >= 3 && current.K <= 12,
                "independent parameter decoder produced invalid dimensions");
        require(std::isfinite(current.w0) && current.w0 > 0, "invalid decoded pitch");
        ++parameter_checks;

        /* Evaluate the tempting OP25 shifted-u7 convention independently.
         * This observation guards integration, not a claimed air test.
         */
        uint16_t shifted_u[8];
        std::memcpy(shifted_u, u, sizeof(u));
        shifted_u[7] >>= 1;
        dm32_imbe_pack(shifted_u, shifted_packed);
        char shifted_bits[88];
        for (unsigned b = 0; b < 88; ++b)
            shifted_bits[b] = (shifted_packed[b/8] >> (7-b%8)) & 1u;
        const int shifted_reject = mbe_decodeImbe4400Parms(shifted_bits, &shifted_current, &shifted_previous);
        const unsigned pitch = ((u[0] >> 4) & 0xfcu) | ((u[7] >> 1) & 3u);
        const unsigned shifted_pitch = ((u[0] >> 4) & 0xfcu) | ((shifted_u[7] >> 1) & 3u);
        shifted_parameter_mismatches += shifted_reject || pitch != shifted_pitch;
        mbe_moveMbeParms(&shifted_current, &shifted_previous);

        int errs = 0, errs2 = 0;
        char err[128];
        mbe_processImbe4400Data(reference_output, &errs, &errs2, err, bits,
                               &current, &previous, &previous_enhanced, 3);
        mbelib_rejected += err[0] != '\0';
        for (unsigned j = 0; j < 160; ++j) {
            fixed_nonzero += output[j] != 0;
            mbe_nonzero += reference_output[j] != 0;
        }
        if (!out_prefix.empty()) {
            encoded.write(reinterpret_cast<char *>(packed), 11);
            write_samples(original, &pcm[f * 160], 160);
            write_samples(fixed, output, 160);
            write_samples(reference, reference_output, 160);
        }
    }
    /* Negative checks: reserved pitch must mute, and width violations reject. */
    uint16_t invalid[8] = {0xfc0, 0, 0, 0, 0, 0, 0, 6};
    int16_t mute[160];
    require(!dm32_imbe_decode(&decoder, invalid, mute), "reserved pitch accepted");
    for (int16_t s : mute) require(s == 0, "invalid frame not muted");
    invalid[0] = 0x1000;
    uint8_t bytes[11];
    require(!dm32_imbe_pack(invalid, bytes), "oversized parameter silently truncated");

    size_t fuzz_valid = 0, fuzz_reserved = 0, fuzz_faults = 0;
    uint32_t fuzz_rng = 0xb385f673u;
    for (size_t i = 0; i < fuzz_frames; ++i) {
        uint16_t arbitrary[8], unpacked[8];
        for (unsigned j = 0; j < 8; ++j) {
            fuzz_rng = fuzz_rng * 1664525u + 1013904223u;
            const unsigned width = j < 4 ? 12 : j < 7 ? 11 : 7;
            arbitrary[j] = (fuzz_rng >> 16) & ((1u << width) - 1);
        }
        require(dm32_imbe_pack(arbitrary, bytes), "valid-width arbitrary payload rejected");
        dm32_imbe_unpack(bytes, unpacked);
        require(std::memcmp(arbitrary, unpacked, sizeof(arbitrary)) == 0, "random pack roundtrip mismatch");
        const unsigned pitch = ((arbitrary[0] >> 4) & 0xfcu) | ((arbitrary[7] >> 1) & 3u);
        const int ok = dm32_imbe_decode(&decoder, arbitrary, mute);
        if (pitch > 207) {
            require(!ok, "fuzz reserved pitch accepted");
            ++fuzz_reserved;
        } else if (!ok) {
            ++fuzz_faults;
            dm32_imbe_init(&decoder);
        } else {
            ++fuzz_valid;
        }
        if (i % 101 == 0) dm32_imbe_init(&decoder);
    }
    /* Reinitialize after hostile inputs: valid traffic must reproduce exactly. */
    dm32_imbe_context fresh;
    dm32_imbe_init(&fresh); dm32_imbe_init(&decoder);
    const uint16_t reset_fixture[8] = {0x09b, 0x088, 0x0cc, 0x621, 0x7b4, 0x02a, 0x04c, 0};
    int16_t reset_pcm[160];
    require(dm32_imbe_decode(&fresh, reset_fixture, reset_pcm) &&
            dm32_imbe_decode(&decoder, reset_fixture, mute) &&
            !std::memcmp(reset_pcm, mute, sizeof mute), "reset retained stale scratch/history");

    /* The radio shares one context across half-duplex direction changes.
     * Invalid frames must preserve the active direction and its history. */
    dm32_imbe_context shared, expected;
    dm32_imbe_init(&shared);
    uint16_t shared_words[8], expected_words[8];
    for (unsigned i = 0; i < 12; ++i)
        require(dm32_imbe_encode(&shared, &pcm[(i % frames) * 160], shared_words), "shared encode rejected");
    dm32_imbe_init(&expected);
    require(dm32_imbe_decode(&shared, reset_fixture, mute) &&
            dm32_imbe_decode(&expected, reset_fixture, reset_pcm) &&
            !std::memcmp(mute, reset_pcm, sizeof mute), "encode-to-decode retained encoder state");
    dm32_imbe_context saved = shared;
    invalid[0] = 0x1000;
    require(!dm32_imbe_decode(&shared, invalid, mute) &&
            !std::memcmp(&saved, &shared, sizeof shared), "rejected decode advanced state");
    dm32_imbe_init(&expected);
    require(dm32_imbe_encode(&shared, pcm.data(), shared_words) &&
            dm32_imbe_encode(&expected, pcm.data(), expected_words) &&
            !std::memcmp(shared_words, expected_words, sizeof shared_words), "decode-to-encode retained decoder state");

    /* Independently supplied OP25 DEBUG_TX fixture, pinned source:
     * boatbod/op25 3f44d43eb449bb4753247ce848bb42a403823dea,
     * p25p1_voice_encode.cc:1283..1290. This is an implementation test
     * vector, not a TIA conformance vector or an RF capture.
     */
    dm32_imbe_init(&decoder);
    mbe_initMbeParms(&current, &previous, &previous_enhanced);
    for (unsigned i = 0; i < 100; ++i) {
        const uint16_t fixture[8] = {0x09b, 0x088, 0x0cc, 0x621,
                                     0x7b4, 0x02a, 0x04c, static_cast<uint16_t>(i&1)};
        require(dm32_imbe_decode(&decoder, fixture, mute), "independent OP25 vector rejected");
        dm32_imbe_pack(fixture, bytes);
        char bits[88];
        for (unsigned b = 0; b < 88; ++b)
            bits[b] = (bytes[b/8] >> (7-b%8)) & 1;
        require(mbe_decodeImbe4400Parms(bits, &current, &previous) == 0,
                "mbelib rejected OP25 fixture");
        require(current.L == 11 && current.K == 4, "OP25 fixture harmonic/band mismatch");
        mbe_moveMbeParms(&current, &previous);
    }

    std::cout << "{\n  \"platform\": \"host-only; no CK803S or RF timing claim\",\n"
              << "  \"frames\": " << frames << ",\n"
              << "  \"pcm_sample_rate\": 8000,\n"
              << "  \"frame_duration_us\": 20000,\n"
              << "  \"state_bytes\": " << dm32_imbe_state_bytes() << ",\n"
              << "  \"context_capacity_bytes\": " << sizeof(dm32_imbe_context) << ",\n"
              << "  \"packing_roundtrip_checks\": " << packing_checks << ",\n"
              << "  \"mbelib_parameter_admission_checks\": " << parameter_checks << ",\n"
              << "  \"shifted_u7_pitch_mismatches\": " << shifted_parameter_mismatches << ",\n"
              << "  \"mbelib_error_strings\": " << mbelib_rejected << ",\n"
              << "  \"fixed_nonzero_samples\": " << fixed_nonzero << ",\n"
              << "  \"mbelib_nonzero_samples\": " << mbe_nonzero << ",\n";
    print_timing("encode", encode_us);
    print_timing("decode", decode_us);
    std::cout << "  \"negative_pitch_and_width_checks\": \"pass\",\n"
              << "  \"half_duplex_direction_reset_and_rejected_frame_state\": \"pass\",\n"
              << "  \"independent_op25_debug_vector_frames\": 100,\n"
              << "  \"random_payload_checks\": {\"decoded\": " << fuzz_valid
              << ", \"reserved_muted\": " << fuzz_reserved
              << ", \"arithmetic_faults_muted\": " << fuzz_faults << "},\n"
              << "  \"heap_in_codec_path\": false,\n"
              << "  \"rf_or_radio_validation\": false\n}\n";
}

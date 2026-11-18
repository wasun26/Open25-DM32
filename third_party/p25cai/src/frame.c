/* SPDX-License-Identifier: MIT
 * TIA-102.BAAA-B 4.2,4.4-4.6,7.2-7.5,Annex A. See SOURCES.md. */
#include "internal.h"

#define CAI_SYNC UINT64_C(0x5575f5ff77ff)
#define CAI_SYNC_MASK UINT64_C(0xffffffffffff)

size_t p25cai_frame_size(uint8_t duid)
{
    switch (duid) {
    case P25CAI_HDU: return 99;
    case P25CAI_TDU: return 18;
    case P25CAI_LDU1: case P25CAI_LDU2: return 216;
    case P25CAI_TDULC: return 54;
    default: return 0;
    }
}

/* Status insertion includes any trailing status after the last 70 data bits.
 * These five formats have data lengths which are exact multiples of 70. */
static void add_status(const uint8_t *logical, uint8_t *air,
                        size_t bytes, unsigned status)
{
    size_t cursor = 0;
    memset(air, 0, bytes);
    for (size_t bit = 0; bit < bytes * 8; ++bit) {
        unsigned position = (unsigned)(bit % 72);
        if (position >= 70) cai_set(air, bit, (status >> (71 - position)) & 1);
        else cai_set(air, bit, cai_bit(logical, cursor++));
    }
}

static void remove_status(const uint8_t *air, uint8_t *logical, size_t bytes)
{
    size_t cursor = 0;
    memset(logical, 0, 210);
    for (size_t bit = 0; bit < bytes * 8; ++bit)
        if (bit % 72 < 70) cai_set(logical, cursor++, cai_bit(air, bit));
}

static unsigned metadata_bytes(uint8_t duid)
{
    return duid == P25CAI_HDU ? 15 : duid == P25CAI_LDU2 ? 12 : 9;
}

static void metadata_encode(const p25cai_frame *frame, uint8_t *encoded)
{
    unsigned bytes = metadata_bytes(frame->duid);
    unsigned k = bytes * 8 / 6, n = k == 20 ? 36 : 24;
    uint8_t symbols[36];
    for (unsigned i = 0; i < k; ++i)
        symbols[i] = (uint8_t)cai_read(frame->metadata, i * 6, 6);
    p25cai_rs_encode(symbols, n, k, symbols);
    memset(encoded, 0, 81);
    if (frame->duid == P25CAI_HDU) {
        for (unsigned i = 0; i < n; ++i) {
            uint32_t word;
            p25cai_golay_encode(symbols[i], 18, &word);
            cai_write(encoded, i * 18, 18, word);
        }
    } else if (frame->duid == P25CAI_TDULC) {
        for (unsigned i = 0; i < 12; ++i) {
            uint32_t word;
            p25cai_golay_encode((uint16_t)((symbols[2*i] << 6) | symbols[2*i+1]), 24, &word);
            cai_write(encoded, i * 24, 24, word);
        }
    } else {
        for (unsigned i = 0; i < n; ++i) {
            uint16_t word;
            p25cai_hamming_encode(symbols[i], 10, &word);
            cai_write(encoded, i * 10, 10, word);
        }
    }
}

static int metadata_decode(const uint8_t *encoded, p25cai_frame *frame, int *inner)
{
    unsigned bytes = metadata_bytes(frame->duid);
    unsigned k = bytes * 8 / 6, n = k == 20 ? 36 : 24;
    uint8_t symbols[36];
    int total = 0, valid = 1;
    for (unsigned i = 0; i < n;) {
        uint16_t data;
        int correction;
        if (frame->duid == P25CAI_HDU) {
            uint32_t word = (uint32_t)cai_read(encoded, i * 18, 18);
            correction = p25cai_golay_decode(word,18,&data);
            if (correction < 0) data = (uint16_t)(word >> 12);
            symbols[i++] = (uint8_t)data;
        } else if (frame->duid == P25CAI_TDULC) {
            uint32_t word = (uint32_t)cai_read(encoded, (i / 2) * 24, 24);
            correction = p25cai_golay_decode(word,24,&data);
            if (correction < 0) data = (uint16_t)(word >> 12);
            symbols[i++] = (uint8_t)(data >> 6);
            symbols[i++] = (uint8_t)(data & 63);
        } else {
            uint16_t word = (uint16_t)cai_read(encoded, i * 10, 10);
            correction = p25cai_hamming_decode(word,10,&data);
            if (correction < 0) data = word >> 4;
            symbols[i++] = (uint8_t)data;
        }
        if (correction < 0) valid = 0;
        else total += correction;
    }
    *inner = valid ? total : -1;
    int result = p25cai_rs_decode(symbols, n, k);
    if (result < 0) return result;
    for (unsigned i = 0; i < k; ++i) cai_write(frame->metadata, i * 6, 6, symbols[i]);
    return result;
}

int p25cai_frame_encode(const p25cai_frame *frame, uint8_t *air, size_t capacity)
{
    if (!frame || !air || frame->status > 3 || frame->nac > 0xfff) return P25CAI_INVALID;
    size_t bytes = p25cai_frame_size(frame->duid);
    if (!bytes) return P25CAI_UNSUPPORTED;
    if (capacity < bytes) return P25CAI_LENGTH;
    uint8_t logical[210] = {0}, metadata[81];
    uint64_t nid;
    p25cai_nid_encode(frame->nac, frame->duid, &nid);
    cai_write(logical, 0, 48, CAI_SYNC);
    cai_write(logical, 48, 64, nid);
    if (frame->duid != P25CAI_TDU) metadata_encode(frame, metadata);
    if (frame->duid == P25CAI_HDU || frame->duid == P25CAI_TDULC) {
        unsigned length = frame->duid == P25CAI_HDU ? 648 : 288;
        for (unsigned bit = 0; bit < length; ++bit)
            cai_set(logical, 112 + bit, cai_bit(metadata, bit));
    } else if (frame->duid == P25CAI_LDU1 || frame->duid == P25CAI_LDU2) {
        unsigned cursor = 112, metadata_cursor = 0;
        for (unsigned index = 0; index < 9; ++index) {
            uint8_t voice[18];
            int result = p25cai_voice_encode(frame->voice[index], voice);
            if (result < 0) return result;
            for (unsigned bit = 0; bit < 144; ++bit)
                cai_set(logical, cursor++, cai_bit(voice, bit));
            if (index >= 1 && index <= 6)
                for (unsigned bit = 0; bit < 40; ++bit)
                    cai_set(logical, cursor++, cai_bit(metadata, metadata_cursor++));
            if (index == 7)
                for (unsigned i = 0; i < 2; ++i) {
                    uint16_t lsd;
                    p25cai_lsd_encode(frame->low_speed[i], &lsd);
                    cai_write(logical, cursor, 16, lsd);
                    cursor += 16;
                }
        }
    }
    add_status(logical, air, bytes, frame->status);
    return (int)bytes;
}

int p25cai_frame_decode_ex(const uint8_t *air, size_t bytes, unsigned sync_tolerance,
                           p25cai_frame *frame, p25cai_quality *quality)
{
    if (!air || !frame || !quality || sync_tolerance > 4) return P25CAI_INVALID;
    if (bytes < 18 || bytes > P25CAI_MAX_FRAME_BYTES) return P25CAI_LENGTH;
    if (cai_weight(cai_read(air, 0, 48) ^ CAI_SYNC) > sync_tolerance) return P25CAI_INVALID;
    uint8_t logical[210], metadata[81] = {0};
    remove_status(air, logical, bytes);
    p25cai_frame decoded = {0};
    p25cai_quality q = {0};
    q.metadata_corrected = P25CAI_UNSUPPORTED;
    for (unsigned i = 0; i < 9; ++i) q.voice_corrected[i] = P25CAI_UNSUPPORTED;
    for (unsigned i = 0; i < 2; ++i) q.low_speed_corrected[i] = P25CAI_UNSUPPORTED;
    q.nid_corrected = p25cai_nid_decode(cai_read(logical, 48, 64), &decoded.nac, &decoded.duid);
    if (q.nid_corrected < 0) return q.nid_corrected;
    size_t required = p25cai_frame_size(decoded.duid);
    if (!required) return P25CAI_UNSUPPORTED;
    if (required != bytes) return P25CAI_LENGTH;
    decoded.status = (uint8_t)cai_read(air, 70, 2);
    if (decoded.duid == P25CAI_HDU || decoded.duid == P25CAI_TDULC) {
        unsigned length = decoded.duid == P25CAI_HDU ? 648 : 288;
        for (unsigned bit = 0; bit < length; ++bit)
            cai_set(metadata, bit, cai_bit(logical, 112 + bit));
    } else if (decoded.duid == P25CAI_LDU1 || decoded.duid == P25CAI_LDU2) {
        unsigned cursor = 112, metadata_cursor = 0;
        for (unsigned index = 0; index < 9; ++index) {
            uint8_t voice[18] = {0};
            for (unsigned bit = 0; bit < 144; ++bit)
                cai_set(voice, bit, cai_bit(logical, cursor++));
            q.voice_corrected[index] = p25cai_voice_decode_ex(voice,decoded.voice[index],&q.voice_u0_corrected[index]);
            if (q.voice_corrected[index] >= 0) q.voice_valid |= (uint16_t)(1u << index);
            if (index >= 1 && index <= 6)
                for (unsigned bit = 0; bit < 40; ++bit)
                    cai_set(metadata, metadata_cursor++, cai_bit(logical, cursor++));
            if (index == 7)
                for (unsigned i = 0; i < 2; ++i) {
                    q.low_speed_corrected[i] = p25cai_lsd_decode(
                        (uint16_t)cai_read(logical, cursor, 16), &decoded.low_speed[i]);
                    if (q.low_speed_corrected[i] >= 0) q.low_speed_valid |= (uint8_t)(1u << i);
                    cursor += 16;
                }
        }
    }
    if (decoded.duid != P25CAI_TDU) {
        q.metadata_corrected = metadata_decode(metadata,&decoded,&q.metadata_inner_corrected);
        q.metadata_valid = q.metadata_corrected >= 0;
    }
    *frame = decoded;
    *quality = q;
    return 0;
}

int p25cai_frame_decode(const uint8_t *air, size_t bytes,
                        p25cai_frame *frame, p25cai_quality *quality)
{
    return p25cai_frame_decode_ex(air,bytes,2,frame,quality);
}

int p25cai_receiver_init(p25cai_receiver *rx, uint16_t nac_filter)
{
    return p25cai_receiver_init_ex(rx,nac_filter,2,0);
}

int p25cai_receiver_init_ex(p25cai_receiver *rx, uint16_t nac_filter,
                           unsigned sync_tolerance, unsigned reacquire_in_body)
{
    if (!rx || (nac_filter > 0xfff && nac_filter != P25CAI_ANY_NAC) ||
        sync_tolerance > 4 || reacquire_in_body > 1) return P25CAI_INVALID;
    memset(rx, 0, sizeof *rx);
    rx->expected_nac = nac_filter;
    rx->max_sync_errors = (uint8_t)(sync_tolerance | (reacquire_in_body ? 0 : 0x80));
    rx->status_symbol = 2;
    return 0;
}

void p25cai_receiver_gap(p25cai_receiver *rx)
{
    if (!rx) return;
    rx->sync_shift = 0;
    rx->symbols = rx->target_symbols = 0;
    rx->status_valid = false;
    rx->status_symbol = 2;
    ++rx->status_updates;
}

int p25cai_receiver_push(p25cai_receiver *rx, uint8_t dibit,
                         uint8_t out[P25CAI_MAX_FRAME_BYTES], size_t *bytes)
{
    if (!rx || !out || !bytes) return P25CAI_INVALID;
    if (dibit > 3) { p25cai_receiver_gap(rx); return P25CAI_INVALID; }
    rx->sync_shift = ((rx->sync_shift << 2) | dibit) & CAI_SYNC_MASK;
    if (rx->symbols_seen != UINT32_MAX) ++rx->symbols_seen;
    if (rx->symbols_seen < 24) return 0;
    if (!rx->symbols || !(rx->max_sync_errors & 0x80)) {
        uint64_t difference = rx->sync_shift ^ CAI_SYNC;
        unsigned normal = cai_weight(difference);
        unsigned inverted = cai_weight(difference ^ UINT64_C(0xaaaaaaaaaaaa));
        unsigned tolerance = rx->max_sync_errors & 7;
        if (normal <= tolerance || inverted <= tolerance) {
            if (rx->symbols) ++rx->truncated_frames;
            ++rx->sync_acquisitions;
            rx->inverted = inverted < normal;
            memset(rx->frame,0,sizeof rx->frame);
            cai_write(rx->frame,0,48,rx->sync_shift ^ (rx->inverted ? UINT64_C(0xaaaaaaaaaaaa) : 0));
            rx->symbols = 24;
            rx->target_symbols = 0;
            rx->status_valid = false;
            rx->status_symbol = 2;
            ++rx->status_updates;
            return 0;
        }
    }
    if (!rx->symbols) return 0;
    if (rx->target_symbols && rx->symbols % 36 == 35) {
        rx->status_symbol = dibit ^ (rx->inverted ? 2 : 0);
        rx->status_valid = true;
        ++rx->status_updates;
    }
    cai_write(rx->frame,rx->symbols*2,2,dibit ^ (rx->inverted ? 2 : 0));
    ++rx->symbols;
    if (rx->symbols == 57) {
        uint8_t logical[210];
        uint16_t nac;
        uint8_t duid;
        /* All bytes after the partial NID are still zero, initialized at sync. */
        remove_status(rx->frame,logical,15);
        int result = p25cai_nid_decode(cai_read(logical, 48, 64), &nac, &duid);
        size_t length = result >= 0 ? p25cai_frame_size(duid) : 0;
        if (!length || (rx->expected_nac != P25CAI_ANY_NAC && nac != rx->expected_nac)) {
            p25cai_receiver_gap(rx);
            ++rx->bad_nids;
            return 0;
        }
        rx->nid.nac = nac;
        rx->nid.duid = duid;
        rx->nid.corrected_bits = (uint8_t)result;
        rx->target_symbols = (uint16_t)(length*4);
    }
    if (rx->target_symbols && rx->symbols == rx->target_symbols) {
        size_t length = rx->target_symbols/4;
        if (out != rx->frame) memcpy(out,rx->frame,length);
        *bytes = length;
        rx->symbols = 0;
        rx->target_symbols = 0;
        ++rx->complete_frames;
        return 1;
    }
    return 0;
}

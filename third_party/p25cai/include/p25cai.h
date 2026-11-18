/* SPDX-License-Identifier: MIT */
#ifndef P25CAI_H
#define P25CAI_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { P25CAI_INVALID = -1, P25CAI_UNCORRECTABLE = -2,
       P25CAI_LENGTH = -3, P25CAI_UNSUPPORTED = -4,
       P25CAI_MAX_FRAME_BYTES = 216, P25CAI_ANY_NAC = 0xffff };
enum { P25CAI_HDU = 0, P25CAI_TDU = 3, P25CAI_LDU1 = 5, P25CAI_TSDU = 7,
       P25CAI_LDU2 = 10, P25CAI_PDU = 12, P25CAI_TDULC = 15 };

/* All packed byte arrays are MSB first, in transmission order. Codewords
 * passed as integers have their first transmitted bit in the highest used bit.
 * Negative results leave outputs unchanged, unless explicitly documented. */

/* bits = 18, 23 or 24 (Golay); bits = 10 or 15 (Hamming).
 * Decoders return number of corrected bits, or a negative error.
 * Beyond the correction radius a decoder can miscorrect to another codeword. */
int p25cai_golay_encode(uint16_t data, unsigned bits, uint32_t *word);
int p25cai_golay_decode(uint32_t word, unsigned bits, uint16_t *data);
int p25cai_hamming_encode(uint16_t data, unsigned bits, uint16_t *word);
int p25cai_hamming_decode(uint16_t word, unsigned bits, uint16_t *data);
int p25cai_lsd_encode(uint8_t data, uint16_t *word);
int p25cai_lsd_decode(uint16_t word, uint8_t *data);

/* Shortened GF(64) RS, first root 1: n<=63, k>0, n-k=8,12 or16.
 * The voice formats use (n,k)=(36,20),(24,12),(24,16). The wider shortening
 * range retains the existing Open25-DM32 Next helper contract.
 * Each byte holds one GF(64) element, 0..63. Decode commits on success and
 * returns corrected symbols. Input/output may overlap in encode. */
int p25cai_rs_encode(const uint8_t *data, unsigned n, unsigned k, uint8_t *word);
int p25cai_rs_decode(uint8_t *word, unsigned n, unsigned k);

/* NID encoding accepts a four-bit DUID using the Table 19 linear code.
 * Decoding accepts the six BAAA-B DUIDs and the AABB-B TSDU DUID 7.
 * Decode accepts at most 11 changed bits including the final P bit. */
int p25cai_nid_encode(uint16_t nac, uint8_t duid, uint64_t *word);
int p25cai_nid_decode(uint64_t word, uint16_t *nac, uint8_t *duid);

/* Codec boundary: u[0..3] are 12 bits, u[4..6] 11 bits, u[7] 7 bits.
 * u[7] is unshifted. No synthesis, analysis, encryption or sync-bit policy.
 * The caller supplies the required alternating voice sync bit. */
int p25cai_voice_encode(const uint16_t u[8], uint8_t air[18]);
int p25cai_voice_decode(const uint8_t air[18], uint16_t u[8]);
int p25cai_voice_decode_ex(const uint8_t air[18], uint16_t u[8], uint8_t *u0_errors);
/* Serialized coded-word representation for callers that interleave in their
 * own frame assembler. The same FEC/whitening implementation owns both forms. */
int p25cai_voice_encode_serial(const uint16_t u[8], uint8_t coded[18]);
int p25cai_voice_decode_serial(const uint8_t coded[18], uint16_t u[8], uint8_t *u0_errors);

typedef struct {
    uint16_t nac;
    uint8_t duid;
    uint8_t status; /* encoder repeats this dibit; decoder reports first */
    /* Raw HDU: 15 bytes; LDU1/TDULC LC: 9; LDU2 ES: 12; TDU: 0.
     * Semantic field validation and call/audio policy belong to the caller. */
    uint8_t metadata[15];
    uint16_t voice[9][8];
    uint8_t low_speed[2];
} p25cai_frame;

typedef struct {
    int nid_corrected;
    int metadata_corrected; /* RS symbols, not sum of inner corrected bits */
    int metadata_inner_corrected; /* -1 if any inner word could not be decoded */
    int voice_corrected[9];
    uint8_t voice_u0_corrected[9];
    int low_speed_corrected[2];
    uint8_t metadata_valid;
    uint16_t voice_valid; /* bit i means voice[i] was decoded */
    uint8_t low_speed_valid;
} p25cai_quality;

/* Five fixed-size voice data units only. PDU is handled by block FEC below.
 * Encode returns bytes written. Decode returns 0 when NID/framing succeeds;
 * inspect quality before using payload. Invalid payload slots are zeroed.
 * Exact input length required; at most two sync bit errors accepted (policy).
 * Decode does not normalize polarity; use the receiver for inverted dibits. */
size_t p25cai_frame_size(uint8_t duid);
int p25cai_frame_encode(const p25cai_frame *frame, uint8_t *air, size_t capacity);
int p25cai_frame_decode(const uint8_t *air, size_t bytes,
                        p25cai_frame *frame, p25cai_quality *quality);
/* Explicit acquisition policy for existing callers that allow a different
 * sync threshold. sync_tolerance is 0..4; the default decoder uses two. */
int p25cai_frame_decode_ex(const uint8_t *air, size_t bytes, unsigned sync_tolerance,
                           p25cai_frame *frame, p25cai_quality *quality);

/* Rate = 2 (1/2, input 12 bytes) or 3 (3/4, input 18 bytes).
 * 196 encoded bits occupy 25 bytes; low four padding bits must be zero.
 * Decode uses hard-bit Viterbi, starts/ends state 0, returns bit distance.
 * max_distance is caller policy (0..196); equal-cost paths are rejected. */
int p25cai_trellis_encode(const uint8_t *data, unsigned rate, uint8_t air[25]);
int p25cai_trellis_decode(const uint8_t air[25], unsigned rate,
                         unsigned max_distance, uint8_t *data);

/* Explicit standard-MFID voice LC formats 0 (group) and 3 (individual).
 * The protected LC flag and implicit-MFID formats are not interpreted here.
 * Reserved bits are zero on encode and ignored on decode. */
typedef struct {
    uint32_t source, target;
    uint16_t group;
    uint8_t format, manufacturer, service, explicit_source;
} p25cai_link_control;
int p25cai_lc_encode(const p25cai_link_control *lc, uint8_t bytes[9]);
int p25cai_lc_decode(const uint8_t bytes[9], p25cai_link_control *lc);
typedef struct {
    uint8_t indicator[9], manufacturer, algorithm;
    uint16_t key, group;
} p25cai_header;
int p25cai_header_encode(const p25cai_header *header, uint8_t bytes[15]);
int p25cai_header_decode(const uint8_t bytes[15], p25cai_header *header);
int p25cai_header_clear(const p25cai_header *header);

/* Nonreflected CRCs: raw register starts at zero; final complement is
 * applied by the complete CRC functions. Update supports streaming payloads.
 * data may be NULL only for zero length. Output is unchanged on error. */
int p25cai_crc16(const uint8_t *data, size_t bytes, uint16_t *crc);
int p25cai_crc32(const uint8_t *data, size_t bytes, uint32_t *crc);
int p25cai_crc32_update(uint32_t *raw, const uint8_t *data, size_t bytes);
int p25cai_crc9(uint8_t serial, const uint8_t data[16], uint16_t *crc);

enum { P25CAI_PACKET_BLOCKS = 127, P25CAI_MAX_PACKET_DATA = 2028,
       P25CAI_MAX_PACKET_BYTES = 3240 };
typedef struct {
    uint32_t link_id;
    uint16_t nac;
    uint8_t sap, manufacturer, outbound, offset, confirmed;
    uint8_t sequence, fragment, synchronize; /* confirmed header fields */
} p25cai_packet;

/* Complete unconfirmed or full-fragment confirmed CAI packets. No automatic
 * retries, ACKs, duplicate suppression or application admission. Confirmed
 * partial selective-retry packets and enhanced-address SAP 31 are rejected.
 * Decode validates header CRC, confirmed block CRC/serial and packet CRC
 * before committing outputs; max_distance is the per-block trellis budget.
 * Returns wire bytes on encode, payload bytes on decode, or negative error.
 * Empty payload is supported. data may be NULL for zero encode length or
 * zero decode capacity. Outbound/SAP/LLID admission belongs to the caller. */
size_t p25cai_packet_size(size_t payload_bytes, unsigned confirmed);
int p25cai_packet_encode(const p25cai_packet *packet, uint8_t status,
                         const uint8_t *data, size_t bytes,
                         uint8_t *air, size_t capacity);
int p25cai_packet_decode(const uint8_t *air, size_t bytes, uint16_t nac_filter,
                         unsigned max_distance, p25cai_packet *packet,
                         uint8_t *data, size_t capacity);

enum { P25CAI_CONTROL_BYTES = 117 };
typedef struct {
    uint16_t nac;
    uint8_t duid, count;
    /* DUID 7: one to three TSBKs. DUID 12: header plus one to three
     * unconfirmed or alternative multiblock control blocks (SAP 61). */
    uint8_t block[4][12];
} p25cai_control;
size_t p25cai_control_size(uint8_t duid, unsigned blocks);
int p25cai_control_encode(const p25cai_control *unit, uint8_t status,
                          uint8_t *air, size_t capacity);
int p25cai_control_decode(const uint8_t *air, size_t bytes, uint16_t nac_filter,
                          unsigned max_distance, p25cai_control *unit);
/* Decode accepts omitted trailing control-channel null fill. The stream
 * receiver normally completes at a microslot boundary; call finish on a
 * physical end-of-burst to accept a complete ISP without that null fill. */
typedef struct {
    uint64_t shift;
    uint16_t symbols, expected, next_block_end, nac_filter;
    uint8_t duid, blocks, inverted, search_symbols, budget;
    uint8_t air[P25CAI_CONTROL_BYTES];
} p25cai_control_receiver;
int p25cai_control_receiver_init(p25cai_control_receiver *rx, uint16_t nac_filter,
                                unsigned max_distance);
void p25cai_control_receiver_gap(p25cai_control_receiver *rx);
int p25cai_control_receiver_push(p25cai_control_receiver *rx, uint8_t dibit,
                                p25cai_control *unit);
int p25cai_control_receiver_finish(p25cai_control_receiver *rx, p25cai_control *unit);

typedef struct {
    uint32_t base_hz, spacing_hz, bandwidth_hz;
    int32_t offset_hz;
    uint8_t explicit_tx;
} p25cai_bandplan;
typedef struct {
    uint8_t opcode, identifier, lra, rfss, site, service, count, explicit_channels;
    uint16_t system, channel[2], group[2], uplink;
    uint32_t network, source, target;
    p25cai_bandplan plan;
} p25cai_cc_message;
/* CRC-checked unprotected abbreviated OSPs: grants/updates 00,02,03,04,06;
 * FDMA identifier updates 34/3d; network/RFSS broadcasts 3b/3a.
 * No retuning, learned-plan storage or grant authorization. Caller owns the
 * plan table; resolving a frequency does not authorize transmission. */
int p25cai_cc_decode(const uint8_t block[12], p25cai_cc_message *message);
uint32_t p25cai_channel_hz(const p25cai_bandplan plans[16], uint16_t channel,
                          unsigned uplink);

typedef struct {
    uint32_t source, target, additional, network;
    uint16_t value, group, system;
    uint8_t opcode, outbound, manufacturer, service, flags, reason;
    uint8_t lra, rfss, site, result, capabilities;
    uint8_t response[4];
} p25cai_signal;
/* Abbreviated home-system signaling. Direction is required because opcode
 * 27 is an ISP emergency request and an OSP denial. Requests/responses do
 * not update registration, retries, delivery or authentication state.
 * Supported ISP: 00,04,05,18,19,1a,1c,1f,20,23,27,28,2b,2c,2d,38.
 * Supported OSP: 05,18,1a,1c,1f,20,21,27,28,2b,2c,2d,32.
 * value is 16-bit status/message/SI; service is a subtype for ACK/queue/deny.
 * flags uses only AIV/EX bits for OSP ACK, AIV for queue/deny, S for AUTH.
 * Additional information must be zero when AIV is zero. */
int p25cai_signal_encode(const p25cai_signal *signal, uint8_t block[12]);
int p25cai_signal_decode(const uint8_t block[12], unsigned outbound,
                         p25cai_signal *signal);
typedef struct {
    uint32_t target_address, network, unit;
    uint16_t system;
    uint8_t seed[10], challenge[5];
} p25cai_auth_demand;
/* Decode a CRC-verified AUTH_DMD multiblock unit. The caller must compare
 * the complete SUID and registration context before using the challenge.
 * It does not establish registration or authorize an authentication reply. */
int p25cai_auth_demand_decode(const p25cai_control *unit, p25cai_auth_demand *demand);
/* AACE-A AM1/AM3 and AM2/AM4. Key provisioning, challenge admission and
 * session lifetime remain with the caller. mutual is 0 (AM1) or 1 (AM3).
 * These helpers do not encrypt voice or retain any key/session state. */
int p25cai_auth_derive(const uint8_t key[16], const uint8_t seed[10],
                      unsigned mutual, uint8_t session[16]);
int p25cai_auth_response(const uint8_t session[16], const uint8_t challenge[5],
                        uint8_t response[4]);
int p25cai_auth_equal(const uint8_t expected[4], const uint8_t received[4]);

typedef struct {
    uint16_t nac;
    uint8_t duid, corrected_bits;
} p25cai_nid;

typedef struct {
    uint64_t sync_shift;
    uint32_t symbols_seen, sync_acquisitions, complete_frames, bad_nids, truncated_frames;
    uint16_t expected_nac, symbols, target_symbols;
    uint8_t max_sync_errors; /* low bits tolerance; high bit locks payload sync */
    uint8_t inverted;
    uint8_t frame[P25CAI_MAX_FRAME_BYTES];
    p25cai_nid nid;
    /* Normalized channel status after an accepted NID/NAC. No RF permission.
     * Every publication/invalidation advances status_updates; caller owns age. */
    uint32_t status_updates;
    uint8_t status_symbol;
    bool status_valid;
} p25cai_receiver;

/* No heap, callbacks, hardware access or timing assumptions. Init first.
 * Push returns 1 and copies a complete normalized frame, 0 while searching,
 * or a negative error for invalid arguments. out must hold 216 bytes.
 * Output and *bytes are changed only on a completed frame. Frames with bad
 * NIDs, unsupported DUIDs or wrong NAC are discarded. Once locked, an LDU's
 * payload cannot trigger a spurious embedded sync. */
int p25cai_receiver_init(p25cai_receiver *rx, uint16_t nac_filter);
int p25cai_receiver_init_ex(p25cai_receiver *rx, uint16_t nac_filter,
                           unsigned sync_tolerance, unsigned reacquire_in_body);
/* Discontinuity drops framing/status, retaining configuration and counters. */
void p25cai_receiver_gap(p25cai_receiver *rx);
int p25cai_receiver_push(p25cai_receiver *rx, uint8_t dibit,
                         uint8_t out[P25CAI_MAX_FRAME_BYTES], size_t *bytes);

#ifdef __cplusplus
}
#endif
#endif

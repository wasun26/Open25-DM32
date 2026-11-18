/* SPDX-License-Identifier: MIT
 * BAAA-B 4.2; AABF-D 7.2,7.3.1,7.3.3; BAAC-D 2.3,2.6-2.8. */
#include "internal.h"

int p25cai_lc_encode(const p25cai_link_control *lc, uint8_t bytes[9])
{
    if (!lc || !bytes || lc->manufacturer > 1 || lc->source > 0xffffff ||
        lc->target > 0xffffff || lc->explicit_source > 1) return P25CAI_INVALID;
    if (lc->format != 0 && lc->format != 3) return P25CAI_UNSUPPORTED;
    if (lc->format == 3 && lc->explicit_source) return P25CAI_INVALID;
    uint8_t encoded[9] = {0};
    encoded[0] = lc->format;
    encoded[1] = lc->manufacturer;
    encoded[2] = lc->service;
    if (lc->format == 0) {
        encoded[3] = lc->explicit_source;
        cai_write(encoded,32,16,lc->group);
    } else cai_write(encoded,24,24,lc->target);
    cai_write(encoded,48,24,lc->source);
    memcpy(bytes,encoded,sizeof encoded);
    return 0;
}

int p25cai_lc_decode(const uint8_t bytes[9], p25cai_link_control *lc)
{
    if (!bytes || !lc) return P25CAI_INVALID;
    if ((bytes[0] != 0 && bytes[0] != 3) || bytes[1] > 1) return P25CAI_UNSUPPORTED;
    p25cai_link_control decoded = {0};
    decoded.format = bytes[0];
    decoded.manufacturer = bytes[1];
    decoded.service = bytes[2];
    decoded.source = (uint32_t)cai_read(bytes,48,24);
    if (bytes[0] == 0) {
        decoded.group = (uint16_t)cai_read(bytes,32,16);
        decoded.explicit_source = bytes[3] & 1;
    } else decoded.target = (uint32_t)cai_read(bytes,24,24);
    *lc = decoded;
    return 0;
}

int p25cai_header_encode(const p25cai_header *header, uint8_t bytes[15])
{
    if (!header || !bytes || header->manufacturer > 1) return P25CAI_INVALID;
    uint8_t encoded[15];
    memcpy(encoded,header->indicator,9);
    encoded[9] = header->manufacturer;
    encoded[10] = header->algorithm;
    cai_write(encoded,88,16,header->key);
    cai_write(encoded,104,16,header->group);
    memcpy(bytes,encoded,sizeof encoded);
    return 0;
}

int p25cai_header_decode(const uint8_t bytes[15], p25cai_header *header)
{
    if (!bytes || !header) return P25CAI_INVALID;
    if (bytes[9] > 1) return P25CAI_UNSUPPORTED;
    p25cai_header decoded = {0};
    memcpy(decoded.indicator,bytes,9);
    decoded.manufacturer = bytes[9];
    decoded.algorithm = bytes[10];
    decoded.key = (uint16_t)cai_read(bytes,88,16);
    decoded.group = (uint16_t)cai_read(bytes,104,16);
    *header = decoded;
    return 0;
}

int p25cai_header_clear(const p25cai_header *header)
{
    if (!header || header->manufacturer > 1 || header->algorithm != 0x80 || header->key)
        return 0;
    for (unsigned i = 0; i < 9; ++i) if (header->indicator[i]) return 0;
    return 1;
}

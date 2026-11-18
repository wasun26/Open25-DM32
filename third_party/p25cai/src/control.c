/* SPDX-License-Identifier: MIT
 * TIA-102.AABB-B 4-6: TSBK and multiblock control transport. */
#include "internal.h"

size_t p25cai_control_size(uint8_t duid, unsigned blocks)
{
    if ((duid == P25CAI_TSDU && (blocks < 1 || blocks > 3)) ||
        (duid == P25CAI_PDU && (blocks < 2 || blocks > 4)) ||
        (duid != P25CAI_TSDU && duid != P25CAI_PDU)) return 0;
    return ((112+196*blocks+69)/70)*9;
}

static int control_header(const uint8_t *block)
{
    unsigned format = block[0] & 31;
    if (block[0] & 0xc0 || (format != 0x15 && format != 0x17) ||
        block[1] != 0xfd || !(block[6] & 0x80)) return 0;
    unsigned following = block[6] & 127;
    return following >= 1 && following <= 3;
}

static uint32_t control_checksum(const p25cai_control *unit)
{
    uint32_t raw = 0;
    for (unsigned i = 1; i < unit->count; ++i)
        p25cai_crc32_update(&raw,unit->block[i],i+1 == unit->count ? 8 : 12);
    return ~raw;
}

int p25cai_control_encode(const p25cai_control *unit, uint8_t status,
                          uint8_t *air, size_t capacity)
{
    if (!unit || !air || unit->nac > 4095 || status > 3) return P25CAI_INVALID;
    size_t bytes = p25cai_control_size(unit->duid,unit->count);
    if (!bytes || bytes > capacity) return P25CAI_LENGTH;
    p25cai_control content = *unit;
    if (unit->duid == P25CAI_TSDU) {
        for (unsigned i = 0; i < unit->count; ++i) {
            if (content.block[i][0] & 0x40) return P25CAI_UNSUPPORTED;
            content.block[i][0] = (uint8_t)((content.block[i][0] & 0x3f) |
                (i+1 == unit->count ? 0x80 : 0));
            uint16_t crc;
            p25cai_crc16(content.block[i],10,&crc);
            cai_write(content.block[i],80,16,crc);
        }
    } else {
        content.block[0][6] = (uint8_t)(0x80 | (unit->count-1));
        if (!control_header(content.block[0])) return P25CAI_UNSUPPORTED;
        uint16_t crc;
        p25cai_crc16(content.block[0],10,&crc);
        cai_write(content.block[0],80,16,crc);
        uint32_t checksum = control_checksum(&content);
        cai_write(content.block[unit->count-1],64,32,checksum);
    }
    memset(air,0,bytes);
    cai_wire_write(air,0,48,UINT64_C(0x5575f5ff77ff));
    uint64_t nid;
    p25cai_nid_encode(unit->nac,unit->duid,&nid);
    cai_wire_write(air,48,64,nid);
    for (size_t bit = 70; bit < bytes*8; bit += 72) cai_write(air,bit,2,status);
    for (unsigned i = 0; i < unit->count; ++i) cai_block_write(air,i,2,content.block[i]);
    return (int)bytes;
}

static int control_nid(const uint8_t *air, uint16_t *nac, uint8_t *duid)
{
    uint64_t nid = 0;
    for (unsigned i = 0; i < 64; ++i) {
        unsigned logical = 48+i;
        nid = (nid << 1) | cai_bit(air,logical+2*(logical/70));
    }
    return p25cai_nid_decode(nid,nac,duid);
}

int p25cai_control_decode(const uint8_t *air, size_t bytes, uint16_t nac_filter,
                          unsigned max_distance, p25cai_control *unit)
{
    if (!air || !unit || max_distance > 196 ||
        (nac_filter > 4095 && nac_filter != P25CAI_ANY_NAC)) return P25CAI_INVALID;
    if (bytes < 40 || bytes > P25CAI_CONTROL_BYTES) return P25CAI_LENGTH;
    if (cai_weight(cai_read(air,0,48) ^ UINT64_C(0x5575f5ff77ff)) > 2) return P25CAI_INVALID;
    p25cai_control decoded = {0};
    int result = control_nid(air,&decoded.nac,&decoded.duid);
    if (result < 0) return result;
    if (decoded.duid != P25CAI_TSDU && decoded.duid != P25CAI_PDU) return P25CAI_UNSUPPORTED;
    if (nac_filter != P25CAI_ANY_NAC && decoded.nac != nac_filter) return P25CAI_INVALID;
    unsigned limit = decoded.duid == P25CAI_TSDU ? 3 : 1;
    for (unsigned i = 0; i < limit; ++i) {
        unsigned logical_end = 112+196*(i+1);
        size_t required = (logical_end+2*(logical_end/70)+7)/8;
        if (bytes < required) return P25CAI_LENGTH;
        if (cai_block_read(air,i,2,max_distance,decoded.block[i]) < 0) return P25CAI_UNCORRECTABLE;
        if (decoded.duid == P25CAI_TSDU || i == 0) {
            uint16_t crc;
            p25cai_crc16(decoded.block[i],10,&crc);
            if (crc != cai_read(decoded.block[i],80,16)) return P25CAI_UNCORRECTABLE;
        }
        if (decoded.duid == P25CAI_TSDU) {
            if (decoded.block[i][0] & 0x40) return P25CAI_UNSUPPORTED;
            if (decoded.block[i][0] & 0x80) { decoded.count = (uint8_t)(i+1); break; }
        } else if (i == 0) {
            if (!control_header(decoded.block[0])) return P25CAI_UNSUPPORTED;
            limit = (decoded.block[0][6] & 127)+1;
            decoded.count = (uint8_t)limit;
        }
    }
    if (!decoded.count) return P25CAI_LENGTH;
    unsigned logical_end = 112+196*decoded.count;
    size_t minimum = (logical_end+2*(logical_end/70)+7)/8;
    size_t padded = p25cai_control_size(decoded.duid,decoded.count);
    if (bytes < minimum || bytes > padded) return P25CAI_LENGTH;
    if (decoded.duid == P25CAI_PDU) {
        uint32_t crc = control_checksum(&decoded);
        if (crc != cai_read(decoded.block[decoded.count-1],64,32)) return P25CAI_UNCORRECTABLE;
    }
    *unit = decoded;
    return 0;
}

int p25cai_control_receiver_init(p25cai_control_receiver *rx, uint16_t nac_filter,
                                unsigned max_distance)
{
    if (!rx || max_distance > 196 || (nac_filter > 4095 && nac_filter != P25CAI_ANY_NAC))
        return P25CAI_INVALID;
    memset(rx,0,sizeof *rx);
    rx->nac_filter = nac_filter;
    rx->budget = (uint8_t)max_distance;
    return 0;
}

void p25cai_control_receiver_gap(p25cai_control_receiver *rx)
{
    if (!rx) return;
    rx->shift = 0; rx->symbols = 0; rx->expected = 0; rx->search_symbols = 0;
    rx->next_block_end = 0; rx->blocks = 0;
}

int p25cai_control_receiver_push(p25cai_control_receiver *rx, uint8_t dibit,
                                p25cai_control *unit)
{
    if (!rx || !unit || dibit > 3) return P25CAI_INVALID;
    rx->shift = ((rx->shift << 2) | dibit) & UINT64_C(0xffffffffffff);
    if (!rx->symbols) {
        if (rx->search_symbols < 24) ++rx->search_symbols;
        if (rx->search_symbols < 24) return 0;
        uint64_t difference = rx->shift ^ UINT64_C(0x5575f5ff77ff);
        unsigned normal = cai_weight(difference), inverted = cai_weight(difference ^ UINT64_C(0xaaaaaaaaaaaa));
        if (normal > 2 && inverted > 2) return 0;
        rx->inverted = inverted < normal;
        memset(rx->air,0,sizeof rx->air);
        cai_write(rx->air,0,48,rx->shift ^ (rx->inverted ? UINT64_C(0xaaaaaaaaaaaa) : 0));
        rx->symbols = 24; rx->blocks = 0; rx->expected = 0; rx->next_block_end = 158;
        return 0;
    }
    cai_write(rx->air,rx->symbols*2,2,dibit ^ (rx->inverted ? 2 : 0));
    ++rx->symbols;
    if (rx->symbols == 57) {
        uint16_t nac;
        if (control_nid(rx->air,&nac,&rx->duid) < 0 ||
            (rx->duid != P25CAI_TSDU && rx->duid != P25CAI_PDU) ||
            (rx->nac_filter != P25CAI_ANY_NAC && nac != rx->nac_filter)) {
            rx->symbols = 0;
            return 0;
        }
    }
    if (!rx->expected && rx->symbols == rx->next_block_end) {
        uint8_t block[12];
        uint16_t crc;
        if (cai_block_read(rx->air,rx->blocks,2,rx->budget,block) < 0) { rx->symbols = 0; return 0; }
        p25cai_crc16(block,10,&crc);
        if (crc != cai_read(block,80,16)) { rx->symbols = 0; return 0; }
        ++rx->blocks;
        if (rx->duid == P25CAI_PDU) {
            if (!control_header(block)) { rx->symbols = 0; return 0; }
            rx->expected = (uint16_t)(4*p25cai_control_size(rx->duid,(block[6]&127)+1));
        } else if (block[0] & 0x80)
            rx->expected = (uint16_t)(4*p25cai_control_size(rx->duid,rx->blocks));
        else {
            if (rx->blocks == 3) { rx->symbols = 0; return 0; }
            unsigned logical_end = 112+196*(rx->blocks+1);
            rx->next_block_end = (uint16_t)((logical_end+2*(logical_end/70))/2);
        }
    }
    if (rx->expected && rx->symbols == rx->expected) {
        int result = p25cai_control_decode(rx->air,rx->expected/4,rx->nac_filter,rx->budget,unit);
        rx->symbols = 0;
        return result < 0 ? 0 : 1;
    }
    return 0;
}

int p25cai_control_receiver_finish(p25cai_control_receiver *rx, p25cai_control *unit)
{
    if (!rx || !unit) return P25CAI_INVALID;
    int result = 0;
    if (rx->symbols && rx->expected) {
        p25cai_control decoded;
        if (p25cai_control_decode(rx->air,(rx->symbols*2+7)/8,rx->nac_filter,rx->budget,&decoded) == 0) {
            unsigned logical_end = 112+196*decoded.count;
            unsigned required_symbols = (logical_end+2*(logical_end/70))/2;
            if (rx->symbols >= required_symbols) { *unit = decoded; result = 1; }
        }
    }
    p25cai_control_receiver_gap(rx);
    return result;
}

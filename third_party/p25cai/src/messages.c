/* SPDX-License-Identifier: MIT
 * TIA-102.AABC-E abbreviated messages and field definitions; SOURCES.md. */
#include "internal.h"
#include <limits.h>

static int checked_block(const uint8_t *block)
{
    if (!block) return P25CAI_INVALID;
    if ((block[0] & 0x40) || block[1] > 1) return P25CAI_UNSUPPORTED;
    uint16_t crc;
    p25cai_crc16(block,10,&crc);
    return crc == cai_read(block,80,16) ? 0 : P25CAI_UNCORRECTABLE;
}

int p25cai_cc_decode(const uint8_t block[12], p25cai_cc_message *message)
{
    if (!message) return P25CAI_INVALID;
    int result = checked_block(block);
    if (result < 0) return result;
    p25cai_cc_message m = {0};
    m.opcode = block[0] & 63;
    switch (m.opcode) {
    case 0x00:
        m.service = block[2]; m.channel[0] = (uint16_t)cai_read(block,24,16);
        m.group[0] = (uint16_t)cai_read(block,40,16); m.source = (uint32_t)cai_read(block,56,24);
        m.count = 1;
        break;
    case 0x02:
        for (unsigned i = 0; i < 2; ++i) {
            m.channel[i] = (uint16_t)cai_read(block,16+32*i,16);
            m.group[i] = (uint16_t)cai_read(block,32+32*i,16);
        }
        m.count = 2;
        break;
    case 0x03:
        m.service = block[2]; m.channel[0] = (uint16_t)cai_read(block,32,16);
        m.uplink = (uint16_t)cai_read(block,48,16); m.group[0] = (uint16_t)cai_read(block,64,16);
        m.explicit_channels = m.uplink != 0xffff; m.count = 1;
        break;
    case 0x04: case 0x06:
        m.channel[0] = (uint16_t)cai_read(block,16,16);
        m.target = (uint32_t)cai_read(block,32,24); m.source = (uint32_t)cai_read(block,56,24);
        m.count = 1;
        break;
    case 0x3a:
        m.lra = block[2]; m.system = (uint16_t)cai_read(block,28,12);
        m.rfss = block[5]; m.site = block[6]; m.channel[0] = (uint16_t)cai_read(block,56,16);
        m.service = block[9];
        break;
    case 0x3b:
        m.lra = block[2]; m.network = (uint32_t)cai_read(block,24,20);
        m.system = (uint16_t)cai_read(block,44,12); m.channel[0] = (uint16_t)cai_read(block,56,16);
        m.service = block[9];
        break;
    case 0x34: case 0x3d: {
        m.identifier = block[2] >> 4;
        uint64_t base = cai_read(block,48,32)*5;
        uint32_t spacing = (uint32_t)cai_read(block,38,10)*125;
        if (!base || base > UINT32_MAX || !spacing) return P25CAI_INVALID;
        m.plan.base_hz = (uint32_t)base; m.plan.spacing_hz = spacing;
        unsigned offset, sign;
        uint64_t magnitude;
        if (m.opcode == 0x34) {
            unsigned bw = block[2]&15;
            if (bw != 4 && bw != 5) return P25CAI_UNSUPPORTED;
            m.plan.bandwidth_hz = bw == 4 ? 6250 : 12500;
            offset = (unsigned)cai_read(block,24,14); sign = 0x2000;
            magnitude = (uint64_t)(offset&0x1fff)*spacing;
        } else {
            unsigned bw = (unsigned)cai_read(block,20,9);
            if (!bw) return P25CAI_INVALID;
            m.plan.bandwidth_hz = bw*125;
            offset = (unsigned)cai_read(block,29,9); sign = 0x100;
            magnitude = (uint64_t)(offset&0xff)*250000;
        }
        if (magnitude > INT32_MAX) return P25CAI_INVALID;
        m.plan.explicit_tx = offset == sign;
        m.plan.offset_hz = (offset&sign) ? (int32_t)magnitude : -(int32_t)magnitude;
        break;
    }
    default: return P25CAI_UNSUPPORTED;
    }
    *message = m;
    return 0;
}

uint32_t p25cai_channel_hz(const p25cai_bandplan plans[16], uint16_t channel,
                          unsigned uplink)
{
    if (!plans || uplink > 1) return 0;
    const p25cai_bandplan *plan = &plans[channel >> 12];
    if (!plan->base_hz || !plan->spacing_hz || (uplink && plan->explicit_tx)) return 0;
    int64_t hz = (int64_t)plan->base_hz+(int64_t)plan->spacing_hz*(channel&4095);
    if (uplink) hz += plan->offset_hz;
    return hz > 0 && hz <= UINT32_MAX ? (uint32_t)hz : 0;
}

static int signal_fields(p25cai_signal *s, uint8_t *block, int encoding)
{
    unsigned opcode = s->opcode;
    if (opcode == 0x18 || opcode == 0x1c || opcode == 0x1f || opcode == 0x1a ||
        (!s->outbound && opcode == 0x19) || (s->outbound && (opcode == 5 || opcode == 0x2d))) {
        if (encoding) {
            if (opcode == 0x18 || opcode == 0x19 || opcode == 0x1c) cai_write(block,16,16,s->value);
            if (opcode == 5) block[2] = s->service;
            cai_write(block,32,24,s->target); cai_write(block,56,24,s->source);
        } else {
            if (opcode == 0x18 || opcode == 0x19 || opcode == 0x1c) s->value = (uint16_t)cai_read(block,16,16);
            if (opcode == 5) s->service = block[2];
            s->target = (uint32_t)cai_read(block,32,24); s->source = (uint32_t)cai_read(block,56,24);
        }
        return 0;
    }
    if (!s->outbound) {
        switch (opcode) {
        case 0x00: case 0x04: case 0x05:
            if (encoding) {
                block[2] = s->service;
                if (opcode == 5) {
                    if (s->value < 0x20 || s->value > 0x22) return P25CAI_INVALID;
                    block[3] = (uint8_t)s->value;
                }
                if (opcode == 0) cai_write(block,40,16,s->group);
                else cai_write(block,32,24,s->target);
            } else {
                s->service = block[2];
                if (opcode == 5) {
                    s->value = block[3];
                    if (s->value < 0x20 || s->value > 0x22) return P25CAI_INVALID;
                }
                if (opcode == 0) s->group = (uint16_t)cai_read(block,40,16);
                else s->target = (uint32_t)cai_read(block,32,24);
            }
            break;
        case 0x23:
            if (encoding) {
                if (s->service > 63 || (s->flags&~0x80u) || s->additional > 0xffffff ||
                    (!(s->flags&0x80) && s->additional)) return P25CAI_INVALID;
                block[2] = s->flags | s->service; block[3] = s->reason;
                cai_write(block,32,24,s->additional);
            } else {
                s->service = block[2]&63; s->flags = block[2]&0x80; s->reason = block[3];
                if (s->flags) s->additional = (uint32_t)cai_read(block,32,24);
            }
            break;
        case 0x20:
            if (encoding) {
                if (s->service > 63) return P25CAI_INVALID;
                block[2] = s->service; cai_write(block,32,24,s->target);
            } else { s->service = block[2]&63; s->target = (uint32_t)cai_read(block,32,24); }
            break;
        case 0x27:
            if (encoding) { cai_write(block,16,16,s->value); cai_write(block,40,16,s->group); }
            else { s->value = (uint16_t)cai_read(block,16,16); s->group = (uint16_t)cai_read(block,40,16); }
            break;
        case 0x28:
            if (encoding) { block[2] = s->service&0x80; cai_write(block,28,12,s->system); cai_write(block,40,16,s->group); }
            else { s->service = block[2]&0x80; s->system = (uint16_t)cai_read(block,28,12); s->group = (uint16_t)cai_read(block,40,16); }
            break;
        case 0x2b: case 0x2c:
            if (encoding) { block[2] = opcode == 0x2c ? s->capabilities : 0; cai_write(block,24,20,s->network); cai_write(block,44,12,s->system); }
            else { s->capabilities = opcode == 0x2c ? block[2] : 0; s->network = (uint32_t)cai_read(block,24,20); s->system = (uint16_t)cai_read(block,44,12); }
            break;
        case 0x2d:
            if (encoding) { block[2] = s->capabilities; block[4] = s->lra; cai_write(block,40,16,s->group); }
            else { s->capabilities = block[2]; s->lra = block[4]; s->group = (uint16_t)cai_read(block,40,16); }
            break;
        case 0x38:
            if (encoding) { if (s->flags > 1) return P25CAI_INVALID; block[2] = s->flags; memcpy(block+3,s->response,4); }
            else { s->flags = block[2]&1; memcpy(s->response,block+3,4); }
            break;
        default: return P25CAI_UNSUPPORTED;
        }
        if (encoding) cai_write(block,56,24,s->source);
        else s->source = (uint32_t)cai_read(block,56,24);
    } else {
        switch (opcode) {
        case 0x20:
            if (encoding) {
                if (s->service > 63 || (s->flags&~0xc0u) || (!(s->flags&0x80) && s->additional)) return P25CAI_INVALID;
                block[2] = s->flags | s->service; cai_write(block,24,32,s->additional);
            } else { s->service = block[2]&63; s->flags = block[2]&0xc0; if (s->flags&0x80) s->additional = (uint32_t)cai_read(block,24,32); }
            break;
        case 0x21: case 0x27:
            if (encoding) {
                if (s->service > 63 || (s->flags&~0x80u) || s->additional > 0xffffff || (!(s->flags&0x80) && s->additional)) return P25CAI_INVALID;
                block[2] = s->flags | s->service; block[3] = s->reason; cai_write(block,32,24,s->additional);
            } else { s->service = block[2]&63; s->flags = block[2]&0x80; s->reason = block[3]; if (s->flags) s->additional = (uint32_t)cai_read(block,32,24); }
            break;
        case 0x28:
            if (encoding) { block[2] = s->result; cai_write(block,24,16,s->value); cai_write(block,40,16,s->group); }
            else { s->result = block[2]&3; s->value = (uint16_t)cai_read(block,24,16); s->group = (uint16_t)cai_read(block,40,16); }
            break;
        case 0x2b:
            if (encoding) { block[2] = s->result; cai_write(block,24,16,s->group); block[5] = s->rfss; block[6] = s->site; }
            else { s->result = block[2]&3; s->group = (uint16_t)cai_read(block,24,16); s->rfss = block[5]; s->site = block[6]; }
            break;
        case 0x2c:
            if (encoding) { cai_write(block,16,16,(uint16_t)((s->result<<12)|s->system)); cai_write(block,32,24,s->source); }
            else { s->result = (block[2]>>4)&3; s->system = (uint16_t)cai_read(block,20,12); s->source = (uint32_t)cai_read(block,32,24); }
            break;
        case 0x32:
            if (encoding) memcpy(block+3,s->response,4);
            else memcpy(s->response,block+3,4);
            break;
        default: return P25CAI_UNSUPPORTED;
        }
        if (encoding) cai_write(block,56,24,s->target);
        else s->target = (uint32_t)cai_read(block,56,24);
    }
    return 0;
}

int p25cai_signal_encode(const p25cai_signal *signal, uint8_t block[12])
{
    if (!signal || !block || signal->opcode > 63 || signal->outbound > 1 ||
        signal->manufacturer > 1 || signal->source > 0xffffff || signal->target > 0xffffff ||
        signal->network > 0xfffff || signal->system > 4095 || signal->result > 3) return P25CAI_INVALID;
    uint8_t encoded[12] = {0};
    p25cai_signal fields = *signal;
    encoded[0] = 0x80 | signal->opcode; encoded[1] = signal->manufacturer;
    int result = signal_fields(&fields,encoded,1);
    if (result < 0) return result;
    uint16_t crc;
    p25cai_crc16(encoded,10,&crc); cai_write(encoded,80,16,crc);
    memcpy(block,encoded,sizeof encoded);
    return 0;
}

int p25cai_signal_decode(const uint8_t block[12], unsigned outbound, p25cai_signal *signal)
{
    if (!signal || outbound > 1) return P25CAI_INVALID;
    int result = checked_block(block);
    if (result < 0) return result;
    p25cai_signal decoded = {0};
    decoded.opcode = block[0]&63; decoded.manufacturer = block[1]; decoded.outbound = (uint8_t)outbound;
    uint8_t copy[12]; memcpy(copy,block,sizeof copy);
    result = signal_fields(&decoded,copy,0);
    if (result < 0) return result;
    *signal = decoded;
    return 0;
}

int p25cai_auth_demand_decode(const p25cai_control *unit, p25cai_auth_demand *demand)
{
    if (!unit || !demand) return P25CAI_INVALID;
    const uint8_t *h = unit->block[0], *a = unit->block[1], *b = unit->block[2];
    if (unit->duid != P25CAI_PDU || unit->count != 3 || h[0] != 0x37 || h[1] != 0xfd ||
        h[2] > 1 || h[6] != 0x82 || h[7] != 0x31) return P25CAI_UNSUPPORTED;
    /* Revalidate CRCs because callers may construct units directly. */
    uint16_t crc;
    p25cai_crc16(h,10,&crc);
    if (crc != cai_read(h,80,16)) return P25CAI_UNCORRECTABLE;
    uint32_t raw = 0;
    p25cai_crc32_update(&raw,a,12); p25cai_crc32_update(&raw,b,8);
    if ((uint32_t)~raw != cai_read(b,64,32)) return P25CAI_UNCORRECTABLE;
    p25cai_auth_demand decoded = {0};
    decoded.target_address = (uint32_t)cai_read(h,24,24);
    decoded.network = (uint32_t)((cai_read(h,64,16)<<4)|(a[0]>>4));
    decoded.system = (uint16_t)(((a[0]&15)<<8)|a[1]);
    decoded.unit = (uint32_t)cai_read(a,16,24);
    memcpy(decoded.seed,a+5,7); memcpy(decoded.seed+7,b,3);
    memcpy(decoded.challenge,b+3,5);
    *demand = decoded;
    return 0;
}

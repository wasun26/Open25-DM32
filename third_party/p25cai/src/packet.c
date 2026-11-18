/* SPDX-License-Identifier: MIT
 * TIA-102.BAAA-B 5.2-5.6,7.2.4. All bit order is MSB first. */
#include "internal.h"

size_t p25cai_packet_size(size_t payload_bytes, unsigned confirmed)
{
    if (confirmed > 1) return 0;
    size_t block_bytes = confirmed ? 16 : 12;
    if (payload_bytes > block_bytes * 127 - 4) return 0;
    size_t blocks = (payload_bytes + 4 + block_bytes - 1) / block_bytes;
    return ((112 + 196 * (blocks + 1) + 69) / 70) * 9;
}

int p25cai_packet_encode(const p25cai_packet *packet, uint8_t status,
                         const uint8_t *data, size_t bytes,
                         uint8_t *air, size_t capacity)
{
    if (!packet || !air || (!data && bytes) || status > 3 || packet->nac > 4095 ||
        packet->link_id > 0xffffff || packet->sap > 63 || packet->sap == 31 ||
        packet->outbound > 1 || packet->offset > 63 || packet->offset > bytes ||
        packet->confirmed > 1 || packet->sequence > 7 || packet->fragment > 15 ||
        packet->synchronize > 1) return P25CAI_INVALID;
    size_t wire = p25cai_packet_size(bytes,packet->confirmed);
    if (!wire || capacity < wire) return P25CAI_LENGTH;
    unsigned block_bytes = packet->confirmed ? 16 : 12;
    unsigned blocks = (unsigned)((bytes+4+block_bytes-1)/block_bytes);
    unsigned padding = blocks*block_bytes-4-(unsigned)bytes;
    uint8_t header[12] = {0};
    header[0] = (uint8_t)((packet->confirmed ? 0x56 : 0x15) | (packet->outbound << 5));
    header[1] = 0xc0 | packet->sap;
    header[2] = packet->manufacturer;
    cai_write(header,24,24,packet->link_id);
    header[6] = (uint8_t)(0x80 | blocks);
    header[7] = (uint8_t)padding;
    if (packet->confirmed)
        header[8] = (uint8_t)((packet->synchronize << 7) | (packet->sequence << 4) | packet->fragment);
    header[9] = packet->offset;
    uint16_t header_crc;
    p25cai_crc16(header,10,&header_crc);
    cai_write(header,80,16,header_crc);
    uint32_t raw = 0;
    p25cai_crc32_update(&raw,data,bytes);
    uint8_t zeros[16] = {0};
    p25cai_crc32_update(&raw,zeros,padding);
    uint32_t final_crc = ~raw;
    memset(air,0,wire);
    cai_wire_write(air,0,48,UINT64_C(0x5575f5ff77ff));
    uint64_t nid;
    p25cai_nid_encode(packet->nac,P25CAI_PDU,&nid);
    cai_wire_write(air,48,64,nid);
    for (size_t pos = 70; pos < wire*8; pos += 72) cai_write(air,pos,2,status);
    cai_block_write(air,0,2,header);
    for (unsigned block = 0; block < blocks; ++block) {
        uint8_t content[18] = {0};
        unsigned prefix = packet->confirmed ? 2 : 0;
        for (unsigned i = 0; i < block_bytes; ++i) {
            size_t offset = block*block_bytes+i;
            if (offset < bytes) content[prefix+i] = data[offset];
            else if (offset >= blocks*block_bytes-4)
                content[prefix+i] = (uint8_t)(final_crc >> (8*(blocks*block_bytes-1-offset)));
        }
        if (packet->confirmed) {
            uint16_t crc;
            p25cai_crc9((uint8_t)block,content+2,&crc);
            content[0] = (uint8_t)((block << 1) | (crc >> 8));
            content[1] = (uint8_t)crc;
        }
        cai_block_write(air,block+1,packet->confirmed?3:2,content);
    }
    return (int)wire;
}

int p25cai_packet_decode(const uint8_t *air, size_t bytes, uint16_t nac_filter,
                         unsigned max_distance, p25cai_packet *packet,
                         uint8_t *data, size_t capacity)
{
    if (!air || !packet || (!data && capacity) || max_distance > 196 ||
        (nac_filter > 4095 && nac_filter != P25CAI_ANY_NAC)) return P25CAI_INVALID;
    /* One header and at least one payload block, rounded through final SS. */
    if (bytes < 72 || bytes > P25CAI_MAX_PACKET_BYTES) return P25CAI_LENGTH;
    if (cai_weight(cai_read(air,0,48) ^ UINT64_C(0x5575f5ff77ff)) > 2) return P25CAI_INVALID;
    uint64_t nid = 0;
    for (unsigned i = 0; i < 64; ++i) {
        unsigned position = 48+i;
        nid = (nid << 1) | cai_bit(air,position+2*(position/70));
    }
    p25cai_packet decoded = {0};
    uint8_t duid, header[12];
    int result = p25cai_nid_decode(nid,&decoded.nac,&duid);
    if (result < 0) return result;
    if (duid != P25CAI_PDU) return P25CAI_UNSUPPORTED;
    if (nac_filter != P25CAI_ANY_NAC && decoded.nac != nac_filter) return P25CAI_INVALID;
    if (cai_block_read(air,0,2,max_distance,header) < 0) return P25CAI_UNCORRECTABLE;
    uint16_t crc;
    p25cai_crc16(header,10,&crc);
    if (crc != cai_read(header,80,16)) return P25CAI_UNCORRECTABLE;
    unsigned format = header[0] & 31;
    if (format != 0x15 && format != 0x16) return P25CAI_UNSUPPORTED;
    decoded.confirmed = format == 0x16;
    if ((header[0] & 0xc0) != (decoded.confirmed ? 0x40 : 0) ||
        (header[1] & 0xc0) != 0xc0 || !(header[6] & 0x80)) return P25CAI_UNSUPPORTED;
    unsigned blocks = header[6] & 127, padding = header[7] & 31;
    unsigned block_bytes = decoded.confirmed ? 16 : 12;
    if (!blocks || blocks*block_bytes < 4+padding) return P25CAI_LENGTH;
    size_t expected = ((112+196*(blocks+1)+69)/70)*9;
    if (expected != bytes) return P25CAI_LENGTH;
    size_t payload = blocks*block_bytes-4-padding;
    if (capacity < payload) return P25CAI_LENGTH;
    decoded.sap = header[1] & 63;
    if (decoded.sap == 31) return P25CAI_UNSUPPORTED;
    decoded.manufacturer = header[2];
    decoded.outbound = (header[0] >> 5) & 1;
    decoded.link_id = (uint32_t)cai_read(header,24,24);
    decoded.offset = header[9] & 63;
    if (decoded.offset > payload) return P25CAI_INVALID;
    if (decoded.confirmed) {
        decoded.synchronize = header[8] >> 7;
        decoded.sequence = (header[8] >> 4) & 7;
        decoded.fragment = header[8] & 15;
    }
    uint8_t staging[2032];
    for (unsigned block = 0; block < blocks; ++block) {
        uint8_t content[18];
        if (cai_block_read(air,block+1,decoded.confirmed?3:2,max_distance,content) < 0)
            return P25CAI_UNCORRECTABLE;
        unsigned prefix = decoded.confirmed ? 2 : 0;
        if (decoded.confirmed) {
            p25cai_crc9((uint8_t)block,content+2,&crc);
            if ((content[0] >> 1) != block ||
                (((content[0]&1) << 8) | content[1]) != crc) return P25CAI_UNCORRECTABLE;
        }
        memcpy(staging+block*block_bytes,content+prefix,block_bytes);
    }
    uint32_t crc32;
    p25cai_crc32(staging,blocks*block_bytes-4,&crc32);
    if (crc32 != cai_read(staging,(blocks*block_bytes-4)*8,32)) return P25CAI_UNCORRECTABLE;
    if (payload) memcpy(data,staging,payload);
    *packet = decoded;
    return (int)payload;
}

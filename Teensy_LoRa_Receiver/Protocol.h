#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <Arduino.h>

// --- AX.25 Configuration ---
#define AX25_CALLSIGN_LEN 6
#define AX25_ADDR_LEN 7
#define AX25_CTRL_UI 0x03
#define AX25_PID_NONE 0xF0

// --- CCSDS Configuration ---
#define CCSDS_HEADER_LEN 6
#define CCSDS_VERSION_TM 0
#define CCSDS_TYPE_TM 0
#define CCSDS_TYPE_TC 1

// APIDs
#define APID_LDR_DATA 100
#define APID_HANDSHAKE_REQ 200
#define APID_HANDSHAKE_ACK 201

// CRC-16 CCITT for AX.25 FCS (Frame Check Sequence)
// Polynomial: X^16 + X^12 + X^5 + 1 (0x1021, reflected as 0x8408)
inline uint16_t update_crc(uint16_t crc, uint8_t data) {
    crc ^= data;
    for (int i = 0; i < 8; i++) {
        if (crc & 0x0001) {
            crc = (crc >> 1) ^ 0x8408;
        } else {
            crc >>= 1;
        }
    }
    return crc;
}

inline uint16_t calculate_ax25_fcs(const uint8_t* buffer, uint16_t len) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc = update_crc(crc, buffer[i]);
    }
    return ~crc; // One's complement
}

// Shift callsign characters by 1 bit to the left (AX.25 standard)
// Set SSID and the last-address-octet indicator bit (bit 0)
inline void make_ax25_address(uint8_t* outAddr, const char* callsign, uint8_t ssid, bool isLast) {
    for (int i = 0; i < 6; i++) {
        char c = ' ';
        if (callsign && callsign[i] != '\0') {
            c = callsign[i];
        }
        outAddr[i] = c << 1;
    }
    // SSID byte formatting:
    // bit 7 = 0
    // bit 6-5 = 11 (reserved/standard)
    // bit 4-1 = SSID (4 bits)
    // bit 0 = isLast (1 if this is the last address field, 0 otherwise)
    outAddr[6] = (0x03 << 5) | ((ssid & 0x0F) << 1) | (isLast ? 0x01 : 0x00);
}

// Encodes a payload into an AX.25 UI Frame (Unnumbered Information)
// Returns the total size of the generated AX.25 frame
inline uint16_t encode_ax25(const char* destCall, uint8_t destSsid,
                     const char* srcCall, uint8_t srcSsid,
                     const uint8_t* payload, uint16_t payloadLen,
                     uint8_t* outBuf) {
    uint16_t index = 0;
    
    // 1. Destination Callsign (7 bytes)
    make_ax25_address(&outBuf[index], destCall, destSsid, false);
    index += AX25_ADDR_LEN;
    
    // 2. Source Callsign (7 bytes, isLast = true since we don't use repeaters)
    make_ax25_address(&outBuf[index], srcCall, srcSsid, true);
    index += AX25_ADDR_LEN;
    
    // 3. Control Field (UI Frame = 0x03)
    outBuf[index++] = AX25_CTRL_UI;
    
    // 4. PID (Protocol Identifier) Field (0xF0 for no layer 3 protocol)
    outBuf[index++] = AX25_PID_NONE;
    
    // 5. Copy payload
    memcpy(&outBuf[index], payload, payloadLen);
    index += payloadLen;
    
    // 6. Calculate CRC-16 CCITT FCS (2 bytes)
    uint16_t fcs = calculate_ax25_fcs(outBuf, index);
    outBuf[index++] = fcs & 0xFF;        // LSB
    outBuf[index++] = (fcs >> 8) & 0xFF; // MSB
    
    return index;
}

// Decodes an AX.25 UI Frame
// Returns payload length if decoded and verified successfully, otherwise 0
inline uint16_t decode_ax25(const uint8_t* ax25Buf, uint16_t ax25Len,
                     char* destCall, uint8_t* destSsid,
                     char* srcCall, uint8_t* srcSsid,
                     uint8_t* outPayload) {
    if (ax25Len < 18) { // Min: 7 (dest) + 7 (src) + 1 (ctrl) + 1 (pid) + 2 (fcs) = 18 bytes
        return 0;
    }
    
    // 1. Validate FCS CRC
    uint16_t calcFcs = calculate_ax25_fcs(ax25Buf, ax25Len - 2);
    uint16_t rxFcs = ax25Buf[ax25Len - 2] | (ax25Buf[ax25Len - 1] << 8);
    if (calcFcs != rxFcs) {
        return 0; // CRC mismatch
    }
    
    // 2. Extract Destination calls
    for (int i = 0; i < 6; i++) {
        destCall[i] = (ax25Buf[i] >> 1) & 0x7F;
    }
    destCall[6] = '\0';
    *destSsid = (ax25Buf[6] >> 1) & 0x0F;
    
    // 3. Extract Source calls
    for (int i = 0; i < 6; i++) {
        srcCall[i] = (ax25Buf[7 + i] >> 1) & 0x7F;
    }
    srcCall[6] = '\0';
    *srcSsid = (ax25Buf[13] >> 1) & 0x0F;
    
    // 4. Validate Control and PID fields
    if (ax25Buf[14] != AX25_CTRL_UI || ax25Buf[15] != AX25_PID_NONE) {
        return 0;
    }
    
    // 5. Copy Payload
    uint16_t payloadLen = ax25Len - 18;
    memcpy(outPayload, &ax25Buf[16], payloadLen);
    
    return payloadLen;
}

// Encodes a payload into a CCSDS Space Packet
// Returns the total size of the CCSDS packet
inline uint16_t encode_ccsds(uint16_t apid, uint16_t seqCount,
                      const uint8_t* payload, uint16_t payloadLen,
                      uint8_t* outBuf) {
    // 1. Packet Version Number (3 bits) | Packet Type (1 bit) | Secondary Header Flag (1 bit) | APID (11 bits)
    // Version = 0 (TM Space Packet), Type = 0 (Telemetry), Secondary Header = 0 (None)
    uint16_t word0 = (0 << 13) | (0 << 12) | (0 << 11) | (apid & 0x07FF);
    outBuf[0] = (word0 >> 8) & 0xFF;
    outBuf[1] = word0 & 0xFF;
    
    // 2. Sequence Flags (2 bits) | Packet Sequence Count (14 bits)
    // Sequence Flags = 3 (11 binary, indicates standalone unsegmented packet)
    uint16_t word1 = (3 << 14) | (seqCount & 0x3FFF);
    outBuf[2] = (word1 >> 8) & 0xFF;
    outBuf[3] = word1 & 0xFF;
    
    // 3. Packet Data Length (16 bits)
    // Note: CCSDS packet length field value is (Payload Bytes - 1)
    uint16_t packetLenValue = payloadLen - 1;
    outBuf[4] = (packetLenValue >> 8) & 0xFF;
    outBuf[5] = packetLenValue & 0xFF;
    
    // 4. Copy payload data
    memcpy(&outBuf[6], payload, payloadLen);
    
    return CCSDS_HEADER_LEN + payloadLen;
}

// Decodes a CCSDS Space Packet
// Returns the payload length on success, otherwise 0
inline uint16_t decode_ccsds(const uint8_t* ccsdsBuf, uint16_t ccsdsLen,
                      uint16_t* apid, uint16_t* seqCount,
                      uint8_t* outPayload) {
    if (ccsdsLen < CCSDS_HEADER_LEN) {
        return 0;
    }
    
    // 1. Decode word 0
    uint16_t word0 = (ccsdsBuf[0] << 8) | ccsdsBuf[1];
    uint8_t version = (word0 >> 13) & 0x07;
    uint8_t type = (word0 >> 12) & 0x01;
    uint8_t secHeader = (word0 >> 11) & 0x01;
    *apid = word0 & 0x07FF;
    
    // 2. Decode word 1
    uint16_t word1 = (ccsdsBuf[2] << 8) | ccsdsBuf[3];
    uint8_t seqFlags = (word1 >> 14) & 0x03;
    *seqCount = word1 & 0x3FFF;
    
    // 3. Decode payload length
    uint16_t packetLenValue = (ccsdsBuf[4] << 8) | ccsdsBuf[5];
    uint16_t expectedPayloadLen = packetLenValue + 1;
    
    if (ccsdsLen < CCSDS_HEADER_LEN + expectedPayloadLen) {
        return 0; // Underflow
    }
    
    // 4. Copy payload
    memcpy(outPayload, &ccsdsBuf[CCSDS_HEADER_LEN], expectedPayloadLen);
    
    return expectedPayloadLen;
}

#endif // PROTOCOL_H

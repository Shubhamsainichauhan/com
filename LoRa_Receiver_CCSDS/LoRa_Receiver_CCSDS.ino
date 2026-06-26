#include <SPI.h>
#include <LoRa.h>
#include "Protocol.h"

#define LORA_CS   10
#define LORA_RST  9
#define LORA_DIO0 2

const long LORA_FREQUENCY = 430E6; 
const int LORA_SYNC_WORD = 0x12;

const uint8_t MAGIC_1 = 0xCA;
const uint8_t MAGIC_2 = 0xFE;

void setup() {
  Serial.begin(9600);
  while (!Serial);

  LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);
  pinMode(LORA_RST, OUTPUT);
  digitalWrite(LORA_RST, LOW);
  delay(20);
  digitalWrite(LORA_RST, HIGH);
  delay(20);

  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println(F("LoRa Init FAILED!"));
    while (true);
  }

  // Configure LoRa settings for range, reliability and CRC matching
  LoRa.setTxPower(20);
  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.enableCrc();
  LoRa.setSyncWord(LORA_SYNC_WORD);

  Serial.println(F("Ready. Listening for incoming CCSDS transmissions...\n"));
}

void loop() {
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    // Read all packet bytes
    uint8_t rxBuffer[256];
    int bytesRead = 0;
    while (LoRa.available() && bytesRead < 256) {
      rxBuffer[bytesRead++] = LoRa.read();
    }

    // Decode CCSDS packet
    uint8_t decodedAx25[128];
    uint16_t rxApid = 0;
    uint16_t rxSeq = 0;
    uint16_t ax25Len = decode_ccsds(rxBuffer, bytesRead, &rxApid, &rxSeq, decodedAx25);

    bool isHandshake = false;

    if (ax25Len > 0) {
      if (rxApid == APID_HANDSHAKE_REQ) {
        // Decode AX.25 Frame
        char rxDest[8], rxSrc[8];
        uint8_t rxDestSsid, rxSrcSsid;
        uint8_t decodedPayload[64];
        uint16_t payloadLen = decode_ax25(decodedAx25, ax25Len, rxDest, &rxDestSsid, rxSrc, &rxSrcSsid, decodedPayload);

        if (payloadLen > 0) {
          decodedPayload[payloadLen] = '\0';
          if (strcmp((char*)decodedPayload, "REQ_CONN") == 0) {
            isHandshake = true;

            // Build ACK payload: exactly "ACK_CONN" as expected by transmitter
            const char* ackStr = "ACK_CONN";
            uint8_t ackPayloadLen = strlen(ackStr);
            uint8_t rawAckPayload[32];
            memcpy(rawAckPayload, ackStr, ackPayloadLen);

            // Build AX.25 Frame
            // Swapping addresses: Destination becomes transmitter (rxSrc), Source becomes receiver (rxDest)
            uint8_t outAx25Frame[64];
            uint16_t outAx25Len = encode_ax25(rxSrc, rxSrcSsid, rxDest, rxDestSsid, rawAckPayload, ackPayloadLen, outAx25Frame);

            // Build CCSDS Packet (APID = APID_HANDSHAKE_ACK)
            uint8_t outCcsdsPacket[128];
            uint16_t outCcsdsLen = encode_ccsds(APID_HANDSHAKE_ACK, rxSeq, outAx25Frame, outAx25Len, outCcsdsPacket);

            // Send the ACK back
            delay(10); 
            LoRa.beginPacket();
            LoRa.write(outCcsdsPacket, outCcsdsLen);
            LoRa.endPacket();

            // Print status AFTER transmitting
            Serial.println(F("[CONN] Handshake Request detected! Sending ACK..."));
            Serial.println(F("[CONN] ACK sent successfully!"));
          }
        }
      } else if (rxApid == APID_LDR_DATA) {
        // Handle incoming LDR Telemetry Data
        char rxDest[8], rxSrc[8];
        uint8_t rxDestSsid, rxSrcSsid;
        uint8_t decodedPayload[64];
        uint16_t payloadLen = decode_ax25(decodedAx25, ax25Len, rxDest, &rxDestSsid, rxSrc, &rxSrcSsid, decodedPayload);

        if (payloadLen > 0) {
          decodedPayload[payloadLen] = '\0';
          Serial.print(F("[TELEMETRY] Telemetry data: "));
          Serial.println((char*)decodedPayload);
        }
      }
    }

    // Print raw debug info (done after time-critical TX is complete)
    Serial.print(F("Packet Rx (Size: "));
    Serial.print(packetSize);
    Serial.print(F(", RSSI: "));
    Serial.print(LoRa.packetRssi());
    Serial.print(F("dBm) -> Raw Hex: "));
    for (int i = 0; i < bytesRead; i++) {
      if (rxBuffer[i] < 0x10) Serial.print(F("0"));
      Serial.print(rxBuffer[i], HEX);
      Serial.print(F(" "));
    }
    Serial.println();

    if (isHandshake) {
      return;
    }

    // Process image chunks (starts with 0xCA 0xFE magic bytes) - if any
    if (bytesRead >= 8 && rxBuffer[0] == MAGIC_1 && rxBuffer[1] == MAGIC_2) {
      uint8_t fileId = rxBuffer[2];
      uint16_t chunkIdx = (rxBuffer[3] << 8) | rxBuffer[4];
      uint16_t totalChunks = (rxBuffer[5] << 8) | rxBuffer[6];
      uint8_t chunkSize = rxBuffer[7];

      // Format for Python Ground Station script
      Serial.print(F("[DATA] "));
      Serial.print(fileId); Serial.print(F(","));
      Serial.print(chunkIdx); Serial.print(F(","));
      Serial.print(totalChunks); Serial.print(F(","));
      Serial.print(chunkSize); Serial.print(F(":"));
      
      for (int i = 8; i < bytesRead; i++) {
        if (rxBuffer[i] < 0x10) Serial.print(F("0"));
        Serial.print(rxBuffer[i], HEX);
      }
      Serial.println();

      // Human-readable debugging
      Serial.print(F("[DEBUG] Received Chunk "));
      Serial.print(chunkIdx + 1); Serial.print(F("/"));
      Serial.print(totalChunks); Serial.print(F(" | RSSI: "));
      Serial.print(LoRa.packetRssi()); Serial.println(F(" dBm"));
    }
  }
}

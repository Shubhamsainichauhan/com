#include <RadioLib.h>
#include <SPI.h>

// ==========================================
// Configuration (MUST MATCH TRANSMITTER)
// ==========================================

// LoRa Module Pins (Adjust based on your receiver wiring)
#define LORA_NSS    10
#define LORA_DIO0   2
#define LORA_RESET  9
#define LORA_DIO1   RADIOLIB_NC

// LoRa Settings 
#define FREQUENCY           433.0 // MHz
#define BANDWIDTH           125.0 // kHz
#define SPREADING_FACTOR    9 
#define CODING_RATE         7     // 4/7
#define OUTPUT_POWER        2     // dBm
#define PREAMBLE_LEN        8
#define SYNC_WORD           0x12

// CCSDS Settings
#define APID                0x042 // Must match the transmitter's APID

// Initialize LoRa Module (SX1278 as an example)
SX1278 radio = new Module(LORA_NSS, LORA_DIO0, LORA_RESET, LORA_DIO1);

void setup() {
  Serial.begin(9600);
  
  // Wait for the python script on the receiver laptop to connect
  // while (!Serial); 

  Serial.println(F("[LoRa RX] Initializing..."));
  
  int state = radio.begin(FREQUENCY, BANDWIDTH, SPREADING_FACTOR, CODING_RATE, SYNC_WORD, OUTPUT_POWER, PREAMBLE_LEN);
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println(F("[LoRa RX] Success! Listening for packets..."));
  } else {
    Serial.print(F("[LoRa RX] Failed, code "));
    Serial.println(state);
    while (true); // Halt on error
  }
}

void loop() {
  uint8_t packet[255];
  
  // Receive a packet (this function blocks until a packet arrives)
  int state = radio.receive(packet, 255);
  
  if (state == RADIOLIB_ERR_NONE) {
    // We got a packet! Let's get its actual size.
    uint16_t packet_len = radio.getPacketLength();
    
    // Minimum size for a CCSDS header is 6 bytes
    if (packet_len >= 6) {
      // Decode CCSDS Header
      uint16_t received_apid = ((packet[0] & 0x07) << 8) | packet[1];
      uint8_t seq_flags = (packet[2] >> 6) & 0x03;
      uint16_t seq_count = ((packet[2] & 0x3F) << 8) | packet[3];
      uint16_t data_length = (packet[4] << 8) | packet[5]; 
      
      // The payload length is data_length + 1
      uint16_t payload_len = data_length + 1;
      
      // Ensure this packet matches our expected APID (0x042 or 0x0A0)
      if (received_apid == APID || received_apid == 0x0A0) {
        // Send a unique marker over Serial to the python script so it knows a new chunk arrived
        // We send: "CHUNK:" + 2 byte size + seq_flag + payload
        Serial.write("CHUNK:");
        Serial.write((uint8_t)(payload_len & 0xFF));
        Serial.write((uint8_t)((payload_len >> 8) & 0xFF));
        Serial.write(seq_flags);
        
        // Write the actual image data payload to Serial
        Serial.write(&packet[6], payload_len);
      } else {
        Serial.print(F("Ignored packet with wrong APID: "));
        Serial.println(received_apid, HEX);
      }
    }
  } else if (state == RADIOLIB_ERR_RX_TIMEOUT) {
    // Timeout occurred while waiting for a packet, just loop again.
  } else if (state == RADIOLIB_ERR_CRC_MISMATCH) {
    // Packet was corrupted in the air
    Serial.println(F("CRC error!"));
  } else {
    // Some other error
    Serial.print(F("Rx error: "));
    Serial.println(state);
  }
}

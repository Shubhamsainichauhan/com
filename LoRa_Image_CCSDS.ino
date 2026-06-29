#include <SPI.h>
#include <LoRa.h>

/*
 * LoRa CCSDS Satellite Image Transmitter (Teensy Version)
 * Tuned for 433 MHz and Sync Word 0x12
 * 
 * HARDWARE WIRING (Teensy 3.x/4.x 3.3V Logic):
 * 3.3V -> VCC
 * GND  -> GND
 * Pin 10 -> NSS / CS
 * Pin 13 -> SCK
 * Pin 12 -> MISO
 * Pin 11 -> MOSI
 * Pin 9  -> RST
 * Pin 2  -> DIO0
 * 
 * *NOTE*: Teensy operates natively at 3.3V logic level. You do NOT need 
 * level shifters or resistor dividers. Connect directly!
 */

#define NSS_PIN   10
#define RESET_PIN 9
#define DIO0_PIN  2

// LoRa Frequency Settings
#define LORA_FREQUENCY 433E6
const int LORA_SYNC_WORD = 0x12;

// CCSDS APID configuration
#define APID_IMAGE 0x0A0  // Application Process ID for Image telemetry

// Max chunk size for image slice inside a LoRa packet.
#define IMAGE_CHUNK_SIZE 128

// --- Dummy Image Data (A tiny 1x1 green dot JPEG for initial testing) ---
const uint8_t dummy_jpeg[] = {
  0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x01, 0x00, 0x60,
  0x00, 0x60, 0x00, 0x00, 0xFF, 0xDB, 0x00, 0x43, 0x00, 0x08, 0x06, 0x06, 0x07, 0x06, 0x05, 0x08,
  0x07, 0x07, 0x07, 0x09, 0x09, 0x08, 0x0A, 0x0C, 0x14, 0x0D, 0x0C, 0x0B, 0x0B, 0x0C, 0x19, 0x12,
  0x13, 0x0F, 0x14, 0x1D, 0x1A, 0x1F, 0x1E, 0x1D, 0x1A, 0x1C, 0x1C, 0x20, 0x24, 0x2E, 0x27, 0x20,
  0x22, 0x2C, 0x23, 0x1C, 0x1C, 0x28, 0x37, 0x29, 0x2C, 0x30, 0x31, 0x34, 0x34, 0x34, 0x1F, 0x27,
  0x39, 0x3D, 0x38, 0x32, 0x3C, 0x2E, 0x33, 0x34, 0x32, 0xFF, 0xC0, 0x00, 0x0B, 0x08, 0x00, 0x01,
  0x00, 0x01, 0x01, 0x01, 0x11, 0x00, 0xFF, 0xC4, 0x00, 0x14, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0xFF, 0xDA, 0x00, 0x08,
  0x01, 0x01, 0x00, 0x00, 0x3F, 0x00, 0xAF, 0x00, 0x3F, 0xFF, 0xD9
};
const uint32_t dummy_jpeg_len = sizeof(dummy_jpeg);

uint16_t globalSequenceCount = 0;

// --- Function to build the CCSDS Primary Header ---
void buildCCSDSHeader(uint8_t *header, uint16_t apid, uint8_t seqFlags, uint16_t seqCount, uint16_t payloadSize) {
  uint16_t packetID = apid & 0x07FF; 
  uint16_t seqControl = ((seqFlags & 0x03) << 14) | (seqCount & 0x3FFF);
  uint16_t dataLengthWord = payloadSize - 1;

  header[0] = (packetID >> 8) & 0xFF;
  header[1] = packetID & 0xFF;
  header[2] = (seqControl >> 8) & 0xFF;
  header[3] = seqControl & 0xFF;
  header[4] = (dataLengthWord >> 8) & 0xFF;
  header[5] = dataLengthWord & 0xFF;
}

void setup() {
  Serial.begin(9600);
  while (!Serial && millis() < 4000); // Wait for Teensy Serial

  Serial.println(F("Starting Teensy CCSDS LoRa Image Transmitter..."));

  // Configure SPI Pins
  LoRa.setPins(NSS_PIN, RESET_PIN, DIO0_PIN);

  // Initialize LoRa transceiver
  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println(F("Starting LoRa failed! Check wiring."));
    while (1);
  }

  // Adjust LoRa settings to match receiver parameters
  LoRa.setTxPower(17);
  LoRa.setSpreadingFactor(9);             // Set to SF9
  LoRa.setSignalBandwidth(125E3);          // Set to 125 kHz
  LoRa.setCodingRate4(7);                  // Set to Coding Rate 4/7
  LoRa.setSyncWord(LORA_SYNC_WORD);        // Set Sync Word to 0x12
  LoRa.setPreambleLength(8);               // Set Preamble Length to 8
  LoRa.enableCrc();                        // Enable CRC

  Serial.println(F("LoRa Transceiver Initialized successfully."));
}

// --- Image Packetization and Transmission ---
void transmitImage(const uint8_t *imageBuffer, uint32_t imageLength) {
  uint16_t totalPackets = (imageLength + IMAGE_CHUNK_SIZE - 1) / IMAGE_CHUNK_SIZE;
  
  Serial.print(F("Sending image of "));
  Serial.print(imageLength);
  Serial.print(F(" bytes in "));
  Serial.print(totalPackets);
  Serial.println(F(" packets..."));

  for (uint16_t i = 0; i < totalPackets; i++) {
    uint32_t offset = (uint32_t)i * IMAGE_CHUNK_SIZE;
    uint16_t payloadSize = min((uint32_t)IMAGE_CHUNK_SIZE, imageLength - offset);

    // Sequence Flags: 01 = First, 00 = Continuation, 10 = Last, 11 = Standalone
    uint8_t seqFlags;
    if (totalPackets == 1) {
      seqFlags = 0x03;
    } else if (i == 0) {
      seqFlags = 0x01;
    } else if (i == totalPackets - 1) {
      seqFlags = 0x02;
    } else {
      seqFlags = 0x00;
    }

    // Build the 6-byte CCSDS Header
    uint8_t ccsdsHeader[6];
    buildCCSDSHeader(ccsdsHeader, APID_IMAGE, seqFlags, globalSequenceCount, payloadSize);
    globalSequenceCount = (globalSequenceCount + 1) & 0x3FFF;

    // Load payload directly from Flash (PROGMEM) to save SRAM
    uint8_t payloadBuffer[IMAGE_CHUNK_SIZE];
    memcpy_P(payloadBuffer, imageBuffer + offset, payloadSize);

    // Transmit over LoRa
    LoRa.beginPacket();
    LoRa.write(ccsdsHeader, 6);
    LoRa.write(payloadBuffer, payloadSize);
    LoRa.endPacket();

    Serial.print(F("Sent packet "));
    Serial.print(i + 1);
    Serial.print(F("/"));
    Serial.print(totalPackets);
    Serial.print(F(" (Flags: 0b"));
    Serial.print(seqFlags, BIN);
    Serial.println(F(")"));

    // Inter-packet delay (gives receiver time to process and print serial output)
    delay(250); 
  }

  Serial.println(F("Image transmission complete."));
  Serial.println(F("----------------------------------------"));
}

void loop() {
  transmitImage(dummy_jpeg, dummy_jpeg_len);
  delay(15000); // Wait 15 seconds before repeating
}

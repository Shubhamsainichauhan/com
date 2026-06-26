#include <SPI.h>
#include <LoRa.h>

// =========================================================================
// ARDUINO UNO TO LORA (SX1278 / RFM95W) WIRING CONNECTION
// =========================================================================
//  LoRa Module pin   ->  Arduino Uno Pin  ->  Notes
//  ------------------------------------------------------------------------
//  3.3V              ->  3.3V             ->  Do NOT connect to 5V! (Needs ~120mA)
//  GND               ->  GND              ->  Common ground
//  MISO              ->  Pin 12           ->  Direct connection (3.3V output to 5V input is OK)
//  MOSI              ->  Pin 11           ->  REQUIRES Level Shifter or 10k/20k Resistor Divider
//  SCK               ->  Pin 13           ->  REQUIRES Level Shifter or 10k/20k Resistor Divider
//  NSS (CS)          ->  Pin 10           ->  REQUIRES Level Shifter or 10k/20k Resistor Divider
//  RST (Reset)       ->  Pin 9            ->  REQUIRES Level Shifter or 10k/20k Resistor Divider
//  DIO0 (IRQ)        ->  Pin 2            ->  Direct connection (3.3V output to 5V input is OK)
//
//  *CRITICAL WARNING*: The Arduino Uno operates at 5V logic, but standard LoRa modules 
//  operate at 3.3V. Sending 5V signals from Uno pins 10, 11, 13, and 9 can damage the 
//  LoRa module. Use a level shifter (e.g., CD4050 or TXB0108) or simple resistor voltage 
//  dividers on the Uno output lines.
// =========================================================================

#define NSS_PIN   10
#define RESET_PIN 9
#define DIO0_PIN  2

// LoRa Frequency Settings (Change depending on your region: e.g. 433E6, 868E6, 915E6)
#define LORA_FREQUENCY 433E6

// CCSDS APID configuration
#define APID_IMAGE 0x0A0  // Application Process ID for Image telemetry

// Max chunk size for image slice inside a LoRa packet.
// For Arduino Uno, 128 bytes is highly recommended to stay within the 2KB SRAM budget
// while maximizing packet reliability.
#define IMAGE_CHUNK_SIZE 128

// --- Dummy Image Data (A tiny 1x1 green dot JPEG for initial testing) ---
// Since the Arduino Uno has only 2 KB of SRAM, storing images in RAM is impossible.
// We use PROGMEM (Flash memory) to store our test image.
// Uno has 32 KB of Flash, allowing us to store static images up to ~25 KB.
const uint8_t PROGMEM dummy_jpeg[] = {
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
// Structure is 6 bytes (48 bits):
// - Packet ID (16 bits): Version (3 bits) | Type (1 bit) | Sec Header (1 bit) | APID (11 bits)
// - Packet Sequence Control (16 bits): Sequence Flags (2 bits) | Sequence Count (14 bits)
// - Packet Data Length (16 bits): Payload Size - 1
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
  while (!Serial);

  Serial.println(F("Starting Arduino Uno CCSDS LoRa Image Transmitter..."));

  // Configure SPI Pins
  LoRa.setPins(NSS_PIN, RESET_PIN, DIO0_PIN);

  // Initialize LoRa transceiver
  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println(F("Starting LoRa failed! Check wiring & level shifters."));
    while (1);
  }

  // Adjust LoRa settings for range and reliability
  LoRa.setTxPower(17);
  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.enableCrc();

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

#include <SPI.h>
#include <LoRa.h>

// Firmware
#define FIRMWARE_VERSION "0.2.0"

// Role
#define ROLE_TX 1
#define ROLE_RX 2
#define DEVICE_ROLE ROLE_TX
// #define DEVICE_ROLE ROLE_RX

// LilyGO LoRa32/T3 v1.6.1
#define LORA_SCK 5
#define LORA_MISO 19
#define LORA_MOSI 27
#define LORA_SS 18
#define LORA_RST 14
#define LORA_DIO0 26

// LoRa configuration
#define LORA_FREQ 915E6
#define LORA_BW 125E3
#define LORA_CR 5
#define LORA_SF 7
#define LORA_TX_POWER 17

// Experiment configuration
#define RUN_ID 1
#define PACKET_COUNT 10
#define ACK_TIMEOUT_MS 1000
#define PACKET_INTERVAL_MS 500

// Protocol
#define TYPE_DATA 0x01
#define TYPE_ACK 0x02

#define ACK_ERROR 0x00
#define ACK_OK 0x01


#if DEVICE_ROLE == ROLE_TX
uint32_t currentPacketId = 1;
#endif


void writeUint16(uint16_t value) {
  LoRa.write((uint8_t)(value >> 8));
  LoRa.write((uint8_t)(value & 0xFF));
}


void writeUint32(uint32_t value) {
  LoRa.write((uint8_t)(value >> 24));
  LoRa.write((uint8_t)(value >> 16));
  LoRa.write((uint8_t)(value >> 8));
  LoRa.write((uint8_t)(value & 0xFF));
}


uint16_t readUint16() {
  uint16_t value = 0;

  value |= ((uint16_t)LoRa.read() << 8);
  value |= (uint16_t)LoRa.read();

  return value;
}


uint32_t readUint32() {
  uint32_t value = 0;

  value |= ((uint32_t)LoRa.read() << 24);
  value |= ((uint32_t)LoRa.read() << 16);
  value |= ((uint32_t)LoRa.read() << 8);
  value |= (uint32_t)LoRa.read();

  return value;
}


void initLoRa() {
  SPI.begin(
    LORA_SCK,
    LORA_MISO,
    LORA_MOSI,
    LORA_SS
  );

  LoRa.setPins(
    LORA_SS,
    LORA_RST,
    LORA_DIO0
  );

  if (!LoRa.begin(LORA_FREQ)) {
    Serial.println("[ERROR] LoRa initialization failed!");
    while (true);
  }

  LoRa.setSpreadingFactor(LORA_SF);
  LoRa.setSignalBandwidth(LORA_BW);
  LoRa.setCodingRate4(LORA_CR);
  LoRa.setTxPower(LORA_TX_POWER);

  Serial.println("[OK] LoRa initialized");
}


#if DEVICE_ROLE == ROLE_TX

void setupTX() {
  Serial.println();
  Serial.println("=== LoRa TX ===");
  Serial.print("Firmware : ");
  Serial.println(FIRMWARE_VERSION);
  Serial.print("RUN_ID   : ");
  Serial.println(RUN_ID);
}


bool sendDataPacket(uint32_t packetId, uint32_t &txTimestamp) {
  const char payload[] = "HELLO";

  txTimestamp = micros();

  LoRa.beginPacket();

  LoRa.write(TYPE_DATA);
  writeUint16(RUN_ID);
  writeUint32(packetId);
  writeUint32(txTimestamp);

  LoRa.write(
    (const uint8_t *)payload,
    sizeof(payload) - 1
  );

  return LoRa.endPacket() != 0;
}


bool waitForAck(
  uint32_t expectedPacketId,
  uint32_t txTimestamp,
  uint32_t &rtt
) {
  uint32_t startTime = millis();

  while (millis() - startTime < ACK_TIMEOUT_MS) {
    int packetSize = LoRa.parsePacket();

    if (packetSize == 0) {
      continue;
    }

    if (packetSize < 8) {
      while (LoRa.available()) {
        LoRa.read();
      }
      continue;
    }

    uint8_t type = LoRa.read();
    uint16_t runId = readUint16();
    uint32_t packetId = readUint32();
    uint8_t status = LoRa.read();

    if (type != TYPE_ACK) {
      continue;
    }

    if (runId != RUN_ID) {
      continue;
    }

    if (packetId != expectedPacketId) {
      continue;
    }

    if (status != ACK_OK) {
      return false;
    }

    rtt = micros() - txTimestamp;

    return true;
  }

  return false;
}


void transmitPacket(uint32_t packetId) {
  uint32_t txTimestamp = 0;
  uint32_t rtt = 0;

  Serial.println();
  Serial.print("[TX] Packet ");
  Serial.println(packetId);

  if (!sendDataPacket(packetId, txTimestamp)) {
    Serial.println("[TX] Send failed");
    return;
  }

  bool ackReceived = waitForAck(
    packetId,
    txTimestamp,
    rtt
  );

  Serial.print("[TX] ACK  : ");
  Serial.println(ackReceived ? "YES" : "NO");

  if (ackReceived) {
    Serial.print("[TX] RTT  : ");
    Serial.print(rtt / 1000.0);
    Serial.println(" ms");
  }
}


void loopTX() {
  if (currentPacketId > PACKET_COUNT) {
    Serial.println();
    Serial.println("[TX] Run finished");

    while (true) {
      delay(1000);
    }
  }

  transmitPacket(currentPacketId);

  currentPacketId++;

  delay(PACKET_INTERVAL_MS);
}

#endif


#if DEVICE_ROLE == ROLE_RX

void setupRX() {
  Serial.println();
  Serial.println("=== LoRa RX ===");
  Serial.print("Firmware : ");
  Serial.println(FIRMWARE_VERSION);
  Serial.print("RUN_ID   : ");
  Serial.println(RUN_ID);
}


void sendAck(uint16_t runId, uint32_t packetId) {
  LoRa.beginPacket();

  LoRa.write(TYPE_ACK);
  writeUint16(runId);
  writeUint32(packetId);
  LoRa.write(ACK_OK);

  LoRa.endPacket();
}


void processDataPacket(int packetSize) {
  if (packetSize < 11) {
    while (LoRa.available()) {
      LoRa.read();
    }
    return;
  }

  uint8_t type = LoRa.read();
  uint16_t runId = readUint16();
  uint32_t packetId = readUint32();
  uint32_t txTimestamp = readUint32();

  String payload;

  while (LoRa.available()) {
    payload += (char)LoRa.read();
  }

  uint32_t rxTimestamp = micros();
  int rssi = LoRa.packetRssi();
  float snr = LoRa.packetSnr();

  if (type != TYPE_DATA) {
    return;
  }

  if (runId != RUN_ID) {
    return;
  }

  Serial.println();
  Serial.print("[RX] Packet  : ");
  Serial.println(packetId);

  Serial.print("[RX] Payload : ");
  Serial.println(payload);

  Serial.print("[RX] RSSI    : ");
  Serial.print(rssi);
  Serial.println(" dBm");

  Serial.print("[RX] SNR     : ");
  Serial.print(snr);
  Serial.println(" dB");

  Serial.print("[RX] TX Time : ");
  Serial.println(txTimestamp);

  Serial.print("[RX] RX Time : ");
  Serial.println(rxTimestamp);

  sendAck(runId, packetId);

  Serial.println("[RX] ACK sent");
}


void loopRX() {
  int packetSize = LoRa.parsePacket();

  if (packetSize == 0) {
    return;
  }

  processDataPacket(packetSize);
}

#endif


void setup() {
  Serial.begin(115200);
  delay(1000);

  initLoRa();

#if DEVICE_ROLE == ROLE_TX
  setupTX();
#elif DEVICE_ROLE == ROLE_RX
  setupRX();
#else
  Serial.println("[ERROR] Invalid DEVICE_ROLE");
  while (true);
#endif
}


void loop() {
#if DEVICE_ROLE == ROLE_TX
  loopTX();
#elif DEVICE_ROLE == ROLE_RX
  loopRX();
#endif
}
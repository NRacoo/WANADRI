
#include <SPI.h>
#include <LoRa.h>
#include <math.h>

#define ROLE_TX 1
#define ROLE_RX 2
#define DEVICE_ROLE ROLE_RX

#define FIRMWARE_VERSION "0.5.0"

#define LORA_FREQ 915E6
#define LORA_BANDWIDTH 125E3
#define LORA_CODING_RATE 5
#define LORA_SPREADING_FACTOR 7
#define LORA_TX_POWER 17
#define LORA_PREAMBLE_LENGTH 8

#define RUN_ID 1
#define PACKET_COUNT 10
#define PAYLOAD_SIZE 100
#define PACKET_INTERVAL_MS 500

#define DISTANCE_M -1
#define RX_PROCESSING_MS 20
#define TIMEOUT_GUARD_FACTOR 0.25

#define LORA_SS 18
#define LORA_RST 14
#define LORA_DIO0 26

#define TYPE_DATA 0x01
#define TYPE_ACK 0x02

#define ACK_ERROR 0x00
#define ACK_OK 0x01

#define DATA_HEADER_SIZE 11
#define ACK_PACKET_SIZE 8
#define DATA_PACKET_SIZE (DATA_HEADER_SIZE + PAYLOAD_SIZE)

uint32_t dataToAUs = 0;
uint32_t ackToAUs = 0;
uint32_t ackTimeoutMs = 0;

uint32_t txTimestampUs = 0;

const char CSV_HEADER[] =
  "role,event,firmware,run_id,packet_id,sf,bw_hz,cr,"
  "payload_bytes,distance_m,tx_timestamp_us,rx_timestamp_us,"
  "ack_timestamp_us,rtt_ms,rssi_dbm,snr_db,ack_received,"
  "payload_valid,ack_sent,data_toa_ms,ack_toa_ms,ack_timeout_ms";

void writeU16(uint16_t value) {
  LoRa.write((uint8_t)(value >> 8));
  LoRa.write((uint8_t)value);
}

void writeU32(uint32_t value) {
  LoRa.write((uint8_t)(value >> 24));
  LoRa.write((uint8_t)(value >> 16));
  LoRa.write((uint8_t)(value >> 8));
  LoRa.write((uint8_t)value);
}

uint16_t readU16() {
  uint16_t value = (uint16_t)LoRa.read() << 8;
  value |= (uint8_t)LoRa.read();
  return value;
}

uint32_t readU32() {
  uint32_t value = (uint32_t)(uint8_t)LoRa.read() << 24;
  value |= (uint32_t)(uint8_t)LoRa.read() << 16;
  value |= (uint32_t)(uint8_t)LoRa.read() << 8;
  value |= (uint8_t)LoRa.read();
  return value;
}

uint32_t calculateToAUs(uint8_t packetLength) {
  const double sf = LORA_SPREADING_FACTOR;
  const double bw = LORA_BANDWIDTH;
  const double symbolTime = pow(2.0, sf) / bw;

  const int de = (symbolTime >= 0.016) ? 1 : 0;
  const int ih = 0;
  const int crc = 1;
  const int cr = LORA_CODING_RATE - 4;

  const double numerator =
    8.0 * packetLength - 4.0 * sf + 28.0
    + 16.0 * crc - 20.0 * ih;

  const double denominator = 4.0 * (sf - 2.0 * de);

  double groups = ceil(numerator / denominator);
  if (groups < 0) groups = 0;

  const double payloadSymbols =
    8.0 + groups * (cr + 4);

  const double totalSymbols =
    LORA_PREAMBLE_LENGTH + 4.25 + payloadSymbols;

  return (uint32_t)ceil(totalSymbols * symbolTime * 1000000.0);
}

void calculateTiming() {
  dataToAUs = calculateToAUs(DATA_PACKET_SIZE);
  ackToAUs = calculateToAUs(ACK_PACKET_SIZE);

  const double timeout =
    ((double)dataToAUs + (double)ackToAUs)
    / 1000.0 * (1.0 + TIMEOUT_GUARD_FACTOR)
    + RX_PROCESSING_MS;

  ackTimeoutMs = (uint32_t)ceil(timeout);
}

void printEmpty() {
  Serial.print(',');
}

void printDistance() {
  if (DISTANCE_M < 0) {
    printEmpty();
  } else {
    Serial.print(DISTANCE_M);
  }
}

void printConfigPrefix(const char *event, uint32_t packetId) {
#if DEVICE_ROLE == ROLE_TX
  Serial.print("TX");
#else
  Serial.print("RX");
#endif

  Serial.print(',');
  Serial.print(event);
  Serial.print(',');
  Serial.print(FIRMWARE_VERSION);
  Serial.print(',');
  Serial.print(RUN_ID);
  Serial.print(',');
  Serial.print(packetId);
  Serial.print(',');
  Serial.print(LORA_SPREADING_FACTOR);
  Serial.print(',');
  Serial.print((uint32_t)LORA_BANDWIDTH);
  Serial.print(',');
  Serial.print(LORA_CODING_RATE);
  Serial.print(',');
  Serial.print(PAYLOAD_SIZE);
  Serial.print(',');
  printDistance();
}

void logStart() {
  printConfigPrefix("START", 0);

  for (int i = 0; i < 9; i++) {
    printEmpty();
  }

  Serial.print(',');
  Serial.print(dataToAUs / 1000.0, 3);
  Serial.print(',');
  Serial.print(ackToAUs / 1000.0, 3);
  Serial.print(',');
  Serial.println(ackTimeoutMs);
}

void logTxSent(uint32_t packetId, uint32_t txTimestamp) {
  printConfigPrefix("TX_SENT", packetId);

  Serial.print(',');
  Serial.print(txTimestamp);

  for (int i = 0; i < 7; i++) {
    printEmpty();
  }

  Serial.print(',');
  Serial.print(dataToAUs / 1000.0, 3);
  Serial.print(',');
  Serial.print(ackToAUs / 1000.0, 3);
  Serial.print(',');
  Serial.println(ackTimeoutMs);
}

void logTxAck(
  uint32_t packetId,
  uint32_t txTimestamp,
  uint32_t ackTimestamp,
  uint32_t rttUs,
  int rssi,
  float snr,
  bool ackReceived,
  const char *event
) {
  printConfigPrefix(event, packetId);

  Serial.print(',');
  Serial.print(txTimestamp);
  printEmpty();
  Serial.print(',');
  Serial.print(ackTimestamp);
  Serial.print(',');
  Serial.print(rttUs / 1000.0, 3);
  Serial.print(',');
  Serial.print(rssi);
  Serial.print(',');
  Serial.print(snr, 2);
  Serial.print(',');
  Serial.print(ackReceived ? 1 : 0);

  for (int i = 0; i < 2; i++) {
    printEmpty();
  }

  Serial.print(',');
  Serial.print(dataToAUs / 1000.0, 3);
  Serial.print(',');
  Serial.print(ackToAUs / 1000.0, 3);
  Serial.print(',');
  Serial.println(ackTimeoutMs);
}

void logRxData(
  uint32_t packetId,
  uint32_t txTimestamp,
  uint32_t rxTimestamp,
  int rssi,
  float snr,
  bool payloadValid,
  bool ackSent
) {
  printConfigPrefix("RX_DATA", packetId);

  Serial.print(',');
  Serial.print(txTimestamp);
  Serial.print(',');
  Serial.print(rxTimestamp);
  printEmpty();
  printEmpty();
  Serial.print(',');
  Serial.print(rssi);
  Serial.print(',');
  Serial.print(snr, 2);
  printEmpty();
  Serial.print(',');
  Serial.print(payloadValid ? 1 : 0);
  Serial.print(',');
  Serial.print(ackSent ? 1 : 0);
  Serial.print(',');
  Serial.print(dataToAUs / 1000.0, 3);
  Serial.print(',');
  Serial.print(ackToAUs / 1000.0, 3);
  Serial.print(',');
  Serial.println(ackTimeoutMs);
}

void logRxInvalid(uint32_t packetId) {
  printConfigPrefix("RX_INVALID", packetId);

  for (int i = 0; i < 9; i++) {
    printEmpty();
  }

  Serial.print(',');
  Serial.print(dataToAUs / 1000.0, 3);
  Serial.print(',');
  Serial.print(ackToAUs / 1000.0, 3);
  Serial.print(',');
  Serial.println(ackTimeoutMs);
}


bool initLoRa() {
  // Inisialisasi SPI dengan pin khusus LilyGO LoRa32/T3 v1.6.1
  SPI.begin(5, 19, 27, 18);

  // Konfigurasi pin modul LoRa: SS, RESET, DIO0
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  Serial.println("INFO,Initializing LoRa");

  if (!LoRa.begin(LORA_FREQ)) {
    Serial.println("ERROR,LoRa.begin failed");
    return false;
  }

  LoRa.setSignalBandwidth(LORA_BANDWIDTH);
  LoRa.setSpreadingFactor(LORA_SPREADING_FACTOR);
  LoRa.setCodingRate4(LORA_CODING_RATE);
  LoRa.setTxPower(LORA_TX_POWER);
  LoRa.setPreambleLength(LORA_PREAMBLE_LENGTH);
  LoRa.enableCrc();

  calculateTiming();

  Serial.println("INFO,LoRa initialization successful");
  return true;
}

void sendDataPacket(uint32_t packetId) {
  txTimestampUs = micros();

  LoRa.beginPacket();
  LoRa.write(TYPE_DATA);
  writeU16(RUN_ID);
  writeU32(packetId);
  writeU32(txTimestampUs);

  for (uint16_t i = 0; i < PAYLOAD_SIZE; i++) {
    LoRa.write((uint8_t)i);
  }

  LoRa.endPacket();

  logTxSent(packetId, txTimestampUs);
}

bool waitForAck(
  uint32_t packetId,
  uint32_t txTimestamp
) {
  const uint32_t startMs = millis();

  while ((uint32_t)(millis() - startMs) < ackTimeoutMs) {
    int packetSize = LoRa.parsePacket();

    if (packetSize <= 0) {
      continue;
    }

    if (packetSize != ACK_PACKET_SIZE) {
      while (LoRa.available()) LoRa.read();
      continue;
    }

    uint8_t type = LoRa.read();
    uint16_t runId = readU16();
    uint32_t receivedPacketId = readU32();
    uint8_t status = (uint8_t)LoRa.read();

    if (type != TYPE_ACK ||
        runId != RUN_ID ||
        receivedPacketId != packetId) {
      continue;
    }

    uint32_t ackTimestamp = micros();
    uint32_t rttUs = ackTimestamp - txTimestamp;

    int rssi = LoRa.packetRssi();
    float snr = LoRa.packetSnr();

    logTxAck(
      packetId,
      txTimestamp,
      ackTimestamp,
      rttUs,
      rssi,
      snr,
      true,
      status == ACK_OK ? "ACK_OK" : "ACK_ERROR"
    );

    return status == ACK_OK;
  }

  logTxAck(
    packetId,
    txTimestamp,
    0,
    0,
    0,
    0,
    false,
    "ACK_TIMEOUT"
  );

  return false;
}

void runTx() {
  for (uint32_t packetId = 1;
       packetId <= PACKET_COUNT;
       packetId++) {
    sendDataPacket(packetId);
    waitForAck(packetId, txTimestampUs);
    delay(PACKET_INTERVAL_MS);
  }
}

void sendAck(uint32_t packetId, uint8_t status) {
  LoRa.beginPacket();
  LoRa.write(TYPE_ACK);
  writeU16(RUN_ID);
  writeU32(packetId);
  LoRa.write(status);
  LoRa.endPacket();
}

void runRx() {
  while (true) {
    int packetSize = LoRa.parsePacket();

    if (packetSize <= 0) {
      continue;
    }

    if (packetSize != DATA_PACKET_SIZE) {
      while (LoRa.available()) LoRa.read();
      logRxInvalid(0);
      continue;
    }

    uint8_t type = (uint8_t)LoRa.read();
    uint16_t runId = readU16();
    uint32_t packetId = readU32();
    uint32_t txTimestamp = readU32();
    uint32_t rxTimestamp = micros();

    int rssi = LoRa.packetRssi();
    float snr = LoRa.packetSnr();

    bool payloadValid = true;

    for (uint16_t i = 0; i < PAYLOAD_SIZE; i++) {
      if (!LoRa.available()) {
        payloadValid = false;
        break;
      }

      uint8_t value = (uint8_t)LoRa.read();

      if (value != (uint8_t)i) {
        payloadValid = false;
      }
    }

    if (type != TYPE_DATA || runId != RUN_ID) {
      continue;
    }

    uint8_t status = payloadValid ? ACK_OK : ACK_ERROR;
    sendAck(packetId, status);

    logRxData(
      packetId,
      txTimestamp,
      rxTimestamp,
      rssi,
      snr,
      payloadValid,
      true
    );
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println(CSV_HEADER);

  if (!initLoRa()) {
    printConfigPrefix("INIT_ERROR", 0);
    Serial.println();
    while (true) delay(1000);
  }

  logStart();

#if DEVICE_ROLE == ROLE_TX
  runTx();
#elif DEVICE_ROLE == ROLE_RX
  runRx();
#else
  while (true) delay(1000);
#endif
}

void loop() {
}
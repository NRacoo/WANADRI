#include <SPI.h>
#include <LoRa.h>
#include <stdlib.h>

#define FIRMWARE_VERSION "0.6.0"

// Select ROLE_TX on the sender and ROLE_RX on the receiver.
#define ROLE_TX 1
#define ROLE_RX 2
#define DEVICE_ROLE ROLE_TX

#define LORA_SCK 5
#define LORA_MISO 19
#define LORA_MOSI 27
#define LORA_SS 18
#define LORA_RST 14
#define LORA_DIO0 26

#define LORA_FREQ 915E6
#define LORA_BANDWIDTH 125E3
#define LORA_CODING_RATE 5
#define DEFAULT_SF 7
#define DEFAULT_TX_POWER 17

#define DEFAULT_RUN_ID 1
#define DEFAULT_PACKET_COUNT 10
#define DEFAULT_DISTANCE_M -1
#define PAYLOAD_SIZE 100
#define PACKET_INTERVAL_MS 500
#define ACK_TIMEOUT_MARGIN_MS 100
#define ACK_TIMEOUT_MIN_MS 300
#define ACK_TIMEOUT_MAX_MS 10000

const uint8_t TYPE_DATA = 0x01;
const uint8_t TYPE_ACK = 0x02;

uint16_t runId = DEFAULT_RUN_ID;
uint32_t packetCount = DEFAULT_PACKET_COUNT;
int32_t distanceM = DEFAULT_DISTANCE_M;
uint8_t spreadingFactor = DEFAULT_SF;
int8_t txPower = DEFAULT_TX_POWER;
bool radioInitialized = false;
bool experimentStarted = false;

uint32_t txSentCount = 0;
uint32_t ackReceivedCount = 0;
uint32_t ackTimeoutCount = 0;
uint32_t rxReceivedCount = 0;
uint32_t rxValidCount = 0;
uint32_t rxAckSentCount = 0;
uint32_t lastPacketId = 0;
uint32_t estimatedDataToAMs = 0;
uint32_t estimatedAckToAMs = 0;
uint32_t ackTimeoutMs = 0;

const char CSV_HEADER[] =
"role,event,firmware,run_id,packet_id,sf,bw_hz,cr,payload_bytes,distance_m,"
"tx_timestamp_us,rx_timestamp_us,ack_timestamp_us,rtt_ms,rssi_dbm,snr_db,"
"ack_received,payload_valid,ack_sent,data_toa_ms,ack_toa_ms,ack_timeout_ms";

void csvEmptyField() { Serial.print(','); }

void printConfigPrefix(const char *event, uint32_t packetId) {
  Serial.print(DEVICE_ROLE == ROLE_TX ? "TX," : "RX,");
  Serial.print(event); Serial.print(',');
  Serial.print(FIRMWARE_VERSION); Serial.print(',');
  Serial.print(runId); Serial.print(',');
  Serial.print(packetId); Serial.print(',');
  Serial.print(spreadingFactor); Serial.print(',');
  Serial.print((uint32_t)LORA_BANDWIDTH); Serial.print(',');
  Serial.print(LORA_CODING_RATE); Serial.print(',');
  Serial.print(PAYLOAD_SIZE); Serial.print(',');
  if (distanceM >= 0) Serial.print(distanceM);
}

void printCsvEvent(const char *event, uint32_t packetId,
                   long txUs, long rxUs, long ackUs, float rttMs,
                   int rssi, float snr, int ackReceived, int payloadValid,
                   int ackSent) {
  printConfigPrefix(event, packetId);
  Serial.print(',');
  if (txUs >= 0) Serial.print(txUs);
  Serial.print(',');
  if (rxUs >= 0) Serial.print(rxUs);
  Serial.print(',');
  if (ackUs >= 0) Serial.print(ackUs);
  Serial.print(',');
  if (rttMs >= 0) Serial.print(rttMs, 3);
  Serial.print(',');
  if (rssi > -1000) Serial.print(rssi);
  Serial.print(',');
  if (snr > -1000) Serial.print(snr, 2);
  Serial.print(',');
  Serial.print(ackReceived); Serial.print(',');
  Serial.print(payloadValid); Serial.print(',');
  Serial.print(ackSent); Serial.print(',');
  Serial.print(estimatedDataToAMs); Serial.print(',');
  Serial.print(estimatedAckToAMs); Serial.print(',');
  Serial.println(ackTimeoutMs);
}

void calculateTiming() {
  // Approximate LoRa time-on-air for explicit header, CRC, preamble=8,
  // low-data-rate optimization selected automatically by the radio library.
  const float sf = spreadingFactor;
  const float bw = (float)LORA_BANDWIDTH;
  const float symbolMs = (pow(2.0f, sf) / bw) * 1000.0f;
  const float de = (sf >= 11 && bw <= 125000.0f) ? 1.0f : 0.0f;
  const float ih = 0.0f;
  const float crc = 1.0f;
  const float cr = (float)(LORA_CODING_RATE - 4);
  const float numerator = 8.0f * (float)PAYLOAD_SIZE - 4.0f * sf + 28.0f + 16.0f * crc - 20.0f * ih;
  const float denominator = 4.0f * (sf - 2.0f * de);
  float payloadSymbols = 8.0f;
  if (denominator > 0.0f) {
    payloadSymbols += ceilf(fmaxf(0.0f, numerator / denominator) * (cr + 4.0f));
  }
  estimatedDataToAMs = (uint32_t)ceilf((8.0f + 4.25f + payloadSymbols) * symbolMs);
  const float ackNumerator = 8.0f * 5.0f - 4.0f * sf + 28.0f + 16.0f * crc - 20.0f * ih;
  float ackPayloadSymbols = 8.0f;
  if (denominator > 0.0f) {
    ackPayloadSymbols += ceilf(fmaxf(0.0f, ackNumerator / denominator) * (cr + 4.0f));
  }
  estimatedAckToAMs = (uint32_t)ceilf((8.0f + 4.25f + ackPayloadSymbols) * symbolMs);
  uint32_t timeout = estimatedDataToAMs + estimatedAckToAMs + ACK_TIMEOUT_MARGIN_MS;
  if (timeout < ACK_TIMEOUT_MIN_MS) timeout = ACK_TIMEOUT_MIN_MS;
  if (timeout > ACK_TIMEOUT_MAX_MS) timeout = ACK_TIMEOUT_MAX_MS;
  ackTimeoutMs = timeout;
}

bool initLoRa() {
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(LORA_FREQ)) return false;
  LoRa.setSignalBandwidth(LORA_BANDWIDTH);
  LoRa.setCodingRate4(LORA_CODING_RATE);
  LoRa.setSpreadingFactor(spreadingFactor);
  LoRa.setTxPower(txPower);
  calculateTiming();
  return true;
}

void printHelp() {
  Serial.println("CONTROL,help | status | set run <1..65535> | set packets <1..1000000>");
  Serial.println("CONTROL,set distance <-1 or meters> | set sf <7..12> | set power <2..17> | start");
}

void printStatus() {
  Serial.print("STATUS,firmware,"); Serial.print(FIRMWARE_VERSION);
  Serial.print(",role,"); Serial.print(DEVICE_ROLE == ROLE_TX ? "TX" : "RX");
  Serial.print(",run_id,"); Serial.print(runId);
  Serial.print(",packets,"); Serial.print(packetCount);
  Serial.print(",distance_m,"); Serial.print(distanceM);
  Serial.print(",sf,"); Serial.print(spreadingFactor);
  Serial.print(",bw_hz,"); Serial.print((uint32_t)LORA_BANDWIDTH);
  Serial.print(",cr,"); Serial.print(LORA_CODING_RATE);
  Serial.print(",tx_power_dbm,"); Serial.print(txPower);
  Serial.print(",data_toa_ms,"); Serial.print(estimatedDataToAMs);
  Serial.print(",ack_toa_ms,"); Serial.print(estimatedAckToAMs);
  Serial.print(",ack_timeout_ms,"); Serial.print(ackTimeoutMs);
  Serial.print(",state,"); Serial.println(experimentStarted ? "RUNNING" : "CONFIGURABLE");
}

bool parseInteger(const String &text, long &value) {
  if (!text.length()) return false;
  const char *start = text.c_str();
  char *end = nullptr;
  value = strtol(start, &end, 10);
  return end != start && *end == '\0';
}

void sendAck(uint32_t packetId) {
  LoRa.beginPacket();
  LoRa.write(TYPE_ACK);
  LoRa.write((uint8_t)(packetId >> 24));
  LoRa.write((uint8_t)(packetId >> 16));
  LoRa.write((uint8_t)(packetId >> 8));
  LoRa.write((uint8_t)packetId);
  LoRa.endPacket();
  rxAckSentCount++;
}

void runTx() {
  txSentCount = ackReceivedCount = ackTimeoutCount = 0;
  for (uint32_t packetId = 1; packetId <= packetCount; packetId++) {
    char payload[PAYLOAD_SIZE + 1];
    snprintf(payload, sizeof(payload), "LORA-EXP-R%u-P%lu-", runId, (unsigned long)packetId);
    size_t prefixLen = strlen(payload);
    for (size_t i = prefixLen; i < PAYLOAD_SIZE; i++) payload[i] = 'A' + (i % 26);
    payload[PAYLOAD_SIZE] = '\0';

    uint32_t txTimestamp = micros();
    LoRa.beginPacket();
    LoRa.write(TYPE_DATA);
    LoRa.write((uint8_t)(packetId >> 24));
    LoRa.write((uint8_t)(packetId >> 16));
    LoRa.write((uint8_t)(packetId >> 8));
    LoRa.write((uint8_t)packetId);
    LoRa.write((const uint8_t *)payload, PAYLOAD_SIZE);
    LoRa.endPacket();
    txSentCount++;

    uint32_t waitStart = millis();
    bool ackOk = false;
    uint32_t ackTimestamp = 0;
    while (millis() - waitStart < ackTimeoutMs) {
      int size = LoRa.parsePacket();
      if (size >= 5) {
        uint8_t type = LoRa.read();
        uint32_t ackId = 0;
        for (int i = 0; i < 4; i++) ackId = (ackId << 8) | (uint8_t)LoRa.read();
        while (LoRa.available()) LoRa.read();
        if (type == TYPE_ACK && ackId == packetId) {
          ackOk = true;
          ackTimestamp = micros();
          break;
        }
      }
      delay(1);
    }

    if (ackOk) {
      ackReceivedCount++;
      printCsvEvent("TX_ACK_OK", packetId, txTimestamp, -1, ackTimestamp,
                    (uint32_t)(ackTimestamp - txTimestamp) / 1000.0f,
                    -1001, -1001.0f, 1, 1, 0);
    } else {
      ackTimeoutCount++;
      printCsvEvent("TX_ACK_TIMEOUT", packetId, txTimestamp, -1, -1, -1,
                    -1001, -1001.0f, 0, 0, 0);
    }
    delay(PACKET_INTERVAL_MS);
  }

  Serial.print("SUMMARY,TX,run_id,"); Serial.print(runId);
  Serial.print(",sent,"); Serial.print(txSentCount);
  Serial.print(",ack_received,"); Serial.print(ackReceivedCount);
  Serial.print(",ack_timeout,"); Serial.print(ackTimeoutCount);
  Serial.print(",pdr_percent,");
  Serial.println(txSentCount ? 100.0f * ackReceivedCount / txSentCount : 0.0f, 2);
}

void processRxPacket() {
  int packetSize = LoRa.parsePacket();
  if (packetSize <= 0) return;

  uint8_t type = LoRa.read();
  if (type == TYPE_ACK) {
    while (LoRa.available()) LoRa.read();
    return;
  }
  if (type != TYPE_DATA || packetSize < 5) {
    while (LoRa.available()) LoRa.read();
    return;
  }

  uint32_t packetId = 0;
  for (int i = 0; i < 4; i++) packetId = (packetId << 8) | (uint8_t)LoRa.read();

  char payload[PAYLOAD_SIZE + 1];
  size_t n = 0;
  while (LoRa.available() && n < PAYLOAD_SIZE) payload[n++] = (char)LoRa.read();
  while (LoRa.available()) LoRa.read();
  payload[n] = '\0';

  bool valid = (n == PAYLOAD_SIZE);
  uint32_t rxTimestamp = micros();
  int rssi = LoRa.packetRssi();
  float snr = LoRa.packetSnr();
  rxReceivedCount++;
  if (valid) rxValidCount++;

  printCsvEvent("RX_DATA", packetId, -1, rxTimestamp, -1, -1,
                rssi, snr, 0, valid ? 1 : 0, 0);
  sendAck(packetId);
  printCsvEvent("RX_ACK_SENT", packetId, -1, rxTimestamp, -1, -1,
                rssi, snr, 0, valid ? 1 : 0, 1);
}

void processCommand(String command) {
  command.trim();
  command.toLowerCase();
  if (command == "help") { printHelp(); return; }
  if (command == "status") { printStatus(); return; }
  if (command == "start") {
    if (!radioInitialized) { Serial.println("ERROR,Radio not initialized"); return; }
    if (experimentStarted) { Serial.println("ERROR,Experiment already started"); return; }
    calculateTiming();
    experimentStarted = true;
    Serial.println("CONTROL,STARTING");
    if (DEVICE_ROLE == ROLE_TX) {
      runTx();
      experimentStarted = false;
      Serial.println("CONTROL,RUN_COMPLETE");
      printStatus();
    } else {
      rxReceivedCount = rxValidCount = rxAckSentCount = 0;
      Serial.println("CONTROL,RX_READY");
    }
    return;
  }
  if (experimentStarted) { Serial.println("ERROR,Stop/reset before changing settings"); return; }
  if (!command.startsWith("set ")) { Serial.println("ERROR,Unknown command; type help"); return; }

  int separator = command.indexOf(' ', 4);
  if (separator < 0) { Serial.println("ERROR,Use set <key> <value>"); return; }
  String key = command.substring(4, separator);
  String textValue = command.substring(separator + 1);
  textValue.trim();
  long value;
  if (!parseInteger(textValue, value)) { Serial.println("ERROR,Value must be an integer"); return; }

  if (key == "run" && value >= 1 && value <= 65535) runId = (uint16_t)value;
  else if (key == "packets" && value >= 1 && value <= 1000000L) packetCount = (uint32_t)value;
  else if (key == "distance" && value >= -1 && value <= 2147483647L) distanceM = (int32_t)value;
  else if (key == "sf" && value >= 7 && value <= 12) spreadingFactor = (uint8_t)value;
  else if (key == "power" && value >= 2 && value <= 17) txPower = (int8_t)value;
  else { Serial.println("ERROR,Unknown setting or value out of range"); return; }

  LoRa.setSpreadingFactor(spreadingFactor);
  LoRa.setTxPower(txPower);
  calculateTiming();
  Serial.println("CONTROL,CONFIG_UPDATED");
  printStatus();
}

void handleSerialCommands() {
  static String input;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (input.length()) { processCommand(input); input = ""; }
    } else if (input.length() < 100) input += c;
    else { input = ""; Serial.println("ERROR,Command too long"); }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println(CSV_HEADER);
  radioInitialized = initLoRa();
  if (!radioInitialized) {
    printCsvEvent("INIT_ERROR", 0, -1, -1, -1, -1, -1001, -1001.0f, 0, 0, 0);
    while (true) delay(1000);
  }
  Serial.println("CONTROL,LoRa initialized; set config then enter start");
  printHelp();
  printStatus();
}

void loop() {
  handleSerialCommands();
#if DEVICE_ROLE == ROLE_RX
  if (experimentStarted) processRxPacket();
#endif
  delay(1);
}

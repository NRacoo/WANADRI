/*
  LilyGo T3 V1 LoRa Bridge (Updated for V1 Pinout)
  ------------------------------------------------
  BOARD: TTGO LoRa32-OLED (Select in Arduino IDE)
  
  WIRING RECAP:
  - LilyGo Pin 25 <--> Pi Pin 8 (TX)
  - LilyGo Pin 23 <--> Pi Pin 10 (RX)
  - LilyGo GND    <--> Pi Pin 6 (GND)
*/

#include <SPI.h>
#include <LoRa.h>

// --- HARDWARE CONFIG (Standard T3 V1) ---
#define SCK     5
#define MISO    19
#define MOSI    27
#define SS      18
#define RST     14
#define DIO0    26

// --- UART CONFIG (UPDATED) ---
// We are using IO25 and IO23 because 16/17 are missing on V1
#define RX_PIN  25  // Connect this to Raspberry Pi TX (Pin 8)
#define TX_PIN  23  // Connect this to Raspberry Pi RX (Pin 10)
#define BAUDRATE 115200

// --- LORA CONFIG (UPDATED) ---
// Your board is 915MHz version.
#define BAND    915E6 

HardwareSerial SerialPi(2); // Use UART2

void setup() {
  // 1. Debug Serial (USB to PC)
  Serial.begin(115200);
  Serial.println("System Init...");

  // 2. Pi Serial (UART to Raspberry Pi)
  // This tells the ESP32 to route Serial2 to pins 25 and 23
  SerialPi.begin(BAUDRATE, SERIAL_8N1, RX_PIN, TX_PIN); 

  // 3. LoRa Init
  SPI.begin(SCK, MISO, MOSI, SS);
  LoRa.setPins(SS, RST, DIO0);
  
  if (!LoRa.begin(BAND)) {
    Serial.println("LoRa Init Failed! Check frequency/antenna.");
    while (1);
  }

  // Reliability Settings
  LoRa.setSpreadingFactor(9);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.setTxPower(20);

  Serial.println("LoRa Bridge Active (915MHz).");
  Serial.println("Connect Pi TX to IO25, Pi RX to IO23.");
}

void loop() {
  // TASK A: Pi -> LoRa
  if (SerialPi.available()) {
    String data = SerialPi.readStringUntil('\n');
    if (data.length() > 0) {
      LoRa.beginPacket();
      LoRa.print(data);
      LoRa.print("\n");
      LoRa.endPacket();
      Serial.println("TX >> " + data); // Show on USB debug
    }
  }

  // TASK B: LoRa -> Pi
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String incoming = "";
    while (LoRa.available()) {
      incoming += (char)LoRa.read();
    }
    SerialPi.println(incoming);      // Send to Pi
    Serial.println("RX << " + incoming); // Show on USB debug
  }
}
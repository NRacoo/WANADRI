/*
  LilyGo T3 v1.6.1 LoRa Transparent Bridge
  ----------------------------------------
  Acts as a serial pipe between Raspberry Pi and LoRa.
  
  1. Data received from Pi (Serial) -> Sent via LoRa
  2. Data received from LoRa -> Sent to Pi (Serial)
*/

#include <SPI.h>
#include <LoRa.h>

// ================= CONFIGURATION =================
// Frequency: 915E6, 868E6, or 433E6 depending on your hardware/region
// Indonesia usually allows 920-923MHz, but hobby modules often match 433/915.
#define BAND    433E6 

// Pin Definitions for LilyGo T3 v1.6.1
#define SCK     5
#define MISO    19
#define MOSI    27
#define SS      18
#define RST     14
#define DIO0    26

// UART Pins to Raspberry Pi
// We use Serial2 (Hardware Serial) for stability
#define RX_PIN  16  // Connect to Pi TX (GPIO 14)
#define TX_PIN  17  // Connect to Pi RX (GPIO 15)
// =================================================

HardwareSerial SerialPi(2); // Use UART2

void setup() {
  // 1. Initialize USB Serial (for Debugging only)
  Serial.begin(115200);
  
  // 2. Initialize UART to Raspberry Pi
  SerialPi.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
  
  // 3. Initialize LoRa
  Serial.println("Starting LoRa...");
  SPI.begin(SCK, MISO, MOSI, SS);
  LoRa.setPins(SS, RST, DIO0);

  if (!LoRa.begin(BAND)) {
    Serial.println("Starting LoRa failed!");
    while (1);
  }
  
  // LoRa Configuration for better range/reliability
  LoRa.setSpreadingFactor(10); // Higher = Slower but longer range (7-12)
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.setTxPower(20);         // Max power (20dBm)
  
  Serial.println("LoRa Bridge Ready.");
}

void loop() {
  // TASK A: Forward Data from Pi -> LoRa
  if (SerialPi.available()) {
    String dataFromPi = SerialPi.readStringUntil('\n');
    
    // Only send if not empty
    if (dataFromPi.length() > 0) {
      LoRa.beginPacket();
      LoRa.print(dataFromPi);
      LoRa.print("\n"); // Restore newline for the receiver
      LoRa.endPacket();
      
      Serial.print("TX: "); // Debug info to USB
      Serial.println(dataFromPi);
    }
  }

  // TASK B: Forward Data from LoRa -> Pi
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String incoming = "";
    while (LoRa.available()) {
      incoming += (char)LoRa.read();
    }
    
    // Send to Pi exactly as received
    SerialPi.print(incoming);
    
    Serial.print("RX: "); // Debug info to USB
    Serial.println(incoming);
  }
}
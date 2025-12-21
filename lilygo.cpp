#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Pins for LilyGO T3 v1.6.1
#define SCK 5
#define MISO 19
#define MOSI 27
#define SS 18
#define RST 14
#define DI0 26
#define BAND 915E6 // Change to 433E6 or 868E6 depending on your region!

// OLED Pins
#define OLED_SDA 4
#define OLED_SCL 15
#define OLED_RST 16
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST);

void setup() {
  Serial.begin(115200);
  
  // Setup OLED
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW); delay(20); digitalWrite(OLED_RST, HIGH);
  Wire.begin(OLED_SDA, OLED_SCL);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3c)) { 
    Serial.println(F("SSD1306 allocation failed"));
  }
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  display.println("LoRa Bridge Ready");
  display.display();

  // Setup LoRa
  SPI.begin(SCK, MISO, MOSI, SS);
  LoRa.setPins(SS, RST, DI0);
  
  if (!LoRa.begin(BAND)) {
    Serial.println("Starting LoRa failed!");
    while (1);
  }
  Serial.println("LoRa Initialized");
}

void loop() {
  // 1. Check if Pi sent data via Serial to transmit over LoRa
  if (Serial.available()) {
    // Read packet size (first byte) or assume fixed stream
    // For simplicity, we read as much as available into a buffer
    int len = Serial.available();
    byte buffer[250]; // LoRa packet limit is 255 bytes
    if (len > 240) len = 240; // Safety cap
    
    Serial.readBytes(buffer, len);
    
    LoRa.beginPacket();
    LoRa.write(buffer, len);
    LoRa.endPacket();
    
    display.clearDisplay();
    display.setCursor(0,0);
    display.print("TX: "); display.print(len); display.println(" bytes");
    display.display();
    
    // Send acknowledgement back to Pi so it knows it can send the next chunk
    Serial.println("ACK"); 
  }

  // 2. Check if LoRa received data to send to Pi via Serial
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    while (LoRa.available()) {
      byte b = LoRa.read();
      Serial.write(b); // Pass byte directly to Pi
    }
    
    display.clearDisplay();
    display.setCursor(0,0);
    display.print("RX: "); display.print(packetSize); display.println(" bytes");
    display.display();
  }
}
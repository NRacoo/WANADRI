#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// --- PIN DEFINITIONS ---
// LoRa Pins (LilyGO T3 v1.6.1)
#define SCK 5
#define MISO 19
#define MOSI 27
#define SS 18
#define RST 14
#define DI0 26
#define BAND 915E6 

// OLED Pins
#define OLED_SDA 4
#define OLED_SCL 15
#define OLED_RST 16

// UART Pins to Raspberry Pi
#define RX_PIN 13  // Connect to Pi TX (GPIO 14)
#define TX_PIN 12  // Connect to Pi RX (GPIO 15)

// Create a new Serial object
HardwareSerial PiSerial(1); 

Adafruit_SSD1306 display(128, 64, &Wire, OLED_RST);

// Variables for Status Display
unsigned long lastActivityTime = 0;
const int idleTimeout = 1000; // How long to wait (ms) before showing "Idle"
bool isIdleDisplayed = false;

void setup() {
  // Debug Serial (USB)
  Serial.begin(115200);
  
  // Communication Serial (GPIO to Pi)
  PiSerial.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);

  // OLED Setup
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW); delay(20); digitalWrite(OLED_RST, HIGH);
  Wire.begin(OLED_SDA, OLED_SCL);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3c)) { 
    Serial.println(F("SSD1306 failed"));
  }
  
  // Initial Screen
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  display.println("Initializing...");
  display.display();

  // LoRa Setup
  SPI.begin(SCK, MISO, MOSI, SS);
  LoRa.setPins(SS, RST, DI0);
  if (!LoRa.begin(BAND)) {
    Serial.println("LoRa failed!");
    display.clearDisplay();
    display.setCursor(0,0);
    display.println("LoRa Error!");
    display.display();
    while (1);
  }
  
  lastActivityTime = millis();
}

void loop() {
  // 1. SENDING: Read from Pi (GPIO), Send via LoRa
  if (PiSerial.available()) {
    int len = PiSerial.available();
    byte buffer[250];
    if (len > 240) len = 240;
    
    PiSerial.readBytes(buffer, len);
    
    LoRa.beginPacket();
    LoRa.write(buffer, len);
    LoRa.endPacket();
    
    // Update Screen immediately for action
    display.clearDisplay();
    display.setCursor(0,0);
    display.println(">> TX SENDING >>");
    display.print("Bytes: "); display.println(len);
    display.display();
    
    // Send ACK back to Pi via GPIO
    PiSerial.println("ACK"); 
    
    // Reset Idle Timer
    lastActivityTime = millis();
    isIdleDisplayed = false;
  }

  // 2. RECEIVING: Read from LoRa, Send to Pi (GPIO)
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    while (LoRa.available()) {
      byte b = LoRa.read();
      PiSerial.write(b); // Send to Pi via GPIO pins
    }
    
    // Update Screen immediately for action
    display.clearDisplay();
    display.setCursor(0,0);
    display.println("<< RX RECEIVING <<");
    display.print("Bytes: "); display.println(packetSize);
    display.display();

    // Reset Idle Timer
    lastActivityTime = millis();
    isIdleDisplayed = false;
  }

  // 3. IDLE STATUS CHECK
  // If no activity for 1 second, show "Idle / Sender Ready"
  if (!isIdleDisplayed && (millis() - lastActivityTime > idleTimeout)) {
    display.clearDisplay();
    display.setCursor(0,0);
    display.setTextSize(1);
    
    display.println("System Status:");
    display.println("----------------");
    display.setTextSize(2);
    display.println(" IDLE");
    display.setTextSize(1);
    display.println("");
    display.println("Sender Ready...");
    
    display.display();
    isIdleDisplayed = true;
  }
}
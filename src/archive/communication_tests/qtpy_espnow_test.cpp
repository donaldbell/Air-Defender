#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// Target M5Stack Atoms3 MAC address
uint8_t m5stack_mac[] = {0x34, 0xb7, 0xda, 0x57, 0x36, 0xfc};

// Low power LED control - use ESP32 hardware RMT
void setStatusLED(uint8_t r, uint8_t g, uint8_t b, bool dim = true) {
  if (dim) {
    // Use low brightness to reduce power consumption and heat
    r = r > 0 ? 8 : 0;   // Dim but visible
    g = g > 0 ? 8 : 0;
    b = b > 0 ? 8 : 0;
  }
  neopixelWrite(39, r, g, b);  // Use ESP32 built-in hardware function
}

// Clear LED to save power
void clearStatusLED() {
  neopixelWrite(39, 0, 0, 0);
}

// ESP-NOW send callback - minimal LED feedback 
void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.printf("ESP-NOW Send Status: %s\n", (status == ESP_NOW_SEND_SUCCESS) ? "SUCCESS" : "FAIL");
  
  // No LED feedback during normal operation to save power
  // Status is available via serial monitor
}

// LED message structure - matches M5Stack receiver (8 bytes)
typedef struct {
  uint8_t command;     // 1=test pattern, 2=clear, 3=game data, 4=environmental
  uint8_t red;
  uint8_t green;
  uint8_t blue;
  uint8_t position;
  uint8_t length;
  uint8_t stripId;     // 0=PM2.5, 1=NO2, 2=O3
  uint8_t gameState;   // Current game state
} DisplayMessage;

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("=== QT PY S3 ESP-NOW TEST (POWER OPTIMIZED) ===");
  
  // Configure NeoPixel with power management
  pinMode(38, OUTPUT);  // NEOPIXEL_POWER
  pinMode(39, OUTPUT);  // NEOPIXEL_PIN
  digitalWrite(38, HIGH);
  
  // Brief startup indicator only
  setStatusLED(0, 0, 255);  // Dim blue = booting
  delay(1000);
  clearStatusLED();         // Turn off after boot - stay off during operation
  
  // Power management settings
  setCpuFrequencyMhz(80);   // Reduce from 240MHz to 80MHz to save power
  
  // Initialize WiFi in STA mode with power saving
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);      // Enable WiFi power saving
  Serial.printf("Local MAC Address: %s\n", WiFi.macAddress().c_str());
  Serial.printf("CPU Frequency: %d MHz\n", getCpuFrequencyMhz());
  
  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ ESP-NOW init failed");
    return;  // No LED error indication
  }
  
  // Register send callback
  esp_now_register_send_cb(onDataSent);
  
  // Add M5Stack peer
  esp_now_peer_info_t peer_info = {};
  memcpy(peer_info.peer_addr, m5stack_mac, 6);
  peer_info.channel = 0;
  peer_info.encrypt = false;
  
  if (esp_now_add_peer(&peer_info) != ESP_OK) {
    Serial.println("❌ Failed to add M5Stack peer");
    return;  // No LED error indication
  }
  
  Serial.println("✅ ESP-NOW initialized - LED will stay off during operation");
}

void loop() {
  static int pattern_count = 0;
  
  Serial.printf("Sending pattern #%d to M5Stack...\n", pattern_count);
  
  DisplayMessage message;
  message.command = 1;      // Test pattern command
  message.position = 0;     // Start position
  message.length = 8;       // Full strip length
  message.stripId = 0;      // First strip
  message.gameState = 0;    // Normal state
  
  // Create different test patterns
  switch (pattern_count % 4) {
    case 0: // Red
      Serial.println("Pattern: Red");
      message.red = 255;
      message.green = 0;
      message.blue = 0;
      break;
      
    case 1: // Green
      Serial.println("Pattern: Green");
      message.red = 0;
      message.green = 255;
      message.blue = 0;
      break;
      
    case 2: // Blue
      Serial.println("Pattern: Blue");
      message.red = 0;
      message.green = 0;
      message.blue = 255;
      break;
      
    case 3: // Purple
      Serial.println("Pattern: Purple");
      message.red = 128;
      message.green = 0;
      message.blue = 128;
      break;
  }
  
  // Send pattern via ESP-NOW
  esp_err_t result = esp_now_send(m5stack_mac, (uint8_t*)&message, sizeof(message));
  
  if (result == ESP_OK) {
    Serial.println("Message sent successfully");
  } else {
    Serial.printf("Send error: %d\n", result);
    // No LED indication for errors - rely on serial output
  }
  
  pattern_count++;
  
  // Power-efficient delay with LED completely off
  clearStatusLED();           // Ensure LED is off
  delay(4000);                // Send new pattern every 4 seconds
}
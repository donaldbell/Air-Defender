// ESP-NOW Communication Test for Adafruit QT Py S3
// Tests wireless communication with M5Stack Atoms3 Display Board
// Visual feedback via built-in NeoPixel

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// QT Py S3 Built-in NeoPixel configuration
#define NEOPIXEL_PIN     39  // Built-in RGB LED
#define NEOPIXEL_POWER   38  // Power control for NeoPixel

// M5Stack Atom S3 Lite MAC address for wireless LED control
uint8_t displayUnitMAC[] = {0x34, 0xb7, 0xda, 0x57, 0x36, 0xfc};

// Message structure for ESP-NOW communication (matches main game)
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

DisplayMessage testMessage;
esp_now_peer_info_t peerInfo;
bool espnowReady = false;
bool lastSendSuccess = false;
unsigned long lastSendTime = 0;
int testPattern = 0;
unsigned long lastPatternChange = 0;

// Status LED functions for QT Py S3 built-in NeoPixel
void setStatusLED(uint8_t r, uint8_t g, uint8_t b) {
  // Simple bit-bang implementation for single NeoPixel (GRB format)
  uint32_t color = ((uint32_t)g << 16) | ((uint32_t)r << 8) | b;
  digitalWrite(NEOPIXEL_PIN, LOW);
  delayMicroseconds(50);  // Reset pulse
  
  for (int i = 23; i >= 0; i--) {
    digitalWrite(NEOPIXEL_PIN, HIGH);
    if (color & (1 << i)) {
      delayMicroseconds(0.8);  // 1 bit timing
      digitalWrite(NEOPIXEL_PIN, LOW);
      delayMicroseconds(0.45);
    } else {
      delayMicroseconds(0.4);   // 0 bit timing
      digitalWrite(NEOPIXEL_PIN, LOW);
      delayMicroseconds(0.85);
    }
  }
  digitalWrite(NEOPIXEL_PIN, LOW);
}

void statusLEDOff() {
  setStatusLED(0, 0, 0);
}

// ESP-NOW callback function
void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  lastSendSuccess = (status == ESP_NOW_SEND_SUCCESS);
  
  Serial.printf("📡 ESP-NOW Send: %s to %02X:%02X:%02X:%02X:%02X:%02X\n",
                lastSendSuccess ? "SUCCESS" : "FAILED",
                mac_addr[0], mac_addr[1], mac_addr[2], 
                mac_addr[3], mac_addr[4], mac_addr[5]);
  
  // Visual feedback on status LED
  if (lastSendSuccess) {
    setStatusLED(0, 255, 0);  // Green flash for success
  } else {
    setStatusLED(255, 0, 0);  // Red flash for failure
  }
  delay(100);
}

// Initialize ESP-NOW
bool initESPNow() {
  // Set up WiFi in station mode
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  
  Serial.print("🔧 ESP32-S3 MAC Address: ");
  Serial.println(WiFi.macAddress());
  
  // Initialize ESP-NOW
  esp_err_t result = esp_now_init();
  if (result != ESP_OK) {
    Serial.printf("❌ ESP-NOW init failed: 0x%x\n", result);
    return false;
  }
  Serial.println("✅ ESP-NOW initialized");
  
  // Register send callback
  result = esp_now_register_send_cb(onDataSent);
  if (result != ESP_OK) {
    Serial.printf("❌ ESP-NOW callback registration failed: 0x%x\n", result);
    return false;
  }
  Serial.println("✅ ESP-NOW callback registered");
  
  // Add peer (M5Stack display board)
  memcpy(peerInfo.peer_addr, displayUnitMAC, 6);
  peerInfo.channel = 0;  
  peerInfo.encrypt = false;
  
  Serial.printf("🎯 Adding peer: %02X:%02X:%02X:%02X:%02X:%02X\n",
                displayUnitMAC[0], displayUnitMAC[1], displayUnitMAC[2],
                displayUnitMAC[3], displayUnitMAC[4], displayUnitMAC[5]);
  
  result = esp_now_add_peer(&peerInfo);
  if (result != ESP_OK) {
    Serial.printf("❌ Failed to add peer: 0x%x\n", result);
    return false;
  }
  Serial.println("✅ M5Stack peer added successfully");
  Serial.println();
  
  return true;
}

// Send test message to display board
void sendTestMessage(uint8_t cmd, uint8_t r, uint8_t g, uint8_t b, uint8_t pos, uint8_t len, uint8_t strip) {
  if (!espnowReady) return;
  
  testMessage.command = cmd;
  testMessage.red = r;
  testMessage.green = g;
  testMessage.blue = b;
  testMessage.position = pos;
  testMessage.length = len;
  testMessage.stripId = strip;
  testMessage.gameState = 1;  // Test state
  
  esp_err_t result = esp_now_send(displayUnitMAC, (uint8_t *)&testMessage, sizeof(testMessage));
  
  if (result == ESP_OK) {
    Serial.printf("📤 Sent: cmd=%d, RGB=(%d,%d,%d), pos=%d, len=%d, strip=%d\n", 
                  cmd, r, g, b, pos, len, strip);
  } else {
    Serial.printf("❌ Send failed: 0x%x\n", result);
    setStatusLED(255, 0, 0);  // Red for send error
  }
  
  lastSendTime = millis();
}

void setup() {
  Serial.begin(115200);
  delay(2000);  // Give time for serial monitor
  
  Serial.println("=== ESP-NOW COMMUNICATION TEST ===");
  Serial.println("🚀 Adafruit QT Py S3 -> M5Stack Atoms3");
  Serial.println();
  
  // Initialize built-in NeoPixel
  pinMode(NEOPIXEL_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_POWER, HIGH);  // Turn on NeoPixel power
  pinMode(NEOPIXEL_PIN, OUTPUT);
  setStatusLED(0, 0, 255);  // Start with blue
  Serial.println("💡 Built-in NeoPixel initialized");
  
  // Check PSRAM
  if (psramFound()) {
    Serial.printf("🧠 PSRAM found: %d bytes total, %d bytes free\n", ESP.getPsramSize(), ESP.getFreePsram());
  } else {
    Serial.println("⚠️ PSRAM not found - using internal RAM only");
  }
  
  Serial.printf("💾 Free heap: %d bytes\n", ESP.getFreeHeap());
  Serial.printf("⚡ CPU frequency: %d MHz\n", getCpuFrequencyMhz());
  Serial.println();
  
  // Initialize ESP-NOW
  setStatusLED(255, 165, 0);  // Orange - initializing
  espnowReady = initESPNow();
  
  if (espnowReady) {
    setStatusLED(0, 255, 255);  // Cyan - ready
    Serial.println("🎮 ESP-NOW ready! Starting test sequence...");
    Serial.println("📺 Watch your M5Stack display for colorful test patterns!");
    Serial.println();
  } else {
    setStatusLED(255, 0, 0);  // Red - failed
    Serial.println("💥 ESP-NOW initialization failed!");
    Serial.println("🔧 Check your M5Stack display board is powered on");
    Serial.println("🔧 Verify the MAC address matches your M5Stack");
  }
  
  Serial.println("═══════════════════════════════════════");
  Serial.println("TEST PATTERNS:");
  Serial.println("🔴 Pattern 1: Red sweep across all strips");
  Serial.println("🟢 Pattern 2: Green pulse on strip 0");
  Serial.println("🔵 Pattern 3: Blue chase on strip 1"); 
  Serial.println("🌈 Pattern 4: Rainbow cycle on strip 2");
  Serial.println("⚫ Pattern 5: Clear all strips");
  Serial.println("═══════════════════════════════════════");
  
  lastPatternChange = millis();
}

void loop() {
  unsigned long now = millis();
  
  // Change test patterns every 3 seconds
  if (now - lastPatternChange > 3000) {
    testPattern = (testPattern + 1) % 6;  // 6 different patterns including delay
    lastPatternChange = now;
    
    switch (testPattern) {
      case 0: // Red sweep across all strips
        Serial.println("🔴 Testing: Red sweep across all strips");
        setStatusLED(255, 0, 0);  // Red status LED
        for (int strip = 0; strip < 3; strip++) {
          for (int pos = 0; pos < 100; pos += 10) {
            sendTestMessage(3, 255, 0, 0, pos, 10, strip);  // Red color
            delay(50);
          }
        }
        break;
        
      case 1: // Green pulse on strip 0 (PM2.5)
        Serial.println("🟢 Testing: Green pulse on PM2.5 strip");
        setStatusLED(0, 255, 0);  // Green status LED
        for (int brightness = 0; brightness < 255; brightness += 25) {
          sendTestMessage(3, 0, brightness, 0, 40, 20, 0);  // Green pulse center
          delay(100);
        }
        break;
        
      case 2: // Blue chase on strip 1 (NO2)
        Serial.println("🔵 Testing: Blue chase on NO₂ strip");
        setStatusLED(0, 0, 255);  // Blue status LED
        for (int pos = 0; pos < 90; pos += 5) {
          sendTestMessage(2, 0, 0, 0, 0, 100, 1);    // Clear strip first
          delay(20);
          sendTestMessage(3, 0, 0, 255, pos, 10, 1); // Blue moving dot
          delay(80);
        }
        break;
        
      case 3: // Rainbow cycle on strip 2 (O3)
        Serial.println("🌈 Testing: Rainbow cycle on O₃ strip");
        setStatusLED(128, 0, 128);  // Purple status LED
        for (int hue = 0; hue < 255; hue += 30) {
          // Simple hue to RGB conversion
          uint8_t r = (hue < 85) ? (255 - hue * 3) : ((hue < 170) ? 0 : (hue - 170) * 3);
          uint8_t g = (hue < 85) ? (hue * 3) : ((hue < 170) ? (255 - (hue - 85) * 3) : 0);
          uint8_t b = (hue < 85) ? 0 : ((hue < 170) ? ((hue - 85) * 3) : (255 - (hue - 170) * 3));
          sendTestMessage(3, r, g, b, 0, 100, 2);  // Full strip rainbow
          delay(200);
        }
        break;
        
      case 4: // Clear all strips
        Serial.println("⚫ Testing: Clear all strips");
        setStatusLED(64, 64, 64);  // White status LED
        for (int strip = 0; strip < 3; strip++) {
          sendTestMessage(2, 0, 0, 0, 0, 100, strip);  // Clear command
          delay(100);
        }
        break;
        
      case 5: // Pause between cycles
        Serial.println("⏸️  Pattern cycle pause...");
        setStatusLED(0, 0, 0);  // LED off
        delay(1000);
        break;
    }
  }
  
  // Update status LED based on last send result
  if (now - lastSendTime > 500) {
    if (espnowReady) {
      if (lastSendSuccess) {
        setStatusLED(0, 64, 0);  // Dim green - connected and working  
      } else {
        // Blink red - connected but having issues
        setStatusLED((now / 250) % 2 ? 255 : 0, 0, 0);
      }
    } else {
      setStatusLED(255, 0, 0);  // Solid red - not initialized
    }
  }
  
  delay(50);  // Small delay to prevent overwhelming
}
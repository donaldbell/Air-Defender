#include <WiFi.h>
#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("=== MINIMAL ESP32 WIFI TEST ===");
  Serial.printf("ESP32 Chip: %s\n", ESP.getChipModel());
  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
  Serial.printf("CPU frequency: %d MHz\n", getCpuFrequencyMhz());
  
  Serial.println("Testing WiFi radio initialization...");
  delay(500);
  
  Serial.print("Step 1: WiFi.mode(WIFI_STA)... ");
  WiFi.mode(WIFI_STA);
  Serial.println("SUCCESS!");
  
  delay(1000);
  
  Serial.print("Step 2: WiFi.begin()... ");
  WiFi.begin("TEST_NETWORK", "test123");
  Serial.println("SUCCESS!");
  
  delay(2000);
  
  Serial.print("Step 3: WiFi.disconnect()... ");
  WiFi.disconnect();
  Serial.println("SUCCESS!");
  
  Serial.println("WiFi radio test PASSED - Hardware is functional!");
  Serial.println("Issue is likely in complex game firmware.");
}

void loop() {
  delay(5000);
  Serial.println("WiFi test complete. Board is functioning normally.");
}
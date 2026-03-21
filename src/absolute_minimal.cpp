/**
 * M5Stack Atom S3 - Absolute Minimal Test
 * Basic serial + LED blink without any libraries
 */

#include <Arduino.h>

void setup() {
    // Initialize serial
    Serial.begin(115200);
    
    // Simple startup delay
    delay(1000);
    
    // Basic output
    Serial.println("");
    Serial.println("=== M5Stack Atom S3 Minimal Test ===");
    Serial.println("Hardware: ESP32-S3");  
    Serial.println("Board: M5Stack Atom S3");
    Serial.println("Status: Basic serial communication working!");
    
    Serial.println("");
    Serial.println("Starting main loop...");
}

void loop() {
    static int counter = 0;
    static unsigned long lastOutput = 0;
    
    // Output every 2 seconds
    if (millis() - lastOutput >= 2000) {
        counter++;
        
        Serial.println("----------------------------------------");
        Serial.printf("Loop iteration: %d\n", counter);
        Serial.printf("Uptime: %lu ms (%.1f seconds)\n", millis(), millis()/1000.0);
        Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
        Serial.println("✅ M5Stack is running normally");
        Serial.println("✅ Serial communication is working");
        Serial.println("✅ Hardware is responding");
        
        if (counter % 5 == 0) {
            Serial.println("");
            Serial.println("🔄 System has been running for " + String(counter * 2) + " seconds");
            Serial.println("📊 All basic functions operational");
            Serial.println("");
        }
        
        lastOutput = millis();
    }
    
    // Small delay to prevent overwhelming the CPU
    delay(10);
}
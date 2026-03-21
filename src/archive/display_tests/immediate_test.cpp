/**
 * M5Stack ESP32-S3 - Immediate Output Test
 */
#include <Arduino.h>

void setup() {
    // Immediate serial without delay
    Serial.begin(115200);
    Serial.print("BOOT");
    Serial.print("BOOT");
    Serial.print("BOOT");
    Serial.println("M5Stack ESP32-S3 ALIVE!");
    Serial.println("Serial working immediately!");
}

void loop() {
    Serial.println("Loop running");
    delay(1000);
}
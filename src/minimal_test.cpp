#include <Arduino.h>

// Use ESP32's built-in neopixelWrite function for accurate timing
void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("QT Py S3 NEOPIXEL TEST - Using built-in neopixelWrite");
  Serial.println("Boot successful - testing very dim NeoPixel");
  
  // Configure NeoPixel pins according to Adafruit QT Py S3 pinout
  pinMode(38, OUTPUT);  // NEOPIXEL_POWER  
  pinMode(39, OUTPUT);  // NEOPIXEL
  
  digitalWrite(38, HIGH);  // Turn on NeoPixel power
  Serial.println("NeoPixel power enabled");
  
  // Start with off state
  neopixelWrite(39, 0, 0, 0);
  delay(1000);
}

void loop() {
  Serial.println("LED RED (very dim)");
  neopixelWrite(39, 8, 0, 0);  // Very dim red
  delay(3000);
  
  Serial.println("LED GREEN (very dim)");
  neopixelWrite(39, 0, 8, 0);  // Very dim green  
  delay(3000);
  
  Serial.println("LED BLUE (very dim)");
  neopixelWrite(39, 0, 0, 8);  // Very dim blue
  delay(3000);
  
  Serial.println("LED OFF");
  neopixelWrite(39, 0, 0, 0);    // Off
  delay(3000);
}
#include <Arduino.h>

// Ultra-Simple QT Py S3 Blink Test
// Just blinks the built-in NeoPixel to verify hardware works

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("=== QT PY S3 SIMPLE BLINK TEST ===");
  
  // Initialize built-in NeoPixel pins
  pinMode(38, OUTPUT);  // NEOPIXEL_POWER
  pinMode(39, OUTPUT);  // NEOPIXEL_PIN
  
  digitalWrite(38, HIGH);  // Turn on NeoPixel power
  Serial.println("NeoPixel power enabled - starting loop");
}

void loop() {
  Serial.println("Setting NeoPixel RED");
  
  // Simple bit-bang red color (R=255, G=0, B=0 in GRB format = 0x00FF00)
  digitalWrite(39, LOW);
  delayMicroseconds(50);  // Reset pulse
  
  // Send 24 bits for red color
  for (int i = 23; i >= 0; i--) {
    digitalWrite(39, HIGH);
    if (0x00FF00 & (1 << i)) {
      delayMicroseconds(1);     // 1 bit
      digitalWrite(39, LOW);
      delayMicroseconds(1);
    } else {
      delayMicroseconds(1);     // 0 bit
      digitalWrite(39, LOW);
      delayMicroseconds(2);
    }
  }
  digitalWrite(39, LOW);
  
  delay(1000);
  
  Serial.println("Setting NeoPixel OFF");
  
  // Send all zeros (off)
  digitalWrite(39, LOW);
  delayMicroseconds(50);
  for (int i = 0; i < 24; i++) {
    digitalWrite(39, HIGH);
    delayMicroseconds(1);
    digitalWrite(39, LOW);
    delayMicroseconds(2);
  }
  digitalWrite(39, LOW);
  
  delay(1000);
}
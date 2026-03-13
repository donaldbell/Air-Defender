#include <Arduino.h>
#include <FastLED.h>

#define LED_PIN 23
#define NUM_LEDS 10

CRGB leds[NUM_LEDS];

void setup() {
  Serial.begin(115200);
  Serial.println("ESP32 Recovery Test");
  
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(50);
}

void loop() {
  Serial.println("Loop running...");
  
  // Simple red pattern
  for(int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CRGB::Red;
  }
  FastLED.show();
  delay(500);
  
  // Turn off
  FastLED.clear();
  FastLED.show();
  delay(500);
}
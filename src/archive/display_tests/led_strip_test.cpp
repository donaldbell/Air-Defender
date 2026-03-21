/**
 * M5Stack Atom S3 - LED Strip Hardware Test
 * Tests all 3 LED strips + onboard LED with various patterns
 * Use this to verify hardware connections and LED functionality
 */

#include <FastLED.h>

// LED Configuration for M5Stack Atom S3  
#define LED_PIN_ONBOARD 35       // GPIO35 - Onboard RGB LED (status)
#define LED_PIN_STRIP1  8        // GPIO8  - Strip 1 (was PM2.5, blue theme) 
#define LED_PIN_STRIP2  6        // GPIO6  - Strip 2 (was NO2, red theme)
#define LED_PIN_STRIP3  7        // GPIO7  - Strip 3 (was O3, green theme)

#define NUM_ONBOARD_LEDS 1       
#define LEDS_PER_STRIP   100     // 100 LEDs per strip
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB

// LED arrays
CRGB ledsOnboard[NUM_ONBOARD_LEDS];     
CRGB ledsStrip1[LEDS_PER_STRIP];        // Strip 1 (GPIO 8)
CRGB ledsStrip2[LEDS_PER_STRIP];        // Strip 2 (GPIO 6)  
CRGB ledsStrip3[LEDS_PER_STRIP];        // Strip 3 (GPIO 7)

uint8_t brightness = 80;  // Moderate brightness
int testPhase = 0;
unsigned long lastUpdate = 0;

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n=== M5Stack Atom S3 - LED Strip Hardware Test ===");
    Serial.println("Testing 3 LED strips + onboard LED");
    Serial.printf("Strip configuration: Strip1(GPIO%d), Strip2(GPIO%d), Strip3(GPIO%d)\n", 
                  LED_PIN_STRIP1, LED_PIN_STRIP2, LED_PIN_STRIP3);
    Serial.printf("LEDs per strip: %d, Brightness: %d%%\n", LEDS_PER_STRIP, brightness);
    
    // Initialize all LED controllers
    Serial.println("🔧 Initializing LED controllers...");
    FastLED.addLeds<LED_TYPE, LED_PIN_ONBOARD, COLOR_ORDER>(ledsOnboard, NUM_ONBOARD_LEDS);
    FastLED.addLeds<LED_TYPE, LED_PIN_STRIP1, COLOR_ORDER>(ledsStrip1, LEDS_PER_STRIP);
    FastLED.addLeds<LED_TYPE, LED_PIN_STRIP2, COLOR_ORDER>(ledsStrip2, LEDS_PER_STRIP);
    FastLED.addLeds<LED_TYPE, LED_PIN_STRIP3, COLOR_ORDER>(ledsStrip3, LEDS_PER_STRIP);
    
    FastLED.setBrightness(brightness);
    FastLED.clear();
    FastLED.show();
    
    Serial.println("✅ LED controllers initialized");
    Serial.println("🧪 Starting hardware test sequence...");
    
    // Initial status - green onboard LED
    ledsOnboard[0] = CRGB::Green;
    FastLED.show();
    delay(1000);
}

void clearAllStrips() {
    fill_solid(ledsStrip1, LEDS_PER_STRIP, CRGB::Black);
    fill_solid(ledsStrip2, LEDS_PER_STRIP, CRGB::Black);
    fill_solid(ledsStrip3, LEDS_PER_STRIP, CRGB::Black);
    ledsOnboard[0] = CRGB::Black;
    FastLED.show();
}

void testOnboardLED() {
    Serial.println("🔵 Test 1: Onboard LED test");
    
    // Red
    ledsOnboard[0] = CRGB::Red;
    FastLED.show();
    delay(500);
    
    // Green  
    ledsOnboard[0] = CRGB::Green;
    FastLED.show();
    delay(500);
    
    // Blue
    ledsOnboard[0] = CRGB::Blue;
    FastLED.show();
    delay(500);
    
    // White
    ledsOnboard[0] = CRGB::White;
    FastLED.show();
    delay(500);
    
    // Off
    ledsOnboard[0] = CRGB::Black;
    FastLED.show();
    delay(300);
    
    Serial.println("✅ Onboard LED test complete");
}

void testStrip(CRGB* strip, const char* name, CRGB color) {
    Serial.printf("🎨 Testing %s with color (%d,%d,%d)\n", name, color.r, color.g, color.b);
    
    // Light up all LEDs at once
    fill_solid(strip, LEDS_PER_STRIP, color);
    FastLED.show();
    delay(1000);
    
    // Chase pattern
    fill_solid(strip, LEDS_PER_STRIP, CRGB::Black);
    for (int i = 0; i < LEDS_PER_STRIP; i++) {
        strip[i] = color;
        FastLED.show();
        delay(50);
    }
    
    // Reverse chase off
    for (int i = LEDS_PER_STRIP - 1; i >= 0; i--) {
        strip[i] = CRGB::Black;
        FastLED.show();
        delay(30);
    }
    
    Serial.printf("✅ %s test complete\n", name);
    delay(500);
}

void testAllStripsIndividually() {
    Serial.println("\n🧪 Test 2: Individual strip tests");
    
    // Test Strip 1 (GPIO 8) - Blue
    ledsOnboard[0] = CRGB::Blue;
    FastLED.show();
    testStrip(ledsStrip1, "Strip 1 (GPIO 8)", CRGB::Blue);
    
    // Test Strip 2 (GPIO 6) - Red  
    ledsOnboard[0] = CRGB::Red;
    FastLED.show();
    testStrip(ledsStrip2, "Strip 2 (GPIO 6)", CRGB::Red);
    
    // Test Strip 3 (GPIO 7) - Green
    ledsOnboard[0] = CRGB::Green; 
    FastLED.show();
    testStrip(ledsStrip3, "Strip 3 (GPIO 7)", CRGB::Green);
    
    clearAllStrips();
    Serial.println("✅ Individual strip tests complete");
}

void testAllStripsTogether() {
    Serial.println("\n🌈 Test 3: All strips together");
    
    ledsOnboard[0] = CRGB::White;
    FastLED.show();
    
    // All strips same color
    Serial.println("All strips: White");
    fill_solid(ledsStrip1, LEDS_PER_STRIP, CRGB::White);
    fill_solid(ledsStrip2, LEDS_PER_STRIP, CRGB::White);
    fill_solid(ledsStrip3, LEDS_PER_STRIP, CRGB::White);
    FastLED.show();
    delay(2000);
    
    // All strips different colors
    Serial.println("All strips: Blue, Red, Green");
    fill_solid(ledsStrip1, LEDS_PER_STRIP, CRGB::Blue);
    fill_solid(ledsStrip2, LEDS_PER_STRIP, CRGB::Red);
    fill_solid(ledsStrip3, LEDS_PER_STRIP, CRGB::Green);
    FastLED.show();
    delay(2000);
    
    // Wave pattern across all strips
    Serial.println("Wave pattern across all strips");
    for (int wave = 0; wave < 3; wave++) {
        for (int i = 0; i < LEDS_PER_STRIP; i++) {
            clearAllStrips();
            
            // Wave on each strip with offset
            if (i < LEDS_PER_STRIP) ledsStrip1[i] = CRGB::Blue;
            if (i >= 3 && i - 3 < LEDS_PER_STRIP) ledsStrip2[i - 3] = CRGB::Red;  
            if (i >= 6 && i - 6 < LEDS_PER_STRIP) ledsStrip3[i - 6] = CRGB::Green;
            
            FastLED.show();
            delay(100);
        }
    }
    
    clearAllStrips();
    Serial.println("✅ All strips together test complete");
}

void testRainbowPattern() {
    Serial.println("\n🌈 Test 4: Rainbow pattern");
    
    ledsOnboard[0] = CRGB::Purple;
    FastLED.show();
    
    for (int hue = 0; hue < 256; hue += 2) {
        for (int i = 0; i < LEDS_PER_STRIP; i++) {
            int pixelHue = (hue + (i * 256 / LEDS_PER_STRIP)) % 256;
            
            ledsStrip1[i] = CHSV(pixelHue, 255, 255);
            ledsStrip2[i] = CHSV((pixelHue + 85) % 256, 255, 255);  // Offset colors
            ledsStrip3[i] = CHSV((pixelHue + 170) % 256, 255, 255); // Offset colors
        }
        FastLED.show();
        delay(20);
    }
    
    clearAllStrips();
    Serial.println("✅ Rainbow pattern test complete");
}

void testBrightnessLevels() {
    Serial.println("\n💡 Test 5: Brightness levels");
    
    // Test different brightness levels
    int brightnessLevels[] = {20, 50, 100, 150, 200, 255};
    
    fill_solid(ledsStrip1, LEDS_PER_STRIP, CRGB::Blue);
    fill_solid(ledsStrip2, LEDS_PER_STRIP, CRGB::Red);
    fill_solid(ledsStrip3, LEDS_PER_STRIP, CRGB::Green);
    
    for (int i = 0; i < 6; i++) {
        Serial.printf("Brightness: %d/255\n", brightnessLevels[i]);
        FastLED.setBrightness(brightnessLevels[i]);
        FastLED.show();
        delay(1000);
    }
    
    // Restore original brightness
    FastLED.setBrightness(brightness);
    clearAllStrips();
    Serial.println("✅ Brightness test complete");
}

void runDiagnostics() {
    Serial.println("\n🔍 Hardware Diagnostics:");
    Serial.printf("✓ FastLED version: %d.%d.%d\n", FASTLED_VERSION >> 16, (FASTLED_VERSION >> 8) & 0xFF, FASTLED_VERSION & 0xFF);
    Serial.printf("✓ ESP32 chip: %s\n", ESP.getChipModel());
    Serial.printf("✓ Free heap: %d bytes\n", ESP.getFreeHeap());
    Serial.printf("✓ CPU frequency: %d MHz\n", ESP.getCpuFreqMHz());
    
    // Test timing
    unsigned long start = micros();
    FastLED.show();
    unsigned long duration = micros() - start;
    Serial.printf("✓ FastLED.show() timing: %lu microseconds\n", duration);
    
    Serial.println("\n📋 Connection checklist:");
    Serial.printf("   Strip 1: GPIO %d (was pin 5, moved to pin 8 for stability)\n", LED_PIN_STRIP1);
    Serial.printf("   Strip 2: GPIO %d\n", LED_PIN_STRIP2);  
    Serial.printf("   Strip 3: GPIO %d\n", LED_PIN_STRIP3);
    Serial.println("   Power: Ensure adequate power supply for all strips");
    Serial.println("   Ground: Common ground between M5Stack and LED strips");
}

void loop() {
    static int currentTest = 0;
    static unsigned long lastTestTime = 0;
    
    // Run complete test sequence every 30 seconds
    if (millis() - lastTestTime > 30000) {
        lastTestTime = millis();
        currentTest = 0;
    }
    
    // Run each test once per cycle
    if (currentTest == 0) {
        Serial.println("\n🚀 Starting LED Hardware Test Cycle");
        testOnboardLED();
        currentTest++;
        delay(1000);
    }
    else if (currentTest == 1) {
        testAllStripsIndividually();
        currentTest++;
        delay(1000);
    }
    else if (currentTest == 2) {
        testAllStripsTogether();
        currentTest++;
        delay(1000);
    }
    else if (currentTest == 3) {
        testRainbowPattern();
        currentTest++;
        delay(1000);
    }
    else if (currentTest == 4) {
        testBrightnessLevels();
        currentTest++;
        delay(1000);
    }
    else if (currentTest == 5) {
        runDiagnostics();
        currentTest++;
        
        Serial.println("\n🏁 Test cycle complete!");
        Serial.println("Waiting 30 seconds before next cycle...");
        Serial.println("Watch for any strips that don't light up or show incorrect colors.");
        
        // Success indicator
        ledsOnboard[0] = CRGB::Green;
        FastLED.show();
    }
    
    delay(100);
}
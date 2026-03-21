/**
 * M5Stack Atom S3 - Pin Test Version
 * Test one LED strip at a time to find working pins
 */

#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <FastLED.h>

// LED Configuration for M5Stack Atom S3 Lite
#define LED_PIN_ONBOARD 35       // GPIO35 - Onboard RGB LED only
#define NUM_ONBOARD_LEDS 1       // Single onboard status LED
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB

// LED arrays - onboard only for testing
CRGB ledsOnboard[NUM_ONBOARD_LEDS];     // Status indicator

// Message structure matching console unit
typedef struct {
    uint8_t command;
    uint8_t red;
    uint8_t green; 
    uint8_t blue;
    uint8_t position;
    float brightness;
    uint16_t duration;
    uint32_t timestamp;
} DisplayMessage;

unsigned long lastReceivedTime = 0;
const unsigned long CONNECTION_TIMEOUT = 5000; // 5 seconds

void onDataReceive(const uint8_t *mac, const uint8_t *data, int len) {
    Serial.printf("📡 ESP-NOW message received from: %02X:%02X:%02X:%02X:%02X:%02X\n", 
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    if (len != sizeof(DisplayMessage)) {
        Serial.printf("❌ Invalid message size: %d bytes (expected %d)\n", len, sizeof(DisplayMessage));
        return;
    }
    
    DisplayMessage* message = (DisplayMessage*)data;
    lastReceivedTime = millis();
    
    Serial.printf("🎮 Command: %d, RGB: (%d,%d,%d), Brightness: %.1f\n", 
                  message->command, message->red, message->green, message->blue, message->brightness);
    
    // Test both onboard LED with received data
    CRGB color = CRGB(message->red, message->green, message->blue);
    
    // Onboard LED response only
    ledsOnboard[0] = color;
    
    FastLED.show();
    Serial.printf("✅ Updated onboard LED\n");
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n🌍 M5Stack Atom S3 - Onboard LED Only Test");
    Serial.println("Testing with no external LED strips connected");
    
    // Initialize LED system - onboard only
    FastLED.addLeds<LED_TYPE, LED_PIN_ONBOARD, COLOR_ORDER>(ledsOnboard, NUM_ONBOARD_LEDS);
    FastLED.setBrightness(100);
    FastLED.clear();
    FastLED.show();
    
    Serial.println("🔧 Onboard LED system initialized successfully!");
    
    // Test sequence on onboard LED only
    Serial.println("🔧 Running onboard LED test sequence...");
    
    const CRGB testColors[] = {CRGB::Red, CRGB::Green, CRGB::Blue, CRGB::White};
    for (int i = 0; i < 4; i++) {
        ledsOnboard[0] = testColors[i];
        FastLED.show();
        Serial.printf("Onboard LED test color %d: %s\n", i+1, 
                      i==0 ? "Red" : i==1 ? "Green" : i==2 ? "Blue" : "White");
        delay(500);
        
        // Clear
        ledsOnboard[0] = CRGB::Black;
        FastLED.show();
        delay(200);
    }
    
    Serial.println("🔧 Onboard LED test completed successfully!");
    
    // Initialize WiFi
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    
    Serial.printf("📍 MAC Address: %s\n", WiFi.macAddress().c_str());
    
    // Set WiFi channel to match console
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    
    // Initialize ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("❌ ESP-NOW init failed!");
        // Error flash
        for (int i = 0; i < 3; i++) {
            ledsOnboard[0] = CRGB::Red;
            FastLED.show();
            delay(300);
            ledsOnboard[0] = CRGB::Black;
            FastLED.show();
            delay(300);
        }
        return;
    }
    
    esp_now_register_recv_cb(onDataReceive);
    
    Serial.println("✅ ESP-NOW initialized successfully!");
    Serial.printf("🎮 Ready for console communication (onboard LED only)!\n");
    
    // Ready indicator
    ledsOnboard[0] = CRGB::Green;
    FastLED.show();
    delay(1000);
    ledsOnboard[0] = CRGB::Black;
    FastLED.show();
}

void loop() {
    // Heartbeat indicator
    static unsigned long lastHeartbeat = 0;
    if (millis() - lastHeartbeat > 5000) {
        Serial.printf("💓 Heartbeat - Onboard LED test running, Free heap: %d bytes\n", 
                      ESP.getFreeHeap());
        
        // Brief blue flash on onboard LED
        ledsOnboard[0] = CRGB::Blue;
        FastLED.show();
        delay(50);
        ledsOnboard[0] = CRGB::Black;
        FastLED.show();
        
        lastHeartbeat = millis();
    }
    
    delay(10);
}
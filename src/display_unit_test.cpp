/**
 * M5Stack Atom S3 - Basic LED Test
 * Simplified version to test ESP-NOW without large LED arrays
 */

#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <FastLED.h>

// LED Configuration for M5Stack Atom S3 Lite
#define LED_PIN_ONBOARD 35       // GPIO35 - Onboard RGB LED
#define NUM_ONBOARD_LEDS 1       // Single onboard status LED
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB

// Only onboard LED for testing
CRGB ledsOnboard[NUM_ONBOARD_LEDS];

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
    
    // Simple onboard LED response
    switch(message->command) {
        case 1: // Status
            ledsOnboard[0] = CRGB(message->red, message->green, message->blue);
            FastLED.show();
            break;
        default:
            ledsOnboard[0] = CRGB::Purple; // Unknown command
            FastLED.show();
            break;
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n🌍 M5Stack Atom S3 - Basic Test");
    Serial.println("Testing ESP-NOW communication with onboard LED only");
    
    // Initialize only onboard LED
    FastLED.addLeds<LED_TYPE, LED_PIN_ONBOARD, COLOR_ORDER>(ledsOnboard, NUM_ONBOARD_LEDS);
    FastLED.setBrightness(100);
    FastLED.clear();
    FastLED.show();
    
    Serial.println("🔧 Onboard LED initialized");
    
    // LED test sequence
    const CRGB testColors[] = {CRGB::Red, CRGB::Green, CRGB::Blue, CRGB::White};
    for (int i = 0; i < 4; i++) {
        ledsOnboard[0] = testColors[i];
        FastLED.show();
        delay(500);
    }
    ledsOnboard[0] = CRGB::Black;
    FastLED.show();
    
    // Initialize WiFi
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    
    Serial.printf("📍 MAC Address: %s\n", WiFi.macAddress().c_str());
    
    // Set WiFi channel to match console
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    
    // Initialize ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("❌ ESP-NOW init failed!");
        return;
    }
    
    esp_now_register_recv_cb(onDataReceive);
    
    Serial.println("✅ ESP-NOW initialized successfully!");
    Serial.println("🎮 Ready for console communication!");
    
    // Ready indicator
    ledsOnboard[0] = CRGB::Green;
    FastLED.show();
    delay(1000);
    ledsOnboard[0] = CRGB::Black;
    FastLED.show();
}

void loop() {
    // Connection timeout check
    if (millis() - lastReceivedTime > CONNECTION_TIMEOUT && lastReceivedTime > 0) {
        // Timeout indicator - slow red pulse
        static unsigned long lastPulse = 0;
        static bool pulseState = false;
        
        if (millis() - lastPulse > 1000) {
            ledsOnboard[0] = pulseState ? CRGB::Red : CRGB::Black;
            FastLED.show();
            pulseState = !pulseState;
            lastPulse = millis();
        }
    }
    
    delay(10);
}
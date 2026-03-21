/**
 * M5Stack Atom S3 - Onboard LED + ESP-NOW Test
 * Building on successful basic test, adding LED and wireless communication
 */

#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <FastLED.h>

// LED Configuration for M5Stack Atom S3
#define LED_PIN_ONBOARD 35       // GPIO35 - Onboard RGB LED
#define NUM_ONBOARD_LEDS 1       
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB

// LED array
CRGB ledsOnboard[NUM_ONBOARD_LEDS];

// Message structure matching console board (8 bytes)
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

unsigned long lastReceivedTime = 0;

// ESP-NOW receive callback
void onDataReceive(const uint8_t *mac, const uint8_t *data, int len) {
    Serial.printf("📡 ESP-NOW received from: %02X:%02X:%02X:%02X:%02X:%02X\n", 
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    if (len != sizeof(DisplayMessage)) {
        Serial.printf("❌ Invalid size: %d bytes (expected %d)\n", len, sizeof(DisplayMessage));
        return;
    }
    
    DisplayMessage* message = (DisplayMessage*)data;
    lastReceivedTime = millis();
    
    Serial.printf("🎮 Command: %d, RGB: (%d,%d,%d), Pos: %d, Strip: %d, State: %d\n", 
                  message->command, message->red, message->green, message->blue, 
                  message->position, message->stripId, message->gameState);
    
    // Update onboard LED based on command
    CRGB color = CRGB(message->red, message->green, message->blue);
    
    switch(message->command) {
        case 1: // Test pattern
            ledsOnboard[0] = CRGB::White;
            Serial.println("✅ Test pattern - LED set to white");
            break;
        case 2: // Clear
            ledsOnboard[0] = CRGB::Black;
            Serial.println("✅ Clear - LED turned off");
            break;
        case 3: // Game data
        case 4: // Environmental data
            ledsOnboard[0] = color;
            Serial.printf("✅ Game/Environmental data - LED set to RGB(%d,%d,%d)\n", 
                         message->red, message->green, message->blue);
            break;
        default:
            ledsOnboard[0] = CRGB::Purple;
            Serial.printf("❓ Unknown command %d - LED set to purple\n", message->command);
            break;
    }
    
    FastLED.show();
}

void setup() {
    Serial.begin(115200);
    delay(500);
    
    Serial.println("\n=== M5Stack Atom S3 - LED + ESP-NOW Test ===");
    Serial.println("Basic functionality confirmed, adding LED and wireless...");
    
    // Initialize onboard LED
    Serial.println("🔧 Initializing onboard LED...");
    FastLED.addLeds<LED_TYPE, LED_PIN_ONBOARD, COLOR_ORDER>(ledsOnboard, NUM_ONBOARD_LEDS);
    FastLED.setBrightness(100);
    FastLED.clear();
    FastLED.show();
    Serial.println("✅ LED initialized");
    
    // LED test sequence
    Serial.println("🔧 Testing LED colors...");
    const CRGB testColors[] = {CRGB::Red, CRGB::Green, CRGB::Blue, CRGB::White};
    const char* colorNames[] = {"Red", "Green", "Blue", "White"};
    
    for (int i = 0; i < 4; i++) {
        ledsOnboard[0] = testColors[i];
        FastLED.show();
        Serial.printf("LED Test: %s\n", colorNames[i]);
        delay(300);
        
        ledsOnboard[0] = CRGB::Black;
        FastLED.show();
        delay(100);
    }
    Serial.println("✅ LED test completed");
    
    // Initialize WiFi for ESP-NOW
    Serial.println("🔧 Initializing WiFi for ESP-NOW...");
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    
    Serial.printf("📍 MAC Address: %s\n", WiFi.macAddress().c_str());
    
    // Set WiFi channel to match console
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    Serial.println("📡 Set to WiFi channel 1");
    
    // Initialize ESP-NOW
    Serial.println("📡 Initializing ESP-NOW...");
    if (esp_now_init() != ESP_OK) {
        Serial.println("❌ ESP-NOW init failed!");
        // Error pattern - red flashing
        for (int i = 0; i < 5; i++) {
            ledsOnboard[0] = CRGB::Red;
            FastLED.show();
            delay(200);
            ledsOnboard[0] = CRGB::Black;
            FastLED.show();
            delay(200);
        }
        return;
    }
    
    // Register receive callback
    esp_now_register_recv_cb(onDataReceive);
    
    Serial.println("✅ ESP-NOW initialized successfully!");
    Serial.println("🎮 Ready for console communication!");
    
    // Ready indicator - green pulse
    for (int brightness = 0; brightness < 255; brightness += 10) {
        ledsOnboard[0] = CRGB(0, brightness, 0);
        FastLED.show();
        delay(20);
    }
    delay(500);
    for (int brightness = 255; brightness >= 0; brightness -= 10) {
        ledsOnboard[0] = CRGB(0, brightness, 0);
        FastLED.show();
        delay(20);
    }
    
    Serial.println("🚀 Setup complete - M5Stack ready!");
}

void loop() {
    static unsigned long lastHeartbeat = 0;
    static int loopCounter = 0;
    
    loopCounter++;
    
    // Heartbeat every 5 seconds
    if (millis() - lastHeartbeat >= 5000) {
        Serial.printf("💓 Heartbeat %d - Free heap: %d bytes\n", 
                      loopCounter / 500, ESP.getFreeHeap());
        
        // Brief blue flash
        ledsOnboard[0] = CRGB::Blue;
        FastLED.show();
        delay(50);
        ledsOnboard[0] = CRGB::Black;
        FastLED.show();
        
        lastHeartbeat = millis();
    }
    
    // Connection timeout check
    if (lastReceivedTime > 0 && (millis() - lastReceivedTime > 10000)) {
        // No messages for 10 seconds - slow red pulse
        static unsigned long lastPulse = 0;
        static bool pulseState = false;
        
        if (millis() - lastPulse > 2000) {
            ledsOnboard[0] = pulseState ? CRGB::Red : CRGB::Black;
            FastLED.show();
            pulseState = !pulseState;
            lastPulse = millis();
        }
    }
    
    delay(10);
}
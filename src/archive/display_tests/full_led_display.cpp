/**
 * M5Stack Atom S3 - Full LED Strip Environmental Display
 * Drives 3 external LED strips for PM2.5, NO2, and O3 data
 * Plus onboard LED for status indication
 */

#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <FastLED.h>

// LED Configuration for M5Stack Atom S3
#define LED_PIN_ONBOARD 35       // GPIO35 - Onboard RGB LED (status)
#define LED_PIN_PM25    8        // GPIO8  - PM2.5 pollution strip (blue theme) - CHANGED from GPIO5
#define LED_PIN_NO2     6        // GPIO6  - NO2 pollution strip (red theme)  
#define LED_PIN_O3      7        // GPIO7  - O3 pollution strip (green theme)

#define NUM_ONBOARD_LEDS 1       
#define LEDS_PER_STRIP   100     // 100 LEDs per environmental strip
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB

// LED arrays
CRGB ledsOnboard[NUM_ONBOARD_LEDS];     // Status indicator
CRGB ledsPM25[LEDS_PER_STRIP];          // PM2.5 environmental display
CRGB ledsNO2[LEDS_PER_STRIP];           // NO2 environmental display  
CRGB ledsO3[LEDS_PER_STRIP];            // O3 environmental display

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
uint8_t currentBrightness = 80;  // Moderate brightness for strips

// Update specific LED strip based on message
void updateEnvironmentalStrip(DisplayMessage* message) {
    CRGB color = CRGB(message->red, message->green, message->blue);
    
    // Select target strip based on stripId
    CRGB* targetStrip = nullptr;
    const char* stripName = "Unknown";
    
    switch(message->stripId) {
        case 0: // PM2.5
            targetStrip = ledsPM25;
            stripName = "PM2.5";
            break;
        case 1: // NO2
            targetStrip = ledsNO2;
            stripName = "NO2";
            break;
        case 2: // O3
            targetStrip = ledsO3;
            stripName = "O3";
            break;
        default:
            Serial.printf("❌ Invalid strip ID: %d\n", message->stripId);
            return;
    }
    
    // Apply environmental data to strip
    if (message->command == 2) {
        // Clear command
        fill_solid(targetStrip, LEDS_PER_STRIP, CRGB::Black);
        Serial.printf("🧹 Cleared %s strip\n", stripName);
    }
    else if (message->command == 3 || message->command == 4) {
        // Game data or environmental data
        uint8_t pos = message->position;
        uint8_t len = message->length;
        
        // Ensure bounds checking
        if (pos < LEDS_PER_STRIP) {
            uint8_t endPos = min((int)(pos + len), LEDS_PER_STRIP);
            
            // Light up LEDs from position to endPos
            for (uint8_t i = pos; i < endPos; i++) {
                targetStrip[i] = color;
            }
            
            Serial.printf("🎨 %s strip: pos %d-%d, RGB(%d,%d,%d)\n", 
                         stripName, pos, endPos-1, message->red, message->green, message->blue);
        }
    }
}

// ESP-NOW receive callback
void onDataReceive(const uint8_t *mac, const uint8_t *data, int len) {
    Serial.printf("📡 ESP-NOW from: %02X:%02X:%02X:%02X:%02X:%02X\n", 
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    if (len != sizeof(DisplayMessage)) {
        Serial.printf("❌ Invalid size: %d bytes (expected %d)\n", len, sizeof(DisplayMessage));
        return;
    }
    
    DisplayMessage* message = (DisplayMessage*)data;
    lastReceivedTime = millis();
    
    Serial.printf("🎮 Cmd: %d, RGB: (%d,%d,%d), Pos: %d, Len: %d, Strip: %d, State: %d\n", 
                  message->command, message->red, message->green, message->blue, 
                  message->position, message->length, message->stripId, message->gameState);
    
    // Update onboard LED for status indication
    switch(message->command) {
        case 1: // Test pattern
            ledsOnboard[0] = CRGB::White;
            break;
        case 2: // Clear
            ledsOnboard[0] = CRGB::Black;
            break;
        case 3: // Game data
            ledsOnboard[0] = CRGB::Yellow; // Game activity
            break;
        case 4: // Environmental data
            ledsOnboard[0] = CRGB::Green;  // Environmental monitoring
            break;
        default:
            ledsOnboard[0] = CRGB::Purple;
            break;
    }
    
    // Update appropriate environmental strip
    updateEnvironmentalStrip(message);
    
    // Show all changes
    FastLED.show();
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n=== M5Stack Atom S3 - Environmental LED Display ===");
    Serial.println("Onboard LED + 3 Environmental LED Strips");
    Serial.printf("Strip configuration: PM2.5(pin %d), NO2(pin %d), O3(pin %d)\n", 
                  LED_PIN_PM25, LED_PIN_NO2, LED_PIN_O3);
    Serial.printf("LEDs per strip: %d\n", LEDS_PER_STRIP);
    
    // Initialize all LED systems
    Serial.println("🔧 Initializing LED systems...");
    FastLED.addLeds<LED_TYPE, LED_PIN_ONBOARD, COLOR_ORDER>(ledsOnboard, NUM_ONBOARD_LEDS);
    FastLED.addLeds<LED_TYPE, LED_PIN_PM25, COLOR_ORDER>(ledsPM25, LEDS_PER_STRIP);
    FastLED.addLeds<LED_TYPE, LED_PIN_NO2, COLOR_ORDER>(ledsNO2, LEDS_PER_STRIP);
    FastLED.addLeds<LED_TYPE, LED_PIN_O3, COLOR_ORDER>(ledsO3, LEDS_PER_STRIP);
    FastLED.setBrightness(currentBrightness);
    FastLED.clear();
    FastLED.show();
    Serial.println("✅ All LED systems initialized");
    
    // LED test sequence - test each strip
    Serial.println("🔧 Testing all LED strips...");
    
    // Test PM2.5 strip (blue theme)
    Serial.println("Testing PM2.5 strip (blue)...");
    fill_solid(ledsPM25, LEDS_PER_STRIP, CRGB::Blue);
    ledsOnboard[0] = CRGB::Blue;
    FastLED.show();
    delay(800);
    fill_solid(ledsPM25, LEDS_PER_STRIP, CRGB::Black);
    
    // Test NO2 strip (red theme)
    Serial.println("Testing NO2 strip (red)...");
    fill_solid(ledsNO2, LEDS_PER_STRIP, CRGB::Red);
    ledsOnboard[0] = CRGB::Red;
    FastLED.show();
    delay(800);
    fill_solid(ledsNO2, LEDS_PER_STRIP, CRGB::Black);
    
    // Test O3 strip (green theme)
    Serial.println("Testing O3 strip (green)...");
    fill_solid(ledsO3, LEDS_PER_STRIP, CRGB::Green);
    ledsOnboard[0] = CRGB::Green;
    FastLED.show();
    delay(800);
    fill_solid(ledsO3, LEDS_PER_STRIP, CRGB::Black);
    
    // All strips together
    Serial.println("Testing all strips together...");
    fill_solid(ledsPM25, LEDS_PER_STRIP, CRGB::Blue);
    fill_solid(ledsNO2, LEDS_PER_STRIP, CRGB::Red);
    fill_solid(ledsO3, LEDS_PER_STRIP, CRGB::Green);
    ledsOnboard[0] = CRGB::White;
    FastLED.show();
    delay(1000);
    
    // Clear all
    FastLED.clear();
    FastLED.show();
    Serial.println("✅ LED test sequence completed");
    
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
        // Error pattern - red flashing on all strips
        for (int i = 0; i < 3; i++) {
            fill_solid(ledsPM25, LEDS_PER_STRIP, CRGB::Red);
            fill_solid(ledsNO2, LEDS_PER_STRIP, CRGB::Red);
            fill_solid(ledsO3, LEDS_PER_STRIP, CRGB::Red);
            ledsOnboard[0] = CRGB::Red;
            FastLED.show();
            delay(500);
            FastLED.clear();
            FastLED.show();
            delay(500);
        }
        return;
    }
    
    // Register receive callback
    esp_now_register_recv_cb(onDataReceive);
    
    Serial.println("✅ ESP-NOW initialized successfully!");
    Serial.println("🎮 Ready for environmental data from console!");
    
    // Ready indicator - green wave across all strips
    for (int pos = 0; pos < LEDS_PER_STRIP; pos += 3) {
        if (pos < LEDS_PER_STRIP) ledsPM25[pos] = CRGB::Green;
        if (pos < LEDS_PER_STRIP) ledsNO2[pos] = CRGB::Green;
        if (pos < LEDS_PER_STRIP) ledsO3[pos] = CRGB::Green;
        FastLED.show();
        delay(50);
    }
    FastLED.clear();
    ledsOnboard[0] = CRGB::Green;
    FastLED.show();
    delay(1000);
    ledsOnboard[0] = CRGB::Black;
    FastLED.show();
    
    Serial.println("🚀 Environmental display system ready!");
}

void loop() {
    static unsigned long lastHeartbeat = 0;
    static int heartbeatCount = 0;
    
    // Reduced heartbeat frequency and removed LED flash to prevent flicker
    if (millis() - lastHeartbeat >= 30000) {  // Increased from 10s to 30s
        heartbeatCount++;
        Serial.printf("💓 Heartbeat %d - Free heap: %d bytes, Last ESP-NOW: %lu ms ago\n", 
                      heartbeatCount, ESP.getFreeHeap(), 
                      lastReceivedTime > 0 ? millis() - lastReceivedTime : 0);
        
        // LED flash removed to prevent interference with game display
        
        lastHeartbeat = millis();
    }
    
    // Connection timeout check - slow red pulse if no data
    if (lastReceivedTime > 0 && (millis() - lastReceivedTime > 15000)) {
        static unsigned long lastTimeoutPulse = 0;
        static bool pulseState = false;
        
        if (millis() - lastTimeoutPulse > 2000) {
            ledsOnboard[0] = pulseState ? CRGB::Red : CRGB::Black;
            FastLED.show();
            pulseState = !pulseState;
            lastTimeoutPulse = millis();
        }
    }
    
    delay(50);
}
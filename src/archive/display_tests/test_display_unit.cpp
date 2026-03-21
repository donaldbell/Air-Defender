/**
 * Display Unit ESP-NOW Test (M5Stack Atom S3 Lite)
 * Tests ESP-NOW reception and onboard LED control
 * Upload this to your M5Stack Atom S3 Lite for wireless testing
 */

#include <esp_now.h>
#include <WiFi.h>
#include <FastLED.h>

// LED Configuration for M5Stack Atom S3 Lite - Onboard LED
#define LED_PIN 35       // GPIO35 - Onboard RGB LED on Atom S3 Lite  
#define NUM_LEDS 1       // Single onboard LED for testing
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB

CRGB leds[NUM_LEDS];

// Message structure for ESP-NOW communication
typedef struct {
    uint8_t command;     // 1=test pattern, 2=clear, 3=game data
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t position;
    uint8_t length;
} DisplayMessage;

DisplayMessage incomingMessage;

// Function declarations
void onDataReceive(const uint8_t *mac_addr, const uint8_t *data, int len);
void executeDisplayCommand();
void testPattern(uint8_t r, uint8_t g, uint8_t b);
void setLEDSegment(uint8_t start, uint8_t length, CRGB color);

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== Display Unit Compatibility Test ===");
    
    // Initialize FastLED for onboard LED
    FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
    FastLED.setBrightness(50);  // Start with lower brightness for testing
    
    // Test onboard LED - rainbow startup
    Serial.println("Testing onboard LED...");
    
    // Cycle through rainbow colors on the single LED
    for(int hue = 0; hue < 256; hue += 8) {
        leds[0] = CHSV(hue, 255, 255);
        FastLED.show();
        delay(20);
    }
    
    // Flash white to confirm LED is working
    for(int i = 0; i < 3; i++) {
        leds[0] = CRGB::White;
        FastLED.show();
        delay(100);
        leds[0] = CRGB::Black;
        FastLED.show();
        delay(100);
    }
    
    Serial.println("Onboard LED test complete!");
    
    // Initialize WiFi for ESP-NOW
    WiFi.mode(WIFI_STA);
    Serial.print("Display Unit MAC Address: ");
    Serial.println(WiFi.macAddress());
    
    // Initialize ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }
    
    // Register receive callback
    esp_now_register_recv_cb(onDataReceive);
    
    Serial.println("Display unit ready!");
    Serial.println("Waiting for commands from console unit...");
    
    // Indicate ready with green flash
    fill_solid(leds, NUM_LEDS, CRGB::Green);
    FastLED.show();
    delay(500);
    FastLED.clear();
    FastLED.show();
}

void loop() {
    // No heartbeat during ESP-NOW testing - colors should persist
    // ESP-NOW message handling is done via callbacks
    delay(100);
}

void onDataReceive(const uint8_t *mac_addr, const uint8_t *data, int len) {
    Serial.printf("Message received from: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  mac_addr[0], mac_addr[1], mac_addr[2], 
                  mac_addr[3], mac_addr[4], mac_addr[5]);
    
    if (len == sizeof(DisplayMessage)) {
        memcpy(&incomingMessage, data, sizeof(incomingMessage));
        
        Serial.printf("Command: %d, Color: RGB(%d,%d,%d), Pos: %d, Len: %d\n",
                     incomingMessage.command, incomingMessage.red, 
                     incomingMessage.green, incomingMessage.blue,
                     incomingMessage.position, incomingMessage.length);
        
        executeDisplayCommand();
    } else {
        Serial.println("Invalid message length received");
    }
}

void executeDisplayCommand() {
    switch(incomingMessage.command) {
        case 1: // Test pattern
            Serial.println("Executing test pattern...");
            testPattern(incomingMessage.red, incomingMessage.green, incomingMessage.blue);
            break;
            
        case 2: // Clear display
            Serial.println("Clearing display...");
            FastLED.clear();
            FastLED.show();
            break;
            
        case 3: // Set specific LEDs
            Serial.println("Setting LED segment...");
            setLEDSegment(incomingMessage.position, incomingMessage.length,
                         CRGB(incomingMessage.red, incomingMessage.green, incomingMessage.blue));
            break;
            
        default:
            Serial.println("Unknown command received");
            break;
    }
}

void testPattern(uint8_t r, uint8_t g, uint8_t b) {
    // Immediate response for game simulation - no flashing
    Serial.printf("Setting onboard LED: RGB(%d,%d,%d)\n", r, g, b);
    leds[0] = CRGB(r, g, b);
    FastLED.show();
}

void setLEDSegment(uint8_t start, uint8_t length, CRGB color) {
    // For single LED, just set the color
    Serial.printf("Setting onboard LED: RGB(%d,%d,%d)\n", color.r, color.g, color.b);
    leds[0] = color;
    FastLED.show();
}
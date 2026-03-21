/**
 * Environmental Game - Wireless Display Unit
 * M5Stack Atom S3 Lite ESP-NOW Receiver
 * 
 * Receives environmental game data wirelessly from console ESP32
 * and displays pollution levels on onboard RGB LED
 * 
 * Hardware: M5Stack Atom S3 Lite
 * - ESP32-S3 SoC with built-in WiFi
 * - Onboard WS2812 RGB LED (GPIO35)
 * - USB-C for programming and power
 */

#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <FastLED.h>

// =============================================================================
// HARDWARE CONFIGURATION
// =============================================================================

// LED Configuration for M5Stack Atom S3 Lite
#define LED_PIN_ONBOARD 35       // GPIO35 - Onboard RGB LED (status indicator)
#define LED_PIN_PM25    5        // GPIO5  - PM2.5 pollution strip (blue)
#define LED_PIN_NO2     6        // GPIO6  - NO2 pollution strip (red)  
#define LED_PIN_O3      7        // GPIO7  - O3 pollution strip (green)

#define LEDS_PER_STRIP  100      // LEDs per environmental strip
#define NUM_ONBOARD_LEDS 1       // Single onboard status LED
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB

// LED arrays for each strip + onboard status LED
CRGB ledsOnboard[NUM_ONBOARD_LEDS];     // Status indicator
CRGB ledsPM25[LEDS_PER_STRIP];          // PM2.5 pollution display (blue)
CRGB ledsNO2[LEDS_PER_STRIP];           // NO2 pollution display (red)
CRGB ledsO3[LEDS_PER_STRIP];            // O3 pollution display (green)

// =============================================================================
// ESP-NOW COMMUNICATION PROTOCOL
// =============================================================================

// Message structure matching console unit
typedef struct {
    uint8_t command;     // Display command type
    uint8_t red;         // Red color value (0-255)
    uint8_t green;       // Green color value (0-255) 
    uint8_t blue;        // Blue color value (0-255)
    uint8_t position;    // LED position (for future expansion)
    uint8_t length;      // Segment length (for future expansion)
    uint8_t stripId;     // Strip identifier (0=PM2.5, 1=NO2, 2=O3)
    uint8_t gameState;   // Current game state from console
} DisplayMessage;

// Display commands
#define CMD_SOLID        1  // Solid color display
#define CMD_CLEAR        2  // Clear display
#define CMD_FLASH        3  // Flash effect for button press
#define CMD_ENVIRONMENT  4  // Environmental data display
#define CMD_VICTORY      5  // Victory animation
#define CMD_CONNECTING   6  // WiFi connecting animation

DisplayMessage incomingMessage;

// Display state
unsigned long lastMessageTime = 0;
bool connectionActive = false;
uint8_t currentBrightness = 100;

// =============================================================================
// FUNCTION DECLARATIONS
// =============================================================================

// ESP-NOW callback
void onDataReceive(const uint8_t *mac_addr, const uint8_t *data, int len);

// Display functions
void executeDisplayCommand();
void displaySolidColor(uint8_t r, uint8_t g, uint8_t b);
void clearDisplay();
void flashButtonFeedback(uint8_t r, uint8_t g, uint8_t b);
void displayEnvironmentalData(uint8_t stripId, uint8_t r, uint8_t g, uint8_t b);
void displayVictoryAnimation();
void displayConnectingAnimation();

// System monitoring
void checkConnectionStatus();
void printSystemStatus();

// =============================================================================
// ESP-NOW CALLBACK FUNCTIONS
// =============================================================================

void onDataReceive(const uint8_t *mac_addr, const uint8_t *data, int len) {
    // Update connection status
    connectionActive = true;
    lastMessageTime = millis();
    
    // Validate message
    if (len != sizeof(DisplayMessage)) {
        Serial.printf("⚠️ Invalid message size: %d (expected %d)\n", len, sizeof(DisplayMessage));
        return;
    }
    
    // Copy message data
    memcpy(&incomingMessage, data, sizeof(incomingMessage));
    
    // Debug output
    Serial.printf("📡 Received: CMD=%d RGB(%d,%d,%d) Strip=%d State=%d\n",
                  incomingMessage.command, incomingMessage.red, 
                  incomingMessage.green, incomingMessage.blue,
                  incomingMessage.stripId, incomingMessage.gameState);
    
    // Execute display command immediately
    executeDisplayCommand();
}

// =============================================================================
// DISPLAY FUNCTIONS
// =============================================================================

void executeDisplayCommand() {
    switch(incomingMessage.command) {
        case CMD_SOLID:
            displaySolidColor(incomingMessage.red, incomingMessage.green, incomingMessage.blue);
            break;
            
        case CMD_CLEAR:
            clearDisplay();
            break;
            
        case CMD_FLASH:
            flashButtonFeedback(incomingMessage.red, incomingMessage.green, incomingMessage.blue);
            break;
            
        case CMD_ENVIRONMENT:
            displayEnvironmentalData(incomingMessage.stripId, incomingMessage.red, 
                                   incomingMessage.green, incomingMessage.blue);
            break;
            
        case CMD_VICTORY:
            displayVictoryAnimation();
            break;
            
        case CMD_CONNECTING:
            displayConnectingAnimation();
            break;
            
        default:
            Serial.printf("❌ Unknown command: %d\n", incomingMessage.command);
            break;
    }
}

void displaySolidColor(uint8_t r, uint8_t g, uint8_t b) {
    // Display on status LED (onboard)
    Serial.printf("🎨 Status LED: RGB(%d,%d,%d)\n", r, g, b);
    ledsOnboard[0] = CRGB(r, g, b);
    FastLED.show();
}

void clearDisplay() {
    Serial.println("🔲 Clearing all displays");
    // Clear all strips and status LED
    ledsOnboard[0] = CRGB::Black;
    fill_solid(ledsPM25, LEDS_PER_STRIP, CRGB::Black);
    fill_solid(ledsNO2, LEDS_PER_STRIP, CRGB::Black);
    fill_solid(ledsO3, LEDS_PER_STRIP, CRGB::Black);
    FastLED.show();
}

void flashButtonFeedback(uint8_t r, uint8_t g, uint8_t b) {
    Serial.printf("⚡ Button flash: RGB(%d,%d,%d)\n", r, g, b);
    
    // Bright flash on status LED for immediate feedback
    ledsOnboard[0] = CRGB(r, g, b);
    FastLED.setBrightness(255);
    FastLED.show();
    
    delay(150);  // Brief flash duration
    
    // Return to normal brightness
    FastLED.setBrightness(currentBrightness);
    FastLED.show();
}

void displayEnvironmentalData(uint8_t stripId, uint8_t r, uint8_t g, uint8_t b) {
    // Get the appropriate LED strip based on stripId
    CRGB* targetStrip;
    const char* pollutantNames[] = {"PM2.5", "NO₂", "O₃"};
    const char* stripName = "Unknown";
    
    switch(stripId) {
        case 0: // PM2.5
            targetStrip = ledsPM25;
            stripName = pollutantNames[0];
            break;
        case 1: // NO2
            targetStrip = ledsNO2;
            stripName = pollutantNames[1];
            break;
        case 2: // O3
            targetStrip = ledsO3;
            stripName = pollutantNames[2];
            break;
        default:
            Serial.printf("⚠️ Invalid stripId: %d\n", stripId);
            return;
    }
    
    // Map color intensity to pollution level
    uint8_t intensity = max(r, max(g, b));  // Use brightest color component as intensity
    Serial.printf("🌍 %s strip: RGB(%d,%d,%d) Intensity %d\n", stripName, r, g, b, intensity);
    
    // Display pollution level as filled pixels from bottom
    int activePixels = map(intensity, 0, 255, 0, LEDS_PER_STRIP);
    
    // Clear the strip first
    fill_solid(targetStrip, LEDS_PER_STRIP, CRGB::Black);
    
    // Fill from bottom with environmental data
    for(int i = 0; i < activePixels; i++) {
        targetStrip[i] = CRGB(r, g, b);
    }
    
    FastLED.show();
    
    // Also update status LED to show active strip
    ledsOnboard[0] = CRGB(r/4, g/4, b/4);  // Dim version on status LED
}

void displayVictoryAnimation() {
    Serial.println("🏆 Victory animation!");
    
    // Rainbow celebration
    for(int i = 0; i < 3; i++) {  // 3 rainbow cycles
        for(int hue = 0; hue < 256; hue += 8) {
            ledsOnboard[0] = CHSV(hue, 255, 255);
            FastLED.show();
            delay(20);
        }
    }
    
    // End with golden glow
    ledsOnboard[0] = CRGB(255, 215, 0);  // Gold
    FastLED.show();
}

void displayConnectingAnimation() {
    Serial.println("📶 WiFi connecting...");
    
    // Blue pulse
    static uint8_t brightness = 50;
    static bool increasing = true;
    
    if (increasing) {
        brightness += 5;
        if (brightness >= 200) increasing = false;
    } else {
        brightness -= 5;
        if (brightness <= 50) increasing = true;
    }
    
    ledsOnboard[0] = CRGB(0, 0, brightness);
    FastLED.show();
}

// =============================================================================
// SYSTEM MONITORING
// =============================================================================

void checkConnectionStatus() {
    // Check if we've lost connection with console
    if (connectionActive && (millis() - lastMessageTime > 15000)) {
        connectionActive = false;
        Serial.println("⚠️ Lost connection with console unit");
        
        // Show disconnected status (dim red pulse)
        static unsigned long lastPulse = 0;
        if (millis() - lastPulse > 1000) {
            ledsOnboard[0] = CRGB(50, 0, 0);  // Dim red
            FastLED.show();
            delay(100);
            ledsOnboard[0] = CRGB::Black;
            FastLED.show();
            lastPulse = millis();
        }
    }
}

void printSystemStatus() {
    static unsigned long lastStatus = 0;
    if (millis() - lastStatus > 30000) {  // Every 30 seconds
        Serial.println("\n=== Display Unit Status ===");
        Serial.printf("📍 MAC Address: %s\n", WiFi.macAddress().c_str());
        Serial.printf("🔗 Connection: %s\n", connectionActive ? "ACTIVE" : "WAITING");
        Serial.printf("💾 Free Heap: %d bytes\n", ESP.getFreeHeap());
        Serial.printf("⏱️ Uptime: %lu seconds\n", millis() / 1000);
        Serial.println("===========================\n");
        lastStatus = millis();
    }
}

// =============================================================================
// SETUP AND MAIN LOOP
// =============================================================================

void setup() {
    Serial.begin(115200);
    Serial.println("\n🌍 Environmental Game - Wireless Display Unit");
    Serial.println("M5Stack Atom S3 Lite - Starting...");
    
    // Initialize LED system - onboard status LED + 3 environmental strips
    FastLED.addLeds<LED_TYPE, LED_PIN_ONBOARD, COLOR_ORDER>(ledsOnboard, NUM_ONBOARD_LEDS);
    FastLED.addLeds<LED_TYPE, LED_PIN_PM25, COLOR_ORDER>(ledsPM25, LEDS_PER_STRIP);
    FastLED.addLeds<LED_TYPE, LED_PIN_NO2, COLOR_ORDER>(ledsNO2, LEDS_PER_STRIP);
    FastLED.addLeds<LED_TYPE, LED_PIN_O3, COLOR_ORDER>(ledsO3, LEDS_PER_STRIP);
    FastLED.setBrightness(currentBrightness);
    FastLED.clear();
    FastLED.show();
    
    // Clear all LED strips
    Serial.println("🔧 Initializing LED strips...");
    fill_solid(ledsPM25, LEDS_PER_STRIP, CRGB::Black);
    fill_solid(ledsNO2, LEDS_PER_STRIP, CRGB::Black);
    fill_solid(ledsO3, LEDS_PER_STRIP, CRGB::Black);
    FastLED.show();
    
    // Startup LED test sequence on onboard LED
    Serial.println("🔧 Testing onboard status LED...");
    
    // Color test sequence on status LED only
    const CRGB testColors[] = {CRGB::Red, CRGB::Green, CRGB::Blue, CRGB::White};
    for (int i = 0; i < 4; i++) {
        ledsOnboard[0] = testColors[i];
        FastLED.show();
        delay(300);
    }
    
    // Initialize WiFi for ESP-NOW - receiver mode only
    WiFi.mode(WIFI_STA); // Station mode - no AP broadcasting
    WiFi.disconnect(); // Don't connect to any network
    
    Serial.printf("📍 Display Unit MAC: %s\n", WiFi.macAddress().c_str());
    Serial.println("📡 Starting in STA mode for ESP-NOW receive...");
    
    Serial.println("📡 Setting WiFi channel 1 to match console AP...");
    // Set to channel 1 to match console board AP
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    
    Serial.println("📡 Initializing ESP-NOW...");
    
    // Initialize ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("❌ ESP-NOW initialization failed!");
        // Error indication - red flash
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
    Serial.println("🎮 Ready to receive environmental game data!");
    Serial.println("Waiting for console connection...\n");
    
    // Ready indication - green glow
    ledsOnboard[0] = CRGB::Green;
    FastLED.show();
    delay(1000);
    ledsOnboard[0] = CRGB::Black;
    FastLED.show();
}

void loop() {
    checkConnectionStatus();
    printSystemStatus();
    
    // Small delay to prevent overwhelming the system
    delay(100);
}
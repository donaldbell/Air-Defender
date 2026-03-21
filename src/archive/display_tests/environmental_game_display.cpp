/**
 * M5Stack Atom S3 - Environmental Game Display
 * Renders complete environmental gaming experience with heroes, enemies, shots
 */

#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <FastLED.h>

// LED Configuration for M5Stack Atom S3  
#define LED_PIN_ONBOARD 35       // GPIO35 - Onboard RGB LED (status)
#define LED_PIN_PM25    8        // GPIO8  - PM2.5 pollution strip (blue theme) 
#define LED_PIN_NO2     6        // GPIO6  - NO2 pollution strip (red theme)
#define LED_PIN_O3      7        // GPIO7  - O3 pollution strip (green theme)

#define NUM_ONBOARD_LEDS 1       
#define LEDS_PER_STRIP   30      // 30 LEDs per environmental strip
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB

// LED arrays
CRGB ledsOnboard[NUM_ONBOARD_LEDS];     // Status indicator
CRGB ledsPM25[LEDS_PER_STRIP];          // PM2.5 environmental display
CRGB ledsNO2[LEDS_PER_STRIP];           // NO2 environmental display  
CRGB ledsO3[LEDS_PER_STRIP];            // O3 environmental display

// Environmental Game State
struct PollutionEnemy {
    float position;
    int health;
    CRGB color;
    unsigned long lastMove;
    bool active;
};

struct EnvironmentalShot {
    float position;
    CRGB color;
    int stripId;
    unsigned long lastMove;
    bool active;
};

struct HeroState {
    float position; // Around 99+ for heroes
    CRGB color;
    float sparkle;
    bool active;
};

// Game objects per strip
std::vector<PollutionEnemy> pollutionEnemies[3];  // 3 strips
std::vector<EnvironmentalShot> environmentalShots;
HeroState heroes[3];  // Heroes for each strip

// Message structure (8 bytes)
typedef struct {
    uint8_t command;     // 4=environmental data
    uint8_t red;
    uint8_t green;  
    uint8_t blue;
    uint8_t position;
    uint8_t length;
    uint8_t stripId;     // 0=PM2.5, 1=NO2, 2=O3
    uint8_t gameState;   // Current game state
} DisplayMessage;

unsigned long lastReceivedTime = 0;
unsigned long lastGameUpdate = 0;
uint8_t currentBrightness = 80;

// Initialize heroes at top of strips
void initializeHeroes() {
    heroes[0] = {29.0f, CRGB::Blue, 0.0f, true};     // PM2.5 hero (blue)
    heroes[1] = {29.0f, CRGB::Red, 0.0f, true};      // NO2 hero (red)  
    heroes[2] = {29.0f, CRGB::Green, 0.0f, true};    // O3 hero (green)
}

// Add pollution enemy to strip  
void addPollutionEnemy(int stripId, CRGB color, int health = 4) {
    if (stripId >= 0 && stripId < 3) {
        PollutionEnemy enemy;
        enemy.position = 0.0f;  // Start at bottom
        enemy.health = health;
        enemy.color = color;
        enemy.lastMove = millis();
        enemy.active = true;
        pollutionEnemies[stripId].push_back(enemy);
        Serial.printf("🦠 Added pollution enemy to strip %d, count now: %d\n", 
                     stripId, pollutionEnemies[stripId].size());
    }
}

// Add environmental shot
void addEnvironmentalShot(int stripId, CRGB color) {
    if (stripId >= 0 && stripId < 3) {
        EnvironmentalShot shot;
        shot.position = heroes[stripId].position - 1.0f;  // Start below hero
        shot.color = color;
        shot.stripId = stripId;
        shot.lastMove = millis();
        shot.active = true;
        environmentalShots.push_back(shot);
        Serial.printf("💫 Environmental shot fired on strip %d\n", stripId);
    }
}

// Update game physics
void updateGamePhysics() {
    unsigned long now = millis();
    
    // Update pollution enemies (move toward heroes)
    for (int strip = 0; strip < 3; strip++) {
        for (auto& enemy : pollutionEnemies[strip]) {
            if (enemy.active && now - enemy.lastMove > 100) {  // Move every 100ms
                enemy.position += 0.1f;  // Move toward hero
                enemy.lastMove = now;
                
                // Check if enemy reached hero
                if (enemy.position >= heroes[strip].position - 1) {
                    enemy.active = false;
                    Serial.printf("💀 Pollution enemy reached hero on strip %d!\n", strip);
                }
            }
        }
        
        // Clean up inactive enemies
        pollutionEnemies[strip].erase(
            std::remove_if(pollutionEnemies[strip].begin(), 
                          pollutionEnemies[strip].end(),
                          [](const PollutionEnemy& e) { return !e.active; }),
            pollutionEnemies[strip].end()
        );
    }
    
    // Update environmental shots (move away from heroes)
    for (auto& shot : environmentalShots) {
        if (shot.active && now - shot.lastMove > 50) {  // Move every 50ms
            shot.position -= 0.5f;  // Move toward pollution
            shot.lastMove = now;
            
            // Check shot vs pollution enemy collisions
            for (auto& enemy : pollutionEnemies[shot.stripId]) {
                if (enemy.active && 
                    abs(shot.position - enemy.position) < 1.5f) {
                    // Hit!
                    enemy.health--;
                    shot.active = false;
                    Serial.printf("💥 Shot hit pollution enemy! Health now: %d\n", enemy.health);
                    
                    if (enemy.health <= 0) {
                        enemy.active = false;
                        Serial.printf("☠️ Pollution enemy destroyed on strip %d!\n", shot.stripId);
                    }
                    break;
                }
            }
            
            // Remove shots that went off-screen
            if (shot.position < 0) {
                shot.active = false;
            }
        }
    }
    
    // Clean up inactive shots
    environmentalShots.erase(
        std::remove_if(environmentalShots.begin(), environmentalShots.end(),
                      [](const EnvironmentalShot& s) { return !s.active; }),
        environmentalShots.end()
    );
}

// Render complete environmental game to LED strips
void renderEnvironmentalGame() {
    // Clear all strips
    fill_solid(ledsPM25, LEDS_PER_STRIP, CRGB::Black);
    fill_solid(ledsNO2, LEDS_PER_STRIP, CRGB::Black);
    fill_solid(ledsO3, LEDS_PER_STRIP, CRGB::Black);
    
    CRGB* strips[3] = {ledsPM25, ledsNO2, ledsO3};
    
    // Render heroes at top of each strip 
    for (int i = 0; i < 3; i++) {
        if (heroes[i].active) {
            int heroPos = (int)heroes[i].position;
            if (heroPos >= 0 && heroPos < LEDS_PER_STRIP) {
                strips[i][heroPos] = heroes[i].color;
                
                // Add sparkle effect
                heroes[i].sparkle += 0.1f;
                if (heroes[i].sparkle > 6.28f) heroes[i].sparkle = 0.0f;
                int sparkleIntensity = (int)(127 + 127 * sin(heroes[i].sparkle));
                strips[i][heroPos].fadeLightBy(256 - sparkleIntensity);
            }
        }
    }
    
    // Render pollution enemies
    for (int strip = 0; strip < 3; strip++) {
        for (const auto& enemy : pollutionEnemies[strip]) {
            if (enemy.active) {
                int enemyPos = (int)enemy.position;
                if (enemyPos >= 0 && enemyPos < LEDS_PER_STRIP) {
                    // Enemy color intensity based on health
                    CRGB enemyColor = enemy.color;
                    enemyColor.fadeLightBy(256 - (enemy.health * 64));  // Dimmer as health decreases
                    strips[strip][enemyPos] = enemyColor;
                }
            }
        }
    }
    
    // Render environmental shots
    for (const auto& shot : environmentalShots) {
        if (shot.active && shot.stripId >= 0 && shot.stripId < 3) {
            int shotPos = (int)shot.position;
            if (shotPos >= 0 && shotPos < LEDS_PER_STRIP) {
                strips[shot.stripId][shotPos] = shot.color;
            }
        }
    }
    
    // Show all updates
    FastLED.show();
}

// ESP-NOW receive callback - parse environmental commands
void onDataReceive(const uint8_t *mac, const uint8_t *data, int len) {
    if (len != sizeof(DisplayMessage)) return;
    
    DisplayMessage* message = (DisplayMessage*)data;
    lastReceivedTime = millis();
    
    Serial.printf("📡 Environmental data: Cmd:%d, Strip:%d, RGB:(%d,%d,%d), Pos:%d, Len:%d\n", 
                  message->command, message->stripId, message->red, message->green, 
                  message->blue, message->position, message->length);
    
    if (message->command == 4) {  // Environmental game data
        CRGB color = CRGB(message->red, message->green, message->blue);
        
        // Status LED indicates game activity
        ledsOnboard[0] = CRGB::Green;
        
        // Interpret environmental data
        if (message->position >= 25) {
            // Hero area - update hero state
            if (message->stripId >= 0 && message->stripId < 3) {
                heroes[message->stripId].color = color;
                heroes[message->stripId].position = (float)message->position;
            }
        }
        else if (message->position <= 20 && message->length > 0) {
            // Pollution enemy area
            for (int i = 0; i < message->length && i < 5; i++) {  // Max 5 enemies per message
                addPollutionEnemy(message->stripId, color, 4);
            }
        }
        
        // Check for shot indicators
        if (message->red > 200 || message->green > 200 || message->blue > 200) {
            addEnvironmentalShot(message->stripId, color);
        }
    }
    
    FastLED.show();
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n=== M5Stack Atom S3 - Environmental Game Display ===");
    Serial.println("Full Environmental Gaming Visualization");
    
    // Initialize LED systems
    FastLED.addLeds<LED_TYPE, LED_PIN_ONBOARD, COLOR_ORDER>(ledsOnboard, NUM_ONBOARD_LEDS);
    FastLED.addLeds<LED_TYPE, LED_PIN_PM25, COLOR_ORDER>(ledsPM25, LEDS_PER_STRIP);
    FastLED.addLeds<LED_TYPE, LED_PIN_NO2, COLOR_ORDER>(ledsNO2, LEDS_PER_STRIP);
    FastLED.addLeds<LED_TYPE, LED_PIN_O3, COLOR_ORDER>(ledsO3, LEDS_PER_STRIP);
    FastLED.setBrightness(currentBrightness);
    FastLED.clear();
    FastLED.show();
    
    // Initialize environmental game state
    initializeHeroes();
    
    // Add some initial pollution enemies for testing
    addPollutionEnemy(0, CRGB::DarkBlue, 4);    // PM2.5
    addPollutionEnemy(1, CRGB::DarkRed, 3);     // NO2
    addPollutionEnemy(2, CRGB::DarkGreen, 2);   // O3
    
    Serial.println("🎮 Environmental game initialized!");
    Serial.printf("Heroes at positions: PM2.5=%.1f, NO2=%.1f, O3=%.1f\n", 
                  heroes[0].position, heroes[1].position, heroes[2].position);
    
    // LED test sequence
    Serial.println("🔧 Testing environmental game display...");
    for (int i = 0; i < 3; i++) {
        renderEnvironmentalGame();
        delay(1000);
    }
    
    // Initialize WiFi for ESP-NOW
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    
    Serial.printf("📍 MAC Address: %s\n", WiFi.macAddress().c_str());
    
    // Initialize ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("❌ ESP-NOW init failed!");
        return;
    }
    esp_now_register_recv_cb(onDataReceive);
    
    Serial.println("✅ ESP-NOW initialized for environmental gaming!");
    Serial.println("🎯 Ready for environmental battle data from console!");
    
    // Ready indicator
    ledsOnboard[0] = CRGB::Green;
    FastLED.show();
}

void loop() {
    unsigned long now = millis();
    
    // Update game physics 60fps
    if (now - lastGameUpdate > 16) {  // ~60 FPS
        updateGamePhysics();
        renderEnvironmentalGame();
        lastGameUpdate = now;
    }
    
    // Status indicator  
    static unsigned long lastStatusUpdate = 0;
    if (now - lastStatusUpdate > 1000) {
        Serial.printf("🎮 Environmental Status: Enemies=%d,%d,%d, Shots=%d, Heroes=%d\n",
                     pollutionEnemies[0].size(), pollutionEnemies[1].size(), 
                     pollutionEnemies[2].size(), environmentalShots.size(),
                     heroes[0].active + heroes[1].active + heroes[2].active);
        lastStatusUpdate = now;
    }
    
    // Connection status
    if (now - lastReceivedTime > 5000) {
        ledsOnboard[0] = CRGB::Red;  // No data received
    } else {
        ledsOnboard[0] = CRGB::Green;  // Receiving data
    }
    
    delay(1);
}
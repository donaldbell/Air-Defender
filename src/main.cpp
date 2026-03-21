// 1D RGB LED Strip Space Invaders Game for ESP32
// Optimized for Adafruit QT Py S3 with PSRAM
// Environmental Air Quality Mining Game

#include <Arduino.h>
#include <FastLED.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_now.h>
#include "driver/i2s.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "game_config.h"
#include "audio_system.h"

// --------------------------------------------------------------------------
// 3. GLOBAL VARIABLES (now defined in game_config.h)
// --------------------------------------------------------------------------

EnvironmentalState currentState = STATE_WIFI_CONNECTING;
unsigned long stateStartTime = 0;

// LED strip configuration variables
CRGB* leds = nullptr;
int config_num_leds = 300;  // Full 300 LEDs restored
int config_brightness_pct = 40;  // Moderate brightness

// Audio system variables definitions
bool config_sound_on = true;
int config_volume_pct = 50;
bool audioEnabled = false;
QueueHandle_t audioQueue = nullptr;
TaskHandle_t audioTaskHandle = nullptr;
bool priorityMelodyPlaying = false;

// Audio melody definitions
Melody melStart, melWin, melLose, melMistake, melShotBlue, melShotRed, melShotGreen, melShotWhite, melHit;
Melody melShotCannon;
Melody melFireworkLaunch, melFireworkExplode;
Melody melEnemiesBuilding, melLevelVictory;

// Player and game mechanics
int playerPosition = 0;
int currentLevel = 1;
int config_start_level = 1;
int currentScore = 0;
int livesRemaining = 3;
bool won = false;
// Button states - only gaming buttons tracked during operation  
bool btBlue = false, btRed = false, btGreen = false;
bool lastBtBlue = false, lastBtRed = false, lastBtGreen = false;
unsigned long lastButtonCheck = 0;
int enemyFrontIndex = 0;  // Fixed type conflict with game_config.h
unsigned long levelStartTime = 0;

// Collections for game objects
std::vector<Shot> shots;
std::vector<Spark> sparks;  // NEW: Impact sparks
std::vector<Enemy> enemies;
std::vector<BossSegment> bossSegments;
std::vector<BossProjectile> bossProjectiles;
std::vector<Firework> fireworks;  // NEW: Level transition fireworks

// Environmental Game Variables
AirQualityData currentAQI;
bool wifiConnected = false;
std::vector<EnvironmentalShot> environmentalShots;
std::vector<PollutionEnemy> pollutionEnemies[NUM_STRIPS];  // Array of vectors for each strip
int scorePerStrip[NUM_STRIPS] = {0, 0, 0};
float playerPositions[NUM_STRIPS] = {99, 99, 99};  // Heroes at the very top
float playerRecoil[NUM_STRIPS] = {0, 0, 0};  // Hero pushback effect
float playerSparkle[NUM_STRIPS] = {0, 0, 0};  // Hero sparkle animation
bool stripCompleted[NUM_STRIPS] = {false, false, false};  // Track completed strips

// Visual effect objects
std::vector<WipeEffect> wipeEffects;
int completedStripCount = 0;
// Victory sequence timing
bool victoryMusicPlaying = false;
unsigned long victoryMusicStartTime = 0;
const unsigned long VICTORY_MUSIC_DURATION = 4000;  // 4 seconds for Mario-style victory
const unsigned long FINAL_FIREWORKS_DELAY = 5000;  // Start final fireworks after 5s  
const unsigned long BLACKOUT_DELAY = 7000;         // Blackout after 7s
const unsigned long GAME_RESET_DELAY = 10000;      // Reset game after 10s

// Delayed strip fireworks (1 second delay after strip clearing)
const unsigned long STRIP_FIREWORK_DELAY = 1000;  // 1 second delay
unsigned long delayedFireworkTime[NUM_STRIPS] = {0};  // Timestamp for each strip

// Cannon shot charging system
const unsigned long CANNON_CHARGE_TIME = 2000;  // 2 seconds to fully charge
unsigned long buttonPressTime[NUM_STRIPS] = {0};  // When button was first pressed
bool buttonCharging[NUM_STRIPS] = {false};  // Is button currently charging?
bool lastButtonState[NUM_STRIPS] = {false};  // Previous button state for edge detection

// Charging sound system
unsigned long lastChargeSoundTime[NUM_STRIPS] = {0};  // Last time charge sound played for each strip
const unsigned long CHARGE_SOUND_INTERVAL = 300;  // Play charge pulse every 300ms
const unsigned long CHARGE_SOUND_DELAY = 500;  // Wait 500ms before starting charge audio
bool allStripsCompleteEffect = false;
bool finalFireworksTriggered = false;
bool gameBlackedOut = false;
unsigned long allStripsCompleteTime = 0;

// LED strip arrays
CRGB* stripPM25 = nullptr;
CRGB* stripNO2 = nullptr;
CRGB* stripO3 = nullptr;

// Column filling animation variables
int targetColumnHeights[NUM_STRIPS] = {0, 0, 0};  // Target heights for each strip
int currentColumnHeights[NUM_STRIPS] = {0, 0, 0}; // Current animated heights
unsigned long lastColumnUpdate = 0;
bool columnAnimationComplete = false;

// Physics constants
float gravity = -0.008f;  // Increased gravity to keep fireworks lower

// ESP-NOW Wireless Display Communication
// M5Stack Atom S3 Lite MAC address for wireless LED control
uint8_t displayUnitMAC[] = {0x34, 0xb7, 0xda, 0x57, 0x36, 0xfc};

// Message structure for ESP-NOW communication
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

DisplayMessage outgoingMessage;
esp_now_peer_info_t peerInfo;
bool espnowReady = false;
bool displayUnitConnected = false;
unsigned long lastDisplayUpdate = 0;
const unsigned long DISPLAY_UPDATE_INTERVAL = 200; // Reduce from 50ms to 200ms (5fps instead of 20fps)
float shotInitialVelocity = 2.0f;  // Higher velocity for better range

// High scores
int highScores[5] = {0, 0, 0, 0, 0};

// Visual Effects System
CRGBPalette16 currentPalette = RainbowColors_p;
TBlendType currentBlending = LINEARBLEND;
uint8_t gHue = 0; // Global hue for effects
uint8_t attractModeStep = 0;
unsigned long attractModeTimer = 0;

// Colors (customizable via web interface)
CRGB c1 = CRGB::Blue;
CRGB c2 = CRGB::Red;
CRGB c3 = CRGB::Green;
CRGB c4 = CRGB::White;
CRGB c5 = CRGB::Yellow;
CRGB c6 = CRGB::Purple;
CRGB cWall = CRGB::Orange;
CRGB cBoss = CRGB::Purple;

// WiFi Configuration - ACCESS POINT MODE (No external WiFi needed!)
// ESP32 creates its own WiFi network that your phone connects to
String config_ssid = "EnviroGame-AQI";     // ESP32's WiFi network name
String config_pass = "CleanAir123";        // Password to connect to ESP32

// Access Point Configuration
String ap_ssid = "EnviroGame-AQI";          // Same as above for consistency  
String ap_pass = "CleanAir123";             // 8+ characters required
IPAddress ap_ip(192, 168, 4, 1);           // ESP32's IP address
IPAddress ap_gateway(192, 168, 4, 1);
IPAddress ap_subnet(255, 255, 255, 0);

// Manual AQI input mode - no external internet required!
bool useAccessPointMode = true;             // Set to true for standalone operation
String config_ip = "192.168.4.1";
String config_gateway = "192.168.4.1";
String config_subnet = "255.255.255.0";
String config_dns = "192.168.4.1";
bool config_static_ip = false;
String currentProfilePrefix = "default";

// Boss fight variables
BossConfig boss1Cfg = {20, 0.3, 2.0, 15};
BossConfig boss2Cfg = {30, 0.5, 1.5, 25};
BossConfig boss3Cfg = {40, 0.7, 1.0, 35};

enum Boss2State { B2_CHARGE, B2_SHOOT, B2_MOVE };
Boss2State boss2State = B2_CHARGE;
int boss2LockedColor = 1;
int boss2ShotsFired = 0;
int boss2Section = 0;
unsigned long bossActionTimer = 0;

enum Boss3State { B3_SPIN, B3_SHOOT };
Boss3State boss3State = B3_SPIN;
unsigned long boss3Timer = 0;

// Preferences and WiFi
Preferences preferences;
WebServer server(80);

// Forward declarations for functions
void updateLevelPalette(); 
void playShotSound(int color);
void playCannonShotSound();

#ifdef QTPY_S3
// Status LED control for QT Py S3 built-in NeoPixel
void setStatusLED(uint8_t r, uint8_t g, uint8_t b);
void statusLEDOff();
#endif

// Physics and effects function declarations
void createImpactSparks(float position, int color, int stripIndex, bool success);
void createDramaticExplosion(float position, int stripIndex, int intensity = 1);
void updateSparks(float deltaTime);
void createLevelFireworks(int numFireworks);
void createStripFirework(int stripIndex, bool silent);
void createWipeEffect(int stripIndex);
void updateFireworks(float deltaTime);
void updateWipeEffects();
void updatePlayerEffects();

// Persistent storage for AQI data
void saveAQIData();
void loadAQIData();
void clearStoredAQI();

// Web server route handlers for AQI input interface
void handleRoot();
void handleAQIInput();
void handleSetAQI();
void handleAPIHelper();
void setupWebServerRoutes();
void initWiFiConnection();
bool fetchAirQualityData();
void createPollutionEnemies();
void prepareColumnAnimation();
void updateEnvironmentalGame();
void renderEnvironmentalDisplay();
void initLEDStrips();

// ESP-NOW Wireless Display Functions
void initESPNOW();
void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
void sendDisplayCommand(uint8_t cmd, uint8_t r, uint8_t g, uint8_t b, uint8_t pos = 0, uint8_t len = 0, uint8_t strip = 0);
void sendEnvironmentalState();
void updateWirelessDisplay();

// Audio system stub implementations (to be replaced with actual audio system)
// Audio system now in audio_system.cpp - only convenience functions here
void playShotSound(int color) {
  if (!config_sound_on) return;
  
  switch(color) {
    case 1: playSound(EVT_SHOT_BLUE); break;
    case 2: playSound(EVT_SHOT_RED); break;
    case 3: playSound(EVT_SHOT_GREEN); break;
    case 7: playSound(EVT_SHOT_WHITE); break;
    default: playSound(EVT_SHOT_BLUE); break;
  }
}

void playCannonShotSound() {
  if (!config_sound_on) return;
  playSound(EVT_SHOT_CANNON);
}

// --------------------------------------------------------------------------
// 5. BUTTON HANDLING
// --------------------------------------------------------------------------
void readButtons() {
  // Store previous button states for debouncing BEFORE reading new states
  lastBtBlue = btBlue;
  lastBtRed = btRed;
  lastBtGreen = btGreen;
  // Settings button state no longer tracked during gameplay
  
  if (millis() - lastButtonCheck > 12) { // Balanced debounce - responsive but no double shots
    lastButtonCheck = millis();
    
    // Read current button states with enhanced diagnostics
    bool rawBlue = !digitalRead(PIN_BUTTON_BLUE);
    bool rawRed = !digitalRead(PIN_BUTTON_RED);  // Now pin 18
    bool rawGreen = !digitalRead(PIN_BUTTON_GREEN);
    
    // Only update if reading is stable (noise filtering)
    static bool lastRawRed = false;
    static int redNoiseCount = 0;
    
    if (rawRed != lastRawRed) {
      redNoiseCount++;
      if (redNoiseCount > 3) {  // Require 3 consistent readings
        btRed = rawRed;
        redNoiseCount = 0;
      }
    } else {
      btRed = rawRed;
      redNoiseCount = 0;
    }
    lastRawRed = rawRed;
    
    // Normal assignment for other buttons
    btBlue = rawBlue;
    btGreen = rawGreen;
    
    // SETTINGS BUTTON COMPLETELY IGNORED DURING GAMEPLAY
    // Sound is permanently disabled for power efficiency
    // Settings button only checked during startup for WiFi configuration
    
    // Debug output disabled for performance
    
  } else {
    // Don't change button states between debounce intervals
    // This prevents the same press from being detected multiple times
    return;
  }
}

// --------------------------------------------------------------------------
// 6. GAME LOGIC FUNCTIONS  
// --------------------------------------------------------------------------
void saveHighscores() {
  preferences.begin("game", false);
  for (int i = 0; i < 5; i++) {
    String key = "hs" + String(i);
    preferences.putInt(key.c_str(), highScores[i]);
  }
  preferences.end();
}

void loadHighscores() {
  preferences.begin("game", true);
  for (int i = 0; i < 5; i++) {
    String key = "hs" + String(i);
    highScores[i] = preferences.getInt(key.c_str(), 0);
  }
  preferences.end();
}

bool addHighScore(int score) {
  bool newRecord = false;
  for (int i = 0; i < 5; i++) {
    if (score > highScores[i]) {
      // Shift scores down
      for (int j = 4; j > i; j--) {
        highScores[j] = highScores[j-1];
      }
      highScores[i] = score;
      newRecord = true;
      break;
    }
  }
  if (newRecord) saveHighscores();
  return newRecord;
}

void resetGame() {
  currentLevel = config_start_level;
  currentScore = 0;
  livesRemaining = 3;
  won = false;
  playerPosition = 0;
  enemyFrontIndex = config_num_leds - 1;
  updateLevelPalette(); // Set palette for starting level
  
  shots.clear();
  enemies.clear();
  bossSegments.clear();
  bossProjectiles.clear();
}

void checkWinCondition() {
  won = false;
  
  // Environmental game win condition - all pollution defeated
  if (currentState == STATE_ENVIRONMENTAL_GAME) {
    bool allPollutionDefeated = true;
    for (int strip = 0; strip < NUM_STRIPS; strip++) {
      for (const auto& enemy : pollutionEnemies[strip]) {
        if (enemy.active) {
          allPollutionDefeated = false;
          break;
        }
      }
      if (!allPollutionDefeated) break;
    }
    
    if (allPollutionDefeated) {
      won = true;
      currentState = STATE_ENVIRONMENTAL_WIN;
      stateStartTime = millis();
      
      // Immediate wireless victory notification
      if (espnowReady) {
        sendDisplayCommand(1, 255, 255, 0, 0, 0, 0);  // Yellow victory flash
      }
    }
  }
}

void checkLoseCondition() {
  // Environmental game loss condition - pollution reaches top
  if (currentState == STATE_ENVIRONMENTAL_GAME) {
    for (int strip = 0; strip < NUM_STRIPS; strip++) {
      for (const auto& enemy : pollutionEnemies[strip]) {
        if (enemy.active && enemy.position >= LEDS_PER_STRIP - 1) {
          livesRemaining--;
          if (livesRemaining <= 0) {
            currentState = STATE_DATA_ERROR; // Reuse as game over state
            stateStartTime = millis();
            
            // Immediate wireless game over notification
            if (espnowReady) {
              sendDisplayCommand(1, 255, 0, 0, 0, 0, 0);    // Red game over flash
            }
          }
          return;
        }
      }
    }
  }
}

void spawnEnemies() {
  enemies.clear();
  enemyFrontIndex = config_num_leds - 1; // Reset enemy front position to start of strip
  int numEnemies = 5 + currentLevel * 2;
  if (numEnemies > 20) numEnemies = 20;
  
  for (int i = 0; i < numEnemies; i++) {
    Enemy enemy;
    enemy.position = enemyFrontIndex + i * 10;
    enemy.color = random(1, 4); // Randomize colors (RGB only)
    enemy.active = true;
    enemy.speed = 0.2 + currentLevel * 0.1;
    enemy.originalIndex = i;
    enemies.push_back(enemy);
  }
}

void updateLevelIntro() {
  // DISABLED for environmental game - not used in environmental state machine
  /*
  // Create fireworks celebration at start of level intro
  static bool fireworksCreated = false;
  if (!fireworksCreated) {
    sparks.clear();
    fireworks.clear();
    createLevelFireworks(5);  // Create 5 fireworks for celebration
    fireworksCreated = true;
  }
  
  if (millis() - stateStartTime > 3000) {
    fireworksCreated = false;  // Reset for next level
    if (currentLevel % 5 == 0) {
      // Boss level
      bossSegments.clear(); enemies.clear(); shots.clear(); bossProjectiles.clear();
      currentState = STATE_BOSS_INTRO;
      stateStartTime = millis();
    } else {
      // Regular level
      enemies.clear(); shots.clear(); bossProjectiles.clear();
      spawnEnemies();
      currentState = STATE_PLAYING;
      levelStartTime = millis();
    }
  }
  */
}

void spawnBoss() {
  bossSegments.clear();
  BossConfig cfg;
  int bossNumber = currentLevel / 5;
  
  switch(bossNumber) {
    case 1: cfg = boss1Cfg; break;
    case 2: cfg = boss2Cfg; break;
    default: cfg = boss3Cfg; break;
  }
  
  // Position boss segments so they fit on the strip, starting from the far end
  int totalBossLength = cfg.numSegments * 5;
  enemyFrontIndex = config_num_leds - totalBossLength; // Start boss so it fits on strip
  
  Serial.printf("Spawning boss %d with %d segments, positioning from %d to %d\n", 
                bossNumber, cfg.numSegments, enemyFrontIndex, config_num_leds-1);
  
  for (int i = 0; i < cfg.numSegments; i++) {
    BossSegment seg;
    seg.position = enemyFrontIndex + i * 5;
    seg.color = random(1, 4); // Randomize boss colors too
    seg.active = true;
    seg.originalIndex = i;
    bossSegments.push_back(seg);
    Serial.printf("Boss segment %d at position %.1f, color %d\n", i, seg.position, seg.color);
  }
  
  boss2State = B2_CHARGE;
  boss3State = B3_SPIN;
  boss2Section = 0;
  bossActionTimer = millis();
}

void updateBossIntro() {
  // DISABLED for environmental game - not used in environmental state machine
  /*
  // Simplified boss intro for debugging - shortened to 1 second
  if (millis() - stateStartTime > 1000) {
    spawnBoss();
    currentState = STATE_BOSS_PLAYING;
    levelStartTime = millis();  // Set level start time for boss levels
    stateStartTime = millis();
  }
  */
}

void moveBossProjectiles(float deltaTime) {
  for(int i=bossProjectiles.size()-1; i>=0; i--) {
    bossProjectiles[i].position -= 50 * deltaTime;
    if (bossProjectiles[i].position < 0) {
      bossProjectiles.erase(bossProjectiles.begin() + i);
    }
  }
}

void loadColors() {
  preferences.begin("colors", true);
  // Load hex color strings and convert to CRGB
  String hex_c1 = preferences.getString("c1", "0000FF");
  String hex_c2 = preferences.getString("c2", "FF0000"); 
  String hex_c3 = preferences.getString("c3", "00FF00");
  String hex_c4 = preferences.getString("c4", "FFFFFF");
  String hex_c5 = preferences.getString("c5", "FFFF00");
  String hex_c6 = preferences.getString("c6", "FF00FF");
  String hex_cw = preferences.getString("cw", "FFA500");
  String hex_cb = preferences.getString("cb", "800080");
  
  c1 = CRGB(strtol(hex_c1.c_str(), NULL, 16));
  c2 = CRGB(strtol(hex_c2.c_str(), NULL, 16));
  c3 = CRGB(strtol(hex_c3.c_str(), NULL, 16));
  c4 = CRGB(strtol(hex_c4.c_str(), NULL, 16));
  c5 = CRGB(strtol(hex_c5.c_str(), NULL, 16));
  c6 = CRGB(strtol(hex_c6.c_str(), NULL, 16));
  cWall = CRGB(strtol(hex_cw.c_str(), NULL, 16));
  cBoss = CRGB(strtol(hex_cb.c_str(), NULL, 16));
  
  preferences.end();
}

void handleSaveColors() {
  String hex_c1, hex_c2, hex_c3, hex_c4, hex_c5, hex_c6, hex_cw, hex_cb;
  
  if(server.hasArg("c1")) hex_c1 = server.arg("c1");
  if(server.hasArg("c2")) hex_c2 = server.arg("c2");
  if(server.hasArg("c3")) hex_c3 = server.arg("c3");
  if(server.hasArg("c4")) hex_c4 = server.arg("c4");
  if(server.hasArg("c5")) hex_c5 = server.arg("c5");
  if(server.hasArg("c6")) hex_c6 = server.arg("c6");
  if(server.hasArg("cw")) hex_cw = server.arg("cw");
  if(server.hasArg("cb")) hex_cb = server.arg("cb");
  
  preferences.begin("colors", false);
  preferences.putString("c1", hex_c1);
  preferences.putString("c2", hex_c2);
  preferences.putString("c3", hex_c3);
  preferences.putString("c4", hex_c4);
  preferences.putString("c5", hex_c5);
  preferences.putString("c6", hex_c6);
  preferences.putString("cw", hex_cw);
  preferences.putString("cb", hex_cb);
  preferences.end();
  
  loadColors();
  
  server.sendHeader("Location", "/colors");
  server.send(303);
}

// Audio configuration functions removed - no longer needed

void saveCurrentToPreferences(String prefix) {
  preferences.begin("game", false);
  preferences.putInt((prefix + "_num_leds").c_str(), config_num_leds);
  preferences.putInt((prefix + "_brightness").c_str(), config_brightness_pct);
  preferences.putInt((prefix + "_start_level").c_str(), config_start_level);
  preferences.putString((prefix + "_ssid").c_str(), config_ssid);
  preferences.putString((prefix + "_pass").c_str(), config_pass);
  preferences.putString((prefix + "_ip").c_str(), config_ip);
  preferences.putString((prefix + "_gateway").c_str(), config_gateway);
  preferences.putString((prefix + "_subnet").c_str(), config_subnet);
  preferences.putString((prefix + "_dns").c_str(), config_dns);
  preferences.putBool((prefix + "_static_ip").c_str(), config_static_ip);
  preferences.end();
}

void performFactoryReset() {
  preferences.begin("game", true);
  preferences.clear();
  preferences.end();
  preferences.begin("colors", true);
  preferences.clear();
  preferences.end();
  preferences.begin("snds", true);
  preferences.clear();
  preferences.end();
  
  // Reset to defaults - reduced for stability
  config_num_leds = 100;
  config_brightness_pct = 50;
  config_start_level = 1;
  config_ssid = "1DEnviroGame";
  config_pass = "password123";
  for (int i = 0; i < 5; i++) highScores[i] = 0;
  loadColors();
}

void loadConfig(String prefix) {
  preferences.begin("game", true);
  config_num_leds = preferences.getInt((prefix + "_num_leds").c_str(), 100);  // Increased default
  config_brightness_pct = preferences.getInt((prefix + "_brightness").c_str(), 50);  // Reduced default
  config_start_level = preferences.getInt((prefix + "_start_level").c_str(), 1);
  config_ssid = preferences.getString((prefix + "_ssid").c_str(), "1DEnviroGame");
  config_pass = preferences.getString((prefix + "_pass").c_str(), "password123");
  config_ip = preferences.getString((prefix + "_ip").c_str(), "192.168.4.1");
  config_gateway = preferences.getString((prefix + "_gateway").c_str(), "192.168.4.1");
  config_subnet = preferences.getString((prefix + "_subnet").c_str(), "255.255.255.0");
  config_dns = preferences.getString((prefix + "_dns").c_str(), "192.168.4.1");
  config_static_ip = preferences.getBool((prefix + "_static_ip").c_str(), false);
  currentProfilePrefix = prefix;
  preferences.end();
}

void handleProfileSwitch() {
  if (server.hasArg("profile")) {
    String newProfile = server.arg("profile");
    saveCurrentToPreferences(currentProfilePrefix);
    preferences.begin("game", false); preferences.putString("act_prof", currentProfilePrefix); preferences.end();
    loadConfig(newProfile);
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleReset() { performFactoryReset(); server.send(200, "text/html", "<h2>Reset successful!</h2><p>Values & Scores wiped. ESP restarting.</p>"); delay(1000); ESP.restart(); }

void handleSave() {
  if (server.hasArg("leds")) config_num_leds = server.arg("leds").toInt(); if (server.hasArg("bright")) config_brightness_pct = server.arg("bright").toInt(); if (server.hasArg("startlvl")) config_start_level = server.arg("startlvl").toInt();
  if (server.hasArg("ssid")) config_ssid = server.arg("ssid"); if (server.hasArg("pass")) config_pass = server.arg("pass");
  config_static_ip = server.hasArg("static_ip"); config_ip = server.arg("ip"); config_gateway = server.arg("gw"); config_subnet = server.arg("sn"); config_dns = server.arg("dns");
  
  // Console board doesn't have LEDs - they're on M5Stack display board
  // LED visualization handled wirelessly via ESP-NOW
  // Legacy LED array kept for game logic compatibility
  if (leds) delete[] leds;
  leds = new CRGB[config_num_leds];
  // FastLED.addLeds<LED_TYPE, PIN_LED_DATA, COLOR_ORDER>(leds, config_num_leds); // Disabled - no LEDs on console
  // FastLED.setBrightness((config_brightness_pct * 255) / 100); // Disabled - no LEDs on console
  
  preferences.begin("game", false);
  preferences.putInt("num_leds", config_num_leds);
  preferences.putInt("brightness", config_brightness_pct);
  preferences.putInt("start_level", config_start_level);
  preferences.putString("ssid", config_ssid);
  preferences.putString("pass", config_pass);
  preferences.putString("ip", config_ip);
  preferences.putString("gateway", config_gateway);
  preferences.putString("subnet", config_subnet);
  preferences.putString("dns", config_dns);
  preferences.putBool("static_ip", config_static_ip);
  preferences.end();
  
  server.sendHeader("Location", "/");
  server.send(303);
}

CRGB getColor(int colorIndex) {
  switch(colorIndex) {
    case 1: return c1;
    case 2: return c2;
    case 3: return c3;
    case 4: return c4;
    case 5: return c5;
    case 6: return c6;
    case 7: return cWall;
    case 8: return cBoss;
    default: return CRGB::Black;
  }
}

// Enhanced color system with palettes
void updateLevelPalette() {
  if (currentLevel <= 3) currentPalette = PartyColors_p;
  else if (currentLevel <= 7) currentPalette = HeatColors_p; 
  else if (currentLevel <= 12) currentPalette = OceanColors_p;
  else currentPalette = LavaColors_p;
}

CRGB getEnhancedColor(int colorIndex) {
  uint8_t paletteIndex = map(colorIndex, 1, 3, 0, 240);
  return ColorFromPalette(currentPalette, paletteIndex + gHue, 255, currentBlending);
}

uint8_t breathingBrightness() {
  return beatsin8(12, 80, 255); // Slow breathing effect
}

void enableWiFi() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(config_ssid.c_str(), config_pass.c_str());
  
  if (config_static_ip) {
    IPAddress ip, gateway, subnet, dns;
    ip.fromString(config_ip);
    gateway.fromString(config_gateway);
    subnet.fromString(config_subnet);
    dns.fromString(config_dns);
    WiFi.softAPConfig(ip, gateway, subnet);
  }
  
  server.begin();
  
  // Setup web server routes for AQI input
  setupWebServerRoutes();
  
  Serial.print("WiFi AP started: ");
  Serial.println(WiFi.softAPIP());
}

// --------------------------------------------------------------------------
// 7. MAIN SETUP AND LOOP
// --------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("1D RGB Invader Game - Starting...");
  
#ifdef QTPY_S3
  // Adafruit QT Py S3 specific initialization
  Serial.println("🚀 Adafruit QT Py S3 with PSRAM detected!");
  Serial.printf("💡 NeoPixel pin defined as GPIO: %d\n", NEOPIXEL_PIN);
  
  // Test the LED immediately to verify it works
  Serial.println("🔧 Testing NeoPixel LED...");
  neopixelWrite(NEOPIXEL_PIN, 255, 0, 0);  // Red test
  delay(500);
  neopixelWrite(NEOPIXEL_PIN, 0, 255, 0);  // Green test
  delay(500);
  neopixelWrite(NEOPIXEL_PIN, 0, 0, 255);  // Blue test - startup color
  Serial.println("✅ NeoPixel test complete - LED should be blue now");
  
  // Check PSRAM availability
  if (psramFound()) {
    Serial.printf("🧠 PSRAM found: %d bytes total, %d bytes free\n", ESP.getPsramSize(), ESP.getFreePsram());
  } else {
    Serial.println("⚠️ PSRAM not found - using internal RAM only");
  }
  
  // Initialize built-in NeoPixel for status indication
  // QT Py S3 board support handles power automatically
  Serial.printf("🔧 Setting NeoPixel (pin %d) to startup blue...\n", NEOPIXEL_PIN);
  neopixelWrite(NEOPIXEL_PIN, 0, 0, 255);  // Start with blue status
  Serial.println("💡 Built-in NeoPixel enabled - should be showing blue now");
  delay(2000);  // Show blue status for 2 seconds so it's very visible;
  
#else
  Serial.println("📟 ESP32-WROOM board detected");
#endif
  
  Serial.printf("Free heap at start: %d bytes\n", ESP.getFreeHeap());
  
  // Initialize buttons with pullups and extra setup for problematic pins
  pinMode(PIN_BUTTON_BLUE, INPUT_PULLUP);
  pinMode(PIN_BUTTON_RED, INPUT_PULLUP);   // Now pin 18 - red button for red shots
  pinMode(PIN_BUTTON_GREEN, INPUT_PULLUP);
  pinMode(PIN_SOUND_TOGGLE, INPUT_PULLUP);
  
  // Extra pullup configuration for GPIO16 (red button) if it's problematic
  gpio_pullup_en((gpio_num_t)PIN_BUTTON_RED);
  gpio_pulldown_dis((gpio_num_t)PIN_BUTTON_RED);
  
  Serial.println("🔧 Button pins initialized with enhanced pullups");
  
  // STARTUP SETTINGS BUTTON CHECK - WiFi only activated if button held during boot
  Serial.println("⏱️  Checking for settings button during startup...");
  Serial.println("🔘 Hold settings button (sound toggle) to enter AQI data configuration mode");
  
  // Brief window to check settings button
  bool settingsRequested = false;
  for (int i = 0; i < 20; i++) {  // Check for 2 seconds (20 * 100ms)
    if (digitalRead(PIN_SOUND_TOGGLE) == LOW) {
      settingsRequested = true;
      Serial.printf("🔘 Settings button detected! (%d/20)\n", i+1);
    }
    
    // Visual feedback during button check
    if (i % 4 == 0) {
      setStatusLED(255, 255, 0);  // Yellow flash during button check
    } else {
      setStatusLED(0, 0, 0);      // Off
    }
    
    delay(100);
  }
  
  if (settingsRequested) {
    Serial.println("🎛️  SETTINGS MODE ACTIVATED!");
    Serial.println("📶 Starting WiFi Access Point for AQI data entry...");
    setStatusLED(0, 255, 255);  // Cyan - entering settings mode
    
    // Initialize WiFi for settings
    initWiFiConnection();  // This will set up the web interface
    
    // Stay in settings mode - main loop will handle web server
    currentState = STATE_WIFI_CONNECTING;  // Use existing WiFi state
    Serial.println("🌐 Settings mode ready - connect to WiFi AP to enter AQI data");
    Serial.println("⚠️  Device will automatically restart after saving settings");
    
  } else {
    Serial.println("🎮 Normal startup mode - no settings button detected");
    Serial.println("💡 Continuing with ESP-NOW gaming mode");
    setStatusLED(0, 255, 0);    // Green - normal mode
    delay(1500);  // Extended time to show green status
    
    // Initialize ESP-NOW for display communication (low power WiFi mode)
    WiFi.mode(WIFI_STA);  // Station mode required for ESP-NOW
    WiFi.disconnect();     // Don't connect to any AP
    initESPNOW();         // Enable wireless display communication
    
    wifiConnected = false;     // No AP/STA WiFi connections
    useAccessPointMode = false;
    
    Serial.println("📡 ESP-NOW enabled for display communication");
    Serial.println("🚫 High-power WiFi modes disabled for efficiency");
  }
  
  // AUDIO DISABLED: I2S amplifier disabled to save power for WiFi radio
  // pinMode(I2S_SD, OUTPUT);
  // digitalWrite(I2S_SD, HIGH);  // Enable amplifier (SD pin active-low)
  Serial.println("⚡ I2S amplifier DISABLED to prevent WiFi brownout");
  
  // Load configuration
  preferences.begin("game", true);
  config_num_leds = preferences.getInt("num_leds", 300);  // Full 300 LEDs
  config_brightness_pct = preferences.getInt("brightness", 40);  // Moderate brightness
  config_start_level = preferences.getInt("start_level", 1);
  config_ssid = preferences.getString("ssid", "1DEnviroGame");
  config_pass = preferences.getString("pass", "password123");
  config_ip = preferences.getString("ip", "192.168.4.1");
  config_gateway = preferences.getString("gateway", "192.168.4.1");
  config_subnet = preferences.getString("subnet", "255.255.255.0");
  config_dns = preferences.getString("dns", "192.168.4.1");
  config_static_ip = preferences.getBool("static_ip", false);
  config_sound_on = false; // FORCE DISABLE AUDIO to save power for WiFi
  config_volume_pct = preferences.getInt("volume", 50);
  Serial.println("⚡ Audio system DISABLED to prevent brownout during WiFi init");
  currentProfilePrefix = preferences.getString("act_prof", "default");
  preferences.end();
  Serial.printf("Config loaded - LEDs: %d, Brightness: %d%%\n", config_num_leds, config_brightness_pct);
  
  // Check if we have enough memory for LED array
  size_t ledMemoryNeeded = config_num_leds * sizeof(CRGB);
  Serial.printf("LED array needs %d bytes\n", ledMemoryNeeded);
  Serial.printf("Free heap before LED allocation: %d bytes\n", ESP.getFreeHeap());
  
  // Skip old single-strip initialization - using 3-strip environmental setup
  // Initialize LED strips will be done in initLEDStrips()
  Serial.println("Preparing for 3-strip environmental configuration");
  
  Serial.printf("Free heap after LED init: %d bytes\n", ESP.getFreeHeap());
  
  // Add watchdog feed
  delay(100);
  
  // Load data
  loadHighscores();
  loadColors();
  
  // Initialize Environmental Game
  initLEDStrips();
  
  // Load saved AQI data first - this restores persistent data
  loadAQIData();
  
  // If we have valid saved data, prepare to start game immediately
  if (currentAQI.dataValid) {
    Serial.println("✅ Valid AQI data loaded from storage - ready for immediate gameplay!");
    Serial.printf("📊 Loaded data: PM2.5=%d, NO₂=%d, O₃=%d from %s\n", 
                  currentAQI.pm25, currentAQI.no2, currentAQI.o3, currentAQI.city.c_str());
  } else {
    Serial.println("❌ No valid saved AQI data - will need to fetch or input manually");
    currentAQI.dataValid = false;  // Ensure it's false if no saved data
    currentAQI.lastUpdate = 0;
  }
  
  // Audio completely disabled for power management
  config_sound_on = false;
  Serial.println("⚡ Audio system DISABLED - power reserved for communication");
  
  // Set game data based on mode
  if (wifiConnected || useAccessPointMode) {
    // Settings mode - WiFi is active, wait for data input
    Serial.println("🌐 Settings mode - waiting for AQI data input via web interface");
    Serial.println("💾 After settings are saved, device will restart automatically");
    
  } else {
    // Normal gaming mode - use saved or default data
    if (currentAQI.dataValid) {
      Serial.println("🚀 Starting game with saved AQI data!");
      Serial.printf("📊 Using data: PM2.5=%d, NO₂=%d, O₃=%d from %s\n", 
                    currentAQI.pm25, currentAQI.no2, currentAQI.o3, currentAQI.city.c_str());
    } else {
      Serial.println("🎮 No saved data - creating default pollution scenario...");
      currentAQI.pm25 = 25;  // Moderate pollution defaults
      currentAQI.no2 = 15;
      currentAQI.o3 = 20;
      currentAQI.city = "Demo City";
      currentAQI.dataValid = true;
      currentAQI.lastUpdate = millis();
      Serial.printf("📊 Using defaults: PM2.5=%d, NO₂=%d, O₃=%d\n", 
                    currentAQI.pm25, currentAQI.no2, currentAQI.o3);
    }
    
    prepareColumnAnimation();
    currentState = STATE_COLUMN_FILLING;  // Start column animation before game
  }
  
  stateStartTime = millis();
  
  Serial.println("🎮 Environmental Game Mode with Wireless Display");
  if (espnowReady) {
    Serial.println("📡 ESP-NOW display communication ACTIVE");
    Serial.printf("🔗 Connected to display board: %02x:%02x:%02x:%02x:%02x:%02x\n", 
                  displayUnitMAC[0], displayUnitMAC[1], displayUnitMAC[2], 
                  displayUnitMAC[3], displayUnitMAC[4], displayUnitMAC[5]);
  } else {
    Serial.println("⚠️  ESP-NOW display communication failed to initialize");
  }
  
  Serial.printf("Final free heap: %d bytes\n", ESP.getFreeHeap());
  Serial.printf("Current CPU frequency: %d MHz\n", getCpuFrequencyMhz());
  
  Serial.println("🎮 Environmental Air Quality Mining Game Ready!");
  Serial.println("🎯 Goal: Clear all pollution columns by shooting matching colors!");
  
#ifdef QTPY_S3
  if (wifiConnected || useAccessPointMode) {
    setStatusLED(0, 255, 255);  // Cyan - settings mode
    Serial.println("💡 Status LED: Cyan (settings mode active)");
  } else {
    // Brief "ready" flash before power saving
    setStatusLED(255, 255, 255);  // White flash - system ready
    delay(200);
    setStatusLED(0, 0, 0);        // Brief off
    delay(100);
    setStatusLED(255, 255, 255);  // Second flash
    delay(200);
    
    statusLEDOff();               // Turn off LED completely for power saving
    Serial.println("💡 Status LED: Ready flash completed - entering power saving mode");
  }
#endif
  
  Serial.println("⚡ Startup optimized for power efficiency");
  
  // Show appropriate instructions based on mode
  if (wifiConnected || useAccessPointMode) {
    Serial.println("🌐 WiFi SETTINGS MODE ACTIVE:");
    Serial.println("  - Connect to WiFi access point to enter AQI data");
    Serial.println("  - Device will restart automatically after saving settings");
    Serial.println("  - Next boot will start in efficient gaming mode");
  } else {
    Serial.println("🎮 OPTIMIZED GAMING MODE:");
    Serial.println("  - Environmental shooting game with maximum power efficiency");
    Serial.println("  - WiFi disabled to prevent overheating and power issues");
    Serial.println("  - LED feedback for all game interactions");
    Serial.println("  - Serial output shows detailed game progress");
  }
  
  Serial.println();
  Serial.println("🔘 STARTUP SETTINGS BUTTON:");
  Serial.println("  - Hold settings button during power-on to enter AQI data mode");
  Serial.println("  - Normal startup (no button) = efficient gaming mode");
  Serial.println("  - Settings button ignored during gameplay for smooth performance");
  Serial.println();
  Serial.println("🕹️  GAME CONTROLS:");
  Serial.println("  - Blue button: Shoot blue projectiles at PM2.5 particles");
  Serial.println("  - Red button: Shoot red projectiles at NO₂ particles"); 
  Serial.println("  - Green button: Shoot green projectiles at O₃ particles");
  Serial.println("  - Settings button: ONLY checked during startup");
  Serial.println("  - Hold any shot button longer = more powerful cannon blast!");
  Serial.println();
}

#ifdef QTPY_S3
// Status LED functions for QT Py S3 built-in NeoPixel
void setStatusLED(uint8_t r, uint8_t g, uint8_t b) {
  // Use standard Arduino board definitions and ESP32-S3's built-in neopixelWrite()
  Serial.printf("🎨 Setting LED to RGB(%d,%d,%d) on pin %d\n", r, g, b, NEOPIXEL_PIN);
  neopixelWrite(NEOPIXEL_PIN, r, g, b);
  delay(50);  // Brief delay to ensure command is processed
}

void statusLEDOff() {
  // Multiple attempts to ensure LED is completely off
  Serial.printf("🚫 Turning off LED on pin %d...\n", NEOPIXEL_PIN);
  neopixelWrite(NEOPIXEL_PIN, 0, 0, 0);
  delay(100);  
  neopixelWrite(NEOPIXEL_PIN, 0, 0, 0);  // Second attempt
  delay(100);
  
  Serial.println("🚫 LED turned off using explicit pin definition (double attempt)");
}
#endif

void loop() {
  static unsigned long lastUpdate = 0;
  unsigned long now = millis();
  float deltaTime = (now - lastUpdate) / 1000.0f;
  
  // Cap deltaTime to prevent large jumps that could cause piercing
  if (deltaTime > 0.033f) deltaTime = 0.033f;  // Max 30 FPS worth of movement
  
  lastUpdate = now;
  
  // Handle WiFi and web server
  server.handleClient();
  
  // Periodically refresh AQI data during gameplay
  static unsigned long lastDataRefresh = 0;
  if (currentState == STATE_ENVIRONMENTAL_GAME && 
      millis() - lastDataRefresh > UPDATE_INTERVAL && 
      wifiConnected) {
    Serial.println("🔄 Refreshing AQI data...");
    if (fetchAirQualityData()) {
      Serial.println("✓ AQI data updated - pollution levels may have changed!");
    }
    lastDataRefresh = millis();
  }
  
  readButtons();
  
  // Debug current state every 5 seconds
  static unsigned long lastStateDebug = 0;
  if (millis() - lastStateDebug > 5000) {
    lastStateDebug = millis();
    Serial.printf("🎮 Current State: %d, Enemies on strips: %d,%d,%d, Active shots: %d\n", 
                  currentState, pollutionEnemies[0].size(), pollutionEnemies[1].size(), 
                  pollutionEnemies[2].size(), environmentalShots.size());
    Serial.printf("📡 ESP-NOW: %s, Display: %s\n", 
                  espnowReady ? "Ready" : "Disabled", 
                  displayUnitConnected ? "Connected" : "Disconnected");
  }
  
  // Environmental Game State Machine
  switch(currentState) {
    case STATE_WIFI_CONNECTING:
      // Skip WiFi logic if in Access Point mode
      if (useAccessPointMode) {
        // In Access Point mode - check if we should try to start game
        if (currentAQI.dataValid) {
          Serial.println("✅ Access Point mode with valid data - starting game!");
          prepareColumnAnimation();
        }
        // Otherwise wait for user input via web interface
        break;
      }
      
      // Wait for WiFi connection to complete
      if (wifiConnected) {
        Serial.println("✅ WiFi connected! Fetching real AQI data...");
        currentState = STATE_FETCHING_DATA;
        stateStartTime = millis();
      } else {
        // Check if enough time has passed since WiFi connection attempt
        static bool fallbackTriggered = false;
        if (!fallbackTriggered && (millis() - stateStartTime > 3000)) {
          fallbackTriggered = true;
          Serial.println("⚠️ WiFi failed - switching to demo data for immediate gameplay");
          Serial.println("🎮 Loading demo pollution data...");
          Serial.println("📊 DEMO MODE ACTIVE - Using simulated pollution data");
          currentAQI.pm25 = 32;
          currentAQI.no2 = 28;
          currentAQI.o3 = 45;
          currentAQI.city = "Barcelona, Spain";
          currentAQI.dataValid = true;
          currentAQI.lastUpdate = millis();
          prepareColumnAnimation();
        }
      }
      break;
      
    case STATE_FETCHING_DATA:
      // Fetch real AQI data from API
      if (fetchAirQualityData()) {
        Serial.println("✅ Real AQI data loaded successfully!");
        prepareColumnAnimation();
      } else if (millis() - stateStartTime > 10000) {
        // API timeout after 10 seconds - use Barcelona data
        Serial.println("⚠️ AQI API timeout - using Barcelona data as fallback");
        currentAQI.pm25 = 32;
        currentAQI.no2 = 28;
        currentAQI.o3 = 45;
        currentAQI.dataValid = true;
        currentAQI.city = "Barcelona, Spain";
        currentAQI.lastUpdate = millis();
        prepareColumnAnimation();
      }
      break;
      
    case STATE_COLUMN_FILLING:
      // Animate columns filling up from bottom
      if (millis() - lastColumnUpdate > 50) {  // Update every 50ms for smooth animation
        lastColumnUpdate = millis();
        bool allComplete = true;
        
        for (int strip = 0; strip < NUM_STRIPS; strip++) {
          if (currentColumnHeights[strip] < targetColumnHeights[strip]) {
            currentColumnHeights[strip]++;
            allComplete = false;
          }
        }
        
        if (allComplete) {
          columnAnimationComplete = true;
          createPollutionEnemies();  // Now create the actual enemy objects
          Serial.println("✨ Column animation complete - ready to mine!");
        }
      }
      break;
      
    case STATE_ENVIRONMENTAL_GAME:
      // Handle button charging and shot logic
      {
        // Get current button states
        bool currentButtons[NUM_STRIPS] = {btBlue, btRed, btGreen};
        
        // Process each button for charging logic
        for (int strip = 0; strip < NUM_STRIPS; strip++) {
          if (!stripCompleted[strip]) {  // Only if strip not completed
            bool currentButton = currentButtons[strip];
            
            // Button press detected (edge from false to true)
            if (currentButton && !lastButtonState[strip]) {
              buttonPressTime[strip] = millis();
              buttonCharging[strip] = true;
              // Removed debug output for performance
            }
            
            // Button released
            if (!currentButton && lastButtonState[strip]) {
              if (buttonCharging[strip]) {
                unsigned long holdTime = millis() - buttonPressTime[strip];
                
                if (holdTime >= CANNON_CHARGE_TIME) {
                  // CANNON SHOT! 3x power
                  EnvironmentalShot shot;
                  shot.position = LEDS_PER_STRIP - 1;
                  shot.velocity = -shotInitialVelocity;
                  shot.color = strip + 1;  // Color matches strip
                  shot.stripIndex = strip;
                  shot.isCannonShot = true;
                  shot.size = 3;  // 3 pixels wide
                  shot.damage = 4;  // 4x damage
                  
                  environmentalShots.push_back(shot);
                  playerRecoil[strip] = -4.0f;  // Forward spring for cannon
                  playCannonShotSound();
                  
                  // Removed debug output for performance
                } else {
                  // Normal shot (released before full charge)
                  EnvironmentalShot shot;
                  shot.position = LEDS_PER_STRIP - 1;
                  shot.velocity = -shotInitialVelocity;
                  shot.color = strip + 1;
                  shot.stripIndex = strip;
                  shot.isCannonShot = false;
                  shot.size = 1;
                  shot.damage = 1;
                  
                  environmentalShots.push_back(shot);
                  playerRecoil[strip] = -2.0f;  // Forward spring for normal shot
                  playShotSound(strip + 1);
                  
                  // Instant wireless display feedback for button press
                  if (espnowReady) {
                    uint8_t r = (strip == 1) ? 255 : 0;      // Red for NO2 strip
                    uint8_t g = (strip == 2) ? 255 : 0;      // Green for O3 strip 
                    uint8_t b = (strip == 0) ? 255 : 0;      // Blue for PM2.5 strip
                    sendDisplayCommand(3, r, g, b, 0, 20, strip);  // Command 3 = flash effect
                  }
                  
                  // Removed debug output for performance
                }
              }
              
              buttonCharging[strip] = false;
              // Reset charging sound timer when button released
              lastChargeSoundTime[strip] = 0;
            }
            
            // Update previous button state
            lastButtonState[strip] = currentButton;
          }
        }
      }
      
      updateEnvironmentalGame();
      updatePlayerEffects();  // Update hero recoil spring-back animation
      updateSparks(deltaTime);  // Update impact sparks
      updateFireworks(deltaTime);  // Update celebration fireworks
      
      // Check for delayed strip clearing fireworks (1 second after explosion)
      for (int strip = 0; strip < NUM_STRIPS; strip++) {
        if (delayedFireworkTime[strip] > 0 && millis() >= delayedFireworkTime[strip]) {
          createStripFirework(strip, false);  // Launch the delayed celebration firework
          delayedFireworkTime[strip] = 0;  // Clear the timer
        }
      }
      break;
      
    case STATE_DATA_ERROR:
      // Show error, then retry
      if (now - stateStartTime > 3000) {
        currentState = STATE_WIFI_CONNECTING;
        stateStartTime = now;
      }
      break;
      
    case STATE_ENVIRONMENTAL_WIN:
      // Show win state, then restart  
      if (now - stateStartTime > 4000) {
        currentState = STATE_FETCHING_DATA;
        stateStartTime = now;
      }
      break;
  }
  
  // --------------------------------------------------------------------------
  // 8. ENVIRONMENTAL RENDERING
  // --------------------------------------------------------------------------
  
  // Environmental Game Rendering
  switch(currentState) {
    case STATE_WIFI_CONNECTING:
      // No animation - immediately transition to gameplay
      break;
      
    case STATE_FETCHING_DATA:
      // Show data fetching animation
      {
        fill_solid(stripPM25, LEDS_PER_STRIP, CRGB::Black);
        fill_solid(stripNO2, LEDS_PER_STRIP, CRGB::Black);
        fill_solid(stripO3, LEDS_PER_STRIP, CRGB::Black);
        uint8_t brightness = beatsin8(20, 50, 255);
        for (int i = 0; i < LEDS_PER_STRIP; i++) {
          stripPM25[i] = CRGB(brightness, brightness/2, 0); // Orange pulse
          stripNO2[i] = CRGB(brightness, 0, 0);            // Red pulse
          stripO3[i] = CRGB(0, 0, brightness);             // Blue pulse
        }
      }
      break;
      
    case STATE_COLUMN_FILLING:
      // Show columns filling up from bottom with rock-falling effect
      {
        fill_solid(stripPM25, LEDS_PER_STRIP, CRGB::Black);
        fill_solid(stripNO2, LEDS_PER_STRIP, CRGB::Black);
        fill_solid(stripO3, LEDS_PER_STRIP, CRGB::Black);
        
        CRGB* strips[3] = {stripPM25, stripNO2, stripO3};
        unsigned long currentTime = millis();
        
        for (int strip = 0; strip < NUM_STRIPS; strip++) {
          // Fill from bottom to current height with character effects
          for (int i = 0; i < currentColumnHeights[strip]; i++) {
            CRGB pixelColor;
            
            // Apply character-specific animations during build-up too
            if (strip == 0) {  // PM2.5 Blue - watery pulse
              float pulse = sin(currentTime * 0.006f + i * 0.25f) * 0.3f + 0.7f;
              pixelColor = CRGB(0, 0, 255 * pulse);
            } else if (strip == 1) {  // NO₂ Red - fiery flicker
              if (random(0, 100) < 70) {
                pixelColor = CRGB(255, 0, 0);
              } else {
                uint8_t flicker = 200 + random(0, 55);
                pixelColor = CRGB(255, flicker/6, 0);
              }
            } else if (strip == 2) {  // O₃ Green - perlin-like noise
              float noise1 = sin(currentTime * 0.003f + i * 0.1f) * 0.5f + 0.5f;
              float noise2 = sin(currentTime * 0.005f + i * 0.07f + 1.57f) * 0.5f + 0.5f;
              float combined = (noise1 * 0.7f + noise2 * 0.3f);
              float modulation = 0.6f + combined * 0.4f;
              pixelColor = CRGB(0, 255 * modulation, 0);
            }
            
            strips[strip][i] = pixelColor;
          }
          
          // Add a bright "falling rock" effect at the top of the current column
          if (currentColumnHeights[strip] < targetColumnHeights[strip]) {
            int sparkPos = currentColumnHeights[strip];
            if (sparkPos >= 0 && sparkPos < LEDS_PER_STRIP) {
              CRGB rockColor;
              
              // Use character-specific colors for falling rock
              if (strip == 0) {  // PM2.5 Blue
                float brightness = 0.8f + sin(currentTime * 0.02f) * 0.2f;  // 0.6 to 1.0 range
                rockColor = CRGB(0, 0, 255 * brightness);
              } else if (strip == 1) {  // NO₂ Red  
                rockColor = CRGB(255, 10, 0);  // Bright red with tiny orange
              } else if (strip == 2) {  // O₃ Green
                float brightness = 0.8f + sin(currentTime * 0.02f) * 0.2f;  // 0.6 to 1.0 range
                rockColor = CRGB(0, 255 * brightness, 0);
              }
              
              strips[strip][sparkPos] = rockColor;
            }
          }
        }
        
        FastLED.show();
      }
      break;
      
    case STATE_ENVIRONMENTAL_GAME:
      renderEnvironmentalDisplay();
      break;
      
    case STATE_DATA_ERROR:
      // Show error state
      {
        fill_solid(stripPM25, LEDS_PER_STRIP, CRGB::Black);
        fill_solid(stripNO2, LEDS_PER_STRIP, CRGB::Black);
        fill_solid(stripO3, LEDS_PER_STRIP, CRGB::Black);
        uint8_t brightness = beatsin8(10, 0, 255);
        for (int i = 0; i < LEDS_PER_STRIP; i++) {
          stripPM25[i] = CRGB(brightness, 0, 0); // Red flash
          stripNO2[i] = CRGB(brightness, 0, 0);
          stripO3[i] = CRGB(brightness, 0, 0);
        }
      }
      break;
      
    case STATE_ENVIRONMENTAL_WIN:
      // Show win celebration
      {
        fill_solid(stripPM25, LEDS_PER_STRIP, CRGB::Black);
        fill_solid(stripNO2, LEDS_PER_STRIP, CRGB::Black);
        fill_solid(stripO3, LEDS_PER_STRIP, CRGB::Black);
        static uint8_t hue = 0;
        hue += 3;
        for (int i = 0; i < LEDS_PER_STRIP; i++) {
          stripPM25[i] = CHSV(hue + (i * 4), 255, 200);
          stripNO2[i] = CHSV(hue + (i * 4) + 85, 255, 200);
          stripO3[i] = CHSV(hue + (i * 4) + 170, 255, 200);
        }
      }
      break;
  }

  FastLED.show();
  
  // Update wireless display with current game state
  updateWirelessDisplay();

  // Feed watchdog to prevent boot loops
  delay(1);
}

// =============================================================================
// PERSISTENT STORAGE FOR AQI DATA
// =============================================================================

void saveAQIData() {
  preferences.begin("enviro-game", false);
  preferences.putInt("pm25", currentAQI.pm25);
  preferences.putInt("no2", currentAQI.no2);
  preferences.putInt("o3", currentAQI.o3);
  preferences.putString("city", currentAQI.city);
  preferences.putULong("lastUpdate", currentAQI.lastUpdate);
  preferences.putBool("dataValid", currentAQI.dataValid);
  preferences.end();
  
  Serial.println("💾 AQI data saved to persistent storage");
}

void loadAQIData() {
  preferences.begin("enviro-game", true); // Read-only
  
  if (preferences.isKey("pm25")) {
    currentAQI.pm25 = preferences.getInt("pm25", 32);
    currentAQI.no2 = preferences.getInt("no2", 28);
    currentAQI.o3 = preferences.getInt("o3", 45);
    currentAQI.city = preferences.getString("city", "Barcelona, Spain");
    currentAQI.lastUpdate = preferences.getULong("lastUpdate", millis());
    currentAQI.dataValid = preferences.getBool("dataValid", true);
    
    Serial.println("📂 Loaded AQI data from persistent storage:");
    Serial.printf("  PM2.5: %d, NO₂: %d, O₃: %d, Location: %s\n", 
                  currentAQI.pm25, currentAQI.no2, currentAQI.o3, currentAQI.city.c_str());
  } else {
    // No saved data, use Barcelona defaults
    Serial.println("📂 No stored AQI data found, using Barcelona defaults");
    currentAQI.pm25 = 32;
    currentAQI.no2 = 28;  
    currentAQI.o3 = 45;
    currentAQI.city = "Barcelona, Spain";
    currentAQI.dataValid = true;
    currentAQI.lastUpdate = millis();
  }
  
  preferences.end();
}

void clearStoredAQI() {
  preferences.begin("enviro-game", false);
  preferences.clear();
  preferences.end();
  
  Serial.println("🗑️ Cleared all stored AQI data");
}

// =============================================================================
// WEB SERVER INTERFACE FOR MANUAL AQI INPUT
// =============================================================================

void setupWebServerRoutes() {
  server.on("/", handleRoot);
  server.on("/aqi", handleAQIInput);
  server.on("/setaqi", HTTP_POST, handleSetAQI);
  server.on("/api-helper", handleAPIHelper);
  server.onNotFound([]() {
    server.send(404, "text/plain", "Page not found");
  });
}

void handleRoot() {
  // Handle auto-input from API helper extractor
  if (server.hasArg("auto_input") && server.hasArg("pm25") && server.hasArg("no2") && server.hasArg("o3")) {
    currentAQI.pm25 = server.arg("pm25").toInt();
    currentAQI.no2 = server.arg("no2").toInt();
    currentAQI.o3 = server.arg("o3").toInt();
    currentAQI.city = server.hasArg("city") ? server.arg("city") : "API Imported";
    currentAQI.dataValid = true;
    currentAQI.lastUpdate = millis();
    saveAQIData();
    
    Serial.printf("📊 Auto-imported AQI data: PM2.5=%d, NO₂=%d, O₃=%d from %s\n", 
                  currentAQI.pm25, currentAQI.no2, currentAQI.o3, currentAQI.city.c_str());
    prepareColumnAnimation();
  }
  
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Environmental Game - AQI Control</title>
    <style>
        body { font-family: Arial; margin: 20px; background: #f0f8ff; }
        .container { max-width: 600px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 4px 8px rgba(0,0,0,0.1); }
        h1 { color: #2e8b57; text-align: center; }
        .status { background: #e8f5e8; padding: 15px; border-radius: 5px; margin: 15px 0; border-left: 4px solid #4caf50; }
        .input-group { margin: 15px 0; }
        label { display: block; margin-bottom: 5px; font-weight: bold; color: #333; }
        input[type="number"] { width: 100%; padding: 10px; border: 1px solid #ddd; border-radius: 5px; font-size: 16px; }
        .btn { background: #4caf50; color: white; padding: 12px 24px; border: none; border-radius: 5px; cursor: pointer; font-size: 16px; width: 100%; margin: 10px 0; }
        .btn:hover { background: #45a049; }
        .fetch-btn { background: #2196f3; }
        .fetch-btn:hover { background: #1976d2; }
        .info { background: #fff3cd; padding: 10px; border-radius: 5px; margin: 10px 0; border-left: 4px solid #ffc107; }
        .pollutant { display: flex; align-items: center; margin: 10px 0; }
        .pollutant-color { width: 20px; height: 20px; border-radius: 50%; margin-right: 10px; }
        .pm25 { background-color: #4A90E2; }
        .no2 { background-color: #E24A4A; }
        .o3 { background-color: #7ED321; }
    </style>
</head>
<body>
    <div class="container">
        <h1>🌍 Environmental Air Quality Game</h1>
        
        <div class="status">
            <strong>Current AQI Data:</strong><br>
            <div class="pollutant"><div class="pollutant-color pm25"></div>PM2.5: )rawliteral" + String(currentAQI.pm25) + R"rawliteral( μg/m³</div>
            <div class="pollutant"><div class="pollutant-color no2"></div>NO₂: )rawliteral" + String(currentAQI.no2) + R"rawliteral( ppb</div>
            <div class="pollutant"><div class="pollutant-color o3"></div>O₃: )rawliteral" + String(currentAQI.o3) + R"rawliteral( ppb</div>
            <small>Location: )rawliteral" + currentAQI.city + R"rawliteral(</small>
        </div>
        
        <div class="info">
            💡 <strong>How to get AQI data:</strong><br>
            • Use the <a href="/api-helper" class="btn" style="display:inline; padding:5px 10px; font-size:14px; margin:0 5px;">🔗 API Helper</a> for easy data lookup<br>
            • Or visit <a href="https://waqi.info" target="_blank">waqi.info</a> manually<br>
            • Data is automatically saved and restored on reboot
        </div>
        
        <form action="/setaqi" method="POST">
            <div class="input-group">
                <label for="pm25">🔵 PM2.5 (Fine Particles) - μg/m³:</label>
                <input type="number" id="pm25" name="pm25" min="0" max="500" value=")rawliteral" + String(currentAQI.pm25) + R"rawliteral("  required>
                <small>Typical range: 0-200, Unhealthy: >55</small>
            </div>
            
            <div class="input-group">
                <label for="no2">🔴 NO₂ (Nitrogen Dioxide) - ppb:</label>
                <input type="number" id="no2" name="no2" min="0" max="200" value=")rawliteral" + String(currentAQI.no2) + R"rawliteral(" required>
                <small>Typical range: 0-100, Unhealthy: >100</small>
            </div>
            
            <div class="input-group">
                <label for="o3">🟢 O₃ (Ozone) - ppb:</label>
                <input type="number" id="o3" name="o3" min="0" max="300" value=")rawliteral" + String(currentAQI.o3) + R"rawliteral(" required>
                <small>Typical range: 0-150, Unhealthy: >70</small>
            </div>
            
            <input type="text" name="city" placeholder="Location name (optional)" value=")rawliteral" + currentAQI.city + R"rawliteral(">
            
            <button type="submit" class="btn">🎮 Update Game Data</button>
        </form>
        
        <div style="text-align: center; margin-top: 30px; font-size: 14px; color: #666;">
            <p>🎯 Game will create pollution columns based on these values<br>
            Higher values = Taller columns = More challenging!</p>
            
            <div style="margin-top: 20px; padding-top: 20px; border-top: 1px solid #ddd;">
                <small>💾 Data is automatically saved and restored on reboot</small><br>
                <small>Last saved: )rawliteral" + String((millis() - currentAQI.lastUpdate) / 1000) + R"rawliteral( seconds ago</small>
            </div>
        </div>
    </div>
</body>
</html>
  )rawliteral";
  
  server.send(200, "text/html", html);
}

void handleAQIInput() {
  // Alternative simplified input page
  handleRoot();
}

void handleSetAQI() {
  if (server.hasArg("pm25") && server.hasArg("no2") && server.hasArg("o3")) {
    currentAQI.pm25 = server.arg("pm25").toInt();
    currentAQI.no2 = server.arg("no2").toInt();
    currentAQI.o3 = server.arg("o3").toInt();
    
    if (server.hasArg("city") && server.arg("city").length() > 0) {
      currentAQI.city = server.arg("city");
    } else {
      currentAQI.city = "Manual Input";
    }
    
    currentAQI.dataValid = true;
    currentAQI.lastUpdate = millis();
    
    Serial.println("📊 New AQI data received from web interface:");
    Serial.printf("  PM2.5: %d, NO₂: %d, O₃: %d, Location: %s\n", 
                  currentAQI.pm25, currentAQI.no2, currentAQI.o3, currentAQI.city.c_str());
    
    // Trigger new game with updated data
    prepareColumnAnimation();
    
    // Save the new data to persistent storage
    saveAQIData();
    
    String response = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>AQI Updated!</title>
    <style>
        body { font-family: Arial; margin: 20px; background: #f0f8ff; text-align: center; }
        .container { max-width: 400px; margin: 0 auto; background: white; padding: 30px; border-radius: 10px; box-shadow: 0 4px 8px rgba(0,0,0,0.1); }
        .success { color: #4caf50; font-size: 24px; margin: 20px 0; }
        .btn { background: #4caf50; color: white; padding: 12px 24px; border: none; border-radius: 5px; cursor: pointer; font-size: 16px; text-decoration: none; display: inline-block; margin: 10px; }
    </style>
    <meta http-equiv="refresh" content="3;url=/">
</head>
<body>
    <div class="container">
        <div class="success">✅ AQI Data Updated!</div>
        <p>New pollution columns are being generated based on your input...</p>
        <p><strong>PM2.5:</strong> )rawliteral" + String(currentAQI.pm25) + R"rawliteral( μg/m³<br>
        <strong>NO₂:</strong> )rawliteral" + String(currentAQI.no2) + R"rawliteral( ppb<br>
        <strong>O₃:</strong> )rawliteral" + String(currentAQI.o3) + R"rawliteral( ppb</p>
        <p>🎮 Ready to mine!)rawliteral" + (currentAQI.city != "Manual Input" ? " - " + currentAQI.city : "") + R"rawliteral(</p>
        <a href="/" class="btn">🔙 Back to Controls</a>
    </div>
</body>
</html>
    )rawliteral";
    
    server.send(200, "text/html", response);
  } else {
    server.send(400, "text/plain", "Missing required parameters");
  }
}

void handleAPIHelper() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>AQI Data Helper</title>
    <style>
        body { font-family: Arial; margin: 20px; background: #f0f8ff; }
        .container { max-width: 600px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 4px 8px rgba(0,0,0,0.1); }
        h1 { color: #2e8b57; text-align: center; }
        .method { background: #f8f9fa; padding: 15px; border-radius: 5px; margin: 15px 0; border-left: 4px solid #4caf50; }
        .btn { background: #4caf50; color: white; padding: 12px 24px; border: none; border-radius: 5px; cursor: pointer; font-size: 16px; text-decoration: none; display: inline-block; margin: 5px; }
        .btn-blue { background: #2196f3; }
        .btn:hover { opacity: 0.9; }
        .api-link { background: #e8f4fd; padding: 10px; border-radius: 5px; margin: 10px 0; font-family: monospace; word-break: break-all; }
        .quick-cities { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 10px; margin: 15px 0; }
        .city-btn { background: #ff9800; }
        .instructions { background: #fff3cd; padding: 15px; border-radius: 5px; margin: 15px 0; border-left: 4px solid #ffc107; }
    </style>
</head>
<body>
    <div class="container">
        <h1>🌍 AQI Data Helper</h1>
        
        <div class="instructions">
            <strong>📋 Instructions:</strong><br>
            1. Choose a method below to get AQI data for your location<br>
            2. Look for the PM2.5, NO₂, and O₃ values in the results<br>
            3. Return here and enter those values in the main form<br>
            4. The data will be automatically saved for future use!
        </div>
        
        <div class="method">
            <h3>🎯 Method 1: Quick City Lookup</h3>
            <p>Click a major city near you to get instant AQI data:</p>
            <div class="quick-cities">
                <a href="https://api.waqi.info/feed/barcelona/?token=c96c0076ad7616b27dcd240b92f25f40703abe28" target="_blank" class="btn city-btn">Barcelona 🇪🇸</a>
                <a href="https://api.waqi.info/feed/madrid/?token=c96c0076ad7616b27dcd240b92f25f40703abe28" target="_blank" class="btn city-btn">Madrid 🇪🇸</a>
                <a href="https://api.waqi.info/feed/london/?token=c96c0076ad7616b27dcd240b92f25f40703abe28" target="_blank" class="btn city-btn">London 🇬🇧</a>
                <a href="https://api.waqi.info/feed/paris/?token=c96c0076ad7616b27dcd240b92f25f40703abe28" target="_blank" class="btn city-btn">Paris 🇫🇷</a>
                <a href="https://api.waqi.info/feed/newyork/?token=c96c0076ad7616b27dcd240b92f25f40703abe28" target="_blank" class="btn city-btn">New York 🇺🇸</a>
                <a href="https://api.waqi.info/feed/losangeles/?token=c96c0076ad7616b27dcd240b92f25f40703abe28" target="_blank" class="btn city-btn">Los Angeles 🇺🇸</a>
                <a href="https://api.waqi.info/feed/rome/?token=c96c0076ad7616b27dcd240b92f25f40703abe28" target="_blank" class="btn city-btn">Rome 🇮🇹</a>
                <a href="https://api.waqi.info/feed/berlin/?token=c96c0076ad7616b27dcd240b92f25f40703abe28" target="_blank" class="btn city-btn">Berlin 🇩🇪</a>
                <a href="https://api.waqi.info/feed/amsterdam/?token=c96c0076ad7616b27dcd240b92f25f40703abe28" target="_blank" class="btn city-btn">Amsterdam 🇳🇱</a>
                <a href="https://api.waqi.info/feed/tokyo/?token=c96c0076ad7616b27dcd240b92f25f40703abe28" target="_blank" class="btn city-btn">Tokyo 🇯🇵</a>
                <a href="https://api.waqi.info/feed/sydney/?token=c96c0076ad7616b27dcd240b92f25f40703abe28" target="_blank" class="btn city-btn">Sydney 🇦🇺</a>
                <a href="https://api.waqi.info/feed/toronto/?token=c96c0076ad7616b27dcd240b92f25f40703abe28" target="_blank" class="btn city-btn">Toronto 🇨🇦</a>
            </div>
        </div>
        
        <div class="method">
            <h3>📍 Method 2: Auto-Location (IP-based)</h3>
            <p>Get AQI data for your current location automatically:</p>
            <a href="https://api.waqi.info/feed/here/?token=c96c0076ad7616b27dcd240b92f25f40703abe28" target="_blank" class="btn btn-blue">📡 Get My Location's AQI</a>
        </div>
        
        <div class="method">
            <h3>🔍 Method 3: Custom City Search</h3>
            <p>For any other city, use this link format:</p>
            <div class="api-link">
                https://api.waqi.info/feed/[CITY_NAME]/?token=c96c0076ad7616b27dcd240b92f25f40703abe28
            </div>
            <p><small>Replace [CITY_NAME] with your city (e.g., "london", "tokyo", "paris")</small></p>
        </div>
        
        <div class="method">
            <h3>🔍 Smart Value Extractor</h3>
            <p>Get AQI data and have the values automatically extracted for you:</p>
            <div style="background: #fff; padding: 15px; border-radius: 5px; border: 2px solid #4caf50; margin: 10px 0;">
                <label for="api-url" style="font-weight: bold;">Paste your API response here:</label>
                <textarea id="api-input" placeholder="Paste the full JSON response from any AQICN API link above..." 
                         style="width: 100%; height: 120px; margin: 10px 0; padding: 10px; border: 1px solid #ddd; border-radius: 5px;"></textarea>
                <button onclick="extractValues()" class="btn" style="width: 100%;">📊 Extract PM2.5, NO₂, and O₃ Values</button>
            </div>
            <div id="extracted-values" style="background: #e8f5e8; padding: 15px; border-radius: 5px; margin: 10px 0; display: none;">
                <h4>✅ Extracted Values:</h4>
                <div id="value-display"></div>
                <button onclick="useExtractedValues()" class="btn" style="margin-top: 10px;">🎮 Use These Values in Game</button>
            </div>
        </div>
        
        <div class="instructions">
            <h4>📖 Manual Reading (if needed):</h4>
            <p>If the extractor doesn't work, look for these values manually:</p>
            <ul>
                <li><strong>PM2.5:</strong> <code>data.iaqi.pm25.v</code> (Fine particles)</li>
                <li><strong>NO₂:</strong> <code>data.iaqi.no2.v</code> (Nitrogen dioxide)</li>
                <li><strong>O₃:</strong> <code>data.iaqi.o3.v</code> (Ozone)</li>
            </ul>
            <p><small>💡 If a pollutant is missing, use defaults: PM2.5=25, NO₂=15, O₃=30</small></p>
        </div>
        
        <script>
        let extractedPM25 = 0, extractedNO2 = 0, extractedO3 = 0, extractedCity = '';
        
        function extractValues() {
            const input = document.getElementById('api-input').value;
            const resultDiv = document.getElementById('extracted-values');
            const displayDiv = document.getElementById('value-display');
            
            try {
                const data = JSON.parse(input);
                
                // Extract values with fallbacks
                extractedPM25 = (data.data && data.data.iaqi && data.data.iaqi.pm25) ? data.data.iaqi.pm25.v : 25;
                extractedNO2 = (data.data && data.data.iaqi && data.data.iaqi.no2) ? data.data.iaqi.no2.v : 15;
                extractedO3 = (data.data && data.data.iaqi && data.data.iaqi.o3) ? data.data.iaqi.o3.v : 30;
                extractedCity = (data.data && data.data.city && data.data.city.name) ? data.data.city.name : 'Unknown City';
                
                displayDiv.innerHTML = `
                    <p><strong>🔵 PM2.5:</strong> ${extractedPM25} μg/m³</p>
                    <p><strong>🔴 NO₂:</strong> ${extractedNO2} ppb</p>
                    <p><strong>🟢 O₃:</strong> ${extractedO3} ppb</p>
                    <p><strong>📍 Location:</strong> ${extractedCity}</p>
                `;
                resultDiv.style.display = 'block';
            } catch (e) {
                displayDiv.innerHTML = '<p style="color: red;">❌ Error parsing JSON. Please check the format and try again.</p>';
                resultDiv.style.display = 'block';
            }
        }
        
        function useExtractedValues() {
            // Redirect to main page with extracted values
            window.location.href = `/?pm25=${extractedPM25}&no2=${extractedNO2}&o3=${extractedO3}&city=${encodeURIComponent(extractedCity)}&auto_input=1`;
        }
        </script>
        
        <div style="text-align: center; margin-top: 30px;">
            <a href="/" class="btn">🔙 Back to Main Interface</a>
        </div>
    </div>
</body>
</html>
  )rawliteral";
  
  server.send(200, "text/html", html);
}

// =============================================================================
// ENVIRONMENTAL AIR QUALITY GAME FUNCTIONS
// =============================================================================

void initWiFiConnection() {
  if (useAccessPointMode) {
    // AGGRESSIVE POWER MANAGEMENT FOR BROWNOUT PREVENTION
    Serial.println("⚡ POWER CRITICAL: Implementing aggressive power management...");
    
    // Step 1: Reduce CPU frequency to minimum for WiFi operations
    Serial.println("🔽 Reducing CPU frequency to 80 MHz...");
    setCpuFrequencyMhz(80);  // Minimum stable frequency
    delay(100);
    
    // Step 2: Disable all non-essential peripherals during WiFi init
    Serial.println("💤 Disabling non-essential systems temporarily...");
    
    // Step 3: Initialize WiFi with maximum power savings
    Serial.println("📡 Initializing WiFi with power-optimized settings...");
    
    WiFi.mode(WIFI_OFF);     // Start with WiFi completely off
    delay(500);              // Allow power to stabilize
    
    WiFi.mode(WIFI_AP);      // Use AP-only mode (lower power than AP_STA)
    delay(500);              // Stabilization delay
    
    // Configure with minimal power settings
    WiFi.softAPConfig(ap_ip, ap_gateway, ap_subnet);
    
    Serial.println("🌍 Creating ESP32 Access Point with power management...");
    
    // Retry mechanism with exponential backoff
    bool apStarted = false;
    int retryCount = 0;
    const int maxRetries = 3;
    
    while (!apStarted && retryCount < maxRetries) {
      Serial.printf("🔄 WiFi AP attempt %d/%d...", retryCount + 1, maxRetries);
      
      delay(1000 * (retryCount + 1));  // Exponential backoff: 1s, 2s, 3s
      
      apStarted = WiFi.softAP(ap_ssid.c_str(), ap_pass.c_str(), 1, 0, 3); // Ch 1, no hidden, max 3 clients
      
      if (!apStarted) {
        Serial.println(" FAILED - retrying...");
        WiFi.mode(WIFI_OFF);
        delay(1000);  // Cool down period
        WiFi.mode(WIFI_AP);
        delay(500);
      } else {
        Serial.println(" SUCCESS!\n");
      }
      
      retryCount++;
    }
    
    if (apStarted) {
      Serial.println("✅ ESP32 Access Point created with power management!");
      delay(500);  // Allow AP to fully stabilize
      
      // Restore normal CPU frequency after successful WiFi init
      Serial.println("⚡ Restoring normal CPU frequency...");
      setCpuFrequencyMhz(240);  // Back to full speed
      delay(100);
      
      Serial.printf("📶 Network Name: %s\n", ap_ssid.c_str());
      Serial.printf("🔑 Password: %s\n", ap_pass.c_str());
      Serial.printf("🌐 ESP32 IP: %s\n", WiFi.softAPIP().toString().c_str());
      Serial.println();
      Serial.println("📱 SETUP INSTRUCTIONS:");
      Serial.println("  1. On your phone, go to WiFi settings");
      Serial.printf("  2. Connect to '%s'\n", ap_ssid.c_str());
      Serial.printf("  3. Enter password: %s\n", ap_pass.c_str());
      Serial.println("  4. Open browser and go to: 192.168.4.1");
      Serial.println("  5. Use web interface to input AQI data");
      Serial.println();
      
      wifiConnected = true;  // Consider AP as "connected" for game logic
      
      Serial.println("🌐 Starting web server...");
      delay(500);  // Brief delay before starting server
      
      // Setup web server routes for AQI manual input
      setupWebServerRoutes();
      server.begin();
      Serial.println("✓ Web server started for AQI input interface");
      
      // Start with saved data (or defaults if none saved)
      Serial.println("🎮 Loading saved AQI data - game ready immediately");
      loadAQIData(); // This loads from persistent storage or uses defaults
      
      // Skip the WiFi state machine and go directly to game
      prepareColumnAnimation();
    } else {
      Serial.println("❌ CRITICAL: All WiFi attempts failed!");
      Serial.println("🔧 POWER ISSUE DETECTED - Continuing without WiFi...");
      
      // Restore CPU frequency even if WiFi failed
      setCpuFrequencyMhz(240);
      
      wifiConnected = false;
      useAccessPointMode = false;  // Disable WiFi features
      
      Serial.println("🎮 Starting in OFFLINE MODE - no web interface available");
      Serial.println("⚠️  To fix: Use external 5V power supply or powered USB hub");
      
      // Load saved data and start game anyway
      loadAQIData();
      prepareColumnAnimation();
    }
  } else {
    // Original WiFi station mode code...
    Serial.println("🌍 Environmental Game: Connecting to WiFi...");
    Serial.printf("Attempting to connect to: %s\n", config_ssid.c_str());
    
    // Scan for available networks first
    Serial.println("🔍 Scanning for available WiFi networks...");
    int n = WiFi.scanNetworks();
    if (n > 0) {
      Serial.printf("Found %d networks:\n", n);
      for (int i = 0; i < n && i < 10; ++i) {
        Serial.printf("%d: %s (Channel: %d, Strength: %d dBm)%s\n", 
                      i+1, WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.RSSI(i),
                      WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? " [OPEN]" : "");
      }
    }
    
    WiFi.begin(config_ssid.c_str(), config_pass.c_str());
    
#ifdef QTPY_S3
    setStatusLED(255, 165, 0);  // Orange - connecting
#endif
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
      
#ifdef QTPY_S3
      // Blink orange during connection attempts
      if (attempts % 2 == 0) {
        setStatusLED(255, 165, 0);  // Orange
      } else {
        statusLEDOff();
      }
#endif
      
      if (attempts % 5 == 0) {
        Serial.printf(" (Status: %d, Attempt: %d/20) ", WiFi.status(), attempts);
      }
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      wifiConnected = true;
#ifdef QTPY_S3
      setStatusLED(0, 255, 0);  // Green - connected
#endif
      Serial.println();
      Serial.printf("✓ WiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());
      Serial.printf("📶 Signal strength: %d dBm\n", WiFi.RSSI());
    } else {
#ifdef QTPY_S3
      setStatusLED(255, 0, 0);  // Red - failed
#endif
      Serial.println("\n✗ WiFi connection failed!");
      Serial.printf("WiFi Status: %d (should be 3 for connected)\n", WiFi.status());
      Serial.println("TROUBLESHOOTING:");
      Serial.println("- ESP32 only supports 2.4GHz networks");
      Serial.println("- Check if your network broadcasts 2.4GHz");
      Serial.println("- Verify SSID and password are correct");
      Serial.println("Will use demo data instead...");
      wifiConnected = false;
    }
  }
}

bool fetchAirQualityData() {
  if (!wifiConnected) return false;
  
  HTTPClient http;
  
  // Use a specific city for more reliable data (replace with your city)
  // Examples: "beijing", "london", "newyork", "paris", "tokyo", "sydney"
  // Or use "here" for IP geolocation
  String url = String(API_BASE_URL) + "here/?token=" + String(API_KEY);
  // String url = String(API_BASE_URL) + "newyork/?token=" + String(API_KEY);  // Example: NYC
  
  Serial.printf("📡 Fetching air quality data: %s\n", url.c_str());
  http.begin(url);
  
  int httpResponseCode = http.GET();
  
  if (httpResponseCode == 200) {
    String payload = http.getString();
    Serial.println("✓ API Response received");
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    
    if (!error) {
      if (doc["status"] == "ok") {
        currentAQI.pm25 = doc["data"]["iaqi"]["pm25"]["v"] | 25;  // Default if missing
        currentAQI.no2 = doc["data"]["iaqi"]["no2"]["v"] | 15;    // Default if missing  
        currentAQI.o3 = doc["data"]["iaqi"]["o3"]["v"] | 30;     // Default if missing
        currentAQI.city = doc["data"]["city"]["name"] | "Unknown";
        currentAQI.lastUpdate = millis();
        currentAQI.dataValid = true;
        
        Serial.printf("🌍 Air Quality Data - %s:\n", currentAQI.city.c_str());
        Serial.printf("  PM2.5: %d (Blue enemies)\n", currentAQI.pm25);
        Serial.printf("  NO₂:   %d (Red enemies)\n", currentAQI.no2);  
        Serial.printf("  O₃:    %d (Green enemies)\n", currentAQI.o3);
        
        http.end();
        return true;
      }
    } else {
      Serial.printf("✗ JSON parsing error: %s\n", error.c_str());
    }
  } else {
    Serial.printf("✗ HTTP Error: %d\n", httpResponseCode);
  }
  
  http.end();
  return false;
}

// --------------------------------------------------------------------------
// ESP-NOW WIRELESS DISPLAY COMMUNICATION FUNCTIONS
// --------------------------------------------------------------------------

void initESPNOW() {
  espnowReady = false;
  displayUnitConnected = false;
  
  Serial.println("🔗 Initializing ESP-NOW for wireless display...");
  
  // ESP-NOW can work alongside WiFi in station mode
  // In AP mode, we need to set the channel to match
  
  Serial.println("Step 1: Initializing ESP-NOW core...");
  esp_err_t result = esp_now_init();
  if (result != ESP_OK) {
    Serial.printf("ESP-NOW init failed: %s\n", esp_err_to_name(result));
    Serial.println("Game will continue without wireless display");
    return;
  }
  Serial.println("ESP-NOW core initialized successfully");
  
  Serial.println("Step 2: Registering send callback...");
  result = esp_now_register_send_cb(onDataSent);
  if (result != ESP_OK) {
    Serial.printf("Send callback registration failed: %s\n", esp_err_to_name(result));
  } else {
    Serial.println("Send callback registered successfully");
  }
  
  Serial.println("Step 3: Adding M5Stack display peer...");
  Serial.printf("Target MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                displayUnitMAC[0], displayUnitMAC[1], displayUnitMAC[2],
                displayUnitMAC[3], displayUnitMAC[4], displayUnitMAC[5]);
  
  memcpy(peerInfo.peer_addr, displayUnitMAC, 6);
  peerInfo.channel = 0;  // Auto-channel
  peerInfo.encrypt = false;
  
  result = esp_now_add_peer(&peerInfo);
  if (result != ESP_OK) {
    Serial.printf("Failed to add display peer: %s\n", esp_err_to_name(result));
    Serial.println("Wireless display will not work");
    return;
  }
  
  Serial.println("✓ M5Stack display peer added successfully");
  Serial.println("🎮 Wireless LED display ready for game events!");
  
  // Add stabilization delay for ESP-NOW to fully initialize
  Serial.println("⏳ Waiting 3 seconds for ESP-NOW to stabilize...");
  delay(3000);
  Serial.println("✓ ESP-NOW stabilization complete!");
  
  espnowReady = true;
}

void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  displayUnitConnected = (status == ESP_NOW_SEND_SUCCESS);
  
  if (status != ESP_NOW_SEND_SUCCESS) {
    Serial.printf("Display communication failed to %02X:%02X:%02X:%02X:%02X:%02X\n",
                  mac_addr[0], mac_addr[1], mac_addr[2], 
                  mac_addr[3], mac_addr[4], mac_addr[5]);
  }
}

void sendDisplayCommand(uint8_t cmd, uint8_t r, uint8_t g, uint8_t b, uint8_t pos, uint8_t len, uint8_t strip) {
  if (!espnowReady) return;
  
  // Reduced rate limiting - allow burst sending for multi-strip environmental data
  static unsigned long lastSendTime = 0;
  unsigned long now = millis();
  
  if (now - lastSendTime < 15) {  // Reduced from 25ms to 15ms
    delay(15 - (now - lastSendTime));  // Wait if needed
  }
  
  outgoingMessage.command = cmd;
  outgoingMessage.red = r;
  outgoingMessage.green = g;
  outgoingMessage.blue = b;
  outgoingMessage.position = pos;
  outgoingMessage.length = len;
  outgoingMessage.stripId = strip;
  outgoingMessage.gameState = (uint8_t)currentState;
  
  esp_err_t result = esp_now_send(displayUnitMAC, (uint8_t *)&outgoingMessage, sizeof(outgoingMessage));
  
  if (result != ESP_OK) {
    static unsigned long lastErrorTime = 0;
    if (millis() - lastErrorTime > 5000) {
      Serial.printf("ESP-NOW send failed: %s\n", esp_err_to_name(result));
      lastErrorTime = millis();
    }
  } else {
    lastSendTime = millis();
    // Show successful sends for strip data
    if (cmd == 4) {  // Environmental data
      Serial.printf("✓ ESP-NOW environmental data sent (strip=%d, len=%d)\n", strip, len);
    }
  }
}

void sendEnvironmentalState() {
  if (!espnowReady || !currentAQI.dataValid) return;
  
  Serial.println("📊 Sending environmental data to all 3 strips...");
  
  // Send current environmental levels to display with proper delays
  // Command 4 = environmental data display
  
  // Send PM2.5 level (Blue) - Strip 0
  int pm25Height = map(currentAQI.pm25, 0, 200, 0, 100);
  sendDisplayCommand(4, 0, 0, 255, 0, pm25Height, 0);  // Blue for PM2.5
  
  delay(20);  // Increased from 10ms to 20ms to ensure delivery
  
  // Send NO2 level (Red) - Strip 1  
  int no2Height = map(currentAQI.no2, 0, 100, 0, 100);
  sendDisplayCommand(4, 255, 0, 0, 0, no2Height, 1);   // Red for NO2
  
  delay(20);  // Increased from 10ms to 20ms to ensure delivery
  
  // Send O3 level (Green) - Strip 2
  int o3Height = map(currentAQI.o3, 0, 150, 0, 100);
  sendDisplayCommand(4, 0, 255, 0, 0, o3Height, 2);    // Green for O3
  
  Serial.printf("📊 Environmental state sent: PM2.5=%d->%d, NO₂=%d->%d, O₃=%d->%d\n", 
                currentAQI.pm25, pm25Height, currentAQI.no2, no2Height, currentAQI.o3, o3Height);
}

void updateWirelessDisplay() {
  if (!espnowReady) return;
  
  // Throttle display updates
  if (millis() - lastDisplayUpdate < DISPLAY_UPDATE_INTERVAL) return;
  lastDisplayUpdate = millis();
  
  // Send state-specific display commands
  switch (currentState) {
    case STATE_ENVIRONMENTAL_GAME:
      // Only send environmental data (no longer floods every 50ms)
      sendEnvironmentalState();
      break;
      
    case STATE_ENVIRONMENTAL_WIN:
      // Send victory pattern
      sendDisplayCommand(1, 255, 255, 0, 0, 0, 0);  // Yellow victory
      break;
      
    case STATE_DATA_ERROR:
      // Send game over pattern  
      sendDisplayCommand(1, 255, 0, 0, 0, 0, 0);    // Red game over
      break;
      
    case STATE_WIFI_CONNECTING:
      // Send connecting pattern
      sendDisplayCommand(1, 0, 0, 255, 0, 0, 0);    // Blue connecting
      break;
      
    default:
      // Clear display for other states
      sendDisplayCommand(2, 0, 0, 0, 0, 0, 0);      // Clear
      break;
  }
}

void prepareColumnAnimation() {
  if (!currentAQI.dataValid) {
    Serial.println("❌ Cannot create enemies - no valid air quality data");
    return;
  }
  
  Serial.println("📊 Preparing pollution column animation...");
  
  if (currentAQI.city != "Demo City" && currentAQI.city != "Unknown") {
    Serial.printf("🌍 Using REAL AQI data from %s:\n", currentAQI.city.c_str());
    Serial.printf("   PM2.5: %d μg/m³, NO₂: %d ppb, O₃: %d ppb\n", 
                  currentAQI.pm25, currentAQI.no2, currentAQI.o3);
  } else {
    Serial.println("🎮 Using demo data for gameplay testing");
  }
  
  // Clear existing enemies and reset game state
  for (int strip = 0; strip < NUM_STRIPS; strip++) {
    pollutionEnemies[strip].clear();
    currentColumnHeights[strip] = 0;
    stripCompleted[strip] = false;
  }
  
  // Reset celebration state
  completedStripCount = 0;
  allStripsCompleteEffect = false;
  wipeEffects.clear();
  fireworks.clear();
  
  // Calculate target column heights based on air quality readings
  targetColumnHeights[0] = map(currentAQI.pm25, 0, 200, 10, 80);  // PM2.5: 10-80 pixels
  targetColumnHeights[1] = map(currentAQI.no2, 0, 100, 8, 70);    // NO₂: 8-70 pixels
  targetColumnHeights[2] = map(currentAQI.o3, 0, 150, 12, 75);    // O₃: 12-75 pixels
  
  for (int strip = 0; strip < NUM_STRIPS; strip++) {
    Serial.printf("Strip %d (%s): Target height %d pixels\n", 
                  strip, (strip==0?"PM2.5":(strip==1?"NO₂":"O₃")), targetColumnHeights[strip]);
  }
  
  columnAnimationComplete = false;
  lastColumnUpdate = millis();
  currentState = STATE_COLUMN_FILLING;
  
  // NEW: Play enemy building sound as animation starts
  playSound(EVT_ENEMIES_BUILDING);
}

void createPollutionEnemies() {
  // Create the final pollution columns after animation
  for (int strip = 0; strip < NUM_STRIPS; strip++) {
    pollutionEnemies[strip].clear();
    
    // Create a continuous column from bottom up
    for (int i = 0; i < targetColumnHeights[strip]; i++) {
      PollutionEnemy enemy;
      enemy.position = i;  // Stack from bottom (position 0 up)
      enemy.color = strip + 1;    // 1=Blue(PM2.5), 2=Red(NO₂), 3=Green(O₃)
      enemy.stripIndex = strip;
      enemy.active = true;
      enemy.health = 4;           // Takes 4 hits to destroy
      enemy.maxHealth = 4;
      enemy.sparkTimer = 0;
      pollutionEnemies[strip].push_back(enemy);
    }
  }
  
  Serial.println("✅ Static pollution columns created - mining ready!");
  currentState = STATE_ENVIRONMENTAL_GAME;
}

void initLEDStrips() {
  // Console board: LED strips are on M5Stack display board
  // Keep memory allocation for game logic compatibility
  stripPM25 = new CRGB[LEDS_PER_STRIP];
  stripNO2 = new CRGB[LEDS_PER_STRIP];
  stripO3 = new CRGB[LEDS_PER_STRIP];
  
  // LED strips controlled wirelessly via ESP-NOW to M5Stack
  // FastLED.addLeds calls disabled - no LEDs physically connected to console
  // FastLED.addLeds<LED_TYPE, PIN_LED_PM25, COLOR_ORDER>(stripPM25, LEDS_PER_STRIP);
  // FastLED.addLeds<LED_TYPE, PIN_LED_NO2, COLOR_ORDER>(stripNO2, LEDS_PER_STRIP); 
  // FastLED.addLeds<LED_TYPE, PIN_LED_O3, COLOR_ORDER>(stripO3, LEDS_PER_STRIP);
  
  // Visual output handled by M5Stack display board
  Serial.println("✓ Console board: Game logic initialized (LEDs on M5Stack display)");
}

void updateEnvironmentalGame() {
  float deltaTime = 1.0f / 60.0f;  // Assume 60 FPS
  
  // Handle shots for each strip
  for (int i = environmentalShots.size() - 1; i >= 0; i--) {
    // Apply gravity (shots accelerate downward - gravity pulls them down faster)
    environmentalShots[i].velocity += gravity;  // Add gravity to make shots accelerate downward
    environmentalShots[i].position += environmentalShots[i].velocity * 60 * deltaTime;
    
    // Remove shots that go off screen (bottom)
    if (environmentalShots[i].position < 0) {
      createImpactSparks(environmentalShots[i].position, environmentalShots[i].color, environmentalShots[i].stripIndex, false);
      environmentalShots.erase(environmentalShots.begin() + i);
      continue;
    }
    
    // Check collision with pollution enemies (mining mechanic)
    int stripIndex = environmentalShots[i].stripIndex;
    bool shotRemoved = false;
    
    for (int e = pollutionEnemies[stripIndex].size() - 1; e >= 0; e--) {
      if (pollutionEnemies[stripIndex][e].active) {
        float distance = abs(environmentalShots[i].position - pollutionEnemies[stripIndex][e].position);
        // Optimized collision detection
        float collisionRange = environmentalShots[i].isCannonShot ? 4.0f : 3.0f;
        if (distance < collisionRange) {
          if (environmentalShots[i].color == pollutionEnemies[stripIndex][e].color) {
            // Successful hit - chip away at pollution! (Handle cannon shot 3x damage)
            int damage = environmentalShots[i].damage;  // 1 for normal, 3 for cannon
            pollutionEnemies[stripIndex][e].health -= damage;
            pollutionEnemies[stripIndex][e].sparkTimer = millis() + 300;  // Spark effect for 300ms
            
            if (environmentalShots[i].isCannonShot) {
              // Removed debug output for performance
            }
            
            if (pollutionEnemies[stripIndex][e].health <= 0) {
                // Fully destroyed - CREATE DRAMATIC EXPLOSION! (debug output removed for performance)
              
              // Calculate explosion intensity based on proximity to end
              int remainingEnemies = 0;
              for (int s = 0; s < NUM_STRIPS; s++) {
                remainingEnemies += pollutionEnemies[s].size();
              }
              int intensity = max(1, 3 - (remainingEnemies / 10));  // Scale 1-3 based on remaining enemies
              
              createDramaticExplosion(pollutionEnemies[stripIndex][e].position, stripIndex, intensity);
              pollutionEnemies[stripIndex].erase(pollutionEnemies[stripIndex].begin() + e);
              
              // Check if strip is now completely cleared
              if (!stripCompleted[stripIndex] && pollutionEnemies[stripIndex].empty()) {
                stripCompleted[stripIndex] = true;
                completedStripCount++;
                createWipeEffect(stripIndex);
                // Strip cleared - debug output removed for performance
                             
                // Schedule delayed firework (1 second delay to let explosion debris settle)
                delayedFireworkTime[stripIndex] = millis() + STRIP_FIREWORK_DELAY;
                playSound(EVT_HIT_SUCCESS);
              } else {
                playSound(EVT_HIT_SUCCESS);
              }
            } else {
              // Damaged but not destroyed
              Serial.printf("🔨 DAMAGE! Pollution pixel health: %d/4 at pos:%.1f\n", 
                           pollutionEnemies[stripIndex][e].health, pollutionEnemies[stripIndex][e].position);
              scorePerStrip[stripIndex] += 5;
              createImpactSparks(environmentalShots[i].position, environmentalShots[i].color, stripIndex, false);
            }
          } else {
            // Wrong color - no effect
            Serial.printf("❌ MISS! Shot(color:%d) vs Enemy(color:%d) - wrong color!\n", 
                         environmentalShots[i].color, pollutionEnemies[stripIndex][e].color);
            createImpactSparks(environmentalShots[i].position, environmentalShots[i].color, stripIndex, false);
            playSound(EVT_MISTAKE);
          }
          environmentalShots.erase(environmentalShots.begin() + i);
          shotRemoved = true;
          break;
        }
      }
    }
  }
  
  // Update pollution visual effects (static mining game - no enemy movement)
  bool allStripsCleared = true;
  unsigned long currentTime = millis();
  
  for (int strip = 0; strip < NUM_STRIPS; strip++) {
    for (auto& enemy : pollutionEnemies[strip]) {
      if (enemy.active) {
        allStripsCleared = false;
        
        // Update spark timer for damage visual effects
        if (enemy.sparkTimer > currentTime) {
          // Still sparking from recent damage
        }
      }
    }
  }
  
  // Check win condition - only if we actually have enemies to fight
  static bool enemiesCreated = false;
  if (!enemiesCreated) {
    // Check if any enemies exist
    for (int strip = 0; strip < NUM_STRIPS; strip++) {
      if (!pollutionEnemies[strip].empty()) {
        enemiesCreated = true;
        break;
      }
    }
  }
  
  // Debug victory condition check
  static unsigned long lastVictoryDebug = 0;
  if (millis() - lastVictoryDebug > 2000) {
    Serial.printf("🏆 Victory check: strips=%d/%d, created=%d, effect=%d\n",
                  completedStripCount, NUM_STRIPS, (int)enemiesCreated, (int)allStripsCompleteEffect);
    lastVictoryDebug = millis();
  }
  
  if (completedStripCount >= NUM_STRIPS && enemiesCreated && !allStripsCompleteEffect) {
    Serial.println("🎉 ALL POLLUTION DEFEATED! Clean air achieved!");
    allStripsCompleteEffect = true;
    allStripsCompleteTime = millis();
    victoryMusicPlaying = true;
    victoryMusicStartTime = millis();
    finalFireworksTriggered = false;
    gameBlackedOut = false;
    
    // Play victory melody (Mario arpeggios, ~4 seconds)
    Serial.println("🎥 TRIGGERING MARIO-STYLE VICTORY MELODY! (Silent fireworks during music)");
    playSound(EVT_LEVEL_VICTORY);
    
    // Start silent fireworks show during music
    fireworks.clear();
    for (int i = 0; i < 15; i++) {
      createStripFirework(-1, true);  // Silent visual fireworks during music
    }
  }
  
  // Victory sequence timing management
  if (allStripsCompleteEffect) {
    unsigned long elapsed = millis() - allStripsCompleteTime;
    
    // Continue creating silent fireworks during music
    if (elapsed < VICTORY_MUSIC_DURATION && random(100) < 20) {  // 20% chance each frame
      createStripFirework(-1, true);  // Silent fireworks during music
    }
    
    // After music ends: trigger final audible fireworks
    if (elapsed >= FINAL_FIREWORKS_DELAY && !finalFireworksTriggered) {
      Serial.println("🎆 FINAL AUDIBLE FIREWORKS!");
      finalFireworksTriggered = true;
      victoryMusicPlaying = false;  // Stop victory music mode
      
      // One spectacular audible firework per strip
      for (int strip = 0; strip < NUM_STRIPS; strip++) {
        createStripFirework(strip, false);  // Audible final fireworks
        delay(200);  // Stagger them slightly
      }
    }
    
    // Blackout after final fireworks
    if (elapsed >= BLACKOUT_DELAY && !gameBlackedOut) {
      Serial.println("🔳 Victory blackout - preparing to reset game...");
      gameBlackedOut = true;
      
      // Clear all visual effects
      fill_solid(stripPM25, LEDS_PER_STRIP, CRGB::Black);
      fill_solid(stripNO2, LEDS_PER_STRIP, CRGB::Black);
      fill_solid(stripO3, LEDS_PER_STRIP, CRGB::Black);
      FastLED.show();
      
      sparks.clear();
      fireworks.clear();
    }
    
    // Reset game after pause
    if (elapsed >= GAME_RESET_DELAY) {
      Serial.println("🔄 GAME RESET - Starting new level!");
      currentState = STATE_WIFI_CONNECTING;  // Restart from beginning
      
      // Reset all game variables
      completedStripCount = 0;
      allStripsCompleteEffect = false;
      victoryMusicPlaying = false;
      finalFireworksTriggered = false;
      gameBlackedOut = false;
      for (int i = 0; i < NUM_STRIPS; i++) {
        stripCompleted[i] = false;
        pollutionEnemies[i].clear();
        currentColumnHeights[i] = 0;
        targetColumnHeights[i] = 0;
      }
      columnAnimationComplete = false;
      environmentalShots.clear();
      sparks.clear();
      fireworks.clear();
    }
  }
}

void renderEnvironmentalDisplay() {
  // Clear all strips
  fill_solid(stripPM25, LEDS_PER_STRIP, CRGB::Black);
  fill_solid(stripNO2, LEDS_PER_STRIP, CRGB::Black);
  fill_solid(stripO3, LEDS_PER_STRIP, CRGB::Black);
  
  CRGB* strips[3] = {stripPM25, stripNO2, stripO3};
  
  // Render static pollution columns with enhanced character effects
  unsigned long currentTime = millis();
  for (int strip = 0; strip < NUM_STRIPS; strip++) {
    // Skip rendering enemies for completed strips during wipe effect
    bool skipEnemies = false;
    for (const auto& wipe : wipeEffects) {
      if (wipe.stripIndex == strip && wipe.active) {
        skipEnemies = true;
        break;
      }
    }
    
    if (!skipEnemies) {
      for (const auto& enemy : pollutionEnemies[strip]) {
        if (enemy.active && enemy.position >= 0 && enemy.position < LEDS_PER_STRIP) {
          CRGB enemyColor = getColor(enemy.color);
          
          // Add character-specific animations that blend with original colors
          if (enemy.color == 1) {  // PM2.5 Blue - watery pulse
            // Base blue (0,0,255) with subtle watery modulation
            float pulse = sin(currentTime * 0.006f + enemy.position * 0.25f) * 0.3f + 0.7f;  // 0.4 to 1.0 range
            enemyColor = CRGB(0, 0, 255 * pulse);
          } else if (enemy.color == 2) {  // NO₂ Red - fiery flicker
            // Base red (255,0,0) with random flicker, mixing pure red pixels
            if (random(0, 100) < 70) {  // 70% pure red
              enemyColor = CRGB(255, 0, 0);
            } else {  // 30% flickering orange-red
              uint8_t flicker = 200 + random(0, 55);  // 200-255 range
              enemyColor = CRGB(255, flicker/6, 0);  // Mostly red with tiny orange
            }
          } else if (enemy.color == 3) {  // O₃ Green - Debug test pattern
            // Simple test pattern to debug green visibility
            float wave = sin(currentTime * 0.01f + enemy.position * 0.1f) * 0.5f + 0.5f;
            uint8_t greenValue = 50 + wave * 205;  // 50-255 range for high contrast
            enemyColor = CRGB(0, greenValue, 0);
            // Debug: Force some pixels to extreme values
            if ((int(enemy.position) + int(currentTime * 0.001f)) % 10 < 3) {
              enemyColor = CRGB(0, 50, 0);  // Dark green
            } else if ((int(enemy.position) + int(currentTime * 0.001f)) % 10 > 7) {
              enemyColor = CRGB(0, 255, 0);  // Bright green
            }
          }
          
          // Apply damage-based dimming
          if (enemy.health < enemy.maxHealth) {
            float healthRatio = (float)enemy.health / (float)enemy.maxHealth;
            enemyColor.fadeToBlackBy(255 - (255 * healthRatio));
          }
          
          // Add spark effect if recently damaged
          if (enemy.sparkTimer > currentTime) {
            uint8_t sparkBrightness = 128 + sin(currentTime * 0.05f) * 127;
            enemyColor += CRGB(sparkBrightness/3, sparkBrightness/3, sparkBrightness/3);
          }
          
          strips[strip][(int)enemy.position] = enemyColor;
        }
      }
    }
  }
  
  // Render enhanced heroes with sparkle, recoil, and charging effects
  for (int strip = 0; strip < NUM_STRIPS; strip++) {
    if (!stripCompleted[strip]) {  // Only show hero if strip not completed
      float heroPos = playerPositions[strip] + playerRecoil[strip];
      if (heroPos >= 0 && heroPos < LEDS_PER_STRIP) {
        
        // Check if this hero is charging a cannon shot
        bool isCharging = buttonCharging[strip];
        float chargeProgress = 0.0f;
        
        if (isCharging) {
          unsigned long holdTime = millis() - buttonPressTime[strip];
          chargeProgress = min(1.0f, (float)holdTime / CANNON_CHARGE_TIME);
        }
        
        if (isCharging && chargeProgress < 1.0f) {
          // CHARGING: Pink hue, grows to 3 pixels, slow pulse
          CRGB heroColor = CRGB::DeepPink;
          // Optimized pulse calculation
          float pulseValue = sin(currentTime * 0.03f) * 0.5f + 0.5f;  // 0-1 range
          uint8_t chargeBrightness = 140 + (uint8_t)(pulseValue * 80 + chargeProgress * 35);
          heroColor.fadeToBlackBy(255 - chargeBrightness);
          
          // Hero grows progressively to 3 pixels during charging (downward toward enemies)
          int chargeSize = 1 + (int)(chargeProgress * 2.0f);  // 1->3 pixels based on charge progress
          for (int offset = 0; offset < chargeSize; offset++) {
            int pixelPos = (int)heroPos - offset;  // Grow downward from position 99
            if (pixelPos >= 0 && pixelPos < LEDS_PER_STRIP) {
              strips[strip][pixelPos] = heroColor;
            }
          }
          
          // Delayed charging sound effect (avoid overlap with normal shots)
          unsigned long now = millis();
          unsigned long holdTime = now - buttonPressTime[strip];
          
          if (holdTime > CHARGE_SOUND_DELAY && (now - lastChargeSoundTime[strip] > CHARGE_SOUND_INTERVAL)) {
            lastChargeSoundTime[strip] = now;
            
            // Progressive sound only after delay
            float audioProgress = (holdTime - CHARGE_SOUND_DELAY) / (float)(CANNON_CHARGE_TIME - CHARGE_SOUND_DELAY);
            audioProgress = min(1.0f, audioProgress);
            
            int baseFreq = 80 + (int)(audioProgress * 120);  // 80Hz -> 200Hz
            int duration = 70 + (int)(audioProgress * 50);    // 70ms -> 120ms
            
            if (config_sound_on) {
              playToneI2S(baseFreq, duration);
            }
          }
        } else if (isCharging && chargeProgress >= 1.0f) {
          // FULLY CHARGED: Slow blinking red effect with 3 pixels
          CRGB heroColor = CRGB::Red;
          // Slow, smooth brightness pulsing when fully charged
          float blinkValue = sin(currentTime * 0.08f) * 0.5f + 0.5f;  // Slow pulse 0-1
          uint8_t blinkBrightness = 120 + (uint8_t)(blinkValue * 135);  // 120-255 range
          heroColor.fadeToBlackBy(255 - blinkBrightness);
          
          // Full size (3 pixels) when fully charged (downward toward enemies)
          for (int offset = 0; offset < 3; offset++) {
            int pixelPos = (int)heroPos - offset;  // Grow downward from position 99
            if (pixelPos >= 0 && pixelPos < LEDS_PER_STRIP) {
              strips[strip][pixelPos] = heroColor;
            }
          }
        } else {
          // NORMAL: Standard white sparkle (single pixel)
          CRGB heroColor = CRGB::White;
          uint8_t sparkle = 200 + (uint8_t)(sin(currentTime * 0.08f + strip * 1.2f) * 30);
          heroColor.fadeToBlackBy(255 - sparkle);
          strips[strip][(int)heroPos] = heroColor;
        }
      }
    }
  }
  
  // Render wipe effects
  for (const auto& wipe : wipeEffects) {
    if (wipe.active && wipe.stripIndex >= 0 && wipe.stripIndex < NUM_STRIPS) {
      // White chase effect
      for (int i = 0; i < (int)wipe.position; i++) {
        if (i >= 0 && i < LEDS_PER_STRIP) {
          strips[wipe.stripIndex][i] = CRGB::White;
        }
      }
    }
  }
  
  // Render shots (optimized for performance)
  for (const auto& shot : environmentalShots) {
    if (shot.position >= 0 && shot.position < LEDS_PER_STRIP) {
      CRGB shotColor = getColor(shot.color);
      shotColor.maximizeBrightness();
      
      if (shot.isCannonShot) {
        // Cannon shots: 3 pixels wide (optimized rendering)
        int startPos = max(0, (int)shot.position);
        int endPos = min(LEDS_PER_STRIP - 1, startPos + 2);
        for (int pos = startPos; pos <= endPos; pos++) {
          strips[shot.stripIndex][pos] = shotColor;
        }
      } else {
        // Normal shots: single pixel
        strips[shot.stripIndex][(int)shot.position] = shotColor;
      }
    }
  }
  
  // Render sparks on their respective strips (or all strips for celebrations)
  for (const auto& spark : sparks) {
    if (spark.active && spark.position >= 0 && spark.position < LEDS_PER_STRIP) {
      CRGB sparkColor = getColor(spark.color);
      sparkColor.nscale8(spark.brightness);
      
      if (spark.stripIndex == -1) {
        // All-strips spark (from all-strips firework)
        for (int strip = 0; strip < NUM_STRIPS; strip++) {
          strips[strip][(int)spark.position] += sparkColor;
        }
      } else if (spark.stripIndex >= 0 && spark.stripIndex < NUM_STRIPS) {
        // Single strip spark
        strips[spark.stripIndex][(int)spark.position] += sparkColor;
      }
    }
  }
  
  // Render fireworks for completed strips and final celebration
  for (const auto& fw : fireworks) {
    if (fw.active && fw.position >= 0 && fw.position < LEDS_PER_STRIP) {
      CRGB fwColor = (fw.stripIndex == -1) ? CRGB::Gold : getColor(fw.color);
      fwColor.nscale8(fw.brightness);
      
      if (fw.stripIndex == -1) {
        // All-strips firework
        for (int strip = 0; strip < NUM_STRIPS; strip++) {
          strips[strip][(int)fw.position] += fwColor;
        }
      } else if (fw.stripIndex >= 0 && fw.stripIndex < NUM_STRIPS) {
        // Single strip firework
        strips[fw.stripIndex][(int)fw.position] += fwColor;
      }
    }
  }
  
  FastLED.show();
}

// Physics and Effects Functions
// Create dramatic explosion effect for enemy elimination (performance optimized)
void createDramaticExplosion(float position, int stripIndex, int intensity) {
  // Optimized explosion with fewer particles for better performance
  int numSparks = 8 + (intensity * 3);  // Reduced from 15 + intensity * 5
  
  for (int i = 0; i < numSparks; i++) {
    if (sparks.size() < 25) {  // Reduced spark limit for performance
      Spark spark;
      
      // Position: spread around impact point
      spark.position = position + float(random(-2, 3));
      
      // Velocity: Strong upward momentum  
      float upwardVelocity = float(random(600, 1200)) / 1000.0f;  // 0.6 - 1.2 pixels/frame
      float lateralVelocity = float(random(-150, 151)) / 1000.0f; // Small lateral spread
      spark.velocity = upwardVelocity + lateralVelocity;
      
      // Bright yellow/white explosion colors
      spark.color = (random(100) < 60) ? 5 : 4;  // Yellow or white
      spark.brightness = 200 + random(56);  // 200-255 brightness
      
      spark.stripIndex = stripIndex;
      spark.active = true;
      sparks.push_back(spark);
    }
  }
  
  // Reduced debug output for performance
}

void createImpactSparks(float position, int color, int stripIndex, bool success) {
  // Removed debug output for performance
  
  int numSparks = success ? 6 : 12;  // More sparks for dramatic effect
  float direction = success ? 1.0 : -1.0;  // Success sparks go up, failure sparks fall back
  
  // Calculate position-based effects (higher = more dramatic)
  float columnHeight = position / float(LEDS_PER_STRIP);
  float intensityMultiplier = 0.5f + columnHeight;  // 0.5-1.5 based on height
  
  for (int i = 0; i < numSparks; i++) {
    if (sparks.size() < 100) {  // Allow more sparks for drama
      Spark spark;
      spark.position = position + float(random(-2, 3)); // Slight position variation
      // Higher velocity for sparks higher up the column
      spark.velocity = (float(random(150, 500)) / 1000.0f) * direction * intensityMultiplier;
      spark.brightness = 255;
      
      // Color variation based on position in column
      if (columnHeight > 0.8f) {
        // Top of column - bright white sparks
        spark.color = 4;  // White
      } else if (columnHeight > 0.5f) {
        // Middle - original color but brighter
        spark.color = color;
      } else {
        // Bottom - darker, more subdued
        spark.color = color;
        spark.brightness = 180;  // Dimmer
      }
      
      spark.stripIndex = stripIndex;  // Assign to specific strip
      spark.active = true;
      sparks.push_back(spark);
    }
  }
  Serial.printf("🎆 Created %d sparks, total sparks now: %zu\n", numSparks, sparks.size());
}

void updateSparks(float deltaTime) {
  for (int i = sparks.size() - 1; i >= 0; i--) {
    if (!sparks[i].active) continue;
    
    // Enhanced gravity and physics for dramatic explosions 
    float gravity = -0.03f; // Stronger gravity for more realistic fall
    sparks[i].velocity += gravity;
    sparks[i].position += sparks[i].velocity;
    
    // MINING PHYSICS: Find "ground level" - top enemy pixel in this strip
    float groundLevel = 0.0f;  // Default ground if no enemies
    int sparkStripIndex = sparks[i].stripIndex;
    if (sparkStripIndex >= 0 && sparkStripIndex < NUM_STRIPS) {
      if (!pollutionEnemies[sparkStripIndex].empty()) {
        // Find topmost (highest position) enemy pixel in strip
        groundLevel = pollutionEnemies[sparkStripIndex][0].position;
        for (const auto& enemy : pollutionEnemies[sparkStripIndex]) {
          groundLevel = max(groundLevel, enemy.position);
        }
      }
    }
    
    // Don't let sparks fall below ground level (mining debris settles)
    if (sparks[i].position < groundLevel) {
      sparks[i].position = groundLevel;
      sparks[i].velocity = 0.0f;  // Stop bouncing on ground
    }
    
    // Enhanced brightness fade for lingering effect
    if (sparks[i].color == 4 || sparks[i].color == 5) {  // Yellow/white explosion sparks
      sparks[i].brightness = max(0.0f, sparks[i].brightness - 2.0f);  // Slower fade for drama
    } else {
      sparks[i].brightness = max(0.0f, sparks[i].brightness - 4.0f);  // Normal fade for regular sparks
    }
    
    // Remove if too dim or off screen
    if (sparks[i].brightness < 10 || sparks[i].position >= config_num_leds) {
      sparks.erase(sparks.begin() + i);
    }
  }
}

void createLevelFireworks(int numFireworks) {
  for (int i = 0; i < numFireworks; i++) {
    if (fireworks.size() < 6) {  // Reduced from 10 for performance
      Firework fw;
      fw.position = random(5, LEDS_PER_STRIP - 20);  // Calibrated to individual strip length
      fw.velocity = float(random(100, 140)) / 100.0;  // Slightly reduced velocity
      fw.brightness = 255;
      fw.exploded = false;
      fw.active = true;
      fireworks.push_back(fw);
      // NEW: Play firework launch sound
      playSound(EVT_FIREWORK_LAUNCH);
    }
  }
}

void updateFireworks(float deltaTime) {
  for (int i = fireworks.size() - 1; i >= 0; i--) {
    if (!fireworks[i].active) continue;
    
    if (!fireworks[i].exploded) {
      // Rising phase
      fireworks[i].velocity += gravity;
      fireworks[i].position += fireworks[i].velocity * 60 * deltaTime;
      fireworks[i].brightness *= 0.98;
      
      // Explode at apex
      if (fireworks[i].velocity <= 0) {
        fireworks[i].exploded = true;
        
        // Only play explosion sound if not during victory music
        if (!victoryMusicPlaying || millis() - victoryMusicStartTime > VICTORY_MUSIC_DURATION) {
          playSound(EVT_FIREWORK_EXPLODE);
        }
        
        // Create explosion sparks around firework position
        for (int s = 0; s < 20; s++) {
          if (sparks.size() < 100) {
            Spark spark;
            spark.position = fireworks[i].position;
            spark.velocity = (float(random(-200, 200)) / 100.0);
            spark.brightness = 255;
            spark.color = fireworks[i].color;
            spark.stripIndex = fireworks[i].stripIndex;  // Inherit strip from firework
            spark.active = true;
            sparks.push_back(spark);
          }
        }
      }
    } else {
      // Exploded - just fade away
      fireworks[i].brightness *= 0.9;
      if (fireworks[i].brightness < 10) {
        fireworks.erase(fireworks.begin() + i);
      }
    }
  }
}

void createStripFirework(int stripIndex, bool silent) {
  if (fireworks.size() < 50) {  // Allow more fireworks for celebration
    Firework fw;
    if (stripIndex == -1) {
      // All strips firework - use middle of LED range
      fw.position = random(10, LEDS_PER_STRIP - 10);
    } else {
      // Single strip firework - start from bottom
      fw.position = 5; 
    }
    fw.velocity = float(random(100, 140)) / 100.0;
    fw.brightness = 255;
    fw.color = (stripIndex == -1) ? 4 : stripIndex + 1;  // Gold for all strips, strip color for individual
    fw.stripIndex = stripIndex;
    fw.exploded = false;
    fw.active = true;
    fireworks.push_back(fw);
    
    // Only play sound if not in silent mode (during victory music)
    if (!silent) {
      playSound(EVT_FIREWORK_LAUNCH);
    }
  }
}

void createWipeEffect(int stripIndex) {
  WipeEffect wipe;
  wipe.stripIndex = stripIndex;
  wipe.position = 0;
  wipe.active = true;
  wipe.startTime = millis();
  wipeEffects.push_back(wipe);
}

void updatePlayerEffects() {
  unsigned long currentTime = millis();
  
  for (int strip = 0; strip < NUM_STRIPS; strip++) {
    // Update recoil - spring back to original position
    if (playerRecoil[strip] != 0) {
      playerRecoil[strip] *= 0.85f;  // Gradual return to position
      if (abs(playerRecoil[strip]) < 0.1f) {
        playerRecoil[strip] = 0;
      }
    }
    
    // Update sparkle animation
    playerSparkle[strip] = sin(currentTime * 0.08f + strip * 1.2f) * 0.5f + 0.5f;  // Faster sparkle
  }
}

void updateWipeEffects() {
  unsigned long currentTime = millis();
  
  for (int i = wipeEffects.size() - 1; i >= 0; i--) {
    WipeEffect& wipe = wipeEffects[i];
    
    if (wipe.active) {
      // Advance wipe position
      float speed = 3.0f;  // LEDs per update
      wipe.position += speed;
      
      // Check if wipe is complete
      if (wipe.position >= LEDS_PER_STRIP) {
        // Wipe complete - create firework and remove effect
        createStripFirework(wipe.stripIndex, false);  // With sound
        Serial.printf("🎆 Strip %d firework launched!\n", wipe.stripIndex);
        wipeEffects.erase(wipeEffects.begin() + i);
      }
    }
  }
}
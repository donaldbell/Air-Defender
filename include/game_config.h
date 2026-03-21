#pragma once

#include <Arduino.h>
#include <FastLED.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <vector>
#include "driver/i2s.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// --------------------------------------------------------------------------
// HARDWARE CONFIGURATION - ENVIRONMENTAL AIR QUALITY GAME
// --------------------------------------------------------------------------

// LED Strip Configuration - 3 parallel strips for air quality metrics
// Console board pins (ESP32 Wroom32)
// LED strips are on M5Stack display board - not used by console
// #define PIN_LED_PM25    // Not used - M5Stack handles LEDs
// #define PIN_LED_NO2     // Not used - M5Stack handles LEDs  
// #define PIN_LED_O3      // Not used - M5Stack handles LEDs
// #define PIN_LED_DATA    // Not used - M5Stack handles LEDs

// Pin configuration based on board type
#ifdef QTPY_S3
  // Adafruit QT Py S3 pin configuration
  // Available GPIO: 18(A0), 17(A1), 9(A2), 8(A3), 7(SDA), 6(SCL), 43(TX), 44(RX), 36(SCK), 37(MISO), 35(MOSI)
  
  // Built-in NeoPixel - use standard Arduino board definitions
  // PIN_NEOPIXEL and NEOPIXEL_POWER are defined automatically by the board package
  
  // Button pins - using A0-A3 analog pins as digital inputs
  #define PIN_BUTTON_BLUE  18  // A0 - Blue button for PM2.5 shots
  #define PIN_BUTTON_RED   17  // A1 - Red button for NO₂ (red) shots 
  #define PIN_BUTTON_GREEN  9  // A2 - Green button for O₃ shots
  #define PIN_SOUND_TOGGLE  8  // A3 - Sound on/off toggle switch
  
  // I2S Audio pins for amplifier (ESP32-S3 optimized)
  #define I2S_BCLK        35  // MOSI - Bit Clock
  #define I2S_LRC         36  // SCK - Left/Right Clock (Word Select)
  #define I2S_DOUT        37  // MISO - Data Out
  #define I2S_SD           6  // SCL - Shutdown pin (HIGH to enable)
  #define SAMPLE_RATE     44100
  
#else
  // Legacy ESP32-WROOM board pin configuration
  // Button pins
  #define PIN_BUTTON_BLUE  16  // Moved from 15 to avoid board conflicts
  #define PIN_BUTTON_RED   18  // Red button for NO₂ (red) shots 
  #define PIN_BUTTON_GREEN 17
  #define PIN_SOUND_TOGGLE 13  // Sound on/off toggle switch

  // I2S Audio pins for amplifier
  #define I2S_BCLK        25  // Bit Clock - DAC1 pin, great for audio
  #define I2S_LRC         27  // Left/Right Clock (Word Select) - Available GPIO
  #define I2S_DOUT        26  // Data Out - DAC2 pin, perfect for audio
  #define I2S_SD          33  // Shutdown pin - must be HIGH to enable amplifier
  #define SAMPLE_RATE     44100
#endif

#define LEDS_PER_STRIP  100 // Each strip represents one air quality metric
#define NUM_STRIPS      3   // PM2.5, NO₂, O₃
#define MAX_LEDS        (LEDS_PER_STRIP * NUM_STRIPS)
#define LED_TYPE        WS2812B
#define COLOR_ORDER     GRB

// --------------------------------------------------------------------------
// AUDIO DATA STRUCTURES
// --------------------------------------------------------------------------

struct ToneCmd { 
  int freq; 
  int duration; 
};

typedef std::vector<ToneCmd> Melody;

enum SoundEvent { 
  EVT_NONE=0, EVT_START, EVT_WIN, EVT_LOSE, EVT_MISTAKE,      
  EVT_HIT_SUCCESS, EVT_SHOT_BLUE, EVT_SHOT_RED, EVT_SHOT_GREEN, EVT_SHOT_WHITE,
  EVT_FIREWORK_LAUNCH, EVT_FIREWORK_EXPLODE, EVT_ENEMIES_BUILDING, EVT_LEVEL_VICTORY, EVT_SHOT_CANNON
};

// --------------------------------------------------------------------------
// ENVIRONMENTAL DATA STRUCTURES
// --------------------------------------------------------------------------

struct AirQualityData {
  int pm25;     // PM2.5 reading for blue strip enemy count
  int no2;      // NO₂ reading for red strip enemy count  
  int o3;       // O₃ reading for green strip enemy count
  String city;
  unsigned long lastUpdate;
  bool dataValid;
};

// Air Quality API Configuration
#define API_BASE_URL "http://api.waqi.info/feed/"
#define API_KEY "c96c0076ad7616b27dcd240b92f25f40703abe28"  // TODO: Replace with your AQICN API token from https://aqicn.org/data-platform/token/
#define UPDATE_INTERVAL 3600000  // Update every hour (3600000 ms)

struct EnvironmentalShot {
  float position;
  float velocity;
  int color;
  int stripIndex;  // Which strip (0=PM2.5, 1=NO₂, 2=O₃)
  bool isCannonShot;  // True for 3x power cannon shots
  int size;  // 1 for normal, 3 for cannon shots
  int damage;  // 1 for normal, 3 for cannon shots
};

struct PollutionEnemy {
  float position;
  int color;
  int stripIndex;
  bool active;
  int health;          // 4 hits required to destroy
  int maxHealth;       // Original health for visual effects
  float sparkTimer;    // Animation timer for damage effects
};

struct Spark {
  float position;
  float velocity;
  float brightness;
  int color;
  bool active;
  int stripIndex;  // Which strip this spark belongs to
};

struct Shot {
  float position;
  float velocity;
  int color;
};

struct Enemy {
  float position;
  int color;
  bool active;
  float speed;
  int originalIndex;
};

struct BossSegment {
  float position;
  int color;
  bool active;
  int originalIndex;
};

struct BossProjectile {
  float position;
  int color;
};

struct BossConfig {
  int numSegments;
  float speed;
  float shotFreq;
  int hits;
};

struct Firework {
  float position;
  float velocity;
  float brightness;
  int color;
  bool active;
  bool exploded;    // Whether the firework has exploded
  int stripIndex;
  int type;      // 0=normal, 1=delayed, 2=cascade
  float delay;   // For delayed fireworks
  int cascadeLevel;  // For multi-stage effects
};

struct WipeEffect {
  int stripIndex;
  float position;
  float speed;
  int color;
  bool active;
  bool direction; // true = up, false = down
  unsigned long startTime;  // When effect started
};

// Environmental Game State
enum EnvironmentalState {
  STATE_WIFI_CONNECTING,
  STATE_FETCHING_DATA,
  STATE_COLUMN_FILLING,
  STATE_ENVIRONMENTAL_GAME,
  STATE_DATA_ERROR,
  STATE_ENVIRONMENTAL_WIN
};

// --------------------------------------------------------------------------
// GLOBAL VARIABLES DECLARATIONS (defined in main.cpp)
// --------------------------------------------------------------------------

// Environmental Game Variables
extern bool wifiConnected;
extern std::vector<EnvironmentalShot> environmentalShots;
extern std::vector<PollutionEnemy> pollutionEnemies[NUM_STRIPS];  // Array of vectors for each strip
extern std::vector<Spark> sparks;
extern std::vector<Firework> fireworks;
extern std::vector<WipeEffect> wipeEffects;

// LED strip pointers for each pollution type
extern CRGB* stripPM25;  // Blue strip for PM2.5 fine particles
extern CRGB* stripNO2;   // Red strip for NO₂ nitrogen dioxide  
extern CRGB* stripO3;    // Green strip for O₃ ozone

// Game state variables
extern EnvironmentalState currentState;
extern AirQualityData currentAQI;
extern bool gameInitialized;
extern unsigned long stateStartTime;
extern unsigned long lastUpdate;
extern float gravity;

// Column animation variables
extern int currentColumnHeights[NUM_STRIPS];
extern int targetColumnHeights[NUM_STRIPS];
extern bool columnAnimationComplete;
extern unsigned long lastColumnUpdate;

// Victory and completion tracking
extern bool stripCompleted[NUM_STRIPS];
extern int completedStripCount;
extern bool allStripsCompleteEffect;

// Hero/player variables
extern float heroPosition;
extern int heroSize;
extern bool heroGrowing;
extern unsigned long heroGrowthStartTime;
extern bool cannonCharging;
extern unsigned long cannonChargeStartTime;

// Audio system variables
extern bool audioEnabled;
extern QueueHandle_t audioQueue;
extern TaskHandle_t audioTaskHandle;
extern bool priorityMelodyPlaying;

// Audio system variables
extern bool audioEnabled;
extern QueueHandle_t audioQueue;
extern TaskHandle_t audioTaskHandle;
extern bool priorityMelodyPlaying;
extern bool config_sound_on;
extern int config_volume_pct;

// Audio melodies
extern Melody melStart, melWin, melLose, melMistake, melShotBlue, melShotRed, melShotGreen, melShotWhite, melHit;
extern Melody melShotCannon;
extern Melody melFireworkLaunch, melFireworkExplode;
extern Melody melEnemiesBuilding, melLevelVictory;

// Button state tracking
extern bool lastBlueState, lastRedState, lastGreenState;
extern unsigned long lastBluePress, lastRedPress, lastGreenPress;

// Web server and configuration
extern WebServer server;
extern Preferences preferences;

// WiFi Configuration - ACCESS POINT MODE (No external WiFi needed!)
extern bool useAccessPointMode;
extern String config_ssid;
extern String config_pass;
extern String ap_ssid;
extern String ap_pass;

// Game configuration  
extern CRGB* leds;
extern int config_num_leds;
extern int config_brightness_pct;
extern bool config_static_ip;
extern String config_ip, config_gateway, config_subnet, config_dns;

// Legacy game variables (maintained for compatibility)
extern std::vector<Enemy> enemies;
extern std::vector<Shot> shots;
extern int currentLevel;
extern int enemyFrontIndex;
extern unsigned long levelStartTime;
extern float enemySpeed;
extern unsigned long lastEnemyMove;
extern unsigned long lastShotMove;
extern bool gameRunning;

// Constants
extern const float HERO_SPEED;
extern const float SHOT_SPEED;
extern const int DEBOUNCE_DELAY;
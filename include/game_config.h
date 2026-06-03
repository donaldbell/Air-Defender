#pragma once

#include <Arduino.h>
#include <FastLED.h>
#include <Preferences.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <vector>

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
  // A3 (GPIO 8) now free for future use
  
  // I2C LCD Display pins (20x4 with PCF8574 backpack)
  #define LCD_SDA_PIN     7   // I2C Data
  #define LCD_SCL_PIN     6   // I2C Clock  
  #define LCD_I2C_ADDR    0x27 // Standard PCF8574 address
  
  // I2S Audio pins for amplifier (ESP32-S3 optimized)
  #define I2S_BCLK        35  // MOSI - Bit Clock
  #define I2S_LRC         36  // SCK - Left/Right Clock (Word Select)
  #define I2S_DOUT        37  // MISO - Data Out
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
  #define SAMPLE_RATE     44100
#endif

#define LEDS_PER_STRIP  100 // Each strip represents one air quality metric
#define NUM_STRIPS      3   // PM2.5, NO₂, O₃
#define MAX_LEDS        (LEDS_PER_STRIP * NUM_STRIPS)
#define LED_TYPE        WS2812B
#define COLOR_ORDER     GRB

// LED Strip Orientation Configuration
#define FLIP_LED_STRIPS true  // Set to true for upside-down strip installation, false for normal

// --------------------------------------------------------------------------
// GAME PHYSICS & TIMING CONSTANTS
// --------------------------------------------------------------------------

// Physics constants
const float GRAVITY = -0.008f;                           // Shot acceleration
const float SHOT_INITIAL_VELOCITY = 2.0f;               // Starting shot speed
const float RECOIL_AMOUNT = 3.0f;                       // Hero recoil distance

// Timing constants
const unsigned long CANNON_CHARGE_TIME = 2000;          // 2 seconds to fully charge
const unsigned long CANNON_COOLDOWN = 5000;             // 5 seconds between cannon shots
const unsigned long CANNON_HOLD_TIME = 2000;            // 2 seconds for cannon (controller)
const unsigned long CHARGE_SOUND_DELAY = 500;           // Start charge sound after 500ms 
const unsigned long CHARGE_SOUND_INTERVAL = 300;        // Charge pulse every 300ms
const unsigned long STRIP_FIREWORK_DELAY = 1000;        // 1 second delay before fireworks
const unsigned long INPUT_TIMEOUT_MS = 10000;           // Input timeout (10 seconds)
const unsigned long FIRE_COOLDOWN_MS = 100;             // Fire cooldown to prevent button spam

// Button detection (controller startup)
const unsigned long BUTTON_CHECK_STARTUP_WINDOW = 2000;  // 2 seconds for startup button check  
const unsigned long BUTTON_CHECK_INTERVAL = 100;        // 100ms button check frequency

// Victory sequence timing
const unsigned long RAINBOW_WIPE_DURATION = 1000;       // 1 second rainbow wipe
const unsigned long VICTORY_MUSIC_DURATION = 5000;      // 5 seconds for dramatic victory music  
const unsigned long FIREWORKS_END_DELAY = 8000;         // End fireworks at 8 seconds (more time)
const unsigned long BLACKOUT_DELAY = 10000;              // Blackout at 10s, reset at 12s (more time)

// Display constants
const uint8_t LED_BRIGHTNESS = 255;                     // LED brightness (0-255) - maximum

// Realistic Firework Physics (optimized for 100-LED strips)
// Note: Adjusted for 70-80 LED explosion height and faster timing
const float FIREWORK_GRAVITY = -0.003f;           // Balanced gravity for good hangtime and reliable explosion
const float FIREWORK_LAUNCH_VEL_MIN = 0.65f;      // Controlled launch velocity for 70-80 LED height
const float FIREWORK_LAUNCH_VEL_MAX = 0.85f;      // Controlled max velocity for 70-80 LED height
const float FIREWORK_MAX_HEIGHT = 90.0f;          // Maximum height to prevent going off strip
const float FIREWORK_BRIGHTNESS_FADE = 0.990f;    // Launch brightness fade per frame
const float SPARK_FADE_RATE = 0.985f;             // Explosion spark fade rate per frame
const float GRAVITY_WEAKENING = 0.992f;           // Gravity weakens as sparks burn out per frame
const float COLOR_THRESHOLD_HIGH = 120.0f;        // White->yellow transition
const float COLOR_THRESHOLD_LOW = 50.0f;          // Red->black transition
const float FIXED_TIMESTEP = 4.0f;               // Target 240 FPS for extremely fast animation

// Game limits
const int MAX_SHOTS = 20;                               // Max shots to transmit
const int MAX_ENEMIES = 30;                             // Max enemies to transmit  
const int MAX_SPARKS = 50;                              // Max sparks to transmit
const int MAX_WIPE_EFFECTS = 3;                         // Max wipe effects to transmit

// Hardware pin definitions (board-specific)
#ifdef M5STACK_ATOMS3
  // M5Stack Atom S3 Lite (Console)
  #define LED_PIN_ONBOARD 35       // GPIO35 - Onboard RGB LED (status)
  #define LED_PIN_PM25    6        // GPIO6  - PM2.5 pollution strip (blue)
  #define LED_PIN_NO2     7        // GPIO7  - NO2 pollution strip (red)  
  #define LED_PIN_O3      8        // GPIO8  - O3 pollution strip (green)
  #define NUM_ONBOARD_LEDS 1       // Single status LED
#endif

#ifdef QTPY_S3
  // QT Py S3 (Controller)
  #define PIN_BTN_BLUE    A0       // Blue arcade button (PM2.5 pollution)
  #define PIN_BTN_RED     A1       // Red arcade button (NO2 pollution) 
  #define PIN_BTN_GREEN   A2       // Green arcade button (O3 pollution)
  #define NEOPIXEL_PIN    39       // QT Py S3 onboard NeoPixel
  #define NEOPIXEL_POWER  38       // NeoPixel power control
#endif

// --------------------------------------------------------------------------
// WHO AIR QUALITY GUIDELINES (2021 Annual Mean) - Universal thresholds
// Source: WHO Global Air Quality Guidelines 2021
// These are the same worldwide — not city-specific.
// Values are expressed in the same units the WAQI API returns:
//   PM2.5: µg/m³  (iaqi.pm25.v is raw µg/m³ — direct comparison)
//   NO2:   ppb    (web UI manual input / WAQI iaqi.no2.v ≈ ppb at most stations)
//   O3:    ppb    (web UI manual input / WAQI iaqi.o3.v ≈ ppb at most stations)
// Approximate match is acceptable for gameplay purposes.
// --------------------------------------------------------------------------
const int WHO_PM25 = 5;   // 5 µg/m³  — WHO 2021 annual mean
const int WHO_NO2  = 5;   // 5 ppb    — WHO 2021 annual mean (≈ 10 µg/m³)
const int WHO_O3   = 30;  // 30 ppb   — WHO 2021 peak-season (≈ 60 µg/m³)

// Enemy advance speed (pixels per frame at ~100 fps ≈ 1 pixel per 3 seconds)
// Increase to make enemies feel more urgent; decrease for easier play.
const float ENEMY_ADVANCE_SPEED = 0.020f;  // pixels/frame base advance speed

// --------------------------------------------------------------------------
// CITY DATA - Multi-city AQI roster
// --------------------------------------------------------------------------
struct CityData {
  const char* name;    // Display name
  const char* slug;    // WAQI API city slug (e.g. "barcelona")
  int pm25;            // PM2.5 reading µg/m³ (fallback default)
  int no2;             // NO₂ reading ppb (fallback default)
  int o3;              // O₃ reading ppb (fallback default)
};

// Hardcoded city fallbacks — used if "Load All Cities" is never run.
// Ordered by difficulty (easiest first). Default AQI values are conservative
// estimates from recent annual averages; live data always takes priority.
const CityData cityDefaults[] = {
  { "Sydney",      "sydney",       6,  8,  30 },  // Easy
  { "London",      "london",      10, 25,  40 },  // Easy-Med
  { "Barcelona",   "barcelona",   12, 20,  45 },  // Medium
  { "New York",    "new-york",    12, 22,  50 },  // Medium
  { "Mexico City", "mexico-city", 20, 40,  55 },  // Hard
  { "Shanghai",    "shanghai",    30, 35,  60 },  // Hard
  { "Mumbai",      "mumbai",      45, 30,  50 },  // Very Hard
  { "Delhi",       "delhi",       90, 50,  55 },  // Extreme
};
const int NUM_CITIES = sizeof(cityDefaults) / sizeof(cityDefaults[0]);

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
  bool active;        // Whether shot is active
  int size;  // 1 for normal, 3 for cannon shots
  int damage;  // 1 for normal, 4 for cannon shots
};

struct PollutionEnemy {
  float position;
  float startPosition;         // Initial landed position (for reset)
  int color;
  int stripIndex;
  bool active;
  int health;                  // Hits required to destroy
  int maxHealth;               // Original health for visual effects
  unsigned long sparkTimer;    // Animation timer for damage effects
  float advanceVelocity;       // Pixels per frame toward hero (base speed)
  float phaseOffset;           // Per-enemy sine phase for wave-pulse motion
};

struct Spark {
  float position;
  float velocity;
  uint8_t brightness;
  int color;
  bool active;
  int stripIndex;  // Which strip this spark belongs to
};

struct RealisticFirework {
    // Launch phase
    float flarePos;
    float flareVel; 
    float brightness;
    int stripIndex;
    bool active;
    
    // Launch trail sparks (5 sparks following behind)
    float trailSparkPos[5];
    float trailSparkVel[5];
    float trailSparkCol[5];
    
    // Explosion phase
    bool exploded;
    bool launchPhase;  // true during launch, false during explosion
    int nExplosionSparks;
    float explosionSparkPos[51];  // Reduced for 100-LED strips (vs 120 in reference)
    float explosionSparkVel[51];
    float explosionSparkCol[51];
    float dyingGravity;
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
  STATE_ATTRACT,               // Attract/demo loop shown before city-select and after inactivity
  STATE_CITY_SELECT,           // Browse & confirm city before game starts
  STATE_INTRO_ANIMATION,       // Cinematic intro wave
  STATE_ENEMIES_FALLING,       // Dynamic enemy drop-in physics
  STATE_WAITING_FOR_PLAYER,    // All enemies settled; waiting for controller 3-2-1 countdown to end
  STATE_ENVIRONMENTAL_GAME
};


/*
 * Console Unit - Environmental Air Quality Mining Game
 * Adapted from working single-board implementation for dual-board wireless system
 * 
 * This console implements the complete game mechanics with:
 * - Character-specific pollution animations (watery pulse, fiery flicker, wave patterns)
 * - Cannon charging system with 2-second charge time and visual feedback 
 * - Sophisticated spark explosion physics with position-based intensity
 * - Dramatic enemy destruction with 8-24 sparks and upward velocity
 * - Recoil spring-back hero physics 
 * - Wipe effects and delayed fireworks when strips cleared
 * - ESP-NOW wireless control input (adapted from GPIO)
 * 
 * Hardware: M5Stack Atom S3 Lite + 3× 100-LED WS2812B strips
 */

#include <Arduino.h>
#include <FastLED.h>
#include <WiFi.h>
#include <esp_now.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <vector>
#include "../include/protocol.h"
#include "../include/game_config.h"
#include "../include/communication_protocol.h"
#include "../include/status_led.h"
#include "../include/audio_system.h"

// =============================================================================
// DEBUG CONTROL SYSTEM
// =============================================================================

// Debug levels - set to 0 for production, 1 for errors only, 2 for info
#define DEBUG_LEVEL 1

#define DEBUG_ERROR   1   // Critical errors only
#define DEBUG_INFO    2   // Important status messages  
#define DEBUG_VERBOSE 3   // All debug output

// Debug macros - only prints if DEBUG_LEVEL allows it
#define DBG_ERROR(fmt, ...)   if (DEBUG_LEVEL >= DEBUG_ERROR)   Serial.printf("[ERROR] " fmt, ##__VA_ARGS__)
#define DBG_INFO(fmt, ...)    if (DEBUG_LEVEL >= DEBUG_INFO)    Serial.printf("[INFO] " fmt, ##__VA_ARGS__)
#define DBG_VERBOSE(fmt, ...) if (DEBUG_LEVEL >= DEBUG_VERBOSE) Serial.printf("[DEBUG] " fmt, ##__VA_ARGS__)

// =============================================================================
// LED STRIP MAPPING FUNCTION FOR UPSIDE-DOWN INSTALLATION
// =============================================================================

// Helper function to map LED indices for flipped strips
inline int mapLEDIndex(int position) {
    #if FLIP_LED_STRIPS
        return (LEDS_PER_STRIP - 1) - position;  // Flipped addressing
    #else
        return position;  // Normal addressing
    #endif
}

// =============================================================================
// HARDWARE CONFIGURATION - CONSOLE UNIT (M5Stack Atom S3 Lite)
// =============================================================================

// LED Arrays - Hardware Configuration
CRGB ledsOnboard[NUM_ONBOARD_LEDS];     // Status indicator
CRGB ledsPM25[LEDS_PER_STRIP];          // PM2.5 (blue strip)
CRGB ledsNO2[LEDS_PER_STRIP];           // NO₂ (red strip)  
CRGB ledsO3[LEDS_PER_STRIP];            // O₃ (green strip)

// =============================================================================
// GAME STATE VARIABLES
// =============================================================================

// Game State Variables
EnvironmentalState currentState = STATE_ATTRACT;
AirQualityData currentAQI;
unsigned long stateStartTime = 0;
unsigned long lastUpdate = 0;

// Game Collections - Core Implementation
std::vector<EnvironmentalShot> environmentalShots;
std::vector<PollutionEnemy> pollutionEnemies[NUM_STRIPS];  // Array of vectors for each strip
std::vector<Spark> sparks;
std::vector<RealisticFirework> fireworks;
std::vector<WipeEffect> wipeEffects;

// Hero System - Core Implementation
float playerPositions[NUM_STRIPS] = {99.0f, 99.0f, 99.0f};  // Heroes at the very top
float playerRecoil[NUM_STRIPS] = {0.0f, 0.0f, 0.0f};        // Hero pushback effect
float playerSparkle[NUM_STRIPS] = {0.0f, 0.0f, 0.0f};       // Hero sparkle animation
bool stripCompleted[NUM_STRIPS] = {false, false, false};    // Track completed strips

// Cannon Charging System - Core Implementation
unsigned long buttonPressTime[NUM_STRIPS] = {0, 0, 0};     // When button was first pressed
bool buttonCharging[NUM_STRIPS] = {false, false, false};   // Is button currently charging?
unsigned long lastCannonTime[NUM_STRIPS] = {0, 0, 0};    // Cooldown tracking for cannon shots

// ESP-NOW Communication Variables
unsigned long lastInputTime = 0;                           // Last input received timestamp

// =============================================================================
// UNIFIED MODULE INSTANCES
// =============================================================================
StatusLED statusLED;
// Note: CommunicationProtocol comm is global from communication_protocol.cpp

// Controller MAC address (replace with actual MAC from controller serial output)
uint8_t controllerMacAddress[] = {0xB4, 0x3A, 0x45, 0xB0, 0xDB, 0x08};

// Level Generation - Core Implementation  
int currentColumnHeights[NUM_STRIPS] = {0, 0, 0};
int targetColumnHeights[NUM_STRIPS] = {0, 0, 0};
int whoBaselinePixels[NUM_STRIPS] = {0, 0, 0};  // Static WHO-guideline pixel heights

// Multi-city roster — mutable copy of cityDefaults[], updated by CMD_CITY_ROSTER.
// Initialised in setup() so const char* pointers in cityDefaults carry over safely.
CityData cityRoster[NUM_CITIES];
int currentCityIndex = 0;  // Index into cityRoster[] for city-select UI

// Sequential strip intro system
static const unsigned long WHO_ANIM_DURATION = 400; // ms to grow WHO baseline pixels
int introStripIndex = 0;                    // Which strip is currently loading (0/1/2)
bool whoAnimating = false;                  // True while WHO pixels are animating in
unsigned long whoAnimStartTime = 0;         // When current strip WHO anim started
int whoAnimPixels[NUM_STRIPS] = {0, 0, 0};  // Animated WHO pixel heights
bool introStripEnemiesInitialized = false;  // Set after initializeFallingEnemiesForStrip() called

// Per-strip visual phase during the educational intro sequence.
// -1 = strip not yet revealed (dark), 0 = full-white column (slide 0),
//  1 = WHO baseline filling from bottom (slide 1),  2 = enemies dropping in (slide 2)
int introStripPhase[NUM_STRIPS] = {-1, -1, -1};

// Timing for the brief black-flash before the WHO fill animation on slide 1
static const unsigned long WHO_FLASH_DURATION = 400; // ms column stays dark before fill
bool whoFlashing[NUM_STRIPS] = {false, false, false};
unsigned long whoFlashStartTime[NUM_STRIPS] = {0, 0, 0};

// Blink state for slide 0 full-column white flash (3 blinks to draw attention)
static const int   SLIDE0_BLINK_COUNT    = 3;
static const unsigned long SLIDE0_BLINK_HALF_MS = 200; // on/off period (200ms each half = 400ms cycle)
bool slide0BlinkActive[NUM_STRIPS] = {false, false, false};
unsigned long slide0BlinkStartTime[NUM_STRIPS] = {0, 0, 0};

// Victory System - Core Implementation
int completedStripCount = 0;
unsigned long delayedFireworkTime[NUM_STRIPS] = {0, 0, 0};  // Timestamp for each strip
// Victory Music & Sequence - Dramatic Effect System
bool victoryMusicPlaying = false;
unsigned long victoryMusicStartTime = 0;
bool allStripsCompleteEffect = false;
unsigned long allStripsCompleteTime = 0;
bool finalFireworksTriggered = false;
bool gameBlackedOut = false;

// Hero death system
bool heroDead = false;             // True when an enemy has reached the hero
unsigned long heroDeathTime = 0;   // When death was triggered (for timed reset)
const unsigned long HERO_DEATH_DURATION = 6000;  // ms of death flash before city-select

// Encroach warning
const float ENCROACH_THRESHOLD = 15.0f;   // pixels from hero triggers warning sound
const unsigned long ENCROACH_INTERVAL = 4000; // ms between warning sounds
unsigned long lastEncroachWarnTime = 0;

// Enemy comet projectile system
struct EnemyComet {
    bool active;
    float position;
    float velocity;
    int stripIndex;
    int health;
    int maxHealth;
    unsigned long sparkTimer;
};

EnemyComet enemyComets[NUM_STRIPS];
unsigned long lastCometLaunchTime[NUM_STRIPS] = {0, 0, 0};
const unsigned long COMET_START_DELAY_MS = 5000;
const unsigned long COMET_COOLDOWN_MS = 5000;
const float COMET_SPEED = 0.10f;
const int COMET_HEALTH = 3;
const uint8_t COMET_LAUNCH_CHANCE_PERMIL = 6;

// =============================================================================
// ATTRACT SCREEN STATE
// =============================================================================
enum AttractPhase { ATTRACT_AURORA = 0, ATTRACT_DEMO = 1 };
static AttractPhase    attractPhase        = ATTRACT_AURORA;
static unsigned long   attractPhaseStart   = 0;
static bool            inAttractDemo       = false;
static unsigned long   demoLastFire[NUM_STRIPS]  = {0, 0, 0};
static int             demoShotType[NUM_STRIPS]  = {0, 0, 0}; // 0=single, 1=cannon, alternates
// Fixed demo AQI totals — moderate levels, always clears cleanly
static const int       DEMO_PM25_VAL       = 17;   // 12 enemies above WHO baseline of 5
static const int       DEMO_NO2_VAL        = 20;   // 15 enemies above WHO baseline of 5
static const int       DEMO_O3_VAL         = 45;   // 15 enemies above WHO baseline of 30
static const unsigned long ATTRACT_AURORA_DURATION = 10000UL; // ms per aurora theme
static const unsigned long CITY_SELECT_TIMEOUT_MS  = 20000UL; // ms inactivity → attract

// =============================================================================
// FALLING ENEMY SYSTEM - NEW
// =============================================================================

// Dynamic enemy storage for drop-in physics simulation
struct FallingEnemy {
    float position;      // Current Y position (can be above strip)
    float velocity;      // Downward velocity
    float targetPos;     // Final resting position 
    int color;
    int stripIndex;
    bool active;
    bool settled;        // True when enemy has reached final position
    int health;
    int maxHealth;
    unsigned long sparkTimer;
    float bounceVelocity; // For bounce effect when hitting target
    unsigned long spawnDelay; // Staggered spawn times
};

std::vector<FallingEnemy> fallingEnemies[NUM_STRIPS];

// =============================================================================
// ESP-NOW COMMUNICATION 
// =============================================================================

// =============================================================================
// FUNCTION FORWARD DECLARATIONS
// =============================================================================

void createWipeEffect(int stripIndex);
void initializeEnvironmentalGame();
void updateWipeEffects();
void createImpactSparks(float position, int color, int stripIndex, bool success);
void createStripFirework(int stripIndex);
void createLevelFireworks();
void updateFireworks(float deltaTime);
void renderCitySelectPreview();  // Static LED column preview for city-select state

// New function declarations for enhanced intro and falling enemies
void renderWaveIntro();

void initializeFallingEnemiesForStrip(int strip);
void updateFallingEnemies(float deltaTime);
void renderFallingEnemies();
void createPlayerShot(int stripIndex);
void createCannonShot(int stripIndex);
void resetEnemyComets();
void tryLaunchEnemyComets();
void updateEnemyComets(float deltaTime);
void renderAurora();
void initAttractDemo();
void updateAttract();

// =============================================================================
// COLOR SYSTEM - CORE IMPLEMENTATION
// =============================================================================

CRGB getColor(int colorCode) {
    switch (colorCode) {
        case 1: return CRGB(0, 0, 255);     // Blue (PM2.5)
        case 2: return CRGB(255, 0, 0);     // Red (NO₂)  
        case 3: return CRGB(0, 255, 0);     // Green (O₃)
        case 4: return CRGB(255, 255, 255); // White
        case 5: return CRGB(255, 255, 0);   // Yellow
        case 6: return CRGB(255, 0, 255);   // Magenta
        case 7: return CRGB(0, 255, 255);   // Cyan
        default: return CRGB(128, 128, 128); // Gray
    }
}

// =============================================================================
// SPARK SYSTEM - CORE IMPLEMENTATION
// =============================================================================

void createImpactSparks(float position, int color, int stripIndex, bool success) {
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
}

void createDramaticExplosion(float position, int stripIndex, int intensity) {
    // Core Implementation - Optimized explosion
    int numSparks = 8 + (intensity * 3);  // 8-17 sparks based on intensity
    
    for (int i = 0; i < numSparks; i++) {
        if (sparks.size() < 25) {  // Spark limit for performance
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
}

void updateSparks(float deltaTime) {
    for (int i = sparks.size() - 1; i >= 0; i--) {
        if (!sparks[i].active) continue;
        
        // Enhanced gravity and physics for dramatic explosions 
        float sparkGravity = -0.03f; // Stronger gravity for more realistic fall
        sparks[i].velocity += sparkGravity;
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
        
        // Spark physics
        if (sparks[i].position <= groundLevel) {
            // Spark hit ground - settle or bounce
            if (sparks[i].velocity < 0) {  // Falling
                sparks[i].velocity *= -0.3f;  // Small bounce
                sparks[i].position = groundLevel;
            }
        }
        
        // Fade out over time
        if (sparks[i].brightness > 4) {
            sparks[i].brightness -= 4;  // Fade speed
        } else {
            sparks[i].active = false;  // Remove spark
        }
        
        // Remove if off screen
        if (sparks[i].position < 0 || sparks[i].position >= LEDS_PER_STRIP) {
            sparks[i].active = false;
        }
    }
    
    // Clean up inactive sparks
    sparks.erase(
        std::remove_if(sparks.begin(), sparks.end(),
                      [](const Spark& spark) { return !spark.active; }),
        sparks.end());
}

// =============================================================================
// SHOT PHYSICS - CORE IMPLEMENTATION
// =============================================================================

// Send remaining enemy counts to controller LCD scoreboard.
// Called after each kill and once when the game starts.
// Debounced: sends at most once per 150 ms so rapid kills from cannon shots
// don't flood the ESP-NOW channel while fire commands are still arriving.
static void sendAQIStatus() {
    if (inAttractDemo) return;  // Don't update controller scoreboard during attract demo
    static unsigned long lastAQISendTime = 0;
    unsigned long now = millis();
    if (now - lastAQISendTime < 150) return;
    lastAQISendTime = now;

    AQIStatusMessage msg;
    msg.command      = CMD_AQI_STATUS;
    msg.remaining[0] = (int16_t)pollutionEnemies[0].size();
    msg.remaining[1] = (int16_t)pollutionEnemies[1].size();
    msg.remaining[2] = (int16_t)pollutionEnemies[2].size();
    memset(msg.padding, 0, sizeof(msg.padding));
    comm.sendMessage(controllerMacAddress, msg);
}

void resetEnemyComets() {
    for (int strip = 0; strip < NUM_STRIPS; strip++) {
        enemyComets[strip].active = false;
        enemyComets[strip].position = 0.0f;
        enemyComets[strip].velocity = COMET_SPEED;
        enemyComets[strip].stripIndex = strip;
        enemyComets[strip].health = COMET_HEALTH;
        enemyComets[strip].maxHealth = COMET_HEALTH;
        enemyComets[strip].sparkTimer = 0;
        lastCometLaunchTime[strip] = 0;
    }
}

void tryLaunchEnemyComets() {
    if (millis() - stateStartTime < COMET_START_DELAY_MS) return;

    unsigned long now = millis();
    for (int strip = 0; strip < NUM_STRIPS; strip++) {
        if (stripCompleted[strip]) continue;
        if (enemyComets[strip].active) continue;
        if (pollutionEnemies[strip].empty()) continue;
        if (now - lastCometLaunchTime[strip] < COMET_COOLDOWN_MS) continue;
        if (random(1000) >= COMET_LAUNCH_CHANCE_PERMIL) continue;

        float topEnemyPos = -1.0f;
        for (const auto& enemy : pollutionEnemies[strip]) {
            if (!enemy.active) continue;
            topEnemyPos = max(topEnemyPos, enemy.position);
        }

        if (topEnemyPos < 0.0f) continue;

        enemyComets[strip].active = true;
        enemyComets[strip].position = topEnemyPos;
        enemyComets[strip].velocity = COMET_SPEED;
        enemyComets[strip].stripIndex = strip;
        enemyComets[strip].health = COMET_HEALTH;
        enemyComets[strip].maxHealth = COMET_HEALTH;
        enemyComets[strip].sparkTimer = 0;
        lastCometLaunchTime[strip] = now;

        if (!inAttractDemo) {
            AudioMessage launchMsg;
            launchMsg.command    = CMD_PLAY_AUDIO;
            launchMsg.soundEvent = EVT_COMET_LAUNCH;
            memset(launchMsg.padding, 0, sizeof(launchMsg.padding));
            comm.sendMessage(controllerMacAddress, launchMsg);
        }
    }
}

void updateEnemyComets(float deltaTime) {
    (void)deltaTime;

    for (int strip = 0; strip < NUM_STRIPS; strip++) {
        if (!enemyComets[strip].active) continue;

        enemyComets[strip].position += enemyComets[strip].velocity;
        if (enemyComets[strip].position > LEDS_PER_STRIP - 1) {
            enemyComets[strip].position = LEDS_PER_STRIP - 1;
        }

        if (random(100) < 40 && sparks.size() < 100) {
            Spark spark;
            spark.position = enemyComets[strip].position - random(1, 4);
            spark.velocity = float(random(20, 220)) / 1000.0f;
            spark.brightness = 200 + random(56);
            spark.color = (random(100) < 70) ? 4 : 5;
            spark.active = true;
            spark.stripIndex = strip;
            sparks.push_back(spark);
        }
    }
}

void updateEnvironmentalGame() {
    // Calculate delta time for smooth physics
    unsigned long currentTime = millis();
    float deltaTime = (currentTime - lastUpdate) / 1000.0f;
    deltaTime = min(deltaTime, 0.015f);  // Cap at 15ms for extremely fast response
    lastUpdate = currentTime;
    
    // Handle different game states
    switch (currentState) {
        case STATE_CITY_SELECT:
            // No physics — waiting for player to browse and confirm a city
            return;

        case STATE_INTRO_ANIMATION:
            // No physics during intro - just visual effects
            return;
            
        case STATE_ENEMIES_FALLING:
            // Update falling enemy physics only
            updateFallingEnemies(deltaTime);
            return;

        case STATE_WAITING_FOR_PLAYER:
            // Enemies settled and displayed; hold until CMD_PLAYER_READY arrives
            return;

        case STATE_ATTRACT:
            updateAttract();
            return;
            
        case STATE_ENVIRONMENTAL_GAME:
            // Continue with existing game logic...
            break;
            
        default:
            // Handle other states as before
            break;
    }

    // Advance enemies toward the hero with wave-pulse motion.
    // Base advance pushes enemies up; a per-enemy sine wave adds surge-and-retreat
    // rhythm, like waves washing up a beach — visually interesting but net positive.
    if (currentState == STATE_ENVIRONMENTAL_GAME && !allStripsCompleteEffect) {
        float t = millis() * 0.001f;  // time in seconds
        // Pulse amplitude: 30% of base speed so the net motion stays forward
        const float PULSE_AMP = ENEMY_ADVANCE_SPEED * 0.3f;

        for (int strip = 0; strip < NUM_STRIPS; strip++) {
            for (auto& enemy : pollutionEnemies[strip]) {
                if (enemy.active) {
                    float wave = PULSE_AMP * sinf(t * 2.0f + enemy.phaseOffset);
                    enemy.position += enemy.advanceVelocity + wave;
                    if (enemy.position >= LEDS_PER_STRIP - 1) {
                        enemy.position = LEDS_PER_STRIP - 1;
                    }
                    // Don't let pulse push enemy below its start position
                    if (enemy.position < enemy.startPosition) {
                        enemy.position = enemy.startPosition;
                    }
                }
            }
        }

        updateEnemyComets(deltaTime);
        tryLaunchEnemyComets();

        // Demo: silently defeat any enemy that advances to the player position so
        // they never visibly linger at the top of the column.
        if (inAttractDemo) {
            for (int strip = 0; strip < NUM_STRIPS; strip++) {
                if (stripCompleted[strip]) continue;
                bool anyActive = false;
                for (auto& enemy : pollutionEnemies[strip]) {
                    if (enemy.active) {
                        if (enemy.position >= playerPositions[strip] - 1.0f) {
                            enemy.active = false;
                        } else {
                            anyActive = true;
                        }
                    }
                }
                if (!anyActive) {
                    stripCompleted[strip] = true;
                    enemyComets[strip].active = false;
                    completedStripCount++;
                    createWipeEffect(strip);
                    delayedFireworkTime[strip] = millis() + STRIP_FIREWORK_DELAY;
                }
            }
        }

        // Encroach warning — periodic low rumble when any enemy is close to the hero
        if (!heroDead && !inAttractDemo && millis() - lastEncroachWarnTime > ENCROACH_INTERVAL) {
            for (int strip = 0; strip < NUM_STRIPS; strip++) {
                if (enemyComets[strip].active && (playerPositions[strip] - enemyComets[strip].position) < ENCROACH_THRESHOLD) {
                    AudioMessage encroachMsg;
                    encroachMsg.command    = CMD_PLAY_AUDIO;
                    encroachMsg.soundEvent = EVT_ENEMY_ENCROACH;
                    memset(encroachMsg.padding, 0, sizeof(encroachMsg.padding));
                    comm.sendMessage(controllerMacAddress, encroachMsg);
                    lastEncroachWarnTime = millis();
                    goto encroachDone;
                }

                for (const auto& enemy : pollutionEnemies[strip]) {
                    if (enemy.active && (playerPositions[strip] - enemy.position) < ENCROACH_THRESHOLD) {
                        AudioMessage encroachMsg;
                        encroachMsg.command    = CMD_PLAY_AUDIO;
                        encroachMsg.soundEvent = EVT_ENEMY_ENCROACH;
                        memset(encroachMsg.padding, 0, sizeof(encroachMsg.padding));
                        comm.sendMessage(controllerMacAddress, encroachMsg);
                        lastEncroachWarnTime = millis();
                        goto encroachDone;  // Only warn once per interval
                    }
                }
            }
            encroachDone:;
        }

        // Attract demo auto-fire — alternates single shots and single cannon shots.
        // Strips fire on staggered intervals so shots don't all land simultaneously.
        if (inAttractDemo && !allStripsCompleteEffect) {
            unsigned long demoNow = millis();
            static const unsigned long demoInterval[NUM_STRIPS] = {1100, 1400, 900};
            for (int strip = 0; strip < NUM_STRIPS; strip++) {
                if (!stripCompleted[strip] && !pollutionEnemies[strip].empty()) {
                    if (demoNow - demoLastFire[strip] >= demoInterval[strip]) {
                        if (demoShotType[strip] == 0) {
                            // Single player shot
                            createPlayerShot(strip);
                        } else {
                            // Single cannon shot (one projectile, not the triple burst)
                            EnvironmentalShot cs;
                            cs.position     = playerPositions[strip];
                            cs.velocity     = -SHOT_INITIAL_VELOCITY - 0.5f;
                            cs.stripIndex   = strip;
                            cs.color        = strip + 1;
                            cs.active       = true;
                            cs.isCannonShot = true;
                            cs.size         = 3;
                            cs.damage       = 4;
                            environmentalShots.push_back(cs);
                            playerRecoil[strip] = -4.0f;
                        }
                        demoShotType[strip] = 1 - demoShotType[strip];  // toggle
                        demoLastFire[strip] = demoNow;
                    }
                }
            }
        }

        // Hero collision detection — if any enemy reaches the hero, trigger death
        if (!heroDead) {
            for (int strip = 0; strip < NUM_STRIPS; strip++) {
                if (enemyComets[strip].active && enemyComets[strip].position >= playerPositions[strip] - 1.0f) {
                    if (inAttractDemo) {
                        // Demo: silently dismiss the comet — don't trigger death or send messages
                        enemyComets[strip].active = false;
                    } else {
                        heroDead = true;
                        heroDeathTime = millis();
                        AudioMessage deathMsg;
                        deathMsg.command    = CMD_PLAY_AUDIO;
                        deathMsg.soundEvent = EVT_LOSE;
                        memset(deathMsg.padding, 0, sizeof(deathMsg.padding));
                        comm.sendMessage(controllerMacAddress, deathMsg);
                        Serial.println("💀 Comet reached hero — death triggered!");
                        break;
                    }
                }

                for (const auto& enemy : pollutionEnemies[strip]) {
                    if (enemy.active && enemy.position >= playerPositions[strip] - 1.0f) {
                        if (inAttractDemo) break;  // Demo: let enemy pass through silently
                        heroDead = true;
                        heroDeathTime = millis();
                        AudioMessage deathMsg;
                        deathMsg.command    = CMD_PLAY_AUDIO;
                        deathMsg.soundEvent = EVT_LOSE;
                        memset(deathMsg.padding, 0, sizeof(deathMsg.padding));
                        comm.sendMessage(controllerMacAddress, deathMsg);
                        Serial.println("💀 Enemy reached hero — death triggered!");
                        break;
                    }
                }
                if (heroDead) break;
            }
        }

        // Death sequence: flash strips red for HERO_DEATH_DURATION then return to city-select
        if (heroDead) {
            unsigned long elapsed = millis() - heroDeathTime;
            // Flash all strips red at 4 Hz
            bool flashOn = (elapsed / 125) % 2 == 0;
            fill_solid(ledsPM25, LEDS_PER_STRIP, flashOn ? CRGB::Red : CRGB::Black);
            fill_solid(ledsNO2,  LEDS_PER_STRIP, flashOn ? CRGB::Red : CRGB::Black);
            fill_solid(ledsO3,   LEDS_PER_STRIP, flashOn ? CRGB::Red : CRGB::Black);
            FastLED.show();

            if (elapsed >= HERO_DEATH_DURATION) {
                heroDead = false;
                if (inAttractDemo) {
                    // Demo death — return to aurora phase (shouldn't occur with correct auto-fire)
                    inAttractDemo     = false;
                    currentState      = STATE_ATTRACT;
                    attractPhase      = ATTRACT_AURORA;
                    attractPhaseStart = millis();
                } else {
                    AudioMessage endMsg;
                    endMsg.command = CMD_GAME_ENDED;
                    endMsg.soundEvent = 0;
                    comm.sendMessage(controllerMacAddress, &endMsg, sizeof(endMsg));
                    currentState  = STATE_CITY_SELECT;
                    lastInputTime = millis();
                    currentCityIndex = 0;
                    currentAQI.pm25 = cityRoster[0].pm25;
                    currentAQI.no2  = cityRoster[0].no2;
                    currentAQI.o3   = cityRoster[0].o3;
                    currentAQI.city = cityRoster[0].name;
                }
            }
            return;  // Skip normal update during death flash
        }
    }

    // Handle shots for each strip
    for (int i = environmentalShots.size() - 1; i >= 0; i--) {
        // Apply gravity (shots accelerate downward)
        environmentalShots[i].velocity += GRAVITY;  
        environmentalShots[i].position += environmentalShots[i].velocity * 60 * deltaTime;
        
        // Remove shots that go off screen (bottom)
        if (environmentalShots[i].position < 0) {
            createImpactSparks(environmentalShots[i].position, environmentalShots[i].color, 
                             environmentalShots[i].stripIndex, false);
            environmentalShots.erase(environmentalShots.begin() + i);
            continue;
        }
        
        // Check collision with pollution enemies (mining mechanic)
        int stripIndex = environmentalShots[i].stripIndex;
        bool shotRemoved = false;

        // Check collision with comet projectile first.
        if (enemyComets[stripIndex].active) {
            float cometDistance = abs(environmentalShots[i].position - enemyComets[stripIndex].position);
            float cometCollisionRange = environmentalShots[i].isCannonShot ? 4.0f : 3.0f;

            if (cometDistance < cometCollisionRange) {
                enemyComets[stripIndex].health -= max(1, environmentalShots[i].damage);
                enemyComets[stripIndex].sparkTimer = millis() + 250;
                createImpactSparks(environmentalShots[i].position, 5, stripIndex, true);

                if (enemyComets[stripIndex].health <= 0) {
                    createDramaticExplosion(enemyComets[stripIndex].position, stripIndex, 1);
                    enemyComets[stripIndex].active = false;
                    enemyComets[stripIndex].sparkTimer = 0;
                }

                environmentalShots.erase(environmentalShots.begin() + i);
                shotRemoved = true;
            }
        }

        if (shotRemoved) {
            continue;
        }
        
        for (int e = pollutionEnemies[stripIndex].size() - 1; e >= 0; e--) {
            if (pollutionEnemies[stripIndex][e].active) {
                float distance = abs(environmentalShots[i].position - pollutionEnemies[stripIndex][e].position);
                
                // Collision detection - Standard ranges
                float collisionRange = environmentalShots[i].isCannonShot ? 4.0f : 3.0f;
                
                if (distance < collisionRange) {
                    if (environmentalShots[i].color == pollutionEnemies[stripIndex][e].color) {
                        // Successful hit - chip away at pollution!
                        int damage = environmentalShots[i].damage;  // 1 for normal, 4 for cannon
                        pollutionEnemies[stripIndex][e].health -= damage;
                        pollutionEnemies[stripIndex][e].sparkTimer = millis() + 300;  // Spark effect for 300ms
                        
                        if (pollutionEnemies[stripIndex][e].health <= 0) {
                            // Fully destroyed - CREATE DRAMATIC EXPLOSION!
                            
                            // Calculate explosion intensity based on proximity to end
                            int remainingEnemies = 0;
                            for (int s = 0; s < NUM_STRIPS; s++) {
                                remainingEnemies += pollutionEnemies[s].size();
                            }
                            int intensity = max(1, 3 - (remainingEnemies / 10));  // Scale 1-3 based on remaining
                            
                            createDramaticExplosion(pollutionEnemies[stripIndex][e].position, stripIndex, intensity);
                            pollutionEnemies[stripIndex].erase(pollutionEnemies[stripIndex].begin() + e);
                            sendAQIStatus();  // Update LCD scoreboard
                            
                            // Check if strip is now completely cleared
                            if (!stripCompleted[stripIndex] && pollutionEnemies[stripIndex].empty()) {
                                stripCompleted[stripIndex] = true;
                                enemyComets[stripIndex].active = false;
                                // Send ESP-NOW command to disable audio for this strip (real game only)
                                if (!inAttractDemo) {
                                    AudioMessage disableMsg;
                                    disableMsg.command    = CMD_DISABLE_STRIP_AUDIO;
                                    disableMsg.soundEvent = (uint8_t)stripIndex;
                                    memset(disableMsg.padding, 0, sizeof(disableMsg.padding));
                                    comm.sendMessage(controllerMacAddress, disableMsg);
                                }
                                DBG_VERBOSE("Strip %d audio disabled\n", stripIndex);
                                completedStripCount++;
                                createWipeEffect(stripIndex);
                                
                                // Schedule delayed firework (1 second delay)
                                delayedFireworkTime[stripIndex] = millis() + STRIP_FIREWORK_DELAY;
                                DBG_INFO("Strip %d cleared! Fireworks scheduled\n", stripIndex);
                            }
                        } else {
                            // Damaged but not destroyed
                            createImpactSparks(environmentalShots[i].position, environmentalShots[i].color, 
                                             stripIndex, false);
                        }
                    } else {
                        // Wrong color - no effect!
                        createImpactSparks(environmentalShots[i].position, environmentalShots[i].color, 
                                         stripIndex, false);
                        Serial.printf("❌ MISS! Shot(color:%d) vs Enemy(color:%d) - wrong color!\n", 
                                     environmentalShots[i].color, pollutionEnemies[stripIndex][e].color);
                    }
                    environmentalShots.erase(environmentalShots.begin() + i);
                    shotRemoved = true;
                    break;
                }
            }
        }
    }
    
    // Update spark physics
    updateSparks(deltaTime);

    // =============================
    // FIREWORK CELEBRATION SYSTEM
    // =============================
    updateFireworks(deltaTime);

    // Handle delayed strip fireworks
    for (int i = 0; i < NUM_STRIPS; i++) {
        if (stripCompleted[i] && delayedFireworkTime[i] > 0 && millis() >= delayedFireworkTime[i]) {
            createStripFirework(i);
            delayedFireworkTime[i] = 0;
        }
    }

    // Victory Detection - use flags, not immediate state change
    if (currentState == STATE_ENVIRONMENTAL_GAME && !allStripsCompleteEffect) {
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
            DBG_INFO("ALL POLLUTION DEFEATED! Clean air achieved!\n");
            allStripsCompleteEffect = true;
            allStripsCompleteTime = millis();
            victoryMusicPlaying = true;
            victoryMusicStartTime = millis();
            finalFireworksTriggered = false;
            gameBlackedOut = false;
            
            // Play victory melody via ESP-NOW to controller (not during attract demo)
            DBG_INFO("Victory sequence starting - sending music command\n");
            if (!inAttractDemo) {
                AudioMessage victoryMsg;
                victoryMsg.command    = CMD_PLAY_AUDIO;
                victoryMsg.soundEvent = EVT_LEVEL_VICTORY;
                memset(victoryMsg.padding, 0, sizeof(victoryMsg.padding));
                comm.sendMessage(controllerMacAddress, victoryMsg);
            }
            
            // Don't start fireworks yet - wait for rainbow wipe to complete
            DBG_VERBOSE("Rainbow wipe and fireworks starting\n");
        }
    }

    // Victory Sequence Management - Dramatic Version
    if (allStripsCompleteEffect) {
        unsigned long elapsed = millis() - allStripsCompleteTime;
        

        
        // Start fireworks AFTER rainbow wipe completes (at 1.2 seconds) 
        if (!finalFireworksTriggered && elapsed >= 1200) {
            DBG_VERBOSE("Celebration fireworks starting\n");
            finalFireworksTriggered = true;
            
            // Create beautiful fireworks celebration (visible on black background)
            createLevelFireworks();  // Single wave of 3 beautiful fireworks
        }
        
        // Phase 1: Music continues for full 5 seconds
        if (victoryMusicPlaying && elapsed >= VICTORY_MUSIC_DURATION) {
            victoryMusicPlaying = false;
            // Victory music complete - removed verbose logging
        }
        
        // Phase 2: Final SPECTACULAR fireworks (at 4.6 seconds - cut delay in half)
        if (elapsed >= 4600 && finalFireworksTriggered && fireworks.size() < 3) {
            DBG_VERBOSE("Additional celebration fireworks\n");
            
            // Create a few additional final fireworks (not overwhelming)
            for (int strip = 0; strip < NUM_STRIPS; strip++) {
                createStripFirework(strip);  // One final firework per strip
            }
        }
        
        // Phase 3: Blackout and prepare reset (at 7 seconds)
        if (elapsed >= BLACKOUT_DELAY && !gameBlackedOut) {
            DBG_VERBOSE("Victory blackout - preparing reset\n");
            gameBlackedOut = true;
            
            // Clear all effects for clean transition
            fireworks.clear();
            sparks.clear(); 
            wipeEffects.clear();
        }
        
        // Phase 4: Final reset → city select (real game) or aurora (attract demo)
        if (elapsed >= BLACKOUT_DELAY + 2000) {
            // Reset all victory flags
            allStripsCompleteEffect = false;
            victoryMusicPlaying = false;
            finalFireworksTriggered = false;
            gameBlackedOut = false;

            // Clear remaining effects
            fireworks.clear();
            sparks.clear();
            wipeEffects.clear();

            if (inAttractDemo) {
                // Demo complete — return to aurora phase
                inAttractDemo     = false;
                currentState      = STATE_ATTRACT;
                attractPhase      = ATTRACT_AURORA;
                attractPhaseStart = millis();
                DBG_INFO("Attract demo complete — returning to aurora\n");
            } else {
                DBG_INFO("Game over — returning to city select\n");
                // Notify controller to re-enter city-select mode
                AudioMessage endMsg;
                endMsg.command = CMD_GAME_ENDED;
                endMsg.soundEvent = 0;
                comm.sendMessage(controllerMacAddress, &endMsg, sizeof(endMsg));
                currentState  = STATE_CITY_SELECT;
                lastInputTime = millis();
            }
        }
    }
}

// =============================================================================
// HERO PHYSICS - CORE IMPLEMENTATION  
// =============================================================================

void updatePlayerEffects(unsigned long currentTime) {
    // Update hero recoil spring-back animation
    for (int strip = 0; strip < NUM_STRIPS; strip++) {
        playerRecoil[strip] *= 0.85f;  // Spring back to center
        // Hero sparkle: time-based sine wave
        playerSparkle[strip] = sin(currentTime * 0.08f + strip * 1.2f) * 0.5f + 0.5f;
    }
}

// =============================================================================
// WIPE EFFECTS - CORE IMPLEMENTATION
// =============================================================================

void updateWipeEffects() {
    for (int i = wipeEffects.size() - 1; i >= 0; i--) {
        if (wipeEffects[i].active) {
            wipeEffects[i].position += wipeEffects[i].speed;
            
            if (wipeEffects[i].position >= LEDS_PER_STRIP) {
                wipeEffects[i].active = false;
            }
        }
    }
    
    // Clean up inactive wipes
    wipeEffects.erase(
        std::remove_if(wipeEffects.begin(), wipeEffects.end(),
                      [](const WipeEffect& wipe) { return !wipe.active; }),
        wipeEffects.end());
}

// =============================================================================
// LEVEL GENERATION - CORE IMPLEMENTATION
// =============================================================================

// Persistent AQI storage (from backup)
void loadSavedAQI() {
    Preferences preferences;
    preferences.begin("console", true);  // read-only
    currentAQI.pm25 = preferences.getInt("aqi_pm25", 25);
    currentAQI.no2  = preferences.getInt("aqi_no2",  35);
    currentAQI.o3   = preferences.getInt("aqi_o3",   45);
    preferences.end();
    DBG_INFO("Loaded saved AQI - PM2.5: %d, NO2: %d, O3: %d\n", currentAQI.pm25, currentAQI.no2, currentAQI.o3);
}

void saveAQI() {
    Preferences preferences;
    preferences.begin("console", false);
    preferences.putInt("aqi_pm25", currentAQI.pm25);
    preferences.putInt("aqi_no2",  currentAQI.no2);
    preferences.putInt("aqi_o3",   currentAQI.o3);
    preferences.end();
    DBG_VERBOSE("AQI data saved to preferences\n");
}

void generateLevelFromAQI() {
    // Clear existing enemies
    for (int strip = 0; strip < NUM_STRIPS; strip++) {
        pollutionEnemies[strip].clear();
        stripCompleted[strip] = false;
    }
    completedStripCount = 0;
    
    // 1:1 mapping — 1 LED = 1 unit of pollutant
    // WHO baseline and city values are used directly as pixel counts.
    int totalColumnHeights[NUM_STRIPS];
    totalColumnHeights[0] = constrain((int)currentAQI.pm25, 1, LEDS_PER_STRIP - 1);
    totalColumnHeights[1] = constrain((int)currentAQI.no2,  1, LEDS_PER_STRIP - 1);
    totalColumnHeights[2] = constrain((int)currentAQI.o3,   1, LEDS_PER_STRIP - 1);

    // WHO 2021 annual mean baselines — fixed pixel heights, same for every city
    whoBaselinePixels[0] = WHO_PM25;   // 5  pixels (µg/m³)
    whoBaselinePixels[1] = WHO_NO2;    // 5  pixels (ppb)
    whoBaselinePixels[2] = WHO_O3;     // 30 pixels (ppb)

    // Enemy count = pixels above the WHO baseline.
    // WHO baseline is fixed (universal guideline), so never clamp it to the city total.
    // If a city is already below WHO level, there are simply 0 enemies — use 1 for playability.
    for (int strip = 0; strip < NUM_STRIPS; strip++) {
        targetColumnHeights[strip] = max(1, totalColumnHeights[strip] - whoBaselinePixels[strip]);
    }

    Serial.printf("📊 AQI → Total: PM2.5=%d, NO₂=%d, O₃=%d | WHO base: %d, %d, %d | Enemies: %d, %d, %d\n",
                  totalColumnHeights[0], totalColumnHeights[1], totalColumnHeights[2],
                  whoBaselinePixels[0], whoBaselinePixels[1], whoBaselinePixels[2],
                  targetColumnHeights[0], targetColumnHeights[1], targetColumnHeights[2]);
    
    // Reset animation
    for (int strip = 0; strip < NUM_STRIPS; strip++) {
        currentColumnHeights[strip] = 0;
    }
    
    // Reset sequential intro state
    introStripIndex = 0;
    whoAnimating = false;
    introStripEnemiesInitialized = false;
    whoAnimPixels[0] = whoAnimPixels[1] = whoAnimPixels[2] = 0;
    introStripPhase[0] = introStripPhase[1] = introStripPhase[2] = -1;
    whoFlashing[0] = whoFlashing[1] = whoFlashing[2] = false;
    slide0BlinkActive[0] = slide0BlinkActive[1] = slide0BlinkActive[2] = false;

    // Start with wave intro animation
    currentState = STATE_INTRO_ANIMATION;
    stateStartTime = millis();

    DBG_INFO("Starting cinematic intro\n");
}

void createPollutionEnemies() {
    // NEW: Enemies are now created dynamically through falling system
    DBG_INFO("Creating dynamic falling enemies\n");
    // Enemies will be created through the falling system during STATE_ENEMIES_FALLING
    // This function is kept for compatibility but enemies are now created in initializeFallingEnemies()
}

// =============================================================================
// CORE GAME FUNCTIONS - COMPLETE IMPLEMENTATIONS  

// =============================
// FIREWORK CELEBRATION SYSTEM - COMPLETE PORT
// =============================

void createStripFirework(int stripIndex) {
    // Create a realistic firework for a cleared strip
    RealisticFirework fw;
    
    // Initialize launch phase
    fw.flarePos = 0;
    fw.flareVel = FIREWORK_LAUNCH_VEL_MIN + 
                  (float(random16(0, 1000)) / 1000.0f) * 
                  (FIREWORK_LAUNCH_VEL_MAX - FIREWORK_LAUNCH_VEL_MIN);
    fw.brightness = 1.0f;
    fw.stripIndex = stripIndex;
    fw.active = true;
    fw.exploded = false;
    fw.launchPhase = true;
    
    // Initialize 5 trailing sparks behind the flare
    for (int i = 0; i < 5; i++) {
        fw.trailSparkPos[i] = 0;
        fw.trailSparkVel[i] = (float(random8()) / 255.0f) * (fw.flareVel / 5.0f);
        fw.trailSparkCol[i] = fw.trailSparkVel[i] * 1000.0f;
        fw.trailSparkCol[i] = constrain(fw.trailSparkCol[i], 0, 255);
    }
    
    // Explosion phase will be initialized when flare reaches apex
    fw.nExplosionSparks = 0;
    fw.dyingGravity = FIREWORK_GRAVITY;
    
    fireworks.push_back(fw);
    Serial.printf("🎆 Realistic firework launched for strip %d! Launch velocity: %.3f\n", 
                  stripIndex, fw.flareVel);
}

void createLevelFireworks() {
    // Create ONE beautiful firework per strip for level clear - simple and elegant
    for (int i = 0; i < NUM_STRIPS; i++) {
        createStripFirework(i);  // One perfect firework per strip
    }
    DBG_VERBOSE("Level clear: 3 fireworks launched\n");
}

void updateFireworks(float deltaTime) {
    // Convert deltaTime to frame multiplier (reference assumes 240 FPS = 4ms)
    float frameMultiplier = (deltaTime * 1000.0f) / FIXED_TIMESTEP;
    frameMultiplier = constrain(frameMultiplier, 1.5f, 12.0f); // Allow extremely fast timing
    
    for (int i = fireworks.size() - 1; i >= 0; i--) {
        RealisticFirework& fw = fireworks[i];
        if (!fw.active) continue;

        if (fw.launchPhase) {
            // === LAUNCH PHASE ===
            // Update trailing sparks
            for (int s = 0; s < 5; s++) {
                fw.trailSparkPos[s] += fw.trailSparkVel[s] * frameMultiplier;
                fw.trailSparkPos[s] = constrain(fw.trailSparkPos[s], 0, LEDS_PER_STRIP);
                fw.trailSparkVel[s] += FIREWORK_GRAVITY * frameMultiplier;
                fw.trailSparkCol[s] -= 0.8f * frameMultiplier;
                fw.trailSparkCol[s] = constrain(fw.trailSparkCol[s], 0, 255);
            }
            
            // Update main flare
            fw.flarePos += fw.flareVel * frameMultiplier;
            fw.flareVel += FIREWORK_GRAVITY * frameMultiplier;
            fw.brightness *= pow(FIREWORK_BRIGHTNESS_FADE, frameMultiplier);
            
            // Prevent firework from going off the strip
            if (fw.flarePos >= FIREWORK_MAX_HEIGHT) {
                fw.flarePos = FIREWORK_MAX_HEIGHT;
                fw.flareVel = -0.3f; // Force explosion
            }
            
            // Check if flare should explode (velocity becomes negative enough)
            if (fw.flareVel <= -0.2f) {
                // === TRANSITION TO EXPLOSION ===
                fw.launchPhase = false;
                fw.exploded = true;
                
                // Initialize explosion sparks based on flare height
                fw.nExplosionSparks = fw.flarePos / 2; // Height-proportional explosion
                fw.nExplosionSparks = constrain(fw.nExplosionSparks, 10, 50);
                
                for (int s = 0; s < fw.nExplosionSparks; s++) {
                    fw.explosionSparkPos[s] = fw.flarePos;
                    fw.explosionSparkVel[s] = (float(random16(0, 20000)) / 10000.0f) - 1.0f; // -1 to +1
                    fw.explosionSparkCol[s] = abs(fw.explosionSparkVel[s]) * 500.0f;
                    fw.explosionSparkCol[s] = constrain(fw.explosionSparkCol[s], 0, 255);
                    fw.explosionSparkVel[s] *= fw.flarePos / LEDS_PER_STRIP; // Scale by height
                }
                fw.explosionSparkCol[0] = 255; // Known bright spark for timing
                
                Serial.printf("💥 Firework exploded at height %.1f with %d sparks!\n", 
                             fw.flarePos, fw.nExplosionSparks);
            }
        } 
        else {
            // === EXPLOSION PHASE ===
            bool stillBurning = false;
            
            for (int s = 0; s < fw.nExplosionSparks; s++) {
                fw.explosionSparkPos[s] += fw.explosionSparkVel[s] * frameMultiplier;
                fw.explosionSparkPos[s] = constrain(fw.explosionSparkPos[s], 0, LEDS_PER_STRIP);
                fw.explosionSparkVel[s] += fw.dyingGravity * frameMultiplier;
                fw.explosionSparkCol[s] *= pow(SPARK_FADE_RATE, frameMultiplier);
                fw.explosionSparkCol[s] = constrain(fw.explosionSparkCol[s], 0, 255);
                
                if (fw.explosionSparkCol[s] > COLOR_THRESHOLD_LOW/128) {
                    stillBurning = true;
                }
            }
            
            // Weaken gravity as sparks burn out (beautiful effect from reference)
            fw.dyingGravity *= pow(GRAVITY_WEAKENING, frameMultiplier);
            
            // Remove firework when all sparks have faded
            if (!stillBurning) {
                // Firework completed - removed verbose logging
                fireworks.erase(fireworks.begin() + i);
            }
        }
    }
}
// =============================================================================

// =============================================================================
// ENHANCED INTRO AND FALLING ENEMY SYSTEM - NEW
// =============================================================================

// =============================================================================
// WAVE2D-INSPIRED INTRO SYSTEM - ADAPTED FROM FASTLED WAVE2D EXAMPLE
// =============================================================================
// Simplified wave physics for 15×100 matrix with cross-strip ripple effects
// Matrix setup: 3 LED strips at columns 3, 8, 13 in a virtual 15×100 matrix

// Matrix dimensions for our setup
#define MATRIX_WIDTH  15
#define MATRIX_HEIGHT 100

// LED strip positions in the 15-wide matrix
static const uint8_t STRIP_POSITIONS[3] = {3, 8, 13};
static CRGB* STRIPS[3] = {ledsPM25, ledsNO2, ledsO3};

// Virtual Matrix for True Matrix Animation (15×100) - declared after constants
static CRGB virtualMatrix[MATRIX_WIDTH][MATRIX_HEIGHT];

struct SimpleWave {
    float **heights;        // Wave heights at each matrix position
    float **velocities;     // Wave velocities for physics
    int width, height;
    
    SimpleWave(int w, int h) : width(w), height(h) {
        // Allocate 2D arrays
        heights = new float*[width];
        velocities = new float*[width];
        for (int x = 0; x < width; x++) {
            heights[x] = new float[height];
            velocities[x] = new float[height];
            for (int y = 0; y < height; y++) {
                heights[x][y] = 0.0f;
                velocities[x][y] = 0.0f;
            }
        }
    }
    
    ~SimpleWave() {
        for (int x = 0; x < width; x++) {
            delete[] heights[x];
            delete[] velocities[x];
        }
        delete[] heights;
        delete[] velocities;
    }
    
    void triggerRipple(int x, int y, float strength = 1.0f) {
        if (x >= 0 && x < width && y >= 0 && y < height) {
            heights[x][y] += strength;
        }
    }
    
    void update(float speed = 0.18f, float dampening = 9.0f) {
        // Wave physics simulation
        for (int x = 0; x < width; x++) {
            for (int y = 0; y < height; y++) {
                if (x > 0 && x < width-1 && y > 0 && y < height-1) {
                    // Simple wave equation: acceleration based on neighbors
                    float acceleration = 
                        (heights[x-1][y] + heights[x+1][y] + 
                         heights[x][y-1] + heights[x][y+1] - 4.0f * heights[x][y]) * speed;
                    
                    velocities[x][y] += acceleration;
                    velocities[x][y] *= (1.0f - dampening / 100.0f); // Apply dampening
                    heights[x][y] += velocities[x][y];
                    
                    // Prevent runaway values
                    heights[x][y] = constrain(heights[x][y], -2.0f, 2.0f);
                }
            }
        }
    }
    
    uint8_t getIntensity(int x, int y) {
        if (x < 0 || x >= width || y < 0 || y >= height) return 0;
        
        float wave = heights[x][y];
        // Convert wave height (-2 to +2) to brightness (0-255)
        return (uint8_t)constrain((wave + 1.0f) * 127.5f, 0, 255);
    }
};

// Global wave simulation
static SimpleWave* waveSimulation = nullptr;

// Matrix mapping function - maps virtual 15x100 matrix to physical LED strips
void mapVirtualMatrixToStrips() {
    // Clear physical strips
    fill_solid(ledsPM25, LEDS_PER_STRIP, CRGB::Black);
    fill_solid(ledsNO2, LEDS_PER_STRIP, CRGB::Black);
    fill_solid(ledsO3, LEDS_PER_STRIP, CRGB::Black);
    
    const float FALLOFF_RANGE = 4.0f;  // Cross-strip falloff distance
    
    // Map each virtual matrix pixel to physical strips with falloff
    for (int y = 0; y < MATRIX_HEIGHT; y++) {
        for (int x = 0; x < MATRIX_WIDTH; x++) {
            CRGB virtualColor = virtualMatrix[x][y];
            
            // Skip black pixels for performance
            if (virtualColor.r == 0 && virtualColor.g == 0 && virtualColor.b == 0) continue;
            
            // Apply to all physical strips with distance-based falloff
            for (int stripIdx = 0; stripIdx < 3; stripIdx++) {
                float distance = fabsf(x - STRIP_POSITIONS[stripIdx]);
                
                if (distance <= FALLOFF_RANGE) {
                    float falloffFactor;
                    
                    if (distance <= 1.0f) {
                        // Close to strip - full intensity
                        falloffFactor = 1.0f;
                    } else {
                        // Smooth cosine falloff for cross-strip continuity
                        falloffFactor = 0.5f * (1.0f + cosf(PI * (distance - 1.0f) / (FALLOFF_RANGE - 1.0f)));
                    }
                    
                    if (falloffFactor > 0.1f) {
                        // Apply falloff and add to physical strip
                        CRGB falloffColor = virtualColor;
                        falloffColor.nscale8((uint8_t)(falloffFactor * 255));
                        
                        int pixelPos = constrain(y, 0, LEDS_PER_STRIP - 1);
                        STRIPS[stripIdx][mapLEDIndex(pixelPos)] = blend(STRIPS[stripIdx][mapLEDIndex(pixelPos)], falloffColor, 128);
                    }
                }
            }
        }
    }
}



void renderWaveIntro() {
    // WAVE2D-INSPIRED INTRO - Beautiful expanding ripples across matrix
    unsigned long elapsed = millis() - stateStartTime;
    
    // Initialize wave simulation on first call
    if (!waveSimulation) {
        waveSimulation = new SimpleWave(MATRIX_WIDTH, MATRIX_HEIGHT);
        DBG_VERBOSE("Wave simulation initialized\n");
    }
    
    // 2-second intro with auto-ripples and color shifts
    if (elapsed < 2000) {
        // Trigger ripples automatically every 400ms
        static unsigned long lastRipple = 0;
        if (millis() - lastRipple > 400) {
            // Create ripples at strip positions for cross-strip effects
            int rippleStrip = random(3);
            int rippleY = random(MATRIX_HEIGHT);
            waveSimulation->triggerRipple(STRIP_POSITIONS[rippleStrip], rippleY, 2.2f);
            
            // Occasional random ripples for dynamic interest
            if (random(100) < 30) {
                int randomX = random(MATRIX_WIDTH);
                int randomY = random(MATRIX_HEIGHT);
                waveSimulation->triggerRipple(randomX, randomY, 1.8f);
            }
            
            lastRipple = millis();
        }
        
        // Update wave physics
        waveSimulation->update(0.15f, 8.0f); // Slightly slower, more dampened than original
        
        // Clear virtual matrix
        for (int x = 0; x < MATRIX_WIDTH; x++) {
            for (int y = 0; y < MATRIX_HEIGHT; y++) {
                virtualMatrix[x][y] = CRGB::Black;
            }
        }
        
        // Map wave simulation to virtual matrix with color
        float timeProgress = elapsed / 2000.0f; // 0-1 over 2 seconds
        uint8_t baseHue = (uint8_t)(timeProgress * 255.0f); // Color shift over time
        
        for (int x = 0; x < MATRIX_WIDTH; x++) {
            for (int y = 0; y < MATRIX_HEIGHT; y++) {
                uint8_t intensity = waveSimulation->getIntensity(x, y);
                
                if (intensity > 8) { // Lower threshold to show more dim pixels
                    // Color based on position and time
                    uint8_t hue = baseHue + (x * 8) + (y * 2); // Spatial color variation
                    // Boost brightness for more vibrant display
                    uint8_t boostedIntensity = min(255, (intensity * 3) / 2); // 1.5x intensity boost
                    CRGB waveColor = CHSV(hue, 240, boostedIntensity); // High saturation
                    virtualMatrix[x][y] = waveColor;
                }
            }
        }
        
        // Map to physical strips (existing function)
        mapVirtualMatrixToStrips();
        
        // Wave intro progress - removed frequent status updates
        
    } else {
        // Intro complete — enter falling state; wait for CMD_SLIDE_ADVANCE to drive visuals
        delete waveSimulation;
        waveSimulation = nullptr;

        currentState = STATE_ENEMIES_FALLING;
        stateStartTime = millis();

        // Arm blink for any strip whose phase-0 message arrived during the wave
        for (int s = 0; s < NUM_STRIPS; s++) {
            if (introStripPhase[s] == 0) {
                slide0BlinkActive[s] = true;
                slide0BlinkStartTime[s] = millis();
            }
        }

        // Notify controller: PM2.5 strip is ready to be introduced
        StripIntroMessage introMsg;
        introMsg.command = CMD_STRIP_INTRO;
        introMsg.stripIndex = 0;
        memset(introMsg.padding, 0, sizeof(introMsg.padding));
        comm.sendMessage(controllerMacAddress, &introMsg, sizeof(introMsg));
        DBG_INFO("Wave intro complete — waiting for slide advances\n");
    }
}



// Initialize falling enemies for a single strip only (sequential loading).
void initializeFallingEnemiesForStrip(int strip) {
    fallingEnemies[strip].clear();
    pollutionEnemies[strip].clear();

    unsigned long currentTime = millis();
    float spawnHeight = LEDS_PER_STRIP + 30;

    for (int i = 0; i < targetColumnHeights[strip]; i++) {
        int targetPos = whoBaselinePixels[strip] + i;
        FallingEnemy enemy;
        enemy.spawnDelay = currentTime + (i * 150); // 150ms stagger
        enemy.position = spawnHeight + (targetPos * 3);
        enemy.velocity = 0.0f;
        enemy.targetPos = targetPos;
        enemy.color = strip + 1;  // 1=blue, 2=red, 3=green
        enemy.stripIndex = strip;
        enemy.active = false;
        enemy.settled = false;
        enemy.health = 3;
        enemy.maxHealth = 3;
        enemy.sparkTimer = 0;
        enemy.bounceVelocity = 0.0f;
        fallingEnemies[strip].push_back(enemy);
    }
    Serial.printf("🪨 Initialized %d falling enemies for strip %d\n",
                  targetColumnHeights[strip], strip);
}

void updateFallingEnemies(float deltaTime) {
    unsigned long currentTime = millis();

    // Phase 1: WHO pixel fill animation (triggered by CMD_SLIDE_ADVANCE slide 1)
    // After the brief black-flash delay, grow WHO pixels for the active strip.
    for (int strip = 0; strip < NUM_STRIPS; strip++) {
        if (whoFlashing[strip]) {
            if (currentTime - whoFlashStartTime[strip] >= WHO_FLASH_DURATION) {
                // Flash over — start the fill animation
                whoFlashing[strip] = false;
                whoAnimating = true;  // Reuse existing anim machinery
                introStripIndex = strip;
                whoAnimStartTime = currentTime;
                whoAnimPixels[strip] = 0;
            }
        }
    }

    if (whoAnimating) {
        unsigned long elapsed = currentTime - whoAnimStartTime;
        float progress = min(1.0f, (float)elapsed / (float)WHO_ANIM_DURATION);
        whoAnimPixels[introStripIndex] = (int)(progress * whoBaselinePixels[introStripIndex]);
        if (elapsed >= WHO_ANIM_DURATION) {
            whoAnimPixels[introStripIndex] = whoBaselinePixels[introStripIndex];
            whoAnimating = false;
        }
        return; // Don't process enemy physics during WHO animation
    }

    // Phase 2: enemy fall-in — only update strips that have started loading (phase 2)
    for (int strip = 0; strip < NUM_STRIPS; strip++) {
        if (introStripPhase[strip] < 2) continue; // Not yet in enemy phase

        for (int i = fallingEnemies[strip].size() - 1; i >= 0; i--) {
            FallingEnemy& enemy = fallingEnemies[strip][i];
            
            // Check if it's time to spawn this enemy
            if (!enemy.active && currentTime >= enemy.spawnDelay) {
                enemy.active = true;
            }

            if (!enemy.active || enemy.settled) continue;
            
            // Apply realistic falling physics
            const float gravity = 1.2f;
            const float terminalVelocity = 8.0f;
            
            enemy.velocity += gravity * deltaTime * 60; 
            enemy.velocity = min(enemy.velocity, terminalVelocity);
            enemy.position -= enemy.velocity * deltaTime * 60; 
            
            // Check if enemy should land
            if (enemy.position <= enemy.targetPos) {
                enemy.position = enemy.targetPos;
                enemy.bounceVelocity = enemy.velocity * 0.4f;
                enemy.velocity = 0.0f;
                enemy.settled = true;
                
                PollutionEnemy staticEnemy;
                staticEnemy.position = enemy.targetPos;
                staticEnemy.startPosition = enemy.targetPos;
                staticEnemy.color = enemy.color;
                staticEnemy.stripIndex = enemy.stripIndex;
                staticEnemy.active = true;
                staticEnemy.health = enemy.health;
                staticEnemy.maxHealth = enemy.maxHealth;
                staticEnemy.sparkTimer = 0;
                staticEnemy.advanceVelocity = ENEMY_ADVANCE_SPEED;
                staticEnemy.phaseOffset = (float)(random(0, 628)) * 0.01f;
                
                pollutionEnemies[strip].push_back(staticEnemy);
            }
        }
        
        // Clean up settled enemies
        fallingEnemies[strip].erase(
            std::remove_if(fallingEnemies[strip].begin(), fallingEnemies[strip].end(),
                          [](const FallingEnemy& enemy) { return enemy.settled; }),
            fallingEnemies[strip].end());
    }
    
    // Check if the LAST strip (strip 2) has finished settling — then we can advance to waiting
    if (introStripPhase[NUM_STRIPS - 1] == 2 &&
        introStripEnemiesInitialized &&
        fallingEnemies[NUM_STRIPS - 1].empty()) {
        introStripEnemiesInitialized = false;
        // All strips loaded — wait for controller countdown before starting movement
        currentState = STATE_WAITING_FOR_PLAYER;
        stateStartTime = millis();
        AudioMessage readyMsg;
        readyMsg.command = CMD_GAME_READY;
        readyMsg.soundEvent = 0;
        memset(readyMsg.padding, 0, sizeof(readyMsg.padding));
        comm.sendMessage(controllerMacAddress, &readyMsg, sizeof(readyMsg));
        sendAQIStatus();
        DBG_INFO("All strips loaded — waiting for player countdown\n");
    }
}

void renderFallingEnemies() {
    unsigned long currentTime = millis();
    
    // Render active falling enemies with trailing effects
    for (int strip = 0; strip < NUM_STRIPS; strip++) {
        CRGB* currentStrip = (strip == 0) ? ledsPM25 : (strip == 1) ? ledsNO2 : ledsO3;
        
        for (const auto& enemy : fallingEnemies[strip]) {
            if (enemy.active && !enemy.settled && 
                enemy.position >= 0 && enemy.position < LEDS_PER_STRIP) {
                
                CRGB enemyColor = getColor(enemy.color);
                int pos = (int)enemy.position;
                
                // Main falling enemy pixel
                currentStrip[pos] = enemyColor;
                
                // Velocity-based trailing effect
                if (enemy.velocity > 2.0f) {
                    // Add bright trail behind fast-moving enemies
                    int trailLength = min(3, (int)(enemy.velocity / 2));
                    for (int offset = 1; offset <= trailLength; offset++) {
                        int trailPos = pos + offset;
                        if (trailPos < LEDS_PER_STRIP) {
                            CRGB trailColor = enemyColor;
                            trailColor.fadeToBlackBy(60 * offset); // Fade trail
                            currentStrip[trailPos] = trailColor;
                        }
                    }
                }
            }
        }
    }
}

void createWipeEffect(int stripIndex) {
    // Create new wipe effect for strip completion
    WipeEffect newWipe;
    newWipe.stripIndex = stripIndex;
    newWipe.position = 0.0f;
    newWipe.speed = 3.0f;
    newWipe.color = 4;  // White
    newWipe.active = true;
    newWipe.direction = true;  // Wipe upward
    newWipe.startTime = millis();
    
    wipeEffects.push_back(newWipe);
    
    Serial.print("✨ Wipe effect created for strip ");
    Serial.println(stripIndex);
}

void initializeEnvironmentalGame() {
    // Initialize all game systems for environmental mode
    
    // Clear all collections
    environmentalShots.clear();
    sparks.clear();
    fireworks.clear();
    wipeEffects.clear();
    
    for (int i = 0; i < NUM_STRIPS; i++) {
        pollutionEnemies[i].clear();
        stripCompleted[i] = false;
        playerPositions[i] = 99.0f;  // Heroes at top
        playerRecoil[i] = 0.0f;
        playerSparkle[i] = 0.0f;
        buttonCharging[i] = false;
        buttonPressTime[i] = 0;
        delayedFireworkTime[i] = 0;
    }
    resetEnemyComets();
    
    completedStripCount = 0;
    lastInputTime = millis();
    
    // Initialize state timing for core system
    stateStartTime = millis();
    
    // Reset March 13th victory flags
    allStripsCompleteEffect = false;
    victoryMusicPlaying = false;
    finalFireworksTriggered = false;
    gameBlackedOut = false;
    heroDead = false;
    heroDeathTime = 0;
    lastEncroachWarnTime = 0;
    
    // Send ESP-NOW command to re-enable all audio
    AudioMessage enableMsg;
    enableMsg.command    = CMD_ENABLE_ALL_AUDIO;
    enableMsg.soundEvent = 0;
    memset(enableMsg.padding, 0, sizeof(enableMsg.padding));
    comm.sendMessage(controllerMacAddress, enableMsg);
    Serial.println("📢 Sent enable all audio command");
    
    // Generate level from AQI data
    generateLevelFromAQI();
    createPollutionEnemies();
    
    Serial.println("🎮 Environmental game initialized with March 13th systems");
}

// =============================================================================
// CITY SELECT PREVIEW RENDERING
// =============================================================================

// Renders a static LED column preview for the city-select state.
// Shows WHO baseline (dim) at bottom + bright full AQI column above.
// No enemies, no hero, no animation — purely informational.
void renderCitySelectPreview() {
    fill_solid(ledsPM25, LEDS_PER_STRIP, CRGB::Black);
    fill_solid(ledsNO2,  LEDS_PER_STRIP, CRGB::Black);
    fill_solid(ledsO3,   LEDS_PER_STRIP, CRGB::Black);

    CRGB* strips[3] = {ledsPM25, ledsNO2, ledsO3};

    // 1:1 mapping — 1 LED = 1 unit of pollutant (same as generateLevelFromAQI)
    int totalHeight[3];
    totalHeight[0] = constrain((int)currentAQI.pm25, 1, LEDS_PER_STRIP - 1);
    totalHeight[1] = constrain((int)currentAQI.no2,  1, LEDS_PER_STRIP - 1);
    totalHeight[2] = constrain((int)currentAQI.o3,   1, LEDS_PER_STRIP - 1);

    // WHO baseline: fixed universal values, never scaled per city
    const int whoHeight[3] = { WHO_PM25, WHO_NO2, WHO_O3 };

    // WHO baseline is near-white with faint blue tint — clearly distinct from strip colors
    static const CRGB whoColors[3]  = { CRGB(55,60,65), CRGB(55,60,65), CRGB(55,60,65) };
    static const CRGB fullColors[3] = { CRGB(0,0,180),  CRGB(180,0,0),  CRGB(0,180,0) };

    for (int strip = 0; strip < NUM_STRIPS; strip++) {
        // WHO baseline (dim)
        for (int px = 0; px < whoHeight[strip]; px++) {
            strips[strip][mapLEDIndex(px)] = whoColors[strip];
        }
        // Excess above WHO (bright, full opacity)
        for (int px = whoHeight[strip]; px < totalHeight[strip]; px++) {
            strips[strip][mapLEDIndex(px)] = fullColors[strip];
        }
    }
}

// =============================================================================
// MARCH 13TH RENDERING SYSTEM - EXACT IMPLEMENTATION
// =============================================================================

// =============================================================================
// AURORA ATTRACT EFFECT — Northern-lights style, three narrow hue bands
// =============================================================================
void renderAurora() {
    float ts = millis() * 0.001f;
    // Each strip gets its own narrow hue range — no rainbow/colorwheel cycling
    static const uint8_t aHueLo[NUM_STRIPS] = {145, 196, 100};  // cyan-blue, magenta-purple, green
    static const uint8_t aHueHi[NUM_STRIPS] = {163, 213, 116};
    for (int strip = 0; strip < NUM_STRIPS; strip++) {
        CRGB* leds = (strip == 0) ? ledsPM25 : (strip == 1) ? ledsNO2 : ledsO3;
        for (int px = 0; px < LEDS_PER_STRIP; px++) {
            float pos = (float)px / (float)LEDS_PER_STRIP;
            float w1 = sinf(pos * 4.0f + ts * 0.85f + (float)strip * 1.3f);
            float w2 = sinf(pos * 7.0f - ts * 0.55f + (float)strip * 0.9f);
            float w3 = sinf(pos * 2.0f + ts * 0.30f + (float)strip * 2.4f);
            float combined = (w1 * 0.5f + w2 * 0.3f + w3 * 0.2f + 1.0f) * 0.5f;
            float brightness = combined * 255.0f;  // linear (not squared) for fuller brightness
            if (brightness > 8.0f) {
                float hueDrift = (sinf(ts * 0.20f + pos * 2.0f + (float)strip * 0.7f) + 1.0f) * 0.5f;
                uint8_t hue = (uint8_t)((float)aHueLo[strip]
                              + hueDrift * (float)(aHueHi[strip] - aHueLo[strip]));
                uint8_t sat = (uint8_t)(195.0f + hueDrift * 45.0f);
                leds[mapLEDIndex(px)] = CHSV(hue, sat,
                    (uint8_t)constrain((int)brightness, 8, 255));
            } else {
                leds[mapLEDIndex(px)] = CRGB::Black;
            }
        }
    }
    FastLED.show();
}

// =============================================================================
// ATTRACT DEMO — fixed AQI levels, enemies placed directly, auto-fire wins
// =============================================================================
void initAttractDemo() {
    for (int i = 0; i < NUM_STRIPS; i++) {
        pollutionEnemies[i].clear();
        fallingEnemies[i].clear();
        stripCompleted[i]      = false;
        playerPositions[i]     = 99.0f;
        playerRecoil[i]        = 0.0f;
        playerSparkle[i]       = 0.0f;
        buttonCharging[i]      = false;
        delayedFireworkTime[i] = 0;
        demoLastFire[i]        = millis() + 2500;  // 2.5 s grace before first auto-fire
        demoShotType[i]        = 0;
    }
    environmentalShots.clear();
    sparks.clear();
    fireworks.clear();
    wipeEffects.clear();
    resetEnemyComets();
    completedStripCount      = 0;
    allStripsCompleteEffect  = false;
    allStripsCompleteTime    = 0;
    finalFireworksTriggered  = false;
    gameBlackedOut           = false;
    heroDead                 = false;
    heroDeathTime            = 0;
    lastEncroachWarnTime     = 0;

    whoBaselinePixels[0] = WHO_PM25;
    whoBaselinePixels[1] = WHO_NO2;
    whoBaselinePixels[2] = WHO_O3;

    int demoTotals[3] = {DEMO_PM25_VAL, DEMO_NO2_VAL, DEMO_O3_VAL};
    for (int strip = 0; strip < NUM_STRIPS; strip++) {
        targetColumnHeights[strip] = max(1, demoTotals[strip] - whoBaselinePixels[strip]);
    }
    currentAQI.pm25 = DEMO_PM25_VAL;
    currentAQI.no2  = DEMO_NO2_VAL;
    currentAQI.o3   = DEMO_O3_VAL;

    // Place demo enemies directly — no falling animation
    for (int strip = 0; strip < NUM_STRIPS; strip++) {
        for (int i = 0; i < targetColumnHeights[strip]; i++) {
            PollutionEnemy enemy;
            enemy.position        = (float)(whoBaselinePixels[strip] + i);
            enemy.startPosition   = enemy.position;
            enemy.color           = strip + 1;
            enemy.stripIndex      = strip;
            enemy.active          = true;
            enemy.health          = 3;
            enemy.maxHealth       = 3;
            enemy.sparkTimer      = 0;
            enemy.advanceVelocity = ENEMY_ADVANCE_SPEED;
            enemy.phaseOffset     = (float)(random(0, 628)) * 0.01f;
            pollutionEnemies[strip].push_back(enemy);
        }
    }

    inAttractDemo  = true;
    currentState   = STATE_ENVIRONMENTAL_GAME;
    stateStartTime = millis();
    lastUpdate     = millis();
    DBG_INFO("Attract demo: %d PM2.5, %d NO2, %d O3 enemies\n",
             targetColumnHeights[0], targetColumnHeights[1], targetColumnHeights[2]);
}

// =============================================================================
// ATTRACT STATE UPDATE — switches between aurora and demo phases
// =============================================================================
void updateAttract() {
    if (attractPhase == ATTRACT_AURORA) {
        if (millis() - attractPhaseStart >= ATTRACT_AURORA_DURATION) {
            attractPhase      = ATTRACT_DEMO;
            attractPhaseStart = millis();
            initAttractDemo();
            // currentState is now STATE_ENVIRONMENTAL_GAME — demo runs via game engine
        }
    }
    // ATTRACT_DEMO: currentState switched to STATE_ENVIRONMENTAL_GAME,
    // so updateAttract() is no longer called; game engine handles the rest.
}

void renderEnvironmentalDisplay() {
    // Clear all strips
    fill_solid(ledsPM25, LEDS_PER_STRIP, CRGB::Black);
    fill_solid(ledsNO2, LEDS_PER_STRIP, CRGB::Black);
    fill_solid(ledsO3, LEDS_PER_STRIP, CRGB::Black);
    
    // Handle state-specific rendering
    switch (currentState) {
        case STATE_ATTRACT:
            renderAurora();
            return;

        case STATE_CITY_SELECT:
            renderCitySelectPreview();
            return;

        case STATE_INTRO_ANIMATION:
            renderWaveIntro();
            return;
            
        case STATE_ENEMIES_FALLING:
            renderFallingEnemies();
            // Also render any settled static enemies
            break;

        case STATE_WAITING_FOR_PLAYER:
            // Enemies at final positions — render them like the main game but without advancing
            break;
            
        default:
            // Continue with existing rendering for other states
            break;
    }
    
    // March 13th Victory Rendering - DRAMATIC: 1s rainbow wipe + 5s fireworks
    if (gameBlackedOut) {
        // Complete blackout during transition
        return;
    }
    
    if (allStripsCompleteEffect) {
        unsigned long elapsed = millis() - allStripsCompleteTime;
        
        if (elapsed <= RAINBOW_WIPE_DURATION) {
            // Phase 1: DRAMATIC 1-second rainbow wipe effect
            static uint8_t hue = 0;
            hue += 8;  // Fast rainbow for dramatic wipe effect
            float progress = (float)elapsed / RAINBOW_WIPE_DURATION;
            int wipePosition = (int)(progress * LEDS_PER_STRIP);
            
            for (int i = 0; i < LEDS_PER_STRIP; i++) {
                if (i <= wipePosition) {
                    // Bright, saturated rainbow wipe
                    ledsPM25[mapLEDIndex(i)] = CHSV(hue + (i * 3), 255, 255);
                    ledsNO2[mapLEDIndex(i)] = CHSV(hue + (i * 3) + 85, 255, 255);
                    ledsO3[mapLEDIndex(i)] = CHSV(hue + (i * 3) + 170, 255, 255);
                } else {
                    // Black background for clean wipe effect
                    ledsPM25[mapLEDIndex(i)] = CRGB::Black;
                    ledsNO2[mapLEDIndex(i)] = CRGB::Black;
                    ledsO3[mapLEDIndex(i)] = CRGB::Black;
                }
            }
            // Skip fireworks rendering during dramatic wipe
            return;
        } else {
            // Phase 2: Clear black background for MAXIMUM fireworks drama (after 1 second)
            // Let fireworks render on pure black background for stunning visual impact
        }
    }
    
    CRGB* strips[3] = {ledsPM25, ledsNO2, ledsO3};

    // Render WHO 2021 baseline — dim static pixels at the bottom of each strip.
    // These are unshootable and represent the safe level players are targeting,
    // not zero. Victory is reached when the excess above is cleared, not the baseline.
    // WHO baseline is near-white with faint blue tint — clearly distinct from strip colors
    static const CRGB whoColors[3] = {
        CRGB(55, 60, 65),  // Near-white (blue tint) — PM2.5 WHO guideline
        CRGB(55, 60, 65),  // Near-white (blue tint) — NO₂ WHO guideline
        CRGB(55, 60, 65),  // Near-white (blue tint) — O₃ WHO guideline
    };
    for (int strip = 0; strip < NUM_STRIPS; strip++) {
        if (!stripCompleted[strip]) {
            // During educational intro, render each strip according to its current phase.
            // After loading completes (STATE_WAITING_FOR_PLAYER or game), show full baseline.
            if (currentState == STATE_ENEMIES_FALLING) {
                int phase = introStripPhase[strip];
                if (phase < 0) {
                    // Not yet revealed — keep dark (no pixels drawn)
                } else if (phase == 0) {
                    // Slide 0: full column WHO white, blinking 3× to draw attention
                    bool showWhite = true;
                    if (slide0BlinkActive[strip]) {
                        unsigned long blinkElapsed = millis() - slide0BlinkStartTime[strip];
                        unsigned long totalBlink = SLIDE0_BLINK_COUNT * 2 * SLIDE0_BLINK_HALF_MS;
                        if (blinkElapsed < totalBlink) {
                            // Determine on/off half-cycle
                            unsigned long phase_t = blinkElapsed % (2 * SLIDE0_BLINK_HALF_MS);
                            showWhite = (phase_t < SLIDE0_BLINK_HALF_MS); // on for first half
                        } else {
                            slide0BlinkActive[strip] = false; // Done blinking — stay on
                        }
                    }
                    if (showWhite) {
                        for (int px = 0; px < LEDS_PER_STRIP; px++) {
                            strips[strip][mapLEDIndex(px)] = whoColors[strip];
                        }
                    }
                    // else: off half of blink — strip stays black (cleared above)
                } else if (phase == 1) {
                    if (whoFlashing[strip]) {
                        // Brief black flash — keep dark
                    } else {
                        // WHO fill animation (bottom-up)
                        for (int px = 0; px < whoAnimPixels[strip]; px++) {
                            strips[strip][mapLEDIndex(px)] = whoColors[strip];
                        }
                    }
                } else {
                    // Phase 2+: show full WHO baseline (enemies render on top separately)
                    for (int px = 0; px < whoBaselinePixels[strip]; px++) {
                        strips[strip][mapLEDIndex(px)] = whoColors[strip];
                    }
                }
            } else {
                // Post-intro: show full WHO baseline for all strips
                for (int px = 0; px < whoBaselinePixels[strip]; px++) {
                    strips[strip][mapLEDIndex(px)] = whoColors[strip];
                }
            }
        }
    }

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
        
        // During intro, only render settled enemies for strips that have reached phase 2
        if (currentState == STATE_ENEMIES_FALLING && introStripPhase[strip] < 2) {
            skipEnemies = true;
        }
        
        if (!skipEnemies) {
            for (const auto& enemy : pollutionEnemies[strip]) {
                if (enemy.active && enemy.position >= 0 && enemy.position < LEDS_PER_STRIP) {
                    CRGB enemyColor = getColor(enemy.color);
                    
                    // Add character-specific animations - MARCH 13TH IMPLEMENTATION
                    if (enemy.color == 1) {  // PM2.5 Blue - watery pulse
                        // Base blue with subtle watery modulation - MARCH 13TH
                        float pulse = sin(currentTime * 0.006f + enemy.position * 0.25f) * 0.3f + 0.7f;  // 0.4 to 1.0 range
                        enemyColor = CRGB(0, 0, 255 * pulse);
                    } else if (enemy.color == 2) {  // NO₂ Red - fiery flicker
                        // Base red with random flicker, mixing pure red pixels - MARCH 13TH
                        if (random(0, 100) < 70) {  // 70% pure red
                            enemyColor = CRGB(255, 0, 0);
                        } else {  // 30% flickering orange-red
                            uint8_t flicker = 200 + random(0, 55);  // 200-255 range
                            enemyColor = CRGB(255, flicker/6, 0);  // Mostly red with tiny orange
                        }
                    } else if (enemy.color == 3) {  // O₃ Green - wave pattern
                        // Wave pattern with extreme value swings - MARCH 13TH
                        float wave = sin(currentTime * 0.01f + enemy.position * 0.1f) * 0.5f + 0.5f;
                        uint8_t greenValue = 50 + wave * 205;  // 50-255 range for high contrast
                        enemyColor = CRGB(0, greenValue, 0);
                        // Force some pixels to extreme values for testing - MARCH 13TH
                        if ((int(enemy.position) + int(currentTime * 0.001f)) % 10 < 3) {
                            enemyColor = CRGB(0, 50, 0);  // Dark green
                        } else if ((int(enemy.position) + int(currentTime * 0.001f)) % 10 > 7) {
                            enemyColor = CRGB(0, 255, 0);  // Bright green
                        }
                    }
                    
                    // Apply damage-based dimming - MARCH 13TH
                    if (enemy.health < enemy.maxHealth) {
                        float healthRatio = (float)enemy.health / (float)enemy.maxHealth;
                        enemyColor.fadeToBlackBy(255 - (255 * healthRatio));
                    }
                    
                    // Add spark effect if recently damaged - MARCH 13TH
                    if (enemy.sparkTimer > currentTime) {
                        uint8_t sparkBrightness = 128 + sin(currentTime * 0.05f) * 127;
                        enemyColor += CRGB(sparkBrightness/3, sparkBrightness/3, sparkBrightness/3);
                    }
                    
                    strips[strip][mapLEDIndex((int)enemy.position)] = enemyColor;
                }
            }
        }
    }

    // Render enemy comets with a short white-hot tail.
    for (int strip = 0; strip < NUM_STRIPS; strip++) {
        if (!enemyComets[strip].active) continue;

        int headPos = (int)enemyComets[strip].position;
        if (headPos < 0 || headPos >= LEDS_PER_STRIP) continue;

        CRGB cometHead = CRGB(255, 150, 0);
        if (enemyComets[strip].sparkTimer > currentTime) {
            uint8_t pulse = 180 + (uint8_t)(sin(currentTime * 0.08f) * 60.0f);
            cometHead += CRGB(pulse / 2, pulse / 2, pulse / 2);
        }
        strips[strip][mapLEDIndex(headPos)] += cometHead;

        for (int t = 1; t <= 4; t++) {
            int tailPos = headPos - t;
            if (tailPos < 0 || tailPos >= LEDS_PER_STRIP) continue;

            uint8_t fade = (uint8_t)(200 - (t * 45));
            CRGB tailColor = CRGB(255, 120, 20);
            tailColor.nscale8(fade);
            strips[strip][mapLEDIndex(tailPos)] += tailColor;

            if (t <= 2) {
                CRGB whiteHot = CRGB(120, 120, 120);
                whiteHot.nscale8(fade);
                strips[strip][mapLEDIndex(tailPos)] += whiteHot;
            }
        }
    }
    
    // Render enhanced heroes with sparkle, recoil, and charging effects - MARCH 13TH
    for (int strip = 0; strip < NUM_STRIPS; strip++) {
        if (!stripCompleted[strip]) {  // Only show hero if strip not completed
            float heroPos = playerPositions[strip] + playerRecoil[strip];
            if (heroPos >= 0 && heroPos < LEDS_PER_STRIP) {
                
                // Check if this hero is charging a cannon shot - MARCH 13TH
                bool isCharging = buttonCharging[strip];
                float chargeProgress = 0.0f;
                
                if (isCharging) {
                    unsigned long holdTime = millis() - buttonPressTime[strip];
                    // Animation runs for 1 second: from when charging starts (1s) until cannon ready (2s)
                    chargeProgress = min(1.0f, (float)holdTime / 1000.0f);  // 1s animation window
                }
                
                if (isCharging && chargeProgress < 1.0f) {
                    // CHARGING: Pink hue, grows to 3 pixels, slow pulse - MARCH 13TH
                    CRGB heroColor = CRGB::DeepPink;
                    float pulseValue = sin(currentTime * 0.03f) * 0.5f + 0.5f;  // 0-1 range
                    uint8_t chargeBrightness = 140 + (uint8_t)(pulseValue * 80 + chargeProgress * 35);
                    heroColor.fadeToBlackBy(255 - chargeBrightness);
                    
                    // Hero grows progressively to 3 pixels during charging (downward) - MARCH 13TH
                    int chargeSize = 1 + (int)(chargeProgress * 2.0f);  // 1→3 pixels based on charge
                    for (int offset = 0; offset < chargeSize; offset++) {
                        int pixelPos = (int)heroPos - offset;  // Grow downward from position 99
                        if (pixelPos >= 0 && pixelPos < LEDS_PER_STRIP) {
                            strips[strip][mapLEDIndex(pixelPos)] = heroColor;
                        }
                    }
                    
                } else if (isCharging && chargeProgress >= 1.0f) {
                    // FULLY CHARGED: Slow blinking red effect with 3 pixels - MARCH 13TH
                    CRGB heroColor = CRGB::Red;
                    float blinkValue = sin(currentTime * 0.08f) * 0.5f + 0.5f;  // Slow pulse 0-1
                    uint8_t blinkBrightness = 120 + (uint8_t)(blinkValue * 135);  // 120-255 range
                    heroColor.fadeToBlackBy(255 - blinkBrightness);
                    
                    // Full size (3 pixels) when fully charged (downward) - MARCH 13TH
                    for (int offset = 0; offset < 3; offset++) {
                        int pixelPos = (int)heroPos - offset;  // Grow downward from position 99
                        if (pixelPos >= 0 && pixelPos < LEDS_PER_STRIP) {
                            strips[strip][mapLEDIndex(pixelPos)] = heroColor;
                        }
                    }
                } else {
                    // NORMAL: Standard white sparkle (single pixel) - MARCH 13TH
                    CRGB heroColor = CRGB::White;
                    uint8_t sparkle = 200 + (uint8_t)(sin(currentTime * 0.08f + strip * 1.2f) * 30);
                    heroColor.fadeToBlackBy(255 - sparkle);
                    strips[strip][mapLEDIndex((int)heroPos)] = heroColor;
                }
            }
        }
    }
    
    // Render wipe effects - MARCH 13TH
    for (const auto& wipe : wipeEffects) {
        if (wipe.active && wipe.stripIndex >= 0 && wipe.stripIndex < NUM_STRIPS) {
            // White chase effect - MARCH 13TH
            for (int i = 0; i < (int)wipe.position; i++) {
                if (i >= 0 && i < LEDS_PER_STRIP) {
                    strips[wipe.stripIndex][mapLEDIndex(i)] = CRGB::White;
                }
            }
        }
    }

    // Render realistic fireworks (March 13th Enhanced)
    for (const auto& fw : fireworks) {
        if (!fw.active || fw.stripIndex < 0 || fw.stripIndex >= NUM_STRIPS) continue;
        
        if (fw.launchPhase) {
            // === LAUNCH PHASE RENDERING ===
            // Render main flare (much brighter white/yellow)
            int flarePos = (int)fw.flarePos;
            if (flarePos >= 0 && flarePos < LEDS_PER_STRIP) {
                uint8_t brightness = (uint8_t)(fw.brightness * 255);
                // Make flare MUCH brighter than enemies - full white with yellow tint
                strips[fw.stripIndex][mapLEDIndex(flarePos)] = CRGB(255, 240, brightness/4); // Very bright white-yellow
            }
            
            // Render trailing sparks with brighter heat colors
            for (int s = 0; s < 5; s++) {
                int sparkPos = (int)fw.trailSparkPos[s];
                if (sparkPos >= 0 && sparkPos < LEDS_PER_STRIP && fw.trailSparkCol[s] > 0) {
                    CRGB trailColor = HeatColor((uint8_t)fw.trailSparkCol[s]);
                    trailColor.nscale8(240); // Much brighter trails
                    strips[fw.stripIndex][mapLEDIndex(sparkPos)] += trailColor;
                }
            }
        } 
        else if (fw.exploded) {
            // === EXPLOSION PHASE RENDERING ===
            // Render explosion sparks with beautiful color transitions
            for (int s = 0; s < fw.nExplosionSparks; s++) {
                int sparkPos = (int)fw.explosionSparkPos[s];
                if (sparkPos >= 0 && sparkPos < LEDS_PER_STRIP && fw.explosionSparkCol[s] > 0) {
                    CRGB sparkColor;
                    float colorVal = fw.explosionSparkCol[s];
                    
                    // Color transitions based on reference code (brighter)
                    if (colorVal > COLOR_THRESHOLD_HIGH) {
                        // Bright white to yellow fade
                        float factor = (colorVal - COLOR_THRESHOLD_HIGH) / (255 - COLOR_THRESHOLD_HIGH);
                        sparkColor = CRGB(255, 255, (uint8_t)(255 * factor));
                        sparkColor.nscale8(240); // Much brighter than before
                    }
                    else if (colorVal < COLOR_THRESHOLD_LOW) {
                        // Bright red to black fade
                        float factor = colorVal / COLOR_THRESHOLD_LOW;
                        sparkColor = CRGB((uint8_t)(255 * factor), 0, 0);
                        sparkColor.nscale8(220); // Maintain brightness longer
                    }
                    else {
                        // Bright yellow to red fade
                        float factor = (colorVal - COLOR_THRESHOLD_LOW) / (COLOR_THRESHOLD_HIGH - COLOR_THRESHOLD_LOW);
                        sparkColor = CRGB(255, (uint8_t)(255 * factor), 0);
                        sparkColor.nscale8(255); // Maximum brightness during peak
                    }
                    
                    strips[fw.stripIndex][mapLEDIndex(sparkPos)] += sparkColor;
                }
            }
        }
    }

    
    // Render shots (optimized for performance) - MARCH 13TH
    for (const auto& shot : environmentalShots) {
        if (shot.position >= 0 && shot.position < LEDS_PER_STRIP) {
            CRGB shotColor = getColor(shot.color);
            shotColor.maximizeBrightness();
            
            if (shot.isCannonShot) {
                // Cannon shots: 3 pixels wide (optimized rendering) - MARCH 13TH
                int startPos = max(0, (int)shot.position);
                int endPos = min(LEDS_PER_STRIP - 1, startPos + 2);
                for (int pos = startPos; pos <= endPos; pos++) {
                    strips[shot.stripIndex][mapLEDIndex(pos)] = shotColor;
                }
            } else {
                // Normal shots: single pixel - MARCH 13TH
                strips[shot.stripIndex][mapLEDIndex((int)shot.position)] = shotColor;
            }
        }
    }
    
    // Render sparks on their respective strips - MARCH 13TH
    for (const auto& spark : sparks) {
        if (spark.active && spark.position >= 0 && spark.position < LEDS_PER_STRIP) {
            CRGB sparkColor = getColor(spark.color);
            sparkColor.nscale8(spark.brightness);
            
            if (spark.stripIndex >= 0 && spark.stripIndex < NUM_STRIPS) {
                // Single strip spark - MARCH 13TH
                strips[spark.stripIndex][mapLEDIndex((int)spark.position)] += sparkColor;  // Additive blending
            }
        }
    }
}

// =============================================================================
// MARCH 13TH SHOT CREATION - EXACT IMPLEMENTATION
// =============================================================================

void createPlayerShot(int stripIndex) {
    // BLOCK SHOTS during intro and enemy loading phases
    if (currentState == STATE_INTRO_ANIMATION || currentState == STATE_ENEMIES_FALLING) {
        Serial.printf("🚫 Shot BLOCKED - enemies still loading on strip %d\n", stripIndex);
        return;
    }
    
    // Check if strip is completed - block shots on cleared strips
    if (stripCompleted[stripIndex]) {
        Serial.printf("🚫 Shot blocked - strip %d already completed\n", stripIndex);
        return;
    }
    
    // Create normal shot at hero position - MARCH 13TH
    EnvironmentalShot newShot;
    newShot.position = playerPositions[stripIndex];
    newShot.velocity = -SHOT_INITIAL_VELOCITY;
    newShot.stripIndex = stripIndex;
    newShot.color = stripIndex + 1;  // PM25=1(blue), NO2=2(red), O3=3(green)
    newShot.active = true;
    newShot.isCannonShot = false;
    newShot.size = 1;
    newShot.damage = 1;
    // Add to shots array - MARCH 13TH
    environmentalShots.push_back(newShot);
    // Add hero recoil effect - MARCH 13TH
    playerRecoil[stripIndex] = -2.0f;
    // Removed shot creation debug spam for performance
}

void createCannonShot(int stripIndex) {
    // BLOCK CANNON SHOTS during intro and enemy loading phases
    if (currentState == STATE_INTRO_ANIMATION || currentState == STATE_ENEMIES_FALLING) {
        Serial.printf("🚫 CANNON shot BLOCKED - enemies still loading on strip %d\n", stripIndex);
        return;
    }
    
    // Check if strip is completed - block shots on cleared strips
    if (stripCompleted[stripIndex]) {
        Serial.printf("🚫 Cannon shot blocked - strip %d already completed\n", stripIndex);
        return;
    }
    
    // Enforce 5-second cooldown between cannon shots
    if (millis() - lastCannonTime[stripIndex] < CANNON_COOLDOWN) {
        Serial.printf("🚫 Cannon blocked - cooldown active on strip %d\n", stripIndex);
        return;
    }

    // Create QUINTUPLE cannon shot - 5 shots for maximum power
    for (int shotNum = 0; shotNum < 5; shotNum++) {
        EnvironmentalShot newShot;
        newShot.position = playerPositions[stripIndex] - (shotNum * 2.0f);  // Each shot slightly behind
        newShot.velocity = -SHOT_INITIAL_VELOCITY - (shotNum * 0.5f);         // Each shot slightly faster
        newShot.stripIndex = stripIndex;
        newShot.color = stripIndex + 1;  // PM25=1(blue), NO2=2(red), O3=3(green)
        newShot.active = true;
        newShot.isCannonShot = true;
        newShot.size = 3;
        newShot.damage = 4;
        environmentalShots.push_back(newShot);
    }

    lastCannonTime[stripIndex] = millis();  // Record fire time for cooldown

    // Add hero recoil effect - stronger for quintuple shot
    playerRecoil[stripIndex] = -6.0f;

    Serial.printf("💥💥💥💥💥 QUINTUPLE CANNON shot created on strip %d - 5 shots fired!\n", stripIndex);
}

// =============================================================================
// ESP-NOW COMMUNICATION HANDLING - MARCH 13TH SYSTEMS
// =============================================================================

void onDataReceived(const uint8_t *mac_addr, const uint8_t *data, int data_len) {
    // Handle incoming ESP-NOW messages - MARCH 13TH adapted for working controller
    Serial.printf("[ESP-NOW RX] len=%d  state=%d  inAttractDemo=%d\n",
                  data_len, (int)currentState, (int)inAttractDemo);
    lastInputTime = millis();  // Reset timeout on any message

    // Any incoming command exits attract mode (aurora or demo phase)
    if (currentState == STATE_ATTRACT || inAttractDemo) {
        Serial.println("[ATTRACT-EXIT] Exiting attract → STATE_CITY_SELECT");
        inAttractDemo     = false;
        attractPhase      = ATTRACT_AURORA;
        for (int i = 0; i < NUM_STRIPS; i++) {
            pollutionEnemies[i].clear();
            fallingEnemies[i].clear();
            stripCompleted[i]  = false;
        }
        environmentalShots.clear();
        sparks.clear();
        fireworks.clear();
        wipeEffects.clear();
        resetEnemyComets();
        allStripsCompleteEffect = false;
        heroDead    = false;
        currentState = STATE_CITY_SELECT;
        // Fall through to process the command normally as city-select input
    }
    
    // City roster update from web UI "Load All Cities"
    if (data_len == sizeof(CityRosterMessage)) {
        CityRosterMessage* roster = (CityRosterMessage*)data;
        if (roster->command == CMD_CITY_ROSTER) {
            int count = min((int)roster->cityCount, NUM_CITIES);
            for (int i = 0; i < count; i++) {
                cityRoster[i].pm25 = roster->cities[i].pm25;
                cityRoster[i].no2  = roster->cities[i].no2;
                cityRoster[i].o3   = roster->cities[i].o3;
            }
            Serial.printf("🌍 City roster updated: %d cities loaded\n", count);
            for (int i = 0; i < count; i++) {
                Serial.printf("  [%d] %s — PM2.5=%d NO2=%d O3=%d\n",
                    i, cityRoster[i].name,
                    cityRoster[i].pm25, cityRoster[i].no2, cityRoster[i].o3);
            }
            return;
        }
    }

    // Check for working controller message format (ConsoleMessage)
    if (data_len == sizeof(ControllerMessage)) {
        ControllerMessage* ctrlMsg = (ControllerMessage*)data;
        if (ctrlMsg->command == CMD_AQI_DATA) {
            // Update currentAQI and regenerate columns/enemies
            currentAQI.pm25 = ctrlMsg->aqi.pm25;
            currentAQI.no2 = ctrlMsg->aqi.no2;
            currentAQI.o3 = ctrlMsg->aqi.o3;
            currentAQI.city = ctrlMsg->aqi.city;
            Serial.printf("🌫️  Received AQI update: PM2.5=%.1f, NO₂=%.1f, O₃=%.1f, City=%s\n", currentAQI.pm25, currentAQI.no2, currentAQI.o3, currentAQI.city.c_str());
            saveAQI();
            generateLevelFromAQI();
            createPollutionEnemies();
            return;
        }
        // Optionally handle other controller commands here (e.g., settings, start game)
    }
    if (data_len == sizeof(ConsoleMessage)) {
        ConsoleMessage* fireCommand = (ConsoleMessage*)data;
        DBG_VERBOSE("Fire command: %d\n", fireCommand->command);

        // City-select commands — valid in any state
        if (fireCommand->command == CMD_SLIDE_ADVANCE) {
            // Controller moved to a new slide — advance LED visual phase for that strip
            SlideAdvanceMessage* slideMsg = (SlideAdvanceMessage*)fireCommand;
            uint8_t strip        = constrain((int)slideMsg->stripIndex, 0, NUM_STRIPS - 1);
            uint8_t slideInStrip = slideMsg->slideInStrip; // 0, 1, or 2

            // Accept slide advances in any pre-game state — messages may arrive
            // while the wave animation is still running and must not be dropped.
            introStripPhase[strip] = (int)slideInStrip;
            introStripIndex = strip;

            if (slideInStrip == 0) {
                if (currentState == STATE_ENEMIES_FALLING) {
                    // Already in falling state — start blink immediately
                    slide0BlinkActive[strip] = true;
                    slide0BlinkStartTime[strip] = millis();
                }
                // else: wave still running — blink will be armed when wave ends (see below)
            } else if (slideInStrip == 1) {
                // Start the black-flash before WHO fill animation
                whoFlashing[strip] = true;
                whoFlashStartTime[strip] = millis();
                whoAnimPixels[strip] = 0;
                whoAnimating = false;
            } else if (slideInStrip == 2) {
                // Enemy drop-in: cancel any pending animation and initialise falling
                whoAnimating = false;
                whoFlashing[strip] = false;
                whoAnimPixels[strip] = whoBaselinePixels[strip];
                if (currentState == STATE_ENEMIES_FALLING) {
                    initializeFallingEnemiesForStrip(strip);
                    introStripEnemiesInitialized = true;
                }
            }
            DBG_INFO("Slide advance: strip %d -> phase %d\n", strip, slideInStrip);
            return;
        }
        if (fireCommand->command == CMD_PLAYER_READY) {
            if (currentState == STATE_WAITING_FOR_PLAYER) {
                currentState = STATE_ENVIRONMENTAL_GAME;
                stateStartTime = millis();
                DBG_INFO("CMD_PLAYER_READY received — game starting!\n");
            }
            return;
        }
        if (fireCommand->command == CMD_CITY_PREVIEW) {
            currentCityIndex = constrain((int)fireCommand->data[0], 0, NUM_CITIES - 1);
            currentAQI.pm25  = cityRoster[currentCityIndex].pm25;
            currentAQI.no2   = cityRoster[currentCityIndex].no2;
            currentAQI.o3    = cityRoster[currentCityIndex].o3;
            currentState = STATE_CITY_SELECT;
            Serial.printf("[CMD_CITY_PREVIEW] city=%d  state→CITY_SELECT\n", currentCityIndex);
            DBG_INFO("City preview: [%d] %s\n", currentCityIndex, cityRoster[currentCityIndex].name);
            return;
        }
        if (fireCommand->command == CMD_CITY_CONFIRM) {
            currentCityIndex = constrain((int)fireCommand->data[0], 0, NUM_CITIES - 1);
            currentAQI.pm25  = cityRoster[currentCityIndex].pm25;
            currentAQI.no2   = cityRoster[currentCityIndex].no2;
            currentAQI.o3    = cityRoster[currentCityIndex].o3;
            currentAQI.city  = cityRoster[currentCityIndex].name;
            saveAQI();
            initializeEnvironmentalGame();
            DBG_INFO("City confirmed: [%d] %s — starting game\n", currentCityIndex, cityRoster[currentCityIndex].name);
            return;
        }

        if (currentState == STATE_ENVIRONMENTAL_GAME) {
            int stripIndex = -1;
            bool isCannon = false;
            switch (fireCommand->command) {
                case CMD_FIRE_BLUE:   stripIndex = 0; isCannon = false; break;
                case CMD_FIRE_RED:    stripIndex = 1; isCannon = false; break;
                case CMD_FIRE_GREEN:  stripIndex = 2; isCannon = false; break;
                case CMD_CANNON_BLUE: stripIndex = 0; isCannon = true; break;
                case CMD_CANNON_RED:  stripIndex = 1; isCannon = true; break;
                case CMD_CANNON_GREEN:stripIndex = 2; isCannon = true; break;
                case CMD_CHARGING_START: {
                    ChargingStartMessage* chargeData = (ChargingStartMessage*)fireCommand->data;
                    uint8_t strip = chargeData->stripIndex;
                    if (strip < NUM_STRIPS) {
                        buttonCharging[strip] = true;
                        buttonPressTime[strip] = millis();
                        Serial.printf("⚡ Strip %d charging started\n", strip);
                    }
                    return;
                }
                default:
                    Serial.printf("⚠️  Unknown fire command: %d\n", fireCommand->command);
                    return;
            }
            
            // Reset charging state when any shot is fired
            if (stripIndex >= 0) {
                buttonCharging[stripIndex] = false;
            }
            
            if (isCannon) {
                createCannonShot(stripIndex);
                DBG_VERBOSE("CANNON shot on strip %d\n", stripIndex);
            } else {
                createPlayerShot(stripIndex);
                // Removed normal shot confirmation spam
            }
        }  // end STATE_ENVIRONMENTAL_GAME
        return;
    }
    
    Serial.printf("⚠️  Unknown message format received (size: %d)\n", data_len);
}

// =============================================================================
// MAIN SETUP AND LOOP - MARCH 13TH IMPLEMENTATION
// =============================================================================

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n🎮 1D Environmental Game Console - March 13th Implementation");
    Serial.println("Based on actual working code from git tag v1.0-single-board");
    
    // Initialize FastLED for all strips - MARCH 13TH
    FastLED.addLeds<NEOPIXEL, LED_PIN_PM25>(ledsPM25, LEDS_PER_STRIP);
    FastLED.addLeds<NEOPIXEL, LED_PIN_NO2>(ledsNO2, LEDS_PER_STRIP);
    FastLED.addLeds<NEOPIXEL, LED_PIN_O3>(ledsO3, LEDS_PER_STRIP);
    FastLED.setBrightness(LED_BRIGHTNESS);
    
    // Power optimization for ESP32-S3 with multiple LED strips
    FastLED.setMaxPowerInVoltsAndMilliamps(5, 3000); // Prevent power issues
    
    FastLED.clear();
    FastLED.show();
    
    // Initialize unified communication and status systems
    Serial.println("🔧 Initializing unified modules...");
    
    // Initialize status LED
    if (!statusLED.begin()) {
        Serial.println("❌ StatusLED initialization failed");
        return;
    }
    statusLED.showProcessing(); // Blue pulsing during startup
    
    // Initialize communication protocol
    if (!comm.begin(BoardType::CONSOLE, true)) {
        Serial.println("❌ Communication protocol initialization failed");
        statusLED.showError();
        return;
    }
    
    // Add controller as peer for communication
    if (!comm.addPeer(controllerMacAddress)) {
        Serial.println("❌ Failed to add controller as peer");
        statusLED.showError();
        return;
    }
    
    // Register communication callback
    comm.onMessageReceived(onDataReceived);
    
    Serial.println("✅ ESP-NOW initialized successfully");

    // Initialise mutable city roster from hardcoded defaults
    for (int i = 0; i < NUM_CITIES; i++) {
        cityRoster[i] = cityDefaults[i];
    }

    // Load saved AQI before initializing game state
    loadSavedAQI();

    // Start in attract mode — player sees aurora/demo loop before city selection
    currentState      = STATE_ATTRACT;
    attractPhase      = ATTRACT_AURORA;
    attractPhaseStart = millis();
    // Pre-populate currentAQI with first city's data for LED preview
    currentAQI.pm25 = cityRoster[currentCityIndex].pm25;
    currentAQI.no2  = cityRoster[currentCityIndex].no2;
    currentAQI.o3   = cityRoster[currentCityIndex].o3;
    currentAQI.city = cityRoster[currentCityIndex].name;
    
    Serial.println("🎯 Waiting for city selection!");
}

void loop() {
    unsigned long currentTime = millis();
    
    // Update game systems every frame - MARCH 13TH
    updateEnvironmentalGame();  // Contains victory sequence logic
    updatePlayerEffects(currentTime);
    updateWipeEffects();
    
    // Render complete display - MARCH 13TH
    renderEnvironmentalDisplay();  // Now handles victory + fireworks
    
    // Update LED strips - MARCH 13TH
    FastLED.show();
    
    // Performance monitoring every 30 seconds (reduced from 5s for less spam)
    static unsigned long lastStatusTime = 0;
    if (currentTime - lastStatusTime >= 30000) {
        DBG_INFO("Game Status - Shots:%d, Sparks:%d, Enemies:%d, FPS:%.1f\n", 
                     environmentalShots.size(), sparks.size(), 
                     pollutionEnemies[0].size() + pollutionEnemies[1].size() + pollutionEnemies[2].size(),
                     1000.0f / 33.0f);
        lastStatusTime = currentTime;
    }
    
    // City-select inactivity timeout → attract mode.
    // Guard: currentTime >= lastInputTime prevents unsigned underflow when the
    // ESP-NOW callback (Core 0) updates lastInputTime after currentTime was
    // captured on Core 1, which would produce a spurious ~4-billion ms delta.
    if (currentState == STATE_CITY_SELECT && currentTime >= lastInputTime && currentTime - lastInputTime > CITY_SELECT_TIMEOUT_MS) {
        currentState      = STATE_ATTRACT;
        attractPhase      = ATTRACT_AURORA;
        attractPhaseStart = millis();
        inAttractDemo     = false;
        AudioMessage attractMsg;
        attractMsg.command    = CMD_ENTER_ATTRACT;
        attractMsg.soundEvent = 0;
        memset(attractMsg.padding, 0, sizeof(attractMsg.padding));
        comm.sendMessage(controllerMacAddress, attractMsg);
        DBG_INFO("City-select timeout — entering attract mode\n");
    }

    // Check for input timeout (wireless connection monitoring) - MARCH 13TH
    if (currentTime - lastInputTime > INPUT_TIMEOUT_MS) {
        static unsigned long lastTimeoutWarning = 0;
        if (currentTime - lastTimeoutWarning > 60000) {  // Warning every 60 seconds (reduced spam)
            DBG_INFO("No input from controller - check wireless connection\n");
            lastTimeoutWarning = currentTime;
        }
    }
}

// =============================================================================
// MARCH 13TH IMPLEMENTATION COMPLETE
// =============================================================================
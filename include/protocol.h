#pragma once

#include <cstdint>

// =============================================================================
// ESP-NOW COMMUNICATION PROTOCOL  
// Shared message structures and constants for console-controller communication
// =============================================================================

// Message types sent to console
typedef enum {
    CMD_START_GAME = 1,
    CMD_SETTINGS_UPDATE = 2,
    CMD_AQI_DATA = 3, 
    CMD_FIRE_BLUE = 4,     // Fire blue shot at PM2.5 enemies
    CMD_FIRE_RED = 5,      // Fire red shot at NO2 enemies
    CMD_FIRE_GREEN = 6,    // Fire green shot at O3 enemies
    CMD_CANNON_BLUE = 7,   // Cannon (super) shot blue
    CMD_CANNON_RED = 8,    // Cannon (super) shot red
    CMD_CANNON_GREEN = 9,  // Cannon (super) shot green
    CMD_PLAY_AUDIO = 10,   // Audio playback command
    CMD_DISABLE_STRIP_AUDIO = 11,  // Disable audio for specific strip
    CMD_ENABLE_ALL_AUDIO = 12,     // Re-enable all strip audio
    CMD_CHARGING_START = 13,       // Button held for 1s - start charging effects
    CMD_CITY_ROSTER = 14,          // Full city AQI roster from web UI fetch
    CMD_CITY_PREVIEW = 15,         // Browse to city N (controller → console)
    CMD_CITY_CONFIRM = 16,         // Start game with selected city (controller → console)
    CMD_GAME_ENDED = 17,           // Game over / victory — return to city select (console → controller)
    CMD_GAME_READY = 18,           // All enemies settled — firing now enabled (console → controller)
    CMD_AQI_STATUS = 19,           // Live enemy counts per strip (console → controller, sent on each kill)
    CMD_STRIP_INTRO = 20,          // Strip N starting sequential load — controller shows educational slides
    CMD_PLAYER_READY = 21,         // Controller countdown finished — console may start enemy advancement
    CMD_SLIDE_ADVANCE = 22,        // Controller advanced to a new slide — console syncs LED visual phase
    CMD_ENTER_ATTRACT = 23         // Console → controller: display attract/start screen (city-select timeout)
} CommandType;

// Data structure for console communication  
typedef struct {
    uint8_t command;      // Command type (CMD_FIRE_*) 
    uint8_t data[32];     // Flexible data payload
} ConsoleMessage;

// Message structure from controller
struct ControllerMessage {
    CommandType command;
    union {
        struct {
            int level;
            int brightness;
        } start;
        struct {
            float pm25;
            float no2;
            float o3;
            char city[32];
        } aqi;
        struct {
            int brightness;
            bool soundEnabled;
        } settings;
    };
} __attribute__((packed));

// Audio command message between boards
struct AudioMessage {
    uint8_t command;         // CMD_PLAY_AUDIO
    uint8_t soundEvent;      // SoundEvent from audio system
    uint8_t padding[31];     // Match standard message size
} __attribute__((packed));

// Charging start message from controller 
struct ChargingStartMessage {
    uint8_t stripIndex;      // 0=Blue, 1=Red, 2=Green
    uint8_t padding[31];     // Pad to match ConsoleMessage size
} __attribute__((packed));

// City roster message — sent once after "Load All Cities" in web UI.
// Carries live AQI readings for all cities fetched by the phone's browser.
// int16_t is sufficient (AQI values never exceed a few hundred).
struct CityRosterMessage {
    uint8_t command;     // CMD_CITY_ROSTER
    uint8_t cityCount;   // Number of cities in this roster
    struct {
        int16_t pm25;
        int16_t no2;
        int16_t o3;
    } cities[8];         // 8 cities × 6 bytes = 48 bytes; total msg = 50 bytes
} __attribute__((packed));

// AQI status message — sent console → controller after each enemy kill.
// Carries remaining enemy counts so the LCD scoreboard stays current.
struct AQIStatusMessage {
    uint8_t command;       // CMD_AQI_STATUS
    int16_t remaining[3];  // enemies still alive: [0]=PM2.5, [1]=NO2, [2]=O3
    uint8_t padding[26];   // pad to 33 bytes (matches AudioMessage)
} __attribute__((packed));

// Strip intro message — sent console → controller when each strip begins sequential loading.
// Controller uses this to sync educational LCD slides with LED animation.
struct StripIntroMessage {
    uint8_t command;    // CMD_STRIP_INTRO
    uint8_t stripIndex; // 0=PM2.5, 1=NO2, 2=Ozone
    uint8_t padding[31]; // pad to 33 bytes (matches AudioMessage)
} __attribute__((packed));

// Slide advance message — sent controller → console when player moves to a new slide.
// Console uses slideInStrip (0/1/2) to know which LED visual phase to start.
struct SlideAdvanceMessage {
    uint8_t command;      // CMD_SLIDE_ADVANCE
    uint8_t stripIndex;   // 0=PM2.5, 1=NO2, 2=Ozone
    uint8_t slideInStrip; // 0=full white column, 1=WHO fill, 2=enemy drop-in
    uint8_t padding[30];  // pad to 33 bytes
} __attribute__((packed));


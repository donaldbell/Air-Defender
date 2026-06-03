/**
 * Environmental Game - Controller Unit  
 * Adafruit QT Py S3 WiFi & Web Interface Controller
 * 
 * Responsibilities:
 * - WiFi management and web interface 
 * - User button input during startup (2-second window)
 * - Settings configuration via web interface
 * - ESP-NOW transmission to console unit
 * - Status indication via onboard NeoPixel
 * 
 * Hardware: QT Py ESP32-S3 (4MB Flash, 2MB PSRAM)
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <esp_now.h>
#include <Preferences.h>
#include "../include/game_config.h"
#include "../include/protocol.h"
#include "../include/audio_system.h"
#include "../include/communication_protocol.h"
#include "../include/status_led.h"
#include "../include/lcd_display.h"

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

// For one-time status messages (prevent spam)
#define DBG_ONCE(fmt, ...) { static bool printed = false; if (!printed) { Serial.printf(fmt, ##__VA_ARGS__); printed = true; } }

// =============================================================================
// HARDWARE CONFIGURATION - QT Py S3
// =============================================================================

// Console MAC address (from console board upload: 34:b7:da:57:36:fc)
uint8_t consoleMacAddress[6] = {0x34, 0xb7, 0xda, 0x57, 0x36, 0xfc};

// =============================================================================
// UNIFIED MODULE INSTANCES
// =============================================================================
StatusLED statusLED;
LCDDisplay lcdDisplay;
// Note: CommunicationProtocol comm is global from communication_protocol.cpp

// =============================================================================
// GLOBAL STATE VARIABLES
// =============================================================================

Preferences preferences;
WebServer server(80);
AirQualityData currentAQI = {0, 0, 0, "Unknown", 0, false};  // Current air quality data

int gameLanguage = LANG_EN;  // 0=EN 1=ES 2=CA — loaded from Preferences at boot

// Arcade button state tracking
bool buttonsReleased = true;                     // Prevent button hold spam
unsigned long lastFireTime = 0;                  // Fire rate limiting

// Settings that can be configured
struct GameSettings {
    char wifiSSID[32] = "AQI_Game";     // AP mode SSID
    char wifiPassword[32] = "environmental";  // AP mode password
} settings;

// =============================================================================
// SYSTEM MODE MANAGEMENT
// =============================================================================

// Operating modes: separate gaming from configuration
enum SystemMode {
    MODE_GAMING,    // Normal gameplay - ESP-NOW only, no WiFi AP
    MODE_AP_CONFIG  // AP configuration mode - WiFi + web server, no gaming
};

SystemMode currentMode = MODE_GAMING;

// City-select browse state (active before game starts and after each victory)
bool inCitySelectMode = true;
bool inAttractMode    = true;           // True at startup and after console inactivity timeout
unsigned long attractBlinkTime  = 0;   // Tracks last LCD blink toggle
bool attractBlinkState = false;        // true = "PRESS ANY BUTTON" visible
// Deferred LCD refresh flags — set by ESP-NOW callbacks (WiFi task), consumed by loop() (main task).
// Wire I²C is NOT thread-safe: never call lcd.* directly from a callback.
volatile bool pendingCitySelectRedraw = false; // set by CMD_GAME_ENDED, consumed in loop()
int browseIndex = 0;  // Current city index while browsing

// City name captured at city-confirm for victory screen
String confirmedCityName = "";

// Firing lock — disabled until console confirms all enemies have settled
bool firingEnabled = false;

// Live game scoreboard mode — true once CMD_GAME_READY fires, suppresses drift
bool inGameStatusMode = false;

// Victory screen flag — suppresses LCD drift while celebration is displayed
bool inVictoryScreen = false;

// Defeat screen state — smog wipe animation then static defeat message
bool inDefeatScreen       = false;
bool defeatWipeActive     = false;
unsigned long defeatWipeStartTime = 0;
static const unsigned long DEFEAT_WIPE_MS = 1500;  // wipe fills screen over 1.5s

// Intro slide system — active between city confirm and CMD_GAME_READY
// 9 slides: 3 per pollutant (0-2=PM2.5, 3-5=NO2, 6-8=Ozone).
// Slides 0+1 auto-advance; slide 2 (city levels) is a gate requiring button press.
bool inIntroSlides = false;
int  introSlideIndex = 0;
unsigned long slideStartTime = 0;
static const unsigned long SLIDE_DURATION_MS = 5000; // auto-advance every 5s

// Countdown system — 3-2-1 before game starts, triggered only after slides complete
bool inCountdown = false;
int  countdownValue = 3;
unsigned long countdownStartTime = 0;

// Set when console signals all enemies are loaded; countdown deferred until slides finish
bool gameReadyReceived = false;

// Lockout timestamp — ignores button input for 400ms after city-confirm to prevent
// the confirming button press from immediately skipping the first intro slide.
unsigned long introSlideUnlockTime = 0;

// Cached AQI remaining counts from last CMD_AQI_STATUS (used to populate scoreboard after countdown)
int lastAQIRemaining[3] = {0, 0, 0};

// Local city cache — mirrors cityDefaults[], updated when Load All Cities is run
struct LocalCityData { int pm25; int no2; int o3; };
LocalCityData localCityCache[8];
bool localCityCacheLoaded = false;

// =============================================================================  
// FUNCTION DECLARATIONS
// =============================================================================

void handleArcadeButtons();
void sendFireCommand(CommandType fireType);
void sendChargingStartMessage(uint8_t stripIndex);
void sendCityPreview(int idx);
int  getCityValueForStrip(int strip);
bool checkForAPModeRequest();
void sendSlideAdvance(int strip, int slideInStrip);
void setupGamingMode();
void setupAPConfigurationMode();
void saveAQI();
void loadAQI();
void loadLanguage();
void saveLanguage();
void handleLanguage();

// Button press timing for long-press detection
unsigned long bluePressStart = 0;
unsigned long redPressStart = 0;
unsigned long greenPressStart = 0;
bool blueWasPressed = false;
bool redWasPressed = false;
bool greenWasPressed = false;

// Strip-specific audio control state
bool stripAudioEnabled[3] = {true, true, true}; // PM2.5, NO2, O3

// Charging state flags to prevent message spam
bool blueChargingMessageSent = false;
bool redChargingMessageSent = false;
bool greenChargingMessageSent = false;
// =============================================================================
// ESP-NOW COMMUNICATION FUNCTIONS
// =============================================================================

void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (status == ESP_NOW_SEND_SUCCESS) {
        DBG_VERBOSE("Command sent to console\n");
    } else {
        DBG_ERROR("Failed to send command to console\n");
    }
}

void onAudioDataReceived(const uint8_t *mac_addr, const uint8_t *data, int data_len) {
    // Handle incoming audio commands from console
    DBG_VERBOSE("ESP-NOW message received: %d bytes\n", data_len);
    
    if (data_len == sizeof(AudioMessage)) {
        AudioMessage* audioCmd = (AudioMessage*)data;
        DBG_VERBOSE("Audio command: cmd=%d, event=%d\n", 
                     audioCmd->command, audioCmd->soundEvent);
        
        if (audioCmd->command == CMD_PLAY_AUDIO) {
            DBG_VERBOSE("Playing sound event %d\n", audioCmd->soundEvent);
            
            // Verify audio system is ready
            if (isAudioEnabled()) {
                playSound((SoundEvent)audioCmd->soundEvent);
            } else {
                DBG_INFO("Audio system disabled - enabling and retrying\n");
                enableAudio();
                setAudioVolume(90);
                playSound((SoundEvent)audioCmd->soundEvent);
            }

            // Show celebration screen when level is won
            if ((SoundEvent)audioCmd->soundEvent == EVT_LEVEL_VICTORY) {
                inVictoryScreen = true;
                lcdDisplay.displayVictoryScreen(confirmedCityName.c_str());
                DBG_INFO("Victory screen displayed\n");
            }

            // Start smog-wipe defeat animation when pollution wins
            if ((SoundEvent)audioCmd->soundEvent == EVT_LOSE) {
                inDefeatScreen    = true;
                defeatWipeActive  = true;
                defeatWipeStartTime = millis();
                lcdDisplay.startDefeatWipe();
                DBG_INFO("Defeat animation started\n");
            }
            
        } else if (audioCmd->command == CMD_DISABLE_STRIP_AUDIO) {
            int stripIndex = audioCmd->soundEvent; // Strip index passed in soundEvent field
            if (stripIndex >= 0 && stripIndex < 3) {
                stripAudioEnabled[stripIndex] = false;
                DBG_VERBOSE("Audio disabled for strip %d\n", stripIndex);
            }
            
        } else if (audioCmd->command == CMD_ENABLE_ALL_AUDIO) {
            for (int i = 0; i < 3; i++) {
                stripAudioEnabled[i] = true;
            }
            DBG_VERBOSE("Audio enabled for all strips\n");

        } else if (audioCmd->command == CMD_AQI_STATUS) {
            // Live scoreboard update from console — always cache; display only during active gameplay
            AQIStatusMessage* aqiMsg = (AQIStatusMessage*)data;
            if (data_len >= (int)sizeof(AQIStatusMessage)) {
                lastAQIRemaining[0] = aqiMsg->remaining[0];
                lastAQIRemaining[1] = aqiMsg->remaining[1];
                lastAQIRemaining[2] = aqiMsg->remaining[2];
                if (!inAttractMode && !inIntroSlides && !inCountdown && !inDefeatScreen && !inVictoryScreen) {
                    lcdDisplay.displayGameStatus(lastAQIRemaining[0], lastAQIRemaining[1], lastAQIRemaining[2]);
                    inGameStatusMode = true;
                }
                DBG_VERBOSE("AQI status: PM2.5=%d NO2=%d O3=%d enemies remaining\n",
                            aqiMsg->remaining[0], aqiMsg->remaining[1], aqiMsg->remaining[2]);
            }

        } else if (audioCmd->command == CMD_STRIP_INTRO) {
            // Console confirmed strip N is loading — player controls pacing through gate slides
            StripIntroMessage* introMsg = (StripIntroMessage*)data;
            if (data_len >= (int)sizeof(StripIntroMessage)) {
                DBG_INFO("Strip intro: strip %d loading\n", introMsg->stripIndex);
            }

        } else if (audioCmd->command == CMD_GAME_READY) {
            // All enemies settled — record flag; countdown starts after player finishes final slide
            gameReadyReceived = true;
            DBG_INFO("Game ready received — waiting for player to finish slides\n");

        } else if (audioCmd->command == CMD_GAME_ENDED) {
            // Console has returned to city-select — re-enter browse mode
            inCitySelectMode        = true;
            inAttractMode           = false;
            inVictoryScreen         = false;
            inDefeatScreen          = false;
            defeatWipeActive        = false;
            inGameStatusMode        = false;
            inIntroSlides           = false;
            inCountdown             = false;
            gameReadyReceived       = false;
            firingEnabled           = false;
            pendingCitySelectRedraw = true;  // Defer LCD + console notify to main loop (I²C safety)
            DBG_INFO("Game ended — returning to city select\n");

        } else if (audioCmd->command == CMD_ENTER_ATTRACT) {
            // Console city-select timed out — show attract/start screen.
            // Do NOT call any lcd.* here: this callback runs in the WiFi task and
            // Wire I²C is not thread-safe.  Set attractBlinkTime=0 so the blink
            // handler in loop() fires immediately on the next iteration instead.
            inAttractMode      = true;
            inCitySelectMode   = true;
            inVictoryScreen    = false;
            inDefeatScreen     = false;
            defeatWipeActive   = false;
            inGameStatusMode   = false;
            inIntroSlides      = false;
            inCountdown        = false;
            gameReadyReceived  = false;
            firingEnabled      = false;
            attractBlinkState  = true;
            attractBlinkTime   = 0;  // Force blink handler to fire immediately in loop()
            DBG_INFO("Attract mode — waiting for player\n");

        } else {
            DBG_ERROR("Unknown command type: %d\n", audioCmd->command);
        }
    } else {
        DBG_ERROR("Wrong message size: expected %d, got %d\n", 
                     sizeof(AudioMessage), data_len);
    }
}

bool sendToConsole(CommandType cmd, const void* data = nullptr, size_t dataSize = 0) {
    ConsoleMessage message;
    message.command = (uint8_t)cmd;  // Cast enum to uint8_t to match console structure
    
    // Copy data payload if provided
    if (data && dataSize <= sizeof(message.data)) {
        memcpy(message.data, data, dataSize);
    } else {
        memset(message.data, 0, sizeof(message.data));
    }
    
    bool result = comm.sendMessage(consoleMacAddress, &message, sizeof(message));
    return result;
}

// =============================================================================
// SETTINGS & STORAGE FUNCTIONS  
// =============================================================================

void loadSettings() {
    preferences.begin("controller", false);
    
    preferences.getString("wifiSSID", settings.wifiSSID, sizeof(settings.wifiSSID));
    preferences.getString("wifiPassword", settings.wifiPassword, sizeof(settings.wifiPassword));
    
    // Set defaults if empty
    if (strlen(settings.wifiSSID) == 0) {
        strcpy(settings.wifiSSID, "AQI_Game");
    }
    if (strlen(settings.wifiPassword) == 0) {
        strcpy(settings.wifiPassword, "environmental");
    }
    
    preferences.end();
    
    DBG_INFO("Settings loaded from preferences\n");
}

void saveSettings() {
    preferences.begin("controller", false);
    
    preferences.putString("wifiSSID", settings.wifiSSID);
    preferences.putString("wifiPassword", settings.wifiPassword);
    
    preferences.end();
    
    DBG_INFO("Settings saved to preferences\n");
}

void loadLanguage() {
    preferences.begin("controller", true);
    gameLanguage = preferences.getInt("language", LANG_EN);
    preferences.end();
    if (gameLanguage < LANG_EN || gameLanguage > LANG_CA) gameLanguage = LANG_EN;
    DBG_INFO("Language loaded: %d\n", gameLanguage);
}

void saveLanguage() {
    preferences.begin("controller", false);
    preferences.putInt("language", gameLanguage);
    preferences.end();
    DBG_INFO("Language saved: %d\n", gameLanguage);
}

void saveAQI() {
    preferences.begin("controller", false);
    
    preferences.putInt("aqi_pm25", currentAQI.pm25);
    preferences.putInt("aqi_no2", currentAQI.no2);
    preferences.putInt("aqi_o3", currentAQI.o3);
    preferences.putString("aqi_city", currentAQI.city);
    preferences.putULong("aqi_timestamp", currentAQI.lastUpdate);
    preferences.putBool("aqi_valid", currentAQI.dataValid);
    
    preferences.end();
    
    DBG_INFO("AQI data saved to flash storage\n");
}

void loadAQI() {
    preferences.begin("controller", true); // Read-only mode
    
    currentAQI.pm25 = preferences.getInt("aqi_pm25", 0);
    currentAQI.no2 = preferences.getInt("aqi_no2", 0);
    currentAQI.o3 = preferences.getInt("aqi_o3", 0);
    currentAQI.city = preferences.getString("aqi_city", "Unknown");
    currentAQI.lastUpdate = preferences.getULong("aqi_timestamp", 0);
    currentAQI.dataValid = preferences.getBool("aqi_valid", false);
    
    preferences.end();
    
    if (currentAQI.dataValid) {
        DBG_INFO("AQI data loaded from flash: PM2.5=%d, NO2=%d, O3=%d, City=%s\n", 
                     currentAQI.pm25, currentAQI.no2, currentAQI.o3, currentAQI.city.c_str());
    } else {
        DBG_INFO("No valid AQI data found in flash storage\n");
    }
}

// =============================================================================
// WEB SERVER & INTERFACE FUNCTIONS
// =============================================================================

void handleRoot() {
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<title>Environmental Game Controller</title>";
    html += "<style>";
    html += "body { font-family: Arial, sans-serif; margin: 20px; background: #f0f0f0; }";
    html += ".container { max-width: 600px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }";
    html += "h1 { color: #2c3e50; text-align: center; }";
    html += ".section { margin: 20px 0; padding: 15px; border: 1px solid #ddd; border-radius: 5px; }";
    html += ".control { margin: 10px 0; }";
    html += "label { display: inline-block; width: 150px; font-weight: bold; }";
    html += "input, select { width: 200px; padding: 5px; }";
    html += "button { background: #3498db; color: white; padding: 10px 20px; border: none; border-radius: 5px; cursor: pointer; margin: 5px; }";
    html += "button:hover { background: #2980b9; }";
    html += ".city-btn { background: #3498db; color: white; padding: 6px 12px; margin: 3px; font-size: 14px; border-radius: 4px; }";
    html += ".city-btn:hover { background: #2980b9; }";
    html += ".load-btn { background: #8e44ad; color: white; padding: 10px 22px; font-size: 15px; font-weight: bold; border-radius: 6px; }";
    html += ".load-btn:hover { background: #6c3483; }";
    html += ".status { padding: 10px; border-radius: 5px; margin: 10px 0; }";
    html += ".status.good { background: #d4edda; color: #155724; }";
    html += ".status.error { background: #f8d7da; color: #721c24; }";
    html += ".status.warning { background: #fff3cd; color: #856404; }";
    html += ".game-controls { text-align: center; }";
    html += ".game-controls button { font-size: 18px; padding: 15px 30px; margin: 10px; }";
    html += ".city-table { width:100%; border-collapse:collapse; margin:12px 0; }";
    html += ".city-table th { background:#2c3e50; color:white; padding:8px 6px; text-align:center; font-size:13px; }";
    html += ".city-table th:first-child { text-align:left; padding-left:8px; }";
    html += ".city-table td { padding:6px 4px; border-bottom:1px solid #eee; font-size:14px; }";
    html += ".city-table td:first-child { font-weight:bold; color:#2c3e50; padding-left:8px; }";
    html += ".city-table input[type=number] { width:64px; padding:4px; text-align:center; border:1px solid #ccc; border-radius:3px; }";
    html += ".send-btn { background:#27ae60; color:white; font-size:16px; padding:12px 28px; border:none; border-radius:5px; cursor:pointer; font-weight:bold; }";
    html += ".send-btn:hover { background:#219a52; }";
    html += "</style></head><body>";
    
    html += "<div class='container'>";
    html += "<h1>Environmental Game Controller</h1>";
    
    html += "<div class='section'>";
    html += "<h2>City AQI Data</h2>";

    // Load All Cities button + status
    html += "<div class='control' style='text-align:center; padding: 8px 0 6px;'>";
    html += "<button onclick='loadAllCities()' class='load-btn'>&#127758; Load All Cities</button>";
    html += "<div id='rosterStatus' style='font-size:13px; color:#666; margin-top:6px;'>Fetch live AQI for all 8 cities, then review, edit and send.</div>";
    html += "</div>";

    // Per-city editable table — one row per city in cityDefaults[]
    html += "<table class='city-table'>";
    html += "<thead><tr><th>City</th><th>PM2.5 (&#181;g/m&#179;)</th><th>NO2 (ppb)</th><th>O3 (ppb)</th></tr></thead>";
    html += "<tbody>";
    for (int ci = 0; ci < NUM_CITIES; ci++) {
        html += "<tr>";
        html += "<td>" + String(cityDefaults[ci].name) + "</td>";
        html += "<td><input type='number' id='pm25_" + String(ci) + "' min='0' max='500' value='" + String(cityDefaults[ci].pm25) + "'></td>";
        html += "<td><input type='number' id='no2_"  + String(ci) + "' min='0' max='200' value='" + String(cityDefaults[ci].no2)  + "'></td>";
        html += "<td><input type='number' id='o3_"   + String(ci) + "' min='0' max='300' value='" + String(cityDefaults[ci].o3)   + "'></td>";
        html += "</tr>";
    }
    html += "</tbody></table>";

    // Send all cities button + status
    html += "<div style='text-align:center; margin-top:12px;'>";
    html += "<button onclick='sendAllCities()' class='send-btn'>&#128225; Send All Cities to Console</button>";
    html += "<div id='sendStatus' style='font-size:13px; color:#666; margin-top:6px;'></div>";
    html += "</div>";
    html += "</div>";

    html += "<div class='section'>";
    html += "<h2>System Status</h2>";
    html += "<div id='systemStatus' class='status good'>Controller Ready - Console Communication Active</div>";
    html += "<button onclick='location.reload()'>Refresh Status</button>";
    html += "</div>";

    // ── Language selector (admin only — not visible to players) ──────────────
    html += "<div class='section'>";
    html += "<h2>LCD Language</h2>";
    html += "<p style='font-size:13px; color:#555; margin:0 0 10px;'>Sets the language used on the controller LCD screen. Reload the page to see the current setting.</p>";
    html += "<div>";
    html += "<label><input type='radio' name='lang' value='0'" + String(gameLanguage == LANG_EN ? " checked" : "") + "> English</label>&nbsp;&nbsp;";
    html += "<label><input type='radio' name='lang' value='1'" + String(gameLanguage == LANG_ES ? " checked" : "") + "> Espa&ntilde;ol</label>&nbsp;&nbsp;";
    html += "<label><input type='radio' name='lang' value='2'" + String(gameLanguage == LANG_CA ? " checked" : "") + "> Catal&agrave;</label>";
    html += "</div>";
    html += "<div style='margin-top:10px;'>";
    html += "<button onclick='setLanguage()'>Set Language</button>";
    html += "<span id='langStatus' style='margin-left:12px; font-size:13px;'></span>";
    html += "</div>";
    html += "</div>";

    html += "</div>";  // close .container
    
    html += "<script>";
    html += "const CITY_SLUGS = ['sydney','london','barcelona','new-york','mexico-city','shanghai','mumbai','delhi'];";
    html += "const CITY_NAMES = ['Sydney','London','Barcelona','New York','Mexico City','Shanghai','Mumbai','Delhi'];";
    html += "const AQI_TOKEN = 'c96c0076ad7616b27dcd240b92f25f40703abe28';";
    // Convert US EPA PM2.5 AQI sub-index to actual ug/m3 concentration
    html += "function aqiToPm25(a){const b=[[0,50,0,12],[51,100,12.1,35.4],[101,150,35.5,55.4],[151,200,55.5,150.4],[201,300,150.5,250.4],[301,400,250.5,350.4]];for(const[il,ih,cl,ch]of b){if(a>=il&&a<=ih)return Math.round(cl+(ch-cl)*(a-il)/(ih-il));}return Math.round(a*0.24);}";
    html += "function setLanguage() {";
    html += "  const el = document.getElementById('langStatus');";
    html += "  const sel = document.querySelector('input[name=lang]:checked');";
    html += "  if (!sel) { el.style.color='#721c24'; el.innerHTML='Please select a language.'; return; }";
    html += "  el.style.color='#856404'; el.innerHTML='Saving...';";
    html += "  fetch('/api/language', {method:'POST', body: sel.value})";
    html += "    .then(r => r.text())";
    html += "    .then(msg => { el.style.color='#155724'; el.innerHTML='&#10003; ' + msg; })";
    html += "    .catch(e => { el.style.color='#721c24'; el.innerHTML='Error: ' + e.message; });";
    html += "}";
    // Load All Cities: fetch each city and populate the table fields (does NOT send to console)
    html += "async function loadAllCities() {";
    html += "  const rosterEl = document.getElementById('rosterStatus');";
    html += "  rosterEl.style.color = '#856404';";
    html += "  for (let i = 0; i < CITY_SLUGS.length; i++) {";
    html += "    rosterEl.innerHTML = 'Fetching ' + CITY_NAMES[i] + ' (' + (i+1) + '/' + CITY_SLUGS.length + ')...';";
    html += "    try {";
    html += "      const url = 'https://api.waqi.info/feed/' + CITY_SLUGS[i] + '/?token=' + AQI_TOKEN;";
    html += "      const resp = await fetch(url);";
    html += "      const data = await resp.json();";
    html += "      if (data.status === 'ok') {";
    html += "        const iaqi = data.data.iaqi; const aqi = data.data.aqi;";
    html += "        let pm25 = (iaqi.pm25 && iaqi.pm25.v) ? aqiToPm25(iaqi.pm25.v) : aqiToPm25(aqi);";
    html += "        let no2  = (iaqi.no2  && iaqi.no2.v)  ? Math.round(iaqi.no2.v)  : Math.round(aqi * 0.3);";
    html += "        let o3   = (iaqi.o3   && iaqi.o3.v)   ? Math.round(iaqi.o3.v)   : Math.round(aqi * 0.4);";
    html += "        document.getElementById('pm25_' + i).value = pm25;";
    html += "        document.getElementById('no2_'  + i).value = no2;";
    html += "        document.getElementById('o3_'   + i).value = o3;";
    html += "      }";
    html += "    } catch(e) { /* leave existing value */ }";
    html += "  }";
    html += "  rosterEl.style.color = '#155724';";
    html += "  rosterEl.innerHTML = '&#10003; All cities loaded \\u2014 review, edit if needed, then send.';";
    html += "}";
    // Send All Cities: read table fields and POST roster to ESP32
    html += "function sendAllCities() {";
    html += "  const sendEl = document.getElementById('sendStatus');";
    html += "  sendEl.style.color = '#856404'; sendEl.innerHTML = 'Sending...';";
    html += "  const results = [];";
    html += "  for (let i = 0; i < 8; i++) {";
    html += "    results.push({";
    html += "      pm25: parseInt(document.getElementById('pm25_' + i).value) || 0,";
    html += "      no2:  parseInt(document.getElementById('no2_'  + i).value) || 0,";
    html += "      o3:   parseInt(document.getElementById('o3_'   + i).value) || 0";
    html += "    });";
    html += "  }";
    html += "  fetch('/api/city_roster', {";
    html += "    method: 'POST',";
    html += "    headers: {'Content-Type': 'application/json'},";
    html += "    body: JSON.stringify(results)";
    html += "  }).then(r => r.text()).then(msg => {";
    html += "    sendEl.style.color = '#155724';";
    html += "    sendEl.innerHTML = '&#10003; ' + msg;";
    html += "    document.getElementById('systemStatus').innerHTML = 'City roster sent to console';";
    html += "    document.getElementById('systemStatus').className = 'status good';";
    html += "  }).catch(e => {";
    html += "    sendEl.style.color = '#721c24';";
    html += "    sendEl.innerHTML = 'Send failed: ' + e.message;";
    html += "  });";
    html += "}";
    html += "</script></body></html>";
    
    server.send(200, "text/html", html);
}

void handleLanguage() {
    if (server.method() != HTTP_POST) {
        server.send(405, "text/plain", "Method Not Allowed");
        return;
    }
    String body = server.arg("plain");
    int lang = body.toInt();
    if (lang < LANG_EN || lang > LANG_CA) {
        server.send(400, "text/plain", "Invalid language");
        return;
    }
    gameLanguage = lang;
    saveLanguage();
    lcdDisplay.setLanguage(gameLanguage);
    server.send(200, "text/plain", "Language updated");
}

void handleAQIUpdate() {
    if (server.method() == HTTP_POST) {
        // Parse AQI data from POST request
        if (server.hasArg("pm25") && server.hasArg("no2") && server.hasArg("o3")) {
            currentAQI.pm25 = server.arg("pm25").toInt();
            currentAQI.no2 = server.arg("no2").toInt();
            currentAQI.o3 = server.arg("o3").toInt();
            currentAQI.lastUpdate = millis();
            currentAQI.dataValid = true;
            
            // CRITICAL: Send to console first (preserves game functionality)
            ControllerMessage msg;
            msg.command = CMD_AQI_DATA;
            msg.aqi.pm25 = currentAQI.pm25;
            msg.aqi.no2 = currentAQI.no2;
            msg.aqi.o3 = currentAQI.o3;
            strncpy(msg.aqi.city, currentAQI.city.c_str(), sizeof(msg.aqi.city));
            msg.aqi.city[sizeof(msg.aqi.city)-1] = '\0';
            
            bool consoleSendSuccess = comm.sendMessage(consoleMacAddress, &msg, sizeof(msg));
            
            // NEW: Save to flash storage only if console send succeeded (atomic operation)
            if (consoleSendSuccess) {
                saveAQI();
                lcdDisplay.displayAQI(currentAQI);
                DBG_INFO("AQI Updated & Saved: PM2.5=%d, NO2=%d, O3=%d\\n", 
                             currentAQI.pm25, currentAQI.no2, currentAQI.o3);
                server.send(200, "text/plain", "AQI data sent to console and saved locally");
            } else {
                DBG_ERROR("Failed to send AQI to console - not saving locally\\n");
                server.send(500, "text/plain", "Failed to send AQI data to console");
            }
        } else {
            server.send(400, "text/plain", "Missing AQI parameters");
        }
    } else {
        server.send(405, "text/plain", "Method not allowed");
    }
}

void handleCityRoster() {
    if (server.method() != HTTP_POST) {
        server.send(405, "text/plain", "Method not allowed");
        return;
    }
    String body = server.arg("plain");
    if (body.isEmpty()) {
        server.send(400, "text/plain", "Empty body");
        return;
    }

    // Parse JSON array: [{pm25,no2,o3}, ...]
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        server.send(400, "text/plain", String("JSON parse error: ") + err.c_str());
        return;
    }

    JsonArray arr = doc.as<JsonArray>();
    int count = min((int)arr.size(), 8);

    CityRosterMessage msg;
    msg.command   = CMD_CITY_ROSTER;
    msg.cityCount = (uint8_t)count;
    memset(msg.cities, 0, sizeof(msg.cities));

    for (int i = 0; i < count; i++) {
        msg.cities[i].pm25 = (int16_t)arr[i]["pm25"].as<int>();
        msg.cities[i].no2  = (int16_t)arr[i]["no2"].as<int>();
        msg.cities[i].o3   = (int16_t)arr[i]["o3"].as<int>();
    }

    bool ok = comm.sendMessage(consoleMacAddress, &msg, sizeof(msg));
    if (ok) {
        // Also cache locally for LCD city-select display
        for (int i = 0; i < count; i++) {
            localCityCache[i].pm25 = msg.cities[i].pm25;
            localCityCache[i].no2  = msg.cities[i].no2;
            localCityCache[i].o3   = msg.cities[i].o3;
        }
        localCityCacheLoaded = true;
        DBG_INFO("City roster sent to console: %d cities\n", count);
        server.send(200, "text/plain", String(count) + " cities sent to console");
    } else {
        server.send(500, "text/plain", "Failed to send roster to console");
    }
}

bool initWebServer() {
    // WiFi AP already configured; just start it
    WiFi.softAP(settings.wifiSSID, settings.wifiPassword);

    IPAddress IP = WiFi.softAPIP();
    DBG_INFO("Controller AP started: %s (IP: %s)\n", settings.wifiSSID, IP.toString().c_str());
    
    // Set up web server routes
    server.on("/", handleRoot);
    server.on("/api/aqi", HTTP_POST, handleAQIUpdate);
    server.on("/api/city_roster", HTTP_POST, handleCityRoster);
    server.on("/api/language", HTTP_POST, handleLanguage);
    
    server.begin();
    DBG_INFO("Web interface ready at http://%s\n", IP.toString().c_str());
    return true;
}

// =============================================================================
// AIR QUALITY DATA FUNCTIONS
// =============================================================================

// (fetchAndSendAQI function moved above to fix declaration order)

// =============================================================================
// BUTTON HANDLING FUNCTIONS
// =============================================================================

// =============================================================================
// ARCADE BUTTON GAMEPLAY FUNCTIONS  
// =============================================================================

// Return the cached city AQI value for a given strip index (0=PM2.5, 1=NO2, 2=O3).
int getCityValueForStrip(int strip) {
    if (strip == 0) return localCityCacheLoaded ? localCityCache[browseIndex].pm25 : cityDefaults[browseIndex].pm25;
    if (strip == 1) return localCityCacheLoaded ? localCityCache[browseIndex].no2  : cityDefaults[browseIndex].no2;
    return                localCityCacheLoaded ? localCityCache[browseIndex].o3   : cityDefaults[browseIndex].o3;
}

void sendCityPreview(int idx) {
    ConsoleMessage msg;
    msg.command = (uint8_t)CMD_CITY_PREVIEW;
    memset(msg.data, 0, sizeof(msg.data));
    msg.data[0] = (uint8_t)idx;
    bool ok = comm.sendMessage(consoleMacAddress, &msg, sizeof(msg));
    Serial.printf("[ESP-NOW TX] CMD_CITY_PREVIEW idx=%d size=%d → %s\n",
                  idx, (int)sizeof(msg), ok ? "OK" : "FAIL");

    // Update LCD with city data (use live cache if available, else defaults)
    int pm25 = localCityCacheLoaded ? localCityCache[idx].pm25 : cityDefaults[idx].pm25;
    int no2  = localCityCacheLoaded ? localCityCache[idx].no2  : cityDefaults[idx].no2;
    int o3   = localCityCacheLoaded ? localCityCache[idx].o3   : cityDefaults[idx].o3;
    lcdDisplay.displayCitySelect(cityDefaults[idx].name, pm25, no2, o3, idx, NUM_CITIES);
    DBG_INFO("City preview: [%d] %s\n", idx, cityDefaults[idx].name);
}

// Notify console of current slide visual phase so LEDs stay in sync with LCD text.
// Also plays a contextual tone to reinforce the visual moment.
void sendSlideAdvance(int strip, int slideInStrip) {
    SlideAdvanceMessage msg;
    msg.command      = (uint8_t)CMD_SLIDE_ADVANCE;
    msg.stripIndex   = (uint8_t)strip;
    msg.slideInStrip = (uint8_t)slideInStrip;
    memset(msg.padding, 0, sizeof(msg.padding));
    comm.sendMessage(consoleMacAddress, &msg, sizeof(msg));

    // Contextual audio feedback tied to LED visual phase
    if (slideInStrip == 0) {
        // Column reveal — bright rising note
        playToneI2S(660, 120);
    } else if (slideInStrip == 1) {
        // WHO guideline — soft ascending two-note
        playToneI2S(440, 80);
        playToneI2S(550, 100);
    } else if (slideInStrip == 2) {
        // Enemies incoming — short descending warning
        playToneI2S(330, 80);
        playToneI2S(220, 120);
    }
}

void handleArcadeButtons() {
    // Read current button states
    bool bluePressed = (digitalRead(PIN_BTN_BLUE) == LOW);
    bool redPressed = (digitalRead(PIN_BTN_RED) == LOW);
    bool greenPressed = (digitalRead(PIN_BTN_GREEN) == LOW);

    unsigned long now = millis();
    static unsigned long lastCannonFire[3] = {0, 0, 0};  // Per-strip cannon cooldown tracking

    // -------------------------------------------------------------------------
    // ATTRACT MODE — any button press exits to city select
    // -------------------------------------------------------------------------
    static bool justExitedAttract = false;

    if (inAttractMode) {
        if (bluePressed || redPressed || greenPressed) {
            Serial.printf("[ATTRACT] Button press: B=%d R=%d G=%d — sending city preview[%d]\n",
                          bluePressed, redPressed, greenPressed, browseIndex);
            inAttractMode = false;
            justExitedAttract = true;
            playSound(EVT_GAME_START);
            // sendCityPreview updates the LCD and notifies the console.
            // The console's onDataReceived() exits attract and enters city-select
            // when it receives any ESP-NOW message.
            sendCityPreview(browseIndex);
        }
        return;  // Don't process other button logic while in attract mode
    }

    // Guard: wait for button release after exiting attract before processing
    // city-select input — prevents the held button from immediately navigating
    // or confirming a city.
    if (justExitedAttract) {
        if (!bluePressed && !redPressed && !greenPressed) {
            justExitedAttract = false;  // All buttons released — city-select is live
            Serial.println("[ATTRACT] Buttons released — city-select now active");
        }
        return;
    }

    // -------------------------------------------------------------------------
    // CITY SELECT MODE — blue=prev, red=confirm, green=next
    // -------------------------------------------------------------------------
    if (inCitySelectMode) {
        static bool blueWasPressedCS = false;
        static bool redWasPressedCS = false;
        static bool greenWasPressedCS = false;

        // Blue (left): previous city
        if (bluePressed && !blueWasPressedCS) {
            browseIndex = (browseIndex - 1 + NUM_CITIES) % NUM_CITIES;
            sendCityPreview(browseIndex);
        }
        // Green (right): next city
        if (greenPressed && !greenWasPressedCS) {
            browseIndex = (browseIndex + 1) % NUM_CITIES;
            sendCityPreview(browseIndex);
        }
        // Red (center): confirm — start game
        if (redPressed && !redWasPressedCS) {
            ConsoleMessage msg;
            msg.command = (uint8_t)CMD_CITY_CONFIRM;
            memset(msg.data, 0, sizeof(msg.data));
            msg.data[0] = (uint8_t)browseIndex;
            comm.sendMessage(consoleMacAddress, &msg, sizeof(msg));
            // Play cheerful city-confirmed chime on the controller's own speaker
            playSound(EVT_CITY_SELECT);
            inCitySelectMode = false;
            firingEnabled = false;  // Lock until enemies finish populating
            confirmedCityName = String(cityDefaults[browseIndex].name);
            // Start intro slide sequence (PM2.5 slide 0 while console runs wave intro)
            inIntroSlides        = true;
            introSlideIndex      = 0;
            slideStartTime       = millis();
            introSlideUnlockTime = millis() + 400;  // Debounce: ignore first 400ms of button input
            lcdDisplay.displayStripIntro(0, 0, getCityValueForStrip(0), confirmedCityName.c_str());
            sendSlideAdvance(0, 0);  // Tell console: PM2.5 strip, phase 0 (full white column)
            DBG_INFO("City confirmed: %s — starting intro slides\n", cityDefaults[browseIndex].name);
        }

        blueWasPressedCS  = bluePressed;
        redWasPressedCS   = redPressed;
        greenWasPressedCS = greenPressed;
        return;  // Do not fall through to gameplay button logic
    }

    // -------------------------------------------------------------------------
    // INTRO SLIDES MODE
    // Slides 0+1 of each pollutant auto-advance (player may also skip them early).
    // Slide 2 (city levels) is a gate — button press required; auto-advance blocked.
    // After the final gate slide (index 8), start the countdown if game is ready,
    // or show a waiting screen if enemies are still loading.
    // -------------------------------------------------------------------------
    if (inIntroSlides) {
        static bool anyWasPressedIS = false;
        bool anyPressed = bluePressed || redPressed || greenPressed;
        bool isGateSlide = (introSlideIndex % 3 == 2);

        // Only allow button advancement on non-gate slides (for early skip),
        // or on gate slides when the player deliberately presses to continue.
        // Also enforce the debounce lockout after city-confirm.
        bool slidesLocked = (millis() < introSlideUnlockTime);
        if (anyPressed && !anyWasPressedIS && !slidesLocked) {
            if (isGateSlide) {
                // Gate slide: button press advances to next pollutant or ends slides
                bool finalGate = (introSlideIndex == 8); // last of 9 slides
                if (finalGate) {
                    inIntroSlides = false;
                    if (gameReadyReceived) {
                        inCountdown = true;
                        countdownValue = 3;
                        countdownStartTime = millis();
                        lcdDisplay.displayCountdown(3);
                        playToneI2S(440, 150);
                        DBG_INFO("Slides complete + game ready — countdown started\n");
                    } else {
                        lcdDisplay.displayWaiting();
                        DBG_INFO("Slides complete — waiting for enemies to finish loading\n");
                    }
                } else {
                    introSlideIndex++;
                    slideStartTime = millis();
                    int strip = introSlideIndex / 3;
                    int slideInStrip = introSlideIndex % 3;
                    lcdDisplay.displayStripIntro(strip, slideInStrip,
                                                 getCityValueForStrip(strip),
                                                 confirmedCityName.c_str());
                    sendSlideAdvance(strip, slideInStrip);
                }
            } else {
                // Non-gate slide: button skips to next slide early
                playToneI2S(500, 60);  // Quick click to acknowledge the skip
                introSlideIndex++;
                slideStartTime = millis();
                int strip = introSlideIndex / 3;
                int slideInStrip = introSlideIndex % 3;
                if (strip < NUM_STRIPS) {
                    lcdDisplay.displayStripIntro(strip, slideInStrip,
                                                 getCityValueForStrip(strip),
                                                 confirmedCityName.c_str());
                    sendSlideAdvance(strip, slideInStrip);
                } else {
                    // Skipped past all slides
                    inIntroSlides = false;
                    if (gameReadyReceived) {
                        inCountdown = true;
                        countdownValue = 3;
                        countdownStartTime = millis();
                        lcdDisplay.displayCountdown(3);
                        playToneI2S(440, 150);
                    } else {
                        lcdDisplay.displayWaiting();
                    }
                }
            }
        }
        anyWasPressedIS = anyPressed;
        return;
    }

    // If player finished slides before enemies loaded, start countdown when game becomes ready
    // (handled in loop() via gameReadyReceived flag — see below)

    // COUNTDOWN MODE — buttons locked; countdown is brief and dramatic
    if (inCountdown) {
        blueWasPressed  = bluePressed;
        redWasPressed   = redPressed;
        greenWasPressed = greenPressed;
        return;
    }

    // -------------------------------------------------------------------------
    // GAMEPLAY MODE — existing charging + shot logic
    // Firing is locked until the console confirms all enemies have settled.
    if (!firingEnabled) {
        blueWasPressed = bluePressed;
        redWasPressed  = redPressed;
        greenWasPressed = greenPressed;
        return;
    }

    // Blue button - enhanced with charging trigger
    if (bluePressed && !blueWasPressed) {
        bluePressStart = now;
        blueChargingMessageSent = false;
    }
    
    if (bluePressed) {
        unsigned long held = now - bluePressStart;
        // Send charging start message at exactly 1 second
        if (held >= 1000 && !blueChargingMessageSent && (now - lastCannonFire[0] >= CANNON_COOLDOWN)) {
            sendChargingStartMessage(0);  // Strip 0 = Blue
            blueChargingMessageSent = true;
            DBG_VERBOSE("BLUE charging started\n");
        }
        
        // Play progressive charging sounds during hold
        if (held >= 1000 && held < CANNON_CHARGE_TIME && stripAudioEnabled[0]) {
            static unsigned long lastBlueChargeSound = 0;
            if (millis() - lastBlueChargeSound > 300) {  // Progressive charge sound timing
                // Progressive tone: 80Hz -> 200Hz, 70ms -> 120ms
                float audioProgress = (float)(held - 1000) / 1000.0f;  // 0.0 -> 1.0 over 1s
                audioProgress = min(1.0f, audioProgress);
                
                int baseFreq = 80 + (int)(audioProgress * 120);   // 80Hz -> 200Hz
                int duration = 70 + (int)(audioProgress * 50);     // 70ms -> 120ms
                
                // Progressive tone with increasing frequency and duration (Blue button)
                playToneI2S(baseFreq, duration);
                lastBlueChargeSound = millis();
            }
        }
    }
    
    if (!bluePressed && blueWasPressed) {
        unsigned long held = now - bluePressStart;
        
        if (held >= CANNON_CHARGE_TIME && (now - lastCannonFire[0] >= CANNON_COOLDOWN)) {
            if (stripAudioEnabled[0]) { // Blue = strip 0 (PM2.5)
                playSound(EVT_SHOT_CANNON);
            }
            
            // Reset charging stage tracking
            static int lastBlueStage = -1;
            static unsigned long lastBlueChargeSound = 0;
            lastBlueStage = -1;
            lastBlueChargeSound = 0;
            
            lastCannonFire[0] = now;
            sendFireCommand(CMD_CANNON_BLUE);
            DBG_VERBOSE("BLUE cannon\n");
        } else if (held > 20) {
            if (stripAudioEnabled[0]) { // Blue = strip 0 (PM2.5)
                playSound(EVT_SHOT_BLUE);
            }
            sendFireCommand(CMD_FIRE_BLUE);
            DBG_VERBOSE("BLUE shot\n");
        }
    }
    blueWasPressed = bluePressed;

    // Red button - enhanced with charging trigger
    if (redPressed && !redWasPressed) {
        redPressStart = now;
        redChargingMessageSent = false;
    }
    
    if (redPressed) {
        unsigned long held = now - redPressStart;
        // Send charging start message at exactly 1 second
        if (held >= 1000 && !redChargingMessageSent && (now - lastCannonFire[1] >= CANNON_COOLDOWN)) {
            sendChargingStartMessage(1);  // Strip 1 = Red
            redChargingMessageSent = true;
            DBG_VERBOSE("RED charging started\n");
        }
        
        // Play progressive charging sounds during hold (Red button)
        if (held >= 1000 && held < CANNON_CHARGE_TIME && stripAudioEnabled[1]) {
            static unsigned long lastRedChargeSound = 0;
            if (millis() - lastRedChargeSound > 300) {  // Progressive charge sound timing
                // Progressive tone: 80Hz -> 200Hz, 70ms -> 120ms
                float audioProgress = (float)(held - 1000) / 1000.0f;  // 0.0 -> 1.0 over 1s
                audioProgress = min(1.0f, audioProgress);
                
                int baseFreq = 80 + (int)(audioProgress * 120);   // 80Hz -> 200Hz
                int duration = 70 + (int)(audioProgress * 50);     // 70ms -> 120ms
                
                // Progressive tone with increasing frequency and duration (Red button)
                playToneI2S(baseFreq, duration);
                lastRedChargeSound = millis();
            }
        }
    }
    
    if (!redPressed && redWasPressed) {
        unsigned long held = now - redPressStart;
        
        if (held >= CANNON_CHARGE_TIME && (now - lastCannonFire[1] >= CANNON_COOLDOWN)) {
            if (stripAudioEnabled[1]) { // Red = strip 1 (NO2)
                playSound(EVT_SHOT_CANNON);
            }
            
            // Reset charging stage tracking
            static int lastRedStage = -1;
            static unsigned long lastRedChargeSound = 0;
            lastRedStage = -1;
            lastRedChargeSound = 0;
            
            lastCannonFire[1] = now;
            sendFireCommand(CMD_CANNON_RED);
            DBG_VERBOSE("RED cannon\n");
        } else if (held > 20) {
            if (stripAudioEnabled[1]) { // Red = strip 1 (NO2)
                playSound(EVT_SHOT_RED);
            }
            sendFireCommand(CMD_FIRE_RED);
            DBG_VERBOSE("RED shot\n");
        }
    }
    redWasPressed = redPressed;

    // Green button - enhanced with charging trigger
    if (greenPressed && !greenWasPressed) {
        greenPressStart = now;
        greenChargingMessageSent = false;
    }
    
    if (greenPressed) {
        unsigned long held = now - greenPressStart;
        // Send charging start message at exactly 1 second
        if (held >= 1000 && !greenChargingMessageSent && (now - lastCannonFire[2] >= CANNON_COOLDOWN)) {
            sendChargingStartMessage(2);  // Strip 2 = Green
            greenChargingMessageSent = true;
            DBG_VERBOSE("GREEN charging started\n");
        }
        
        // Play progressive charging sounds during hold (Green button)
        if (held >= 1000 && held < CANNON_CHARGE_TIME && stripAudioEnabled[2]) {
            static unsigned long lastGreenChargeSound = 0;
            if (millis() - lastGreenChargeSound > 300) {  // Progressive charge sound timing
                // Progressive tone: 80Hz -> 200Hz, 70ms -> 120ms
                float audioProgress = (float)(held - 1000) / 1000.0f;  // 0.0 -> 1.0 over 1s
                audioProgress = min(1.0f, audioProgress);
                
                int baseFreq = 80 + (int)(audioProgress * 120);   // 80Hz -> 200Hz
                int duration = 70 + (int)(audioProgress * 50);     // 70ms -> 120ms
                
                // Progressive tone with increasing frequency and duration (Green button)
                playToneI2S(baseFreq, duration);
                lastGreenChargeSound = millis();
            }
        }
    }
    
    if (!greenPressed && greenWasPressed) {
        unsigned long held = now - greenPressStart;
        
        if (held >= CANNON_CHARGE_TIME && (now - lastCannonFire[2] >= CANNON_COOLDOWN)) {
            if (stripAudioEnabled[2]) { // Green = strip 2 (O3)
                playSound(EVT_SHOT_CANNON);
            }
            
            // Reset charging stage tracking
            static int lastGreenStage = -1;
            static unsigned long lastGreenChargeSound = 0;
            lastGreenStage = -1;
            lastGreenChargeSound = 0;
            
            lastCannonFire[2] = now;
            sendFireCommand(CMD_CANNON_GREEN);
            DBG_VERBOSE("GREEN cannon\n");
        } else if (held > 20) {
            if (stripAudioEnabled[2]) { // Green = strip 2 (O3)
                playSound(EVT_SHOT_GREEN);
            }
            sendFireCommand(CMD_FIRE_GREEN);
            DBG_VERBOSE("GREEN shot\n");
        }
    }
    greenWasPressed = greenPressed;
}

void sendFireCommand(CommandType fireType) {
    // Stagger rapid sends so the ESP-NOW TX queue doesn't overflow when all
    // three buttons are mashed simultaneously.  20 ms is imperceptible to
    // players but gives the radio time to hand off the previous packet.
    unsigned long now = millis();
    unsigned long elapsed = now - lastFireTime;
    if (lastFireTime > 0 && elapsed < 20) {
        delay(20 - elapsed);
    }
    lastFireTime = millis();

    ConsoleMessage command;
    command.command = (uint8_t)fireType;  // Cast enum to uint8_t to match console structure
    memset(command.data, 0, sizeof(command.data));
    
    // Send to console via unified communication protocol
    bool result = comm.sendMessage(consoleMacAddress, &command, sizeof(command));

    if (result) {
        // Brief LED flash to confirm button press — no blocking delay so the
        // main loop stays responsive during rapid button mashing.
        uint8_t r, g, b;
        if (fireType == CMD_FIRE_BLUE) { r = 0; g = 0; b = 8; }     // Blue
        else if (fireType == CMD_FIRE_RED) { r = 8; g = 0; b = 0; } // Red
        else { r = 0; g = 8; b = 0; }                               // Green

        neopixelWrite(NEOPIXEL_PIN, r, g, b);
        statusLED.setColor(0, 255, 0); // Green
    } else {
        DBG_ERROR("Failed to send fire command\n");
    }
}

void sendChargingStartMessage(uint8_t stripIndex) {
    ConsoleMessage message;
    message.command = CMD_CHARGING_START;
    
    ChargingStartMessage* chargeData = (ChargingStartMessage*)message.data;
    chargeData->stripIndex = stripIndex;
    memset(chargeData->padding, 0, sizeof(chargeData->padding));
    
    bool result = comm.sendMessage(consoleMacAddress, &message, sizeof(message));
    
    if (!result) {
        DBG_ERROR("Failed to send charging start message\n");
    }
}

// =============================================================================
// SYSTEM MODE FUNCTIONS
// =============================================================================

bool checkForAPModeRequest() {
    // Check for BLUE button held during startup (3 second window)
    const unsigned long AP_MODE_WINDOW = 3000;
    const unsigned long CHECK_INTERVAL = 100;
    
    Serial.println("[MODE] Press and hold BLUE button for AP configuration mode...");
    Serial.println("[MODE] Otherwise, starting gaming mode in 3 seconds...");
    
    for (unsigned long elapsed = 0; elapsed < AP_MODE_WINDOW; elapsed += CHECK_INTERVAL) {
        if (digitalRead(PIN_BTN_BLUE) == LOW) {
            // Button pressed - wait for 2 second hold to confirm
            unsigned long holdStart = millis();
            bool stillHeld = true;
            
            while (millis() - holdStart < 2000) {
                if (digitalRead(PIN_BTN_BLUE) == HIGH) {
                    stillHeld = false;
                    break;
                }
                delay(50);
            }
            
            if (stillHeld) {
                Serial.println("[MODE] BLUE button held - AP CONFIGURATION MODE");
                return true;
            }
        }
        delay(CHECK_INTERVAL);
    }
    
    Serial.println("[MODE] No button press - GAMING MODE");
    return false;
}

void setupGamingMode() {
    Serial.println("[MODE] === GAMING MODE - Maximum Power Efficiency ===");

    // Initialise local city cache from hardcoded defaults
    for (int i = 0; i < NUM_CITIES; i++) {
        localCityCache[i] = { cityDefaults[i].pm25, cityDefaults[i].no2, cityDefaults[i].o3 };
    }

    // Load saved AQI data from flash
    loadAQI();
    
    // Initialize LCD display
    DBG_INFO("Initializing LCD display...\n");
    if (lcdDisplay.begin()) {
        DBG_INFO("LCD display ready\n");
        // Start in attract mode — show start screen and wait for player input
        lcdDisplay.displayAttractScreen(true);
        attractBlinkState = true;
        attractBlinkTime  = millis();
        DBG_INFO("LCD showing attract screen\n");
    } else {
        DBG_INFO("LCD not detected - continuing without display\n");
    }
    
    // Initialize audio system for gaming
    DBG_INFO("Initializing audio system...\n");
    initAudio();
    enableAudio();
    setAudioVolume(90);
    playSound(EVT_START);
    delay(500); // Shorter delay to save power
    DBG_INFO("Audio system ready\n");
    
    // Initialize ESP-NOW ONLY (no WiFi AP)
    // Enable debug=true so the board prints its actual MAC address at boot.
    if (!comm.begin(BoardType::CONTROLLER, true)) {
        statusLED.setColor(255, 0, 0); // Red
        DBG_ERROR("ESP-NOW initialization failed\n");
        return;
    }
    
    // Add console as peer for fire commands
    if (!comm.addPeer(consoleMacAddress)) {
        statusLED.setColor(255, 0, 0); // Red
        DBG_ERROR("Failed to add console as peer\n");
        return;
    }
    Serial.printf("[MAC] Controller sending to console: %02x:%02x:%02x:%02x:%02x:%02x\n",
                  consoleMacAddress[0], consoleMacAddress[1], consoleMacAddress[2],
                  consoleMacAddress[3], consoleMacAddress[4], consoleMacAddress[5]);

    // Delivery-confirmation callback — fires after the physical-layer ACK.
    // DELIVERED = console actually received the packet.
    // FAILED    = wrong MAC, wrong channel, or console not responding.
    comm.onMessageSent([](const uint8_t* mac, esp_now_send_status_t status) {
        Serial.printf("[ESP-NOW ACK] %s\n",
                      status == ESP_NOW_SEND_SUCCESS ? "DELIVERED" : "FAILED - check console MAC/channel");
    });

    // Register ESP-NOW receive callback for audio commands
    comm.onMessageReceived(onAudioDataReceived);
    DBG_INFO("ESP-NOW receive callback registered\n");
    
    statusLED.setColor(0, 255, 0); // Green
    Serial.println("[MODE] Gaming mode ready - arcade buttons armed, no WiFi");
}

void setupAPConfigurationMode() {
    Serial.println("[MODE] === AP CONFIGURATION MODE - No Gaming ===");
    
    // Load settings for AP mode
    loadSettings();
    
    // Initialize ESP-NOW for AQI data transfer to console
    if (!comm.begin(BoardType::CONTROLLER, false)) {
        statusLED.setColor(255, 0, 0); // Red
        DBG_ERROR("ESP-NOW initialization failed\n");
        return;
    }
    
    if (!comm.addPeer(consoleMacAddress)) {
        statusLED.setColor(255, 0, 0); // Red
        DBG_ERROR("Failed to add console as peer\n");
        return;
    }
    
    // Initialize WiFi AP and web server
    if (!initWebServer()) {
        statusLED.setColor(255, 0, 0); // Red
        DBG_ERROR("Web server initialization failed\n");
        return;
    }
    
    statusLED.setColor(0, 0, 255); // Blue for AP mode
    Serial.printf("[MODE] AP Configuration ready - Connect to WiFi: %s\n", settings.wifiSSID);
    Serial.println("[MODE] Restart device to return to gaming mode");
}


// =============================================================================
// MAIN ARDUINO FUNCTIONS
// =============================================================================

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("Environmental Game Controller v2.1 - Dual Mode");
    Serial.println("QT Py S3 WiFi Controller Unit");
    
    // Initialize basic hardware first
    if (!statusLED.begin()) {
        Serial.println("[ERROR] StatusLED initialization failed");
        return;
    }
    statusLED.setColor(255, 255, 0); // Yellow during startup
    
    // Set up button pins for mode detection
    pinMode(PIN_BTN_BLUE, INPUT_PULLUP);
    pinMode(PIN_BTN_RED, INPUT_PULLUP);
    pinMode(PIN_BTN_GREEN, INPUT_PULLUP);
    
    DBG_VERBOSE("AudioMessage size: %d bytes\n", sizeof(AudioMessage));
    
    // Determine operating mode based on startup button press
    if (checkForAPModeRequest()) {
        currentMode = MODE_AP_CONFIG;
        loadLanguage();
        lcdDisplay.setLanguage(gameLanguage);
        setupAPConfigurationMode();
    } else {
        currentMode = MODE_GAMING;
        loadLanguage();
        lcdDisplay.setLanguage(gameLanguage);
        setupGamingMode();
    }
}

void loop() {
    if (currentMode == MODE_GAMING) {
        // Pure gaming mode - maximum responsiveness, no WiFi
        handleArcadeButtons();

        // -------------------------------------------------------------------
        // Deferred LCD updates — consume flags set by ESP-NOW callbacks.
        // ESP-NOW callbacks run in the WiFi task; Wire I²C is not thread-safe,
        // so all lcd.* calls must happen here in the main loop task.
        // -------------------------------------------------------------------
        if (pendingCitySelectRedraw) {
            pendingCitySelectRedraw = false;
            sendCityPreview(browseIndex);   // Updates console + controller LCD
        }

        // Attract mode LCD blink (800 ms period — blink the "PRESS ANY BUTTON" row)
        if (inAttractMode) {
            unsigned long now = millis();
            if (now - attractBlinkTime >= 800) {
                attractBlinkState = !attractBlinkState;
                attractBlinkTime  = now;
                lcdDisplay.displayAttractScreen(attractBlinkState);
            }
        }

        // Smog wipe defeat animation — advance each loop() tick
        if (defeatWipeActive) {
            unsigned long elapsed = millis() - defeatWipeStartTime;
            if (lcdDisplay.updateDefeatWipe(elapsed, DEFEAT_WIPE_MS)) {
                // Wipe complete — show static defeat message
                defeatWipeActive = false;
                lcdDisplay.displayDefeatScreen(confirmedCityName.c_str());
            }
        }

        // Typewriter animation for intro slides — advance one character per tick
        lcdDisplay.updateTypewriter(millis());

        // Auto-advance intro slides (only slides 0+1 of each pollutant — gate slides require button press)
        if (inIntroSlides && (introSlideIndex % 3 != 2) && millis() - slideStartTime >= SLIDE_DURATION_MS) {
            introSlideIndex++;
            slideStartTime = millis();
            int strip = introSlideIndex / 3;
            int slideInStrip = introSlideIndex % 3;
            if (strip < NUM_STRIPS) {
                lcdDisplay.displayStripIntro(strip, slideInStrip,
                                             getCityValueForStrip(strip),
                                             confirmedCityName.c_str());
                sendSlideAdvance(strip, slideInStrip);
            } else {
                inIntroSlides = false;
                if (gameReadyReceived) {
                    inCountdown = true;
                    countdownValue = 3;
                    countdownStartTime = millis();
                    lcdDisplay.displayCountdown(3);
                    playToneI2S(440, 150);
                } else {
                    lcdDisplay.displayWaiting();
                }
            }
        }

        // If player is on the waiting screen and enemies just finished loading, start countdown
        if (!inIntroSlides && !inCountdown && !firingEnabled && gameReadyReceived) {
            inCountdown = true;
            countdownValue = 3;
            countdownStartTime = millis();
            lcdDisplay.displayCountdown(3);
            playToneI2S(440, 150);
            DBG_INFO("Enemies done, slides done — countdown started\n");
        }

        // Countdown tick — update display and beep as value decrements
        if (inCountdown) {
            unsigned long elapsed = millis() - countdownStartTime;
            int newVal = 3 - (int)(elapsed / 1000);
            newVal = max(0, newVal);
            if (newVal > 0 && newVal < countdownValue) {
                countdownValue = newVal;
                lcdDisplay.displayCountdown(newVal);
                playToneI2S(newVal == 1 ? 880 : 440, 150);
            }
            if (elapsed >= 3000 && countdownValue > 0) {
                countdownValue = 0;
                inCountdown   = false;
                firingEnabled = true;
                // Signal console to start enemy advancement now that player is ready to fire
                sendToConsole(CMD_PLAYER_READY);
                // Immediately switch LCD to scoreboard using cached enemy counts
                lcdDisplay.displayGameStatus(lastAQIRemaining[0], lastAQIRemaining[1], lastAQIRemaining[2]);
                inGameStatusMode = true;
                DBG_INFO("Countdown complete \u2014 game on!\n");
            }
        }
        
        // Minimal status updates to conserve power
        static unsigned long lastStatusUpdate = 0;
        if (millis() - lastStatusUpdate > 15000) {  // Every 15 seconds
            statusLED.setColor(0, 255, 0); // Green
            Serial.println("[MODE] Gaming active - buttons responsive");
            lastStatusUpdate = millis();
        }
        
    } else if (currentMode == MODE_AP_CONFIG) {
        // AP configuration mode - web server only, no button handling
        server.handleClient();
        
        // Status updates for AP mode
        static unsigned long lastStatusUpdate = 0;
        if (millis() - lastStatusUpdate > 10000) {  // Every 10 seconds
            statusLED.setColor(0, 0, 255); // Blue
            Serial.printf("[MODE] AP active - Clients: %d\n", WiFi.softAPgetStationNum());
            lastStatusUpdate = millis();
        }
    }
    
    delay(1);  // Small delay for stability
}

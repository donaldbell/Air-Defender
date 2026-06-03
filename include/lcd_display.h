/**
 * LCD Display Module for Environmental Game Controller
 * Manages 20x4 I2C LCD display for AQI data visualization
 * 
 * Hardware: 20x4 LCD with PCF8574 I2C backpack 
 * Pins and address configured in game_config.h
 * 
 * Layout:
 * Row 0: "Environmental AQI"
 * Row 1: "PM2.5: 123 ug/m3"  
 * Row 2: "NO2: 45  O3: 67 ppb"
 * Row 3: "Barcelona, Spain"
 */

#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include "game_config.h"
#include "strings.h"

// LCD Display dimensions
#define LCD_COLS           20        // 20x4 character display
#define LCD_ROWS           4

class LCDDisplay {
public:
    /**
     * Constructor - initializes LCD instance with I2C address and dimensions
     */
    LCDDisplay();

    /**
     * Initialize LCD display and I2C communication
     * @return true if LCD initialized successfully, false if hardware not detected
     */
    bool begin();

    /**
     * Display attract/start screen on the 20x4 LCD.
     * Shows game title and a blinking "press any button" prompt.
     * @param showPrompt  true = show prompt row, false = blank row (for blink effect)
     */
    void displayAttractScreen(bool showPrompt);

    /**
     * Display AQI data on the 20x4 LCD screen
     * @param aqiData Air quality data structure to display
     */
    void displayAQI(const AirQualityData& aqiData);

    /**
     * Clear display and show initialization message
     */
    void showStartupMessage();

    /**
     * Display error message when AQI data is not available
     */
    void showNoDataMessage();

    /**
     * Display city-select browse screen
     * Row 0: city name | Row 1: PM2.5 | Row 2: NO2 + O3 | Row 3: nav hint
     */
    void displayCitySelect(const char* cityName, int pm25, int no2, int o3, int idx, int total);

    /**
     * Display one of 6 info slides after city selection.
     * slideIndex 0-5: PM2.5 def, PM2.5 source, NO2 def, NO2 source, O3 def, O3 source.
     * cityName appears on row 0 for context.
     */
    void displayInfoSlide(int slideIndex, const char* cityName);

    /**
     * Display level-won celebration screen.
     * Row 0: "Congratulations!" | Row 1: "Air Defender!" |
     * Row 2: "<city> is clean!" | Row 3: "Choose next city >"
     */
    void displayVictoryScreen(const char* cityName);

    /**
     * Begin the smog-wipe defeat animation.
     * Clears the screen; call updateDefeatWipe() each loop until it returns true,
     * then call displayDefeatScreen() for the static result.
     */
    void startDefeatWipe();

    /**
     * Advance the smog wipe by one step.
     * @param elapsed  ms since startDefeatWipe() was called
     * @param total    total ms for the wipe to complete (e.g. 1500)
     * @return true when the screen is fully covered (wipe complete)
     */
    bool updateDefeatWipe(unsigned long elapsed, unsigned long total);

    /**
     * Show static defeat message with skull custom characters.
     * Call once after updateDefeatWipe() returns true.
     * @param cityName Confirmed city name (used in row 2)
     */
    void displayDefeatScreen(const char* cityName);

    /**
     * Display live gameplay scoreboard.
     * Shows remaining enemy counts (translated to AQI units) alongside WHO goals.
     * @param pm25Rem  Remaining PM2.5 enemies
     * @param no2Rem   Remaining NO2 enemies
     * @param o3Rem    Remaining O3 enemies
     */
    void displayGameStatus(int pm25Rem, int no2Rem, int o3Rem);

    /**
     * Display one of 2 educational intro slides per pollutant strip.
     * slideNum 0 = definition + health impact + World Health Org goal.
     * slideNum 1 = city-specific AQI today vs goal, with button prompt.
     * @param stripIndex 0=PM2.5, 1=NO2, 2=Ozone
     * @param slideNum   0 or 1
     * @param cityValue  City's measured value for this strip
     * @param cityName   Confirmed city name
     */
    void displayStripIntro(int stripIndex, int slideNum, int cityValue, const char* cityName);

    /**
     * Display a 3-2-1 countdown number full-screen before game starts.
     * @param n Countdown value (3, 2, or 1)
     */
    void displayCountdown(int n);

    /**
     * Display "loading next pollutant" waiting screen shown when player
     * skips through all slides before the console finishes loading enemies.
     */
    void displayWaiting();

    /**
     * Begin a typewriter animation for a 4-row LCD screen.
     * Row 0 (header) is printed instantly; rows 1–3 animate character by character.
     * Call updateTypewriter() each loop() iteration until it returns true.
     * @param r0–r3  Pre-formatted 20-char strings for each LCD row
     */
    void startTypewriter(const char* r0, const char* r1, const char* r2, const char* r3);

    /**
     * Advance the typewriter animation by one character if the inter-character
     * delay has elapsed. Safe to call every loop() tick — returns immediately
     * when no animation is active.
     * @param now  Current millis() value
     * @return true when all rows have been fully printed (or no animation running)
     */
    bool updateTypewriter(unsigned long now);

    /** True while a typewriter animation is still in progress. */
    bool isTypewriterActive() const { return _twActive; }

    /**
     * Scan I2C bus for LCD device (debugging)
     * @return true if LCD found at expected address
     */
    /**
     * Set the active display language and load the corresponding custom characters.
     * Call once at startup after loading the language preference from flash.
     * @param lang  LANG_EN (0), LANG_ES (1), or LANG_CA (2)
     */
    void setLanguage(int lang);

    bool scanI2C();

    /**
     * Check if LCD is properly initialized
     * @return true if LCD is ready for display operations
     */
    bool isReady() const { return lcdReady; }

private:
    LiquidCrystal_I2C lcd;
    bool lcdReady;
    const GameStrings* activeStrings;

    // Write language-specific custom characters into CGRAM slots 1–5.
    void initCustomChars();

    // Transliterate UTF-8 multibyte sequences to ASCII equivalents so that
    // dynamic strings (e.g. city names from the API) render correctly on
    // the HD44780 ROM character set.  ü→u, é→e, ñ→n, etc.
    String sanitizeForLCD(const String& text);

    /**
     * Format city name to fit within available screen width
     * @param cityName Original city name string
     * @return Formatted string that fits on display row
     */
    String formatCityName(const String& cityName);

    /**
     * Create centered text for the given row width
     * @param text Text to center
     * @param width Available character width
     * @return Centered text string
     */
    String centerText(const String& text, int width);

    int _defeatWipePos;  // how many cells have been filled in the smog wipe

    // Typewriter animation state
    char          _twRows[4][21];   // buffered row text (≤20 chars + null)
    int           _twRow;           // row currently being typed (1–3; row 0 is instant)
    int           _twCol;           // next character column within _twRow
    unsigned long _twLastMs;        // millis() of last character printed
    bool          _twActive;        // true while animation is running
    static const int TW_CHAR_DELAY_MS = 30;  // ms per character
};

#endif // LCD_DISPLAY_H
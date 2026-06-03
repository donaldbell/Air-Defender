/**
 * LCD Display Module Implementation
 * Provides AQI data visualization on 20x4 I2C LCD display
 */

#include "lcd_display.h"
#include <Wire.h>

LCDDisplay::LCDDisplay() : lcd(LCD_I2C_ADDR, LCD_COLS, LCD_ROWS), lcdReady(false), activeStrings(&STRINGS_EN),
    _twRow(0), _twCol(0), _twLastMs(0), _twActive(false) {
    memset(_twRows, 0, sizeof(_twRows));
}

void LCDDisplay::setLanguage(int lang) {
    if (lang < 0 || lang > 2) lang = LANG_EN;
    activeStrings = LANGUAGE_TABLE[lang];
    if (lcdReady) initCustomChars();
}

void LCDDisplay::initCustomChars() {
    // Slot 0 is reserved for the skull glyph (defined inside displayDefeatScreen).
    // Slots 1–5 hold the current language's accent characters.
    const uint8_t* slots[5] = {
        activeStrings->cc1, activeStrings->cc2, activeStrings->cc3,
        activeStrings->cc4, activeStrings->cc5
    };
    for (int i = 0; i < 5; i++) {
        if (slots[i] != nullptr) {
            lcd.createChar(i + 1, const_cast<uint8_t*>(slots[i]));
        }
    }
    // Slots 6–7: fixed umlaut characters for API city names (language-independent).
    lcd.createChar(6, const_cast<uint8_t*>(CC_U_UMLAUT));  // ü
    lcd.createChar(7, const_cast<uint8_t*>(CC_O_UMLAUT));  // ö
    // Return the LCD cursor to home so the next print starts correctly.
    lcd.home();
}

bool LCDDisplay::begin() {
    // Initialize I2C communication  
    Wire.begin(LCD_SDA_PIN, LCD_SCL_PIN);
    
    // Scan for LCD device first
    if (!scanI2C()) {
        Serial.printf("[LCD] Warning: LCD not detected at address 0x%02X\n", LCD_I2C_ADDR);
        lcdReady = false;
        return false;
    }
    
    // Initialize LCD display
    lcd.init();
    lcd.backlight();
    
    // Test display with startup message
    showStartupMessage();
    delay(2000);

    lcdReady = true;
    initCustomChars();  // load language chars now that LCD is confirmed ready
    Serial.println("[LCD] 20x4 I2C LCD initialized successfully");
    return true;
}

void LCDDisplay::displayAttractScreen(bool showPrompt) {
    if (!lcdReady) return;

    if (showPrompt) {
        // Full redraw on the "prompt visible" half of the blink cycle.
        // Clears the display and draws all four rows, so the screen is
        // always correct after any previous state (city-select, game, etc.).
        lcd.clear();

        // Re-initialise CGRAM after clear — ensures accent chars (e.g. ó) are
        // correct on every full redraw, not just at boot.
        initCustomChars();

        // Row 0: game title (centered in 20 cols)
        lcd.setCursor(0, 0);
        lcd.print(centerText(activeStrings->attract_title, LCD_COLS));

        // Row 1: tagline
        lcd.setCursor(0, 1);
        lcd.print(centerText(activeStrings->attract_tagline, LCD_COLS));

        // Row 2: decorative separator (same in all languages)
        lcd.setCursor(0, 2);
        lcd.print(centerText("- - - - - - - - -", LCD_COLS));

        // Row 3: call-to-action
        lcd.setCursor(0, 3);
        lcd.print(centerText(activeStrings->attract_prompt, LCD_COLS));
    } else {
        // "Off" half of blink: blank only row 3 to avoid full-screen flicker
        lcd.setCursor(0, 3);
        lcd.print("                    ");  // 20 spaces
    }
}

void LCDDisplay::displayAQI(const AirQualityData& aqiData) {
    if (!lcdReady) {
        return;
    }
    
    lcd.clear();
    
    // Row 0: Header
    lcd.setCursor(0, 0);
    lcd.print(centerText("Barcelona AQI", LCD_COLS));
    
    // Row 1: PM2.5 data
    lcd.setCursor(0, 1);
    String pm25Line = "PM2.5: " + String(aqiData.pm25) + " ug/m3";
    lcd.print(pm25Line);
    
    // Row 2: NO2 data only
    lcd.setCursor(0, 2);
    String no2Line = "NO2: " + String(aqiData.no2) + " ppb";
    lcd.print(no2Line);
    
    // Row 3: O3 data only
    lcd.setCursor(0, 3);
    String o3Line = "O3: " + String(aqiData.o3) + " ppb";
    lcd.print(o3Line);
}

void LCDDisplay::showStartupMessage() {
    if (!lcdReady && !scanI2C()) {
        return; // Don't try to display if LCD not present
    }
    
    lcd.clear();
    lcd.setCursor(0, 0);
    String title = String(activeStrings->startup_title);
    title.toUpperCase();
    lcd.print(centerText(title.c_str(), LCD_COLS));
    lcd.setCursor(0, 1);
    lcd.print(centerText("v1.0", LCD_COLS));
    lcd.setCursor(0, 2);
    lcd.print("                    ");  // blank row
    lcd.setCursor(0, 3);
    lcd.print(centerText(activeStrings->startup_loading, LCD_COLS));
}

void LCDDisplay::showNoDataMessage() {
    if (!lcdReady) {
        return;
    }
    
    lcd.clear();
    initCustomChars();
    lcd.setCursor(0, 0);
    lcd.print(centerText(activeStrings->nodata_r0, LCD_COLS));
    lcd.setCursor(0, 1);
    lcd.print(centerText(activeStrings->nodata_r1, LCD_COLS));
    lcd.setCursor(0, 2);
    lcd.print(centerText(activeStrings->nodata_r2, LCD_COLS));
    lcd.setCursor(0, 3);
    lcd.print(centerText(activeStrings->nodata_r3, LCD_COLS));
}

void LCDDisplay::displayCitySelect(const char* cityName, int pm25, int no2, int o3, int idx, int total) {
    if (!lcdReady) return;
    lcd.clear();
    initCustomChars();  // reload CGRAM slots (including ü/ö) after clear

    // Row 0: city name, centred, max 20 chars
    lcd.setCursor(0, 0);
    lcd.print(centerText(sanitizeForLCD(String(cityName)), LCD_COLS));

    // Row 1: column headers matching LED strip order (PM2.5 | NO2 | OZONE)
    lcd.setCursor(0, 1);
    lcd.print("PM2.5 |  NO2 | OZONE");

    // Row 2: measurements, right-justified, no units
    lcd.setCursor(0, 2);
    char vals[21];
    snprintf(vals, sizeof(vals), "%5d | %4d | %5d", pm25, no2, o3);
    lcd.print(vals);

    // Row 3: navigation hint + city index
    lcd.setCursor(0, 3);
    String nav = "<  " + String(idx + 1) + "/" + String(total) + "  [OK]  >";
    lcd.print(centerText(nav, LCD_COLS));
}

void LCDDisplay::displayInfoSlide(int slideIndex, const char* cityName) {
    if (!lcdReady) return;

    // 6 slides: 2 per pollutant (definition + sources / health harm)
    // All 4 rows used for content — city name and slide number omitted.
    struct Slide {
        const char* header;  // Row 0
        const char* line1;   // Row 1
        const char* line2;   // Row 2
        const char* line3;   // Row 3
    };

    static const Slide slides[6] = {
        // PM2.5 slide 1 — what it is
        { "PM2.5: Fine Dust",
          "Smaller than 2.5 um",
          "Reaches deep lungs",
          "WHO annual: 5 ug/m3" },
        // PM2.5 slide 2 — sources/harm
        { "PM2.5: Sources",
          "Exhaust,fire,dust",
          "Heart & lung disease",
          "Carries carcinogens" },
        // NO2 slide 1 — what it is
        { "NO2: Nitrogen Oxide",
          "From burning fuels",
          "Irritates airways",
          "WHO annual: 10 ug/m3" },
        // NO2 slide 2 — sources/harm
        { "NO2: Sources",
          "Cars,trucks,stoves",
          "Worsens asthma,COPD",
          "Makes smog,acid rain" },
        // O3 slide 1 — what it is
        { "O3: Ground Ozone",
          "Not directly emitted",
          "NOx,VOCs + sunlight",
          "WHO peak: 100 ug/m3" },
        // O3 slide 2 — health harm
        { "O3: Health Effects",
          "Chest pain, coughing",
          "Cuts lung function",
          "Peak risk: hot days" },
    };

    int idx = constrain(slideIndex, 0, 5);
    lcd.clear();

    lcd.setCursor(0, 0);
    lcd.print(centerText(slides[idx].header, LCD_COLS));

    lcd.setCursor(0, 1);
    lcd.print(centerText(slides[idx].line1, LCD_COLS));

    lcd.setCursor(0, 2);
    lcd.print(centerText(slides[idx].line2, LCD_COLS));

    lcd.setCursor(0, 3);
    lcd.print(centerText(slides[idx].line3, LCD_COLS));
}

void LCDDisplay::displayVictoryScreen(const char* cityName) {
    if (!lcdReady) return;
    lcd.clear();
    initCustomChars();

    lcd.setCursor(0, 0);
    lcd.print(centerText(activeStrings->victory_r0, LCD_COLS));

    lcd.setCursor(0, 1);
    lcd.print(centerText(activeStrings->victory_r1, LCD_COLS));

    // Row 2: "<city><suffix>" — truncate city name if needed
    lcd.setCursor(0, 2);
    String suffix = String(activeStrings->victory_city_sfx);
    String cityLine = sanitizeForLCD(String(cityName)) + suffix;
    if ((int)cityLine.length() > LCD_COLS) {
        cityLine = String(cityName).substring(0, LCD_COLS - suffix.length()) + suffix;
    }
    lcd.print(centerText(cityLine, LCD_COLS));

    lcd.setCursor(0, 3);
    lcd.print(centerText(activeStrings->victory_r3, LCD_COLS));
}

// -----------------------------------------------------------------------------
// Defeat animation — smog wipe + skull reveal
// -----------------------------------------------------------------------------

void LCDDisplay::startDefeatWipe() {
    if (!lcdReady) return;
    _defeatWipePos = 0;
    lcd.clear();
}

bool LCDDisplay::updateDefeatWipe(unsigned long elapsed, unsigned long total) {
    if (!lcdReady) return true;
    const int totalCells = LCD_COLS * LCD_ROWS;  // 80
    int targetPos = (int)((float)elapsed / (float)total * totalCells);
    if (targetPos > totalCells) targetPos = totalCells;
    while (_defeatWipePos < targetPos) {
        int col = _defeatWipePos % LCD_COLS;
        int row = _defeatWipePos / LCD_COLS;
        lcd.setCursor(col, row);
        lcd.write(0xFF);  // HD44780 full-block character
        _defeatWipePos++;
    }
    return (_defeatWipePos >= totalCells);
}

void LCDDisplay::displayDefeatScreen(const char* cityName) {
    if (!lcdReady) return;

    // Custom char 0: skull (5×8 pixels)
    uint8_t skull[8] = {
        0b01110,  // .XXX.
        0b11111,  // XXXXX
        0b10101,  // X.X.X  (hollow eyes)
        0b11111,  // XXXXX
        0b01110,  // .XXX.
        0b01110,  // .XXX.
        0b00000,  // .....
        0b01010   // .X.X.  (teeth gap)
    };

    // Clear, reload accent chars, then write skull to slot 0.
    // Order matters: initCustomChars() must run before createChar(0) so the
    // skull is not overwritten, and both must run after clear() since
    // lcd.clear() resets CGRAM on this hardware.
    lcd.clear();
    initCustomChars();
    lcd.createChar(0, skull);
    // Slot 3 normally holds ò (Catalan); it is unused in every defeat-screen
    // string, so borrow it for the interpunct (·) in "pol·lució".
    uint8_t middleDot[8] = {
        0b00000,  // .....
        0b00000,  // .....
        0b00000,  // .....
        0b00100,  // ..█..
        0b00000,  // .....
        0b00000,  // .....
        0b00000,  // .....
        0b00000   // .....
    };
    lcd.createChar(3, middleDot);
    lcd.setCursor(0, 0);  // return cursor to DDRAM after createChar

    // Row 0: skull + 4 spaces + title + 4 spaces + skull = 20 chars exactly
    lcd.write((uint8_t)0);
    lcd.print(activeStrings->defeat_title);  // exactly 18 chars
    lcd.write((uint8_t)0);

    // Row 1: "<city><suffix>" — truncate if needed
    lcd.setCursor(0, 1);
    String sfx = String(activeStrings->defeat_city_sfx);
    String cityLine = sanitizeForLCD(String(cityName)) + sfx;
    if ((int)cityLine.length() > LCD_COLS) {
        cityLine = String(cityName).substring(0, LCD_COLS - sfx.length()) + sfx;
    }
    lcd.print(centerText(cityLine, LCD_COLS));

    // Row 2
    lcd.setCursor(0, 2);
    lcd.print(centerText(activeStrings->defeat_r2, LCD_COLS));

    // Row 3
    lcd.setCursor(0, 3);
    lcd.print(centerText(activeStrings->defeat_r3, LCD_COLS));
}

void LCDDisplay::displayGameStatus(int pm25Rem, int no2Rem, int o3Rem) {
    if (!lcdReady) return;
    lcd.clear();
    initCustomChars();

    // Row 0: column headers — "|" divider at col 10, "Now" over current values, "Goal" over targets
    lcd.setCursor(0, 0);
    lcd.print(activeStrings->score_header);

    // Format: left side = label(6) + space + value right-justified(3) = 10 chars,
    //         col 10 = "|", right side = goal right-justified in 5 + trailing spaces = 9 chars
    auto fmtRow = [](const char* label, int cur, int who) -> String {
        // Left side: 10 chars
        String s(label);                         // 6 chars
        s += " ";
        if      (cur < 10)  s += "  ";
        else if (cur < 100) s += " ";
        s += String(cur);                        // 3 chars
        // Divider at col 10
        s += "|";
        // Right side: right-justify goal in 5 chars, pad remainder to LCD_COLS
        String goalStr = String(who);
        for (int i = (int)goalStr.length(); i < 5; i++) s += " ";
        s += goalStr;
        while ((int)s.length() < LCD_COLS) s += " ";
        return s;
    };

    // Current level = WHO baseline + remaining enemies (mirrors console's 1:1 pixel mapping)
    int cur0 = WHO_PM25 + pm25Rem;
    int cur1 = WHO_NO2  + no2Rem;
    int cur2 = WHO_O3   + o3Rem;

    lcd.setCursor(0, 1);
    lcd.print(fmtRow("PM2.5:", cur0, WHO_PM25));

    lcd.setCursor(0, 2);
    lcd.print(fmtRow("  NO2:", cur1, WHO_NO2));

    lcd.setCursor(0, 3);
    lcd.print(fmtRow(activeStrings->score_o3_label, cur2, WHO_O3));
}

bool LCDDisplay::scanI2C() {    Wire.beginTransmission(LCD_I2C_ADDR);
    byte error = Wire.endTransmission();
    
    if (error == 0) {
        Serial.printf("[LCD] I2C device found at address 0x%02X\\n", LCD_I2C_ADDR);
        return true;
    } else {
        Serial.printf("[LCD] No I2C device found at address 0x%02X (error: %d)\\n", LCD_I2C_ADDR, error);
        // Scan all I2C addresses for debugging
        Serial.println("[LCD] Scanning I2C bus for available devices:");
        for (byte addr = 1; addr < 127; addr++) {
            Wire.beginTransmission(addr);
            if (Wire.endTransmission() == 0) {
                Serial.printf("[LCD]   Device found at 0x%02X\n", addr);
            }
        }
        return false;
    }
}

String LCDDisplay::sanitizeForLCD(const String& text) {
    // Walk the byte string.  Any 2-byte UTF-8 sequence in the U+00C0–U+00FF
    // block (the vast majority of European accented letters) is replaced with
    // its closest ASCII base letter.  Other multi-byte sequences are dropped.
    String out;
    out.reserve(text.length());
    const uint8_t* s = (const uint8_t*)text.c_str();
    size_t len = text.length();
    for (size_t i = 0; i < len; ) {
        uint8_t b = s[i];
        if (b < 0x80) {
            out += (char)b;
            i++;
        } else if (b == 0xC3 && i + 1 < len) {
            // U+00C0–U+00FF — covers À-ÿ
            uint8_t b2 = s[i + 1];
            char rep = '?';
            if      (b2 >= 0x80 && b2 <= 0x86) rep = 'A'; // À Á Â Ã Ä Å Æ
            else if (b2 == 0x87)               rep = 'C'; // Ç
            else if (b2 >= 0x88 && b2 <= 0x8B) rep = 'E'; // È É Ê Ë
            else if (b2 >= 0x8C && b2 <= 0x8F) rep = 'I'; // Ì Í Î Ï
            else if (b2 == 0x90)               rep = 'D'; // Ð
            else if (b2 == 0x91)               rep = 'N'; // Ñ
            else if (b2 >= 0x92 && b2 <= 0x95) rep = 'O'; // Ò Ó Ô Õ
            else if (b2 == 0x96)               rep = '\x07'; // Ö → CGRAM slot 7
            else if (b2 == 0x98)               rep = 'O'; // Ø
            else if (b2 >= 0x99 && b2 <= 0x9B) rep = 'U'; // Ù Ú Û
            else if (b2 == 0x9C)               rep = '\x06'; // Ü → CGRAM slot 6
            else if (b2 == 0x9D)               rep = 'Y'; // Ý
            else if (b2 >= 0xA0 && b2 <= 0xA6) rep = 'a'; // à á â ã ä å æ
            else if (b2 == 0xA7)               rep = 'c'; // ç
            else if (b2 >= 0xA8 && b2 <= 0xAB) rep = 'e'; // è é ê ë
            else if (b2 >= 0xAC && b2 <= 0xAF) rep = 'i'; // ì í î ï
            else if (b2 == 0xB0)               rep = 'd'; // ð
            else if (b2 == 0xB1)               rep = 'n'; // ñ
            else if (b2 >= 0xB2 && b2 <= 0xB5) rep = 'o'; // ò ó ô õ
            else if (b2 == 0xB6)               rep = '\x07'; // ö → CGRAM slot 7
            else if (b2 == 0xB8)               rep = 'o'; // ø
            else if (b2 >= 0xB9 && b2 <= 0xBB) rep = 'u'; // ù ú û
            else if (b2 == 0xBC)               rep = '\x06'; // ü → CGRAM slot 6
            else if (b2 == 0xBD || b2 == 0xBF) rep = 'y'; // ý ÿ
            out += rep;
            i += 2;
        } else if ((b & 0xE0) == 0xC0 && i + 1 < len) {
            // Other 2-byte sequence — skip both bytes
            i += 2;
        } else if ((b & 0xF0) == 0xE0 && i + 2 < len) {
            // 3-byte sequence — skip
            i += 3;
        } else if ((b & 0xF8) == 0xF0 && i + 3 < len) {
            // 4-byte sequence — skip
            i += 4;
        } else {
            i++; // stray byte
        }
    }
    return out;
}

String LCDDisplay::formatCityName(const String& cityName) {
    if (cityName.length() <= LCD_COLS) {
        return cityName;
    }
    
    // Try to truncate intelligently
    String formatted = cityName;
    
    // Remove common suffixes to save space
    formatted.replace(", Spain", "");
    formatted.replace(", France", "");
    formatted.replace(", United States", ", US");
    formatted.replace(", United Kingdom", ", UK");
    
    // If still too long, truncate with ellipsis
    if (formatted.length() > LCD_COLS) {
        formatted = formatted.substring(0, LCD_COLS - 3) + "...";
    }
    
    return formatted;
}

String LCDDisplay::centerText(const String& text, int width) {
    if (text.length() >= width) {
        return text.substring(0, width);
    }
    
    int padding = (width - text.length()) / 2;
    String centered = "";
    
    // Add left padding
    for (int i = 0; i < padding; i++) {
        centered += " ";
    }
    
    centered += text;
    
    // Add right padding to fill remaining space
    while (centered.length() < width) {
        centered += " ";
    }
    
    return centered;
}

// -----------------------------------------------------------------------------
// Typewriter animation — row-by-row character reveal
// -----------------------------------------------------------------------------

void LCDDisplay::startTypewriter(const char* r0, const char* r1, const char* r2, const char* r3) {
    if (!lcdReady) return;

    // Buffer all 4 rows (pre-formatted, centred, ≤20 chars)
    strncpy(_twRows[0], r0 ? r0 : "", 20); _twRows[0][20] = '\0';
    strncpy(_twRows[1], r1 ? r1 : "", 20); _twRows[1][20] = '\0';
    strncpy(_twRows[2], r2 ? r2 : "", 20); _twRows[2][20] = '\0';
    strncpy(_twRows[3], r3 ? r3 : "", 20); _twRows[3][20] = '\0';

    lcd.clear();
    initCustomChars();

    // Row 0 (header/title) appears instantly to give immediate context
    lcd.setCursor(0, 0);
    lcd.print(_twRows[0]);

    // Rows 1–3 will be revealed character by character via updateTypewriter()
    _twRow    = 1;
    _twCol    = 0;
    _twLastMs = 0;  // zero forces the first character on the very next call
    _twActive = true;
}

bool LCDDisplay::updateTypewriter(unsigned long now) {
    if (!_twActive) return true;
    if (!lcdReady)  { _twActive = false; return true; }

    if (now - _twLastMs < (unsigned long)TW_CHAR_DELAY_MS) return false;
    _twLastMs = now;

    // Advance through rows until we find the next character to print
    while (_twRow < 4) {
        int len = (int)strlen(_twRows[_twRow]);
        if (_twCol < len) {
            lcd.setCursor(_twCol, _twRow);
            lcd.print(_twRows[_twRow][_twCol]);
            _twCol++;
            return false;  // still animating
        }
        // Current row exhausted — move to the next
        _twRow++;
        _twCol = 0;
    }

    _twActive = false;
    return true;
}

void LCDDisplay::displayStripIntro(int stripIndex, int slideNum, int cityValue, const char* cityName) {
    if (!lcdReady) return;
    // lcd.clear() and initCustomChars() are handled inside startTypewriter()

    int si = constrain(stripIndex, 0, 2);
    const char* units[3] = { "ug/m3", "ppb", "ppb" };
    const int   goals[3] = { WHO_PM25, WHO_NO2, WHO_O3 };

    struct Row4 { const char* r0; const char* r1; const char* r2; const char* r3; };

    // Slide 0: what it is + health impact (all 4 rows educational)
    static const Row4 slide0[3] = {
        { activeStrings->pm25_s0_r0, activeStrings->pm25_s0_r1, activeStrings->pm25_s0_r2, activeStrings->pm25_s0_r3 },
        { activeStrings->no2_s0_r0,  activeStrings->no2_s0_r1,  activeStrings->no2_s0_r2,  activeStrings->no2_s0_r3  },
        { activeStrings->o3_s0_r0,   activeStrings->o3_s0_r1,   activeStrings->o3_s0_r2,   activeStrings->o3_s0_r3   },
    };

    // Slide 1: World Health Org goal
    const Row4 slide1[3] = {
        { activeStrings->who_r0, activeStrings->who_r1, activeStrings->who_pm25_r2, "5 ug/m3" },
        { activeStrings->who_r0, activeStrings->who_r1, activeStrings->who_no2_r2,  "5 ppb"   },
        { activeStrings->who_r0, activeStrings->who_r1, activeStrings->who_o3_r2,   "30 ppb"  },
    };

    // Slide 2 gate prompt — two rows for the button call-to-action
    const char* nextLine2[3] = { activeStrings->gate_next_r2, activeStrings->gate_next_r2, activeStrings->gate_last_r2 };
    const char* nextLine3[3] = { activeStrings->gate_next_r3, activeStrings->gate_next_r3, activeStrings->gate_last_r3 };

    // Build the four centred row strings, then hand them to the typewriter.
    String r0, r1, r2, r3;

    if (slideNum == 0) {
        r0 = centerText(slide0[si].r0, LCD_COLS);
        r1 = centerText(slide0[si].r1, LCD_COLS);
        r2 = centerText(slide0[si].r2, LCD_COLS);
        r3 = centerText(slide0[si].r3, LCD_COLS);

    } else if (slideNum == 1) {
        r0 = centerText(slide1[si].r0, LCD_COLS);
        r1 = centerText(slide1[si].r1, LCD_COLS);
        r2 = centerText(slide1[si].r2, LCD_COLS);
        r3 = centerText(slide1[si].r3, LCD_COLS);

    } else {
        // Slide 2: city's level vs goal + button prompt (gate slide — no auto-advance)
        // Row 0: "[City] [Pollutant]"
        static const char* pollutantShortNames[3] = { "PM2.5", "NO2", nullptr };
        const char* o3name = activeStrings->gate_o3_name;
        String titleLine = sanitizeForLCD(String(cityName)) + " " + String(si == 2 ? o3name : pollutantShortNames[si]);
        if ((int)titleLine.length() > LCD_COLS) titleLine = titleLine.substring(0, LCD_COLS);
        r0 = centerText(titleLine, LCD_COLS);
        r1 = centerText(String(activeStrings->gate_today_prefix) + String(cityValue) + " " + String(units[si]), LCD_COLS);
        r2 = centerText(String(nextLine2[si]), LCD_COLS);
        r3 = centerText(String(nextLine3[si]), LCD_COLS);
    }

    startTypewriter(r0.c_str(), r1.c_str(), r2.c_str(), r3.c_str());
}

void LCDDisplay::displayCountdown(int n) {
    if (!lcdReady) return;
    lcd.clear();

    lcd.setCursor(0, 0);
    lcd.print(centerText("", LCD_COLS));

    lcd.setCursor(0, 1);
    lcd.print(centerText(activeStrings->countdown_label, LCD_COLS));

    lcd.setCursor(0, 2);
    lcd.print(centerText(String(n), LCD_COLS));

    lcd.setCursor(0, 3);
    lcd.print(centerText("", LCD_COLS));
}

void LCDDisplay::displayWaiting() {
    if (!lcdReady) return;
    lcd.clear();
    initCustomChars();

    lcd.setCursor(0, 0);
    lcd.print(centerText("", LCD_COLS));

    lcd.setCursor(0, 1);
    lcd.print(centerText(activeStrings->waiting_r1, LCD_COLS));

    lcd.setCursor(0, 2);
    lcd.print(centerText(activeStrings->waiting_r2, LCD_COLS));

    lcd.setCursor(0, 3);
    lcd.print(centerText("", LCD_COLS));
}

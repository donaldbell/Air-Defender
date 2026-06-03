#include "status_led.h"
#include "game_config.h"

#ifdef M5STACK_ATOMS3
    #include <FastLED.h>
    // M5Stack Atom S3 has a 5x5 NeoPixel matrix, we'll use the center LED
    #define STATUS_LED_PIN      2
    #define STATUS_LED_COUNT    25
    static CRGB statusLEDs[STATUS_LED_COUNT];
#endif

#ifdef QTPY_S3
    // QT Py S3 built-in NeoPixel pins
    #define NEOPIXEL_PIN        39
    #define NEOPIXEL_POWER      38
#endif

bool StatusLED::begin(bool enableAutoDetect, uint8_t brightnessLevel) {
    brightness = brightnessLevel;
    
    if (enableAutoDetect) {
        isQTPy = detectBoardType();
    }
    
    return beginWithBoardType(isQTPy ? QTPY_S3_BOARD : M5STACK_BOARD, brightnessLevel);
}

bool StatusLED::beginWithBoardType(BoardType boardType, uint8_t brightnessLevel) {
    isQTPy = (boardType == QTPY_S3_BOARD);
    brightness = brightnessLevel;
    
    if (isQTPy) {
        // Initialize QT Py S3 NeoPixel
        #ifdef QTPY_S3
        pinMode(NEOPIXEL_POWER, OUTPUT);
        digitalWrite(NEOPIXEL_POWER, HIGH);  // Enable NeoPixel power
        delay(10);  // Allow power to stabilize
        #endif
    } else {
        // Initialize M5Stack Atom S3 LED matrix
        #ifdef M5STACK_ATOMS3
        FastLED.addLeds<NEOPIXEL, STATUS_LED_PIN>(statusLEDs, STATUS_LED_COUNT);
        FastLED.setBrightness(brightness);
        FastLED.clear();
        FastLED.show();
        #endif
    }
    
    initialized = true;
    off();  // Start with LED off
    return true;
}

void StatusLED::setColor(StatusColor color) {
    currentColor = color;
    currentPattern = StatusPattern::SOLID;
    
    uint8_t r, g, b;
    if (color == StatusColor::CUSTOM) {
        r = customR;
        g = customG;
        b = customB;
    } else {
        colorToRGB(color, r, g, b);
    }
    
    setHardwareLED(r, g, b);
}

void StatusLED::setColor(uint8_t red, uint8_t green, uint8_t blue) {
    currentColor = StatusColor::CUSTOM;
    currentPattern = StatusPattern::SOLID;
    customR = red;
    customG = green;
    customB = blue;
    
    setHardwareLED(red, green, blue);
}

void StatusLED::setPattern(StatusColor color, StatusPattern pattern) {
    currentColor = color;
    currentPattern = pattern;
    patternStartTime = millis();
    animationState = false;
}

void StatusLED::off() {
    currentColor = StatusColor::OFF;
    currentPattern = StatusPattern::SOLID;
    setHardwareLED(0, 0, 0);
}

void StatusLED::update() {
    if (!initialized) return;
    
    unsigned long currentTime = millis();
    
    // Handle flash effect
    if (flashDuration > 0) {
        if (currentTime - patternStartTime >= flashDuration) {
            flashDuration = 0;
            off();
        }
        return;
    }
    
    // Handle pattern animations
    if (currentPattern != StatusPattern::SOLID) {
        updatePattern();
    }
}

void StatusLED::setBrightness(uint8_t brightnessLevel) {
    brightness = brightnessLevel;
    
    #ifdef M5STACK_ATOMS3
    if (!isQTPy) {
        FastLED.setBrightness(brightness);
        FastLED.show();
    }
    #endif
    
    // For QT Py, brightness is applied in setHardwareLED
}

void StatusLED::flash(StatusColor color, uint16_t durationMs) {
    flashColor = color;
    flashDuration = durationMs;
    patternStartTime = millis();
    
    uint8_t r, g, b;
    colorToRGB(color, r, g, b);
    setHardwareLED(r, g, b);
}

void StatusLED::setHardwareLED(uint8_t r, uint8_t g, uint8_t b) {
    if (!initialized) return;
    
    if (isQTPy) {
        // QT Py S3 built-in NeoPixel
        #ifdef QTPY_S3
        // Apply brightness scaling
        r = (r * brightness) / 255;
        g = (g * brightness) / 255;
        b = (b * brightness) / 255;
        neopixelWrite(NEOPIXEL_PIN, r, g, b);
        #endif
    } else {
        // M5Stack Atom S3 LED matrix - use center LED
        #ifdef M5STACK_ATOMS3
        // Clear all LEDs first
        FastLED.clear();
        
        // Set center LED (position 12 in 5x5 matrix)
        statusLEDs[12] = CRGB(r, g, b);
        
        FastLED.show();
        #endif
    }
}

void StatusLED::colorToRGB(StatusColor color, uint8_t& r, uint8_t& g, uint8_t& b) {
    switch (color) {
        case StatusColor::OFF:    r = 0;   g = 0;   b = 0;   break;
        case StatusColor::RED:    r = 255; g = 0;   b = 0;   break;
        case StatusColor::GREEN:  r = 0;   g = 255; b = 0;   break;
        case StatusColor::BLUE:   r = 0;   g = 0;   b = 255; break;
        case StatusColor::YELLOW: r = 255; g = 255; b = 0;   break;
        case StatusColor::PURPLE: r = 255; g = 0;   b = 255; break;
        case StatusColor::CYAN:   r = 0;   g = 255; b = 255; break;
        case StatusColor::WHITE:  r = 255; g = 255; b = 255; break;
        case StatusColor::CUSTOM: r = customR; g = customG; b = customB; break;
    }
}

bool StatusLED::detectBoardType() {
    // Auto-detect based on build flags
    #ifdef QTPY_S3
    return true;
    #elif defined(M5STACK_ATOMS3)
    return false;
    #else
    // Default to M5Stack if uncertain
    return false;
    #endif
}

void StatusLED::updatePattern() {
    unsigned long currentTime = millis();
    unsigned long elapsed = currentTime - patternStartTime;
    
    uint8_t r, g, b;
    colorToRGB(currentColor, r, g, b);
    
    switch (currentPattern) {
        case StatusPattern::BLINK_SLOW: {
            // 1Hz = 1000ms period
            bool on = (elapsed % 1000) < 500;
            if (on != animationState) {
                animationState = on;
                setHardwareLED(on ? r : 0, on ? g : 0, on ? b : 0);
            }
            break;
        }
        
        case StatusPattern::BLINK_FAST: {
            // 4Hz = 250ms period
            bool on = (elapsed % 250) < 125;
            if (on != animationState) {
                animationState = on;
                setHardwareLED(on ? r : 0, on ? g : 0, on ? b : 0);
            }
            break;
        }
        
        case StatusPattern::PULSE: {
            // Smooth 2-second pulse cycle
            uint8_t intensity = pulseIntensity(elapsed, 2000);
            setHardwareLED((r * intensity) / 255, (g * intensity) / 255, (b * intensity) / 255);
            break;
        }
        
        case StatusPattern::FLASH:
            // Single flash - handled in flash() method
            break;
            
        case StatusPattern::SOLID:
        default:
            // No animation needed
            break;
    }
}

uint8_t StatusLED::pulseIntensity(unsigned long elapsed, uint16_t period) {
    // Create smooth sine wave pulse
    float phase = (float)(elapsed % period) / period * 2.0 * PI;
    float intensity = (sin(phase) + 1.0) / 2.0;  // Convert to 0-1 range
    return (uint8_t)(intensity * 255);
}
#pragma once

#include <Arduino.h>

/**
 * Unified Status LED Management
 * 
 * Provides consistent status indication across different ESP32 board types.
 * Supports both FastLED-based and built-in NeoPixel LEDs with unified color interface.
 * 
 * Board Support:
 * - M5Stack Atom S3: Uses onboard FastLED NeoPixel matrix
 * - QT Py S3: Uses built-in NeoPixel with power control
 * - Generic ESP32: Configurable GPIO pins
 */

enum class StatusColor {
    OFF,        // LED turned off
    RED,        // Error/failure states  
    GREEN,      // Success/ready states
    BLUE,       // Information/processing states
    YELLOW,     // Warning/attention states
    PURPLE,     // Special states
    CYAN,       // Network/communication states
    WHITE,      // System/debug states
    CUSTOM      // User-defined RGB values
};

enum class StatusPattern {
    SOLID,      // Steady color
    BLINK_SLOW, // 1Hz blink rate
    BLINK_FAST, // 4Hz blink rate
    PULSE,      // Smooth fade in/out
    FLASH       // Single brief flash
};

class StatusLED {
public:
    /**
     * Initialize status LED for the current board type
     * @param enableAutoDetect Automatically detect board type (default)
     * @param brightness LED brightness 0-255 (default 32 for comfortable viewing)
     */
    bool begin(bool enableAutoDetect = true, uint8_t brightness = 32);
    
    /**
     * Manual board type selection (advanced usage)
     * @param boardType QTPY_S3_BOARD or M5STACK_BOARD
     * @param brightness LED brightness 0-255
     */
    enum BoardType { QTPY_S3_BOARD, M5STACK_BOARD };
    bool beginWithBoardType(BoardType boardType, uint8_t brightness = 32);
    
    /**
     * Set LED to solid color
     */
    void setColor(StatusColor color);
    
    /**
     * Set LED to custom RGB color
     */
    void setColor(uint8_t red, uint8_t green, uint8_t blue);
    
    /**
     * Set LED with pattern (blink, pulse, etc.)
     */
    void setPattern(StatusColor color, StatusPattern pattern);
    
    /**
     * Turn off LED
     */
    void off();
    
    /**
     * Update LED animations (call in main loop)
     */
    void update();
    
    /**
     * Set brightness (0-255)
     */
    void setBrightness(uint8_t brightness);
    
    /**
     * Flash LED briefly for feedback
     */
    void flash(StatusColor color, uint16_t durationMs = 100);
    
    // Convenience methods for common patterns
    void showReady() { setColor(StatusColor::GREEN); }
    void showError() { setColor(StatusColor::RED); }
    void showProcessing() { setPattern(StatusColor::BLUE, StatusPattern::PULSE); }
    void showWarning() { setPattern(StatusColor::YELLOW, StatusPattern::BLINK_SLOW); }
    void showConnecting() { setPattern(StatusColor::CYAN, StatusPattern::BLINK_FAST); }
    
private:
    bool initialized = false;
    bool isQTPy = false;
    uint8_t brightness = 32;
    
    // Current state
    StatusColor currentColor = StatusColor::OFF;
    StatusPattern currentPattern = StatusPattern::SOLID;
    uint8_t customR = 0, customG = 0, customB = 0;
    
    // Animation state
    unsigned long lastUpdate = 0;
    unsigned long patternStartTime = 0;
    bool animationState = false;
    uint16_t flashDuration = 0;
    StatusColor flashColor = StatusColor::OFF;
    
    // Hardware-specific methods
    void setHardwareLED(uint8_t r, uint8_t g, uint8_t b);
    void colorToRGB(StatusColor color, uint8_t& r, uint8_t& g, uint8_t& b);
    bool detectBoardType();
    
    // Animation helpers
    void updatePattern();
    uint8_t pulseIntensity(unsigned long elapsed, uint16_t period = 2000);
};
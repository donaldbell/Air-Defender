/**
 * Console Unit ESP-NOW Test (Stable Version)
 * ESP-NOW communication with M5Stack display unit
 * Built on verified stable foundation
 */

#include <WiFi.h>
#include <esp_now.h>

// Function declarations
void showSystemInfo();
void testWiFiScan();
void blinkTest();
void initESPNOW();
void sendTestPattern(uint8_t r, uint8_t g, uint8_t b);
void sendClearCommand();
void sendRainbowTest();
void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);

// Display unit MAC address (M5Stack Atom S3 Lite)
uint8_t displayUnitMAC[] = {0x34, 0xb7, 0xda, 0x57, 0x36, 0xfc};

// Message structure (must match display unit)
typedef struct {
    uint8_t command;     // 1=test pattern, 2=clear, 3=game data
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t position;
    uint8_t length;
} DisplayMessage;

DisplayMessage outgoingMessage;
esp_now_peer_info_t peerInfo;
bool espnowReady = false;

void setup() {
    Serial.begin(115200);
    delay(2000); // Give extra time for serial to initialize
    
    Serial.println("\n================================================");
    Serial.println("=== Console Unit - Game Responsiveness Test ===");
    Serial.println("================================================");
    Serial.println();
    
    Serial.println("ESP32 is running normally!");
    Serial.print("Chip model: ");
    Serial.println(ESP.getChipModel());
    Serial.print("Chip revision: ");
    Serial.println(ESP.getChipRevision());
    Serial.print("Flash size: ");
    Serial.println(ESP.getFlashChipSize());
    Serial.print("Free heap: ");
    Serial.println(ESP.getFreeHeap());
    
    // Initialize WiFi first
    Serial.println("\nInitializing WiFi...");
    WiFi.mode(WIFI_STA);
    delay(500);
    
    Serial.print("Console MAC Address: ");
    Serial.println(WiFi.macAddress());
    
    // Initialize ESP-NOW (separate function for better error handling)
    Serial.println("\nInitializing ESP-NOW...");
    initESPNOW();
    
    Serial.println();
    Serial.println("Commands available:");
    Serial.println("1 - System info");
    Serial.println("2 - WiFi scan");
    Serial.println("3 - Blink test");
    if (espnowReady) {
        Serial.println("=== GAME SIMULATION (Rapid Response) ===");
        Serial.println("4 - RED (Player 1 action)");
        Serial.println("5 - GREEN (Player 2 action)");
        Serial.println("6 - BLUE (Player 3 action)");
        Serial.println("7 - OFF (Clear/Reset)");
        Serial.println("8 - YELLOW (Special action)");
        Serial.println("9 - PURPLE (Bonus action)");
        Serial.println("0 - WHITE (Game over)");
        Serial.println("Type rapidly to test responsiveness!");
    } else {
        Serial.println("ESP-NOW not ready - LED commands disabled");
    }
    Serial.println();
}

void loop() {
    if (Serial.available()) {
        char command = Serial.read();
        
        switch(command) {
            case '1':
                showSystemInfo();
                break;
                
            case '2':
                testWiFiScan();
                break;
                
            case '3':
                blinkTest();
                break;
                
            case '4':
                if (espnowReady) {
                    Serial.print("RED → ");
                    sendTestPattern(255, 0, 0);
                } else {
                    Serial.println("ESP-NOW not ready");
                }
                break;
                
            case '5':
                if (espnowReady) {
                    Serial.print("GREEN → ");
                    sendTestPattern(0, 255, 0);
                } else {
                    Serial.println("ESP-NOW not ready");
                }
                break;
                
            case '6':
                if (espnowReady) {
                    Serial.print("BLUE → ");
                    sendTestPattern(0, 0, 255);
                } else {
                    Serial.println("ESP-NOW not ready");
                }
                break;
                
            case '7':
                if (espnowReady) {
                    Serial.print("OFF → ");
                    sendClearCommand();
                } else {
                    Serial.println("ESP-NOW not ready");
                }
                break;
                
            case '8':
                if (espnowReady) {
                    Serial.print("YELLOW → ");
                    sendTestPattern(255, 255, 0);
                } else {
                    Serial.println("ESP-NOW not ready");
                }
                break;
                
            case '9':
                if (espnowReady) {
                    Serial.print("PURPLE → ");
                    sendTestPattern(128, 0, 128);
                } else {
                    Serial.println("ESP-NOW not ready");
                }
                break;
                
            case '0':
                if (espnowReady) {
                    Serial.print("WHITE → ");
                    sendTestPattern(255, 255, 255);
                } else {
                    Serial.println("ESP-NOW not ready");
                }
                break;
                
            default:
                if (command != '\n' && command != '\r') {
                    Serial.println("Use 0-9 for commands");
                }
                break;
        }
    }
    
    // Minimal heartbeat every 30 seconds (less frequent)
    static unsigned long lastBeat = 0;
    if (millis() - lastBeat > 30000) {
        lastBeat = millis();
        Serial.print(".");
    }
}

void showSystemInfo() {
    Serial.println("\n--- System Information ---");
    Serial.printf("Uptime: %lu ms\n", millis());
    Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
    Serial.printf("CPU frequency: %d MHz\n", ESP.getCpuFreqMHz());
    Serial.printf("SDK version: %s\n", ESP.getSdkVersion());
    Serial.printf("ESP-NOW status: %s\n", espnowReady ? "Ready" : "Not initialized");
    Serial.println();
}

void testWiFiScan() {
    Serial.println("\n--- WiFi Scan Test ---");
    Serial.println("Setting WiFi mode...");
    
    WiFi.mode(WIFI_STA);
    delay(500);
    
    Serial.print("MAC Address: ");
    Serial.println(WiFi.macAddress());
    
    Serial.println("Scanning for networks...");
    int networks = WiFi.scanNetworks();
    
    if (networks == 0) {
        Serial.println("No networks found");
    } else {
        Serial.printf("Found %d networks:\n", networks);
        for (int i = 0; i < networks && i < 10; i++) {
            Serial.printf("%d: %s (%d dBm)\n", i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
        }
    }
    Serial.println();
}

void blinkTest() {
    Serial.println("\n--- Blink Test ---");
    for (int i = 0; i < 5; i++) {
        Serial.printf("Blink %d/5\n", i + 1);
        delay(500);
    }
    Serial.println("Blink test complete!");
    Serial.println();
}

void initESPNOW() {
    // Safe ESP-NOW initialization with detailed error checking
    espnowReady = false;
    
    Serial.println("Step 1: Initializing ESP-NOW core...");
    esp_err_t result = esp_now_init();
    if (result != ESP_OK) {
        Serial.printf("ESP-NOW init failed: %s\n", esp_err_to_name(result));
        Serial.println("Continuing without ESP-NOW...");
        return;
    }
    Serial.println("ESP-NOW core initialized successfully");
    
    Serial.println("Step 2: Registering send callback...");
    result = esp_now_register_send_cb(onDataSent);
    if (result != ESP_OK) {
        Serial.printf("Send callback registration failed: %s\n", esp_err_to_name(result));
        Serial.println("Continuing without callback...");
    } else {
        Serial.println("Send callback registered successfully");
    }
    
    Serial.println("Step 3: Adding M5Stack peer...");
    Serial.printf("Target MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  displayUnitMAC[0], displayUnitMAC[1], displayUnitMAC[2],
                  displayUnitMAC[3], displayUnitMAC[4], displayUnitMAC[5]);
    
    memcpy(peerInfo.peer_addr, displayUnitMAC, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    
    result = esp_now_add_peer(&peerInfo);
    if (result != ESP_OK) {
        Serial.printf("Failed to add peer: %s\n", esp_err_to_name(result));
        Serial.println("ESP-NOW will not work properly");
        return;
    }
    
    Serial.println("M5Stack peer added successfully");
    Serial.println("ESP-NOW ready for communication!");
    espnowReady = true;
}

void sendTestPattern(uint8_t r, uint8_t g, uint8_t b) {
    if (!espnowReady) {
        Serial.println("ESP-NOW not ready!");
        return;
    }
    
    outgoingMessage.command = 1; // Test pattern
    outgoingMessage.red = r;
    outgoingMessage.green = g;
    outgoingMessage.blue = b;
    outgoingMessage.position = 0;
    outgoingMessage.length = 0;
    
    esp_err_t result = esp_now_send(displayUnitMAC, (uint8_t *)&outgoingMessage, sizeof(outgoingMessage));
    
    if (result == ESP_OK) {
        Serial.printf("RGB(%d,%d,%d) sent\n", r, g, b);
    } else {
        Serial.printf("FAILED: %s\n", esp_err_to_name(result));
    }
}

void sendClearCommand() {
    if (!espnowReady) {
        Serial.println("ESP-NOW not ready!");
        return;
    }
    
    outgoingMessage.command = 2; // Clear
    outgoingMessage.red = 0;
    outgoingMessage.green = 0;
    outgoingMessage.blue = 0;
    outgoingMessage.position = 0;
    outgoingMessage.length = 0;
    
    esp_err_t result = esp_now_send(displayUnitMAC, (uint8_t *)&outgoingMessage, sizeof(outgoingMessage));
    
    if (result == ESP_OK) {
        Serial.println("Cleared");
    } else {
        Serial.printf("CLEAR FAILED: %s\n", esp_err_to_name(result));
    }
}

void sendRainbowTest() {
    if (!espnowReady) {
        Serial.println("ESP-NOW not ready!");
        return;
    }
    
    // Send multiple colored segments
    uint8_t colors[][3] = {
        {255, 0, 0},    // Red
        {255, 165, 0},  // Orange  
        {255, 255, 0},  // Yellow
        {0, 255, 0},    // Green
        {0, 0, 255},    // Blue
        {75, 0, 130},   // Indigo
        {238, 130, 238} // Violet
    };
    
    Serial.println("Sending rainbow segments...");
    for(int i = 0; i < 7; i++) {
        outgoingMessage.command = 3; // Set LED segment
        outgoingMessage.red = colors[i][0];
        outgoingMessage.green = colors[i][1];
        outgoingMessage.blue = colors[i][2];
        outgoingMessage.position = i * 20; // Each segment 20 LEDs
        outgoingMessage.length = 20;
        
        esp_err_t result = esp_now_send(displayUnitMAC, (uint8_t *)&outgoingMessage, sizeof(outgoingMessage));
        
        if (result == ESP_OK) {
            Serial.printf("Rainbow segment %d sent\n", i + 1);
        } else {
            Serial.printf("Segment %d failed: %s\n", i + 1, esp_err_to_name(result));
        }
        
        delay(100); // Small delay between segments
    }
    Serial.println("Rainbow sequence complete!");
}

void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (status == ESP_NOW_SEND_SUCCESS) {
        Serial.print("✓ ");
    } else {
        Serial.print("✗ ");
    }
}
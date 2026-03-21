// Simple ESP-NOW Communication Test
// Console Board (ESP32 Wroom32) - Sender
// Tests basic ESP-NOW connectivity without game complexity

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

// M5Stack display MAC (from previous setup)
uint8_t displayMAC[] = {0x34, 0xb7, 0xda, 0x57, 0x36, 0xfc};

typedef struct {
    uint8_t command;
    uint8_t testValue;
    unsigned long timestamp;
} TestMessage;

TestMessage testMsg;
esp_now_peer_info_t peerInfo;
bool espnowReady = false;
unsigned long lastSend = 0;
int sendCount = 0;
int successCount = 0;

void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (status == ESP_NOW_SEND_SUCCESS) {
        successCount++;
        Serial.printf("✓ Send %d successful (Success rate: %d/%d = %.1f%%)\n", 
                     sendCount, successCount, sendCount, 
                     (float)successCount/sendCount*100);
    } else {
        Serial.printf("✗ Send %d failed - Status: %d\n", sendCount, status);
    }
}

void setupESPNOW() {
    Serial.println("\n🔗 ESP-NOW Simple Test Setup");
    
    // Set WiFi to station mode first
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    
    Serial.printf("📡 WiFi MAC: %s\n", WiFi.macAddress().c_str());
    Serial.printf("📡 WiFi Channel: %d\n", WiFi.channel());
    
    // Initialize ESP-NOW
    esp_err_t result = esp_now_init();
    if (result != ESP_OK) {
        Serial.printf("❌ ESP-NOW init failed: %s\n", esp_err_to_name(result));
        return;
    }
    Serial.println("✓ ESP-NOW initialized");
    
    // Register send callback
    esp_now_register_send_cb(onDataSent);
    Serial.println("✓ Send callback registered");
    
    // Add peer
    memcpy(peerInfo.peer_addr, displayMAC, 6);
    peerInfo.channel = 0;  // Auto
    peerInfo.encrypt = false;
    
    result = esp_now_add_peer(&peerInfo);
    if (result != ESP_OK) {
        Serial.printf("❌ Add peer failed: %s\n", esp_err_to_name(result));
        return;
    }
    
    Serial.printf("✓ Display peer added: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  displayMAC[0], displayMAC[1], displayMAC[2],
                  displayMAC[3], displayMAC[4], displayMAC[5]);
    
    espnowReady = true;
    Serial.println("🎯 ESP-NOW ready for testing!\n");
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("Starting ESP-NOW Communication Test...");
    
    setupESPNOW();
}

void loop() {
    if (!espnowReady) {
        delay(1000);
        return;
    }
    
    // Send test message every 2 seconds
    if (millis() - lastSend >= 2000) {
        sendCount++;
        
        testMsg.command = 1;  // Test command
        testMsg.testValue = sendCount % 256;
        testMsg.timestamp = millis();
        
        Serial.printf("📤 Sending test message %d (value=%d)...\n", 
                     sendCount, testMsg.testValue);
        
        esp_err_t result = esp_now_send(displayMAC, (uint8_t*)&testMsg, sizeof(testMsg));
        
        if (result != ESP_OK) {
            Serial.printf("❌ esp_now_send() failed: %s\n", esp_err_to_name(result));
        }
        
        lastSend = millis();
    }
    
    delay(50);
}
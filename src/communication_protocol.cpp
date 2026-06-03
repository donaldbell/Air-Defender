#include "communication_protocol.h"
#include <stdarg.h>

// Global instance
CommunicationProtocol comm;

bool CommunicationProtocol::begin(BoardType type, bool enableDebug) {
    boardType = type;
    debugEnabled = enableDebug;
    currentStatus = CommStatus::NOT_INITIALIZED;
    
    logDebug("[COMM] Initializing communication protocol for %s", 
             (type == BoardType::CONSOLE) ? "CONSOLE" : "CONTROLLER");
    
    if (!initializeWiFi()) {
        logDebug("[ERROR] WiFi initialization failed");
        return false;
    }
    
    if (!initializeESPNOW()) {
        logDebug("[ERROR] ESP-NOW initialization failed");
        return false;
    }
    
    currentStatus = CommStatus::READY;
    
    // Print local MAC for configuration
    printLocalMAC((boardType == BoardType::CONSOLE) ? "CONSOLE MAC" : "CONTROLLER MAC");
    
    logDebug("[COMM] Communication protocol ready");
    return true;
}

bool CommunicationProtocol::initializeWiFi() {
    // Disconnect any existing connections
    WiFi.disconnect();
    
    // Set appropriate WiFi mode based on board type
    if (boardType == BoardType::CONSOLE) {
        WiFi.mode(WIFI_STA);
        logDebug("[WIFI] Console mode: WIFI_STA");
    } else { // CONTROLLER
        WiFi.mode(WIFI_AP_STA);
        logDebug("[WIFI] Controller mode: WIFI_AP_STA (supports web server)");
    }
    
    // Allow time for mode switch
    delay(100);
    
    logDebug("[WIFI] WiFi initialized, MAC: %s", WiFi.macAddress().c_str());
    return true;
}

bool CommunicationProtocol::initializeESPNOW() {
    if (esp_now_init() != ESP_OK) {
        logDebug("[ERROR] ESP-NOW init failed");
        return false;
    }
    
    logDebug("[ESP-NOW] Initialized successfully");
    return true;
}

bool CommunicationProtocol::addPeer(const uint8_t* peerMac, uint8_t channel) {
    if (currentStatus < CommStatus::READY) {
        logDebug("[ERROR] Communication not ready for peer addition");
        return false;
    }
    
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, peerMac, 6);
    peerInfo.channel = channel;
    peerInfo.encrypt = false;
    
    esp_err_t result = esp_now_add_peer(&peerInfo);
    if (result != ESP_OK) {
        logDebug("[ERROR] Failed to add peer %02x:%02x:%02x:%02x:%02x:%02x: %s", 
                peerMac[0], peerMac[1], peerMac[2], peerMac[3], peerMac[4], peerMac[5],
                esp_err_to_name(result));
        return false;
    }
    
    currentStatus = CommStatus::PEER_ADDED;
    logDebug("[PEER] Added peer %02x:%02x:%02x:%02x:%02x:%02x (channel %d)", 
            peerMac[0], peerMac[1], peerMac[2], peerMac[3], peerMac[4], peerMac[5], channel);
    return true;
}

bool CommunicationProtocol::sendMessage(const uint8_t* peerMac, const void* message, size_t messageSize) {
    if (currentStatus < CommStatus::READY) {
        logDebug("[ERROR] Communication not ready for message sending");
        return false;
    }
    
    logDebug("[SEND] Sending %d bytes to %02x:%02x:%02x:%02x:%02x:%02x", 
            messageSize, peerMac[0], peerMac[1], peerMac[2], peerMac[3], peerMac[4], peerMac[5]);
    
    esp_err_t result = esp_now_send(peerMac, (const uint8_t*)message, messageSize);
    
    if (result == ESP_OK) {
        currentStatus = CommStatus::SEND_SUCCESS;
        logDebug("[SEND] Message transmission initiated successfully");
        return true;
    } else {
        currentStatus = CommStatus::SEND_FAILED;
        logDebug("[ERROR] Failed to send message: %s", esp_err_to_name(result));
        return false;
    }
}

void CommunicationProtocol::onMessageSent(esp_now_send_cb_t callback) {
    esp_now_register_send_cb(callback);
    logDebug("[CALLBACK] Send callback registered");
}

void CommunicationProtocol::onMessageReceived(esp_now_recv_cb_t callback) {
    esp_now_register_recv_cb(callback);
    logDebug("[CALLBACK] Receive callback registered");
}

void CommunicationProtocol::getLocalMAC(uint8_t mac[6]) {
    WiFi.macAddress(mac);
}

void CommunicationProtocol::printLocalMAC(const char* prefix) {
    uint8_t mac[6];
    getLocalMAC(mac);
    
    logDebug("🆔 %s: %02x:%02x:%02x:%02x:%02x:%02x", prefix,
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            
    // Additional helpful message for configuration
    if (boardType == BoardType::CONTROLLER) {
        Serial.println("📝 Copy this MAC address to console's peer configuration!");
    }
}

void CommunicationProtocol::logDebug(const char* format, ...) {
    if (!debugEnabled) return;
    
    va_list args;
    va_start(args, format);
    
    char buffer[256];
    vsnprintf(buffer, sizeof(buffer), format, args);
    Serial.println(buffer);
    
    va_end(args);
}
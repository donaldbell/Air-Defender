#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include "protocol.h"

/**
 * Unified ESP-NOW Communication Protocol
 * 
 * Provides standardized ESP-NOW communication for both console and controller boards.
 * Handles initialization, peer management, message transmission, and callback registration.
 * 
 * Features:
 * - Board-specific WiFi mode configuration (STA vs AP_STA) 
 * - Automatic peer discovery and management
 * - Type-safe message sending with error handling
 * - Centralized MAC address handling
 * - Event-driven callback system
 */

enum class BoardType {
    CONSOLE,    // Console runs in WIFI_STA mode
    CONTROLLER  // Controller runs in WIFI_AP_STA mode (supports web server)
};

enum class CommStatus {
    NOT_INITIALIZED,
    READY,
    PEER_ADDED,
    SEND_SUCCESS,
    SEND_FAILED,
    RECEIVE_ERROR
};

class CommunicationProtocol {
public:
    /**
     * Initialize ESP-NOW communication for the specified board type
     * @param boardType Console or Controller configuration
     * @param enableDebug Enable detailed logging
     * @return true if initialization successful
     */
    bool begin(BoardType boardType, bool enableDebug = true);
    
    /**
     * Add a communication peer by MAC address
     * @param peerMac MAC address as byte array
     * @param channel WiFi channel (0 for auto)
     * @return true if peer added successfully
     */
    bool addPeer(const uint8_t* peerMac, uint8_t channel = 0);
    
    /**
     * Send message to specified peer
     * @param peerMac Target MAC address
     * @param message Pointer to message structure
     * @param messageSize Size of message in bytes
     * @return true if send initiated successfully
     */
    bool sendMessage(const uint8_t* peerMac, const void* message, size_t messageSize);
    
    /**
     * Send typed message with automatic size calculation
     */
    template<typename T>
    bool sendMessage(const uint8_t* peerMac, const T& message) {
        return sendMessage(peerMac, &message, sizeof(T));
    }
    
    /**
     * Register callback for sent message confirmation
     */
    void onMessageSent(esp_now_send_cb_t callback);
    
    /**
     * Register callback for received messages
     */
    void onMessageReceived(esp_now_recv_cb_t callback);
    
    /**
     * Get local MAC address
     */
    void getLocalMAC(uint8_t mac[6]);
    
    /**
     * Print local MAC address for debugging/configuration
     */
    void printLocalMAC(const char* prefix = "Local MAC");
    
    /**
     * Get current communication status
     */
    CommStatus getStatus() const { return currentStatus; }
    
    /**
     * Check if ESP-NOW is ready for communication
     */
    bool isReady() const { return currentStatus >= CommStatus::READY; }
    
private:
    BoardType boardType;
    CommStatus currentStatus = CommStatus::NOT_INITIALIZED;
    bool debugEnabled = false;
    
    void logDebug(const char* format, ...);
    bool initializeWiFi();
    bool initializeESPNOW();
};

// Global instance for easy access (optional - can also be instantiated locally)
extern CommunicationProtocol comm;
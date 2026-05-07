/**
 * @file app_state.h
 * @brief Application state machine for ESP32-C3 Hamownia
 */

#ifndef APP_STATE_H
#define APP_STATE_H

#include "config.h"

/**
 * @brief Application states
 * 
 * State transitions:
 * BOOT → IDLE → READY
 *         ↓
 *    TARE_RUNNING (brief, returns to previous state)
 *         ↓
 *    RECORDING ← → SAVING
 *         ↓
 *    COUNTDOWN (heater)
 *         ↓
 *    HEATER_ACTIVE
 *         ↓
 *    IDLE/READY
 * 
 * Error states: ERROR, LOW_BAT_LOCKOUT
 */
enum class AppState {
    BOOT,               // System starting up
    IDLE,               // System ready, waiting for commands
    READY,              // System ready, calibrated
    TARE_RUNNING,       // Tare operation in progress
    RECORDING,          // Recording force data to SD
    SAVING,             // Saving recording to file
    COUNTDOWN,          // Heater countdown before activation
    HEATER_ACTIVE,      // Heater is actively running
    ERROR,              // General error state
    LOW_BAT_LOCKOUT     // Low battery, heater disabled
};

/**
 * @brief Convert AppState to string for debugging
 */
inline const char* stateToString(AppState state) {
    switch (state) {
        case AppState::BOOT:           return "BOOT";
        case AppState::IDLE:           return "IDLE";
        case AppState::READY:          return "READY";
        case AppState::TARE_RUNNING:   return "TARE_RUNNING";
        case AppState::RECORDING:      return "RECORDING";
        case AppState::SAVING:         return "SAVING";
        case AppState::COUNTDOWN:      return "COUNTDOWN";
        case AppState::HEATER_ACTIVE:  return "HEATER_ACTIVE";
        case AppState::ERROR:          return "ERROR";
        case AppState::LOW_BAT_LOCKOUT: return "LOW_BAT_LOCKOUT";
        default:                       return "UNKNOWN";
    }
}

/**
 * @brief Heater operating mode
 */
enum class HeaterMode {
    OFF,        // Heater off
    FULL_ON,    // Full power (100% duty cycle)
    PWM         // PWM mode with configurable duty cycle
};

/**
 * @brief Convert HeaterMode to string
 */
inline const char* heaterModeToString(HeaterMode mode) {
    switch (mode) {
        case HeaterMode::OFF:     return "OFF";
        case HeaterMode::FULL_ON: return "FULL_ON";
        case HeaterMode::PWM:     return "PWM";
        default:                  return "UNKNOWN";
    }
}

/**
 * @brief Application context - shared state across services
 */
struct AppContext {
    // Current state
    AppState currentState = AppState::BOOT;
    AppState previousState = AppState::BOOT;
    
    // Force measurements
    float forceRaw = 0.0f;          // Raw force value
    float forceFiltered = 0.0f;     // Filtered force value
    float forcePeak = 0.0f;         // Peak force since last reset
    float forceTareOffset = 0.0f;   // Tare offset
    
    // Battery voltages
    float logicBatteryVoltage = 0.0f;
    float heaterBatteryVoltage = 0.0f;
    bool logicBatteryLow = false;
    bool heaterBatteryLow = false;
    
    // Heater state
    HeaterMode heaterMode = HeaterMode::OFF;
    uint8_t heaterDutyPercent = HEATER_DEFAULT_DUTY;
    uint32_t heaterDurationMs = HEATER_DEFAULT_DURATION_MS;
    uint8_t heaterCountdownS = HEATER_DEFAULT_COUNTDOWN_S;
    int8_t heaterCountdownRemaining = 0;
    uint32_t heaterStartTime = 0;
    bool heaterRunning = false;
    
    // Recording state
    bool isRecording = false;
    uint32_t recordingStartTime = 0;
    uint32_t recordingSampleCount = 0;
    char currentRecordingFile[32] = "";
    
    // Network state
    bool wifiConnected = false;
    bool apMode = false;
    char ipAddress[16] = "";
    
    // SD card state
    bool sdCardOk = false;
    
    // Error state
    bool hasError = false;
    char errorMessage[64] = "";
    
    /**
     * @brief Transition to a new state
     */
    void setState(AppState newState) {
        if (currentState != newState) {
            previousState = currentState;
            currentState = newState;
            DEBUG_PRINTF("[STATE] %s -> %s\n", stateToString(previousState), stateToString(currentState));
        }
    }
    
    /**
     * @brief Reset peak force
     */
    void resetPeak() {
        forcePeak = 0.0f;
    }
    
    /**
     * @brief Update peak force if current is higher
     */
    void updatePeak(float force) {
        if (force > forcePeak) {
            forcePeak = force;
        }
    }
};

#endif // APP_STATE_H
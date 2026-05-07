/**
 * @file heater_service.h
 * @brief Heater control service for ESP32-C3 Hamownia
 * 
 * Heater MOSFET control on GPIO6 (HEATER_EN)
 * - LOW = OFF (gate pulldown R6=10k ensures safe OFF at boot)
 * - HIGH = ON
 * 
 * Modes:
 * - Full ON: 100% duty cycle
 * - PWM: Configurable duty cycle
 * 
 * Safety features:
 * - Low battery lockout
 * - Maximum duration limit
 * - Countdown before activation
 */

#ifndef HEATER_SERVICE_H
#define HEATER_SERVICE_H

#include <Arduino.h>
#include "config.h"
#include "app_state.h"

/**
 * @brief Heater state machine states
 */
enum class HeaterState {
    IDLE,           // Heater off, ready
    COUNTDOWN,      // Countdown before activation
    ACTIVE,         // Heater actively running
    COMPLETED,      // Heating completed
    CANCELLED,      // Heating cancelled by user
    LOCKED_OUT      // Locked out due to low battery
};

/**
 * @brief Heater Service class - MOSFET control with safety features
 */
class HeaterService {
public:
    HeaterService() : state(HeaterState::IDLE), mode(HeaterMode::OFF),
                      dutyPercent(HEATER_DEFAULT_DUTY), durationMs(HEATER_DEFAULT_DURATION_MS),
                      countdownS(HEATER_DEFAULT_COUNTDOWN_S), countdownRemaining(0),
                      startTime(0), pwmConfigured(false) {}
    
    /**
     * @brief Initialize heater control
     */
    void begin() {
        // Configure pin as output, LOW = OFF
        pinMode(PIN_HEATER_EN, OUTPUT);
        digitalWrite(PIN_HEATER_EN, LOW);
        
        // Configure PWM but don't start it
        configurePWM();
        
        DEBUG_PRINT("[HEATER] Service initialized (OFF)");
    }
    
    /**
     * @brief Update heater state - call in loop()
     */
    void update(AppContext& ctx) {
        uint32_t now = millis();
        
        switch (state) {
            case HeaterState::IDLE:
                // Nothing to do
                break;
                
            case HeaterState::COUNTDOWN:
                // Check if countdown finished
                {
                    int32_t elapsed = (now - startTime) / 1000;
                    countdownRemaining = countdownS - elapsed;
                    
                    if (countdownRemaining <= 0) {
                        // Countdown finished, start heating
                        startHeating(ctx);
                    }
                }
                break;
                
            case HeaterState::ACTIVE:
                // Check if heating duration finished
                if ((now - startTime) >= durationMs) {
                    stopHeating(ctx);
                    state = HeaterState::COMPLETED;
                    DEBUG_PRINT("[HEATER] Heating completed");
                }
                break;
                
            case HeaterState::COMPLETED:
            case HeaterState::CANCELLED:
                // Return to idle after a brief moment
                state = HeaterState::IDLE;
                break;
                
            case HeaterState::LOCKED_OUT:
                // Stay locked out until battery voltage recovers
                break;
        }
        
        // Update context
        ctx.heaterRunning = (state == HeaterState::ACTIVE || state == HeaterState::COUNTDOWN);
        ctx.heaterCountdownRemaining = countdownRemaining;
    }
    
    /**
     * @brief Start heater with parameters
     * @param ctx Application context
     * @param countdown Countdown in seconds before activation
     * @param heaterMode Heating mode (FULL_ON or PWM)
     * @param duration Heating duration in milliseconds
     * @param duty PWM duty cycle in percent (for PWM mode)
     * @return true if started successfully
     */
    bool start(AppContext& ctx, uint8_t countdown, HeaterMode heaterMode, 
               uint32_t duration, uint8_t duty = 50) {
        
        // Check if already running
        if (state == HeaterState::ACTIVE || state == HeaterState::COUNTDOWN) {
            DEBUG_PRINT("[HEATER] Already running");
            return false;
        }
        
        // Check low battery lockout
        if (ctx.heaterBatteryLow || ctx.heaterBatteryVoltage < HEATER_MIN_BATTERY_VOLTAGE) {
            DEBUG_PRINT("[HEATER] Blocked: low heater battery");
            state = HeaterState::LOCKED_OUT;
            return false;
        }
        
        // Validate parameters
        if (duration > HEATER_MAX_DURATION_MS) {
            DEBUG_PRINTF("[HEATER] Duration capped to max: %lu ms\n", HEATER_MAX_DURATION_MS);
            duration = HEATER_MAX_DURATION_MS;
        }
        
        // Store parameters
        countdownS = countdown;
        mode = heaterMode;
        durationMs = duration;
        dutyPercent = duty;
        
        // Start countdown or immediately start heating
        if (countdownS > 0) {
            startTime = millis();
            countdownRemaining = countdownS;
            state = HeaterState::COUNTDOWN;
            ctx.setState(AppState::COUNTDOWN);
            DEBUG_PRINTF("[HEATER] Countdown started: %d seconds\n", countdownS);
        } else {
            startHeating(ctx);
        }
        
        return true;
    }
    
    /**
     * @brief Stop heater immediately
     */
    void stop(AppContext& ctx) {
        if (state == HeaterState::IDLE || state == HeaterState::LOCKED_OUT) {
            return;
        }
        
        turnOff();
        
        if (state == HeaterState::COUNTDOWN) {
            DEBUG_PRINT("[HEATER] Countdown cancelled");
        } else if (state == HeaterState::ACTIVE) {
            DEBUG_PRINT("[HEATER] Heating stopped");
        }
        
        state = HeaterState::CANCELLED;
        ctx.heaterRunning = false;
        
        // Return to appropriate state
        if (ctx.previousState == AppState::RECORDING) {
            ctx.setState(AppState::RECORDING);
        } else {
            ctx.setState(AppState::READY);
        }
    }
    
    /**
     * @brief Check if heater is active
     */
    bool isActive() const {
        return state == HeaterState::ACTIVE;
    }
    
    /**
     * @brief Check if in countdown
     */
    bool isCountdown() const {
        return state == HeaterState::COUNTDOWN;
    }
    
    /**
     * @brief Check if locked out
     */
    bool isLockedOut() const {
        return state == HeaterState::LOCKED_OUT;
    }
    
    /**
     * @brief Get current state
     */
    HeaterState getState() const {
        return state;
    }
    
    /**
     * @brief Get countdown remaining
     */
    int8_t getCountdownRemaining() const {
        return countdownRemaining;
    }
    
    /**
     * @brief Clear lockout (call when battery voltage recovers)
     */
    void clearLockout() {
        if (state == HeaterState::LOCKED_OUT) {
            state = HeaterState::IDLE;
            DEBUG_PRINT("[HEATER] Lockout cleared");
        }
    }

private:
    HeaterState state;
    HeaterMode mode;
    uint8_t dutyPercent;
    uint32_t durationMs;
    uint8_t countdownS;
    int8_t countdownRemaining;
    uint32_t startTime;
    bool pwmConfigured;
    
    /**
     * @brief Configure PWM channel
     */
    void configurePWM() {
        // Configure LEDC PWM for ESP32-C3
        ledcSetup(HEATER_PWM_CHANNEL, HEATER_PWM_FREQUENCY, HEATER_PWM_RESOLUTION);
        ledcAttachPin(PIN_HEATER_EN, HEATER_PWM_CHANNEL);
        ledcWrite(HEATER_PWM_CHANNEL, 0);  // Start with 0% duty
        pwmConfigured = true;
    }
    
    /**
     * @brief Start heating (called after countdown)
     */
    void startHeating(AppContext& ctx) {
        startTime = millis();
        state = HeaterState::ACTIVE;
        ctx.setState(AppState::HEATER_ACTIVE);
        ctx.heaterRunning = true;
        
        if (mode == HeaterMode::FULL_ON) {
            // Full ON - set PWM to 100%
            ledcWrite(HEATER_PWM_CHANNEL, 255);
            DEBUG_PRINTF("[HEATER] Started FULL_ON for %lu ms\n", durationMs);
        } else if (mode == HeaterMode::PWM) {
            // PWM mode - set duty cycle
            uint8_t pwmValue = (dutyPercent * 255) / 100;
            ledcWrite(HEATER_PWM_CHANNEL, pwmValue);
            DEBUG_PRINTF("[HEATER] Started PWM %d%% for %lu ms\n", dutyPercent, durationMs);
        }
    }
    
    /**
     * @brief Stop heating
     */
    void stopHeating(AppContext& ctx) {
        turnOff();
        ctx.heaterRunning = false;
        
        // Return to appropriate state
        if (ctx.previousState == AppState::RECORDING) {
            ctx.setState(AppState::RECORDING);
        } else {
            ctx.setState(AppState::READY);
        }
    }
    
    /**
     * @brief Turn off heater output
     */
    void turnOff() {
        ledcWrite(HEATER_PWM_CHANNEL, 0);
        digitalWrite(PIN_HEATER_EN, LOW);
    }
};

#endif // HEATER_SERVICE_H
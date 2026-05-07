/**
 * @file led_service.h
 * @brief Non-blocking LED control service for ESP32-C3 Hamownia
 */

#ifndef LED_SERVICE_H
#define LED_SERVICE_H

#include <Arduino.h>
#include "config.h"
#include "app_state.h"

/**
 * @brief LED pattern types
 */
enum class LEDPattern {
    BOOT,           // Fast blink
    IDLE,           // Slow heartbeat
    TARE,           // Quick double blink
    RECORDING,      // Medium blink
    COUNTDOWN,      // Fast blink
    HEATER_ACTIVE,  // Very fast / mostly ON
    ERROR,          // Triple blink
    LOW_BAT,        // Long-short pattern
    AP_MODE         // Slow double blink
};

/**
 * @brief LED Service class - non-blocking LED patterns
 */
class LEDService {
public:
    LEDService() : currentPattern(LEDPattern::BOOT), lastToggleTime(0), ledState(false), 
                   patternStep(0), patternActive(false) {}
    
    /**
     * @brief Initialize LED pin
     */
    void begin() {
        pinMode(PIN_STATUS_LED, OUTPUT);
        // LED is active LOW, so HIGH = OFF
        digitalWrite(PIN_STATUS_LED, HIGH);
        DEBUG_PRINT("[LED] Service initialized");
    }
    
    /**
     * @brief Set LED pattern based on application state
     */
    void setPatternForState(AppState state, bool apMode = false) {
        if (apMode) {
            setPattern(LEDPattern::AP_MODE);
            return;
        }
        
        switch (state) {
            case AppState::BOOT:
                setPattern(LEDPattern::BOOT);
                break;
            case AppState::IDLE:
            case AppState::READY:
                setPattern(LEDPattern::IDLE);
                break;
            case AppState::TARE_RUNNING:
                setPattern(LEDPattern::TARE);
                break;
            case AppState::RECORDING:
            case AppState::SAVING:
                setPattern(LEDPattern::RECORDING);
                break;
            case AppState::COUNTDOWN:
                setPattern(LEDPattern::COUNTDOWN);
                break;
            case AppState::HEATER_ACTIVE:
                setPattern(LEDPattern::HEATER_ACTIVE);
                break;
            case AppState::ERROR:
                setPattern(LEDPattern::ERROR);
                break;
            case AppState::LOW_BAT_LOCKOUT:
                setPattern(LEDPattern::LOW_BAT);
                break;
            default:
                setPattern(LEDPattern::IDLE);
                break;
        }
    }
    
    /**
     * @brief Set specific LED pattern
     */
    void setPattern(LEDPattern pattern) {
        if (currentPattern != pattern) {
            currentPattern = pattern;
            patternStep = 0;
            patternActive = false;
            DEBUG_PRINTF("[LED] Pattern changed to %d\n", (int)pattern);
        }
    }
    
    /**
     * @brief Update LED state - call this in loop()
     */
    void update() {
        uint32_t now = millis();
        
        switch (currentPattern) {
            case LEDPattern::BOOT:
                updateBlink(now, LED_BOOT_ON_MS, LED_BOOT_OFF_MS);
                break;
            case LEDPattern::IDLE:
                updateBlink(now, LED_IDLE_ON_MS, LED_IDLE_OFF_MS);
                break;
            case LEDPattern::TARE:
                updateDoubleBlink(now, LED_TARE_ON_MS, LED_TARE_OFF_MS, LED_TARE_PAUSE_MS);
                break;
            case LEDPattern::RECORDING:
                updateBlink(now, LED_RECORDING_ON_MS, LED_RECORDING_OFF_MS);
                break;
            case LEDPattern::COUNTDOWN:
                updateBlink(now, LED_COUNTDOWN_ON_MS, LED_COUNTDOWN_OFF_MS);
                break;
            case LEDPattern::HEATER_ACTIVE:
                updateBlink(now, LED_HEATER_ON_MS, LED_HEATER_OFF_MS);
                break;
            case LEDPattern::ERROR:
                updateTripleBlink(now, LED_ERROR_ON_MS, LED_ERROR_OFF_MS, LED_ERROR_PAUSE_MS);
                break;
            case LEDPattern::LOW_BAT:
                updateLongShortBlink(now, LED_LOWBAT_LONG_MS, LED_LOWBAT_SHORT_MS, LED_LOWBAT_PAUSE_MS);
                break;
            case LEDPattern::AP_MODE:
                updateDoubleBlink(now, LED_AP_ON_MS, LED_AP_OFF_MS, LED_AP_PAUSE_MS);
                break;
        }
    }
    
    /**
     * @brief Turn LED on (active LOW)
     */
    void on() {
        digitalWrite(PIN_STATUS_LED, LOW);  // Active LOW
        ledState = true;
    }
    
    /**
     * @brief Turn LED off
     */
    void off() {
        digitalWrite(PIN_STATUS_LED, HIGH);  // Active LOW
        ledState = false;
    }
    
    /**
     * @brief Toggle LED state
     */
    void toggle() {
        ledState = !ledState;
        digitalWrite(PIN_STATUS_LED, ledState ? LOW : HIGH);
    }

private:
    LEDPattern currentPattern;
    uint32_t lastToggleTime;
    bool ledState;
    uint8_t patternStep;
    bool patternActive;
    
    /**
     * @brief Simple blink pattern
     */
    void updateBlink(uint32_t now, uint32_t onMs, uint32_t offMs) {
        uint32_t interval = ledState ? onMs : offMs;
        if (now - lastToggleTime >= interval) {
            toggle();
            lastToggleTime = now;
        }
    }
    
    /**
     * @brief Double blink pattern (blink twice, pause)
     */
    void updateDoubleBlink(uint32_t now, uint32_t onMs, uint32_t offMs, uint32_t pauseMs) {
        // Pattern: ON-OFF-ON-OFF-PAUSE (repeat)
        // Steps: 0=ON1, 1=OFF1, 2=ON2, 3=OFF2, 4=PAUSE
        uint32_t interval;
        
        switch (patternStep) {
            case 0: // ON1
                if (!patternActive) {
                    on();
                    patternActive = true;
                    lastToggleTime = now;
                }
                interval = onMs;
                break;
            case 1: // OFF1
                interval = offMs;
                break;
            case 2: // ON2
                interval = onMs;
                break;
            case 3: // OFF2
                interval = offMs;
                break;
            case 4: // PAUSE
                interval = pauseMs;
                break;
            default:
                patternStep = 0;
                return;
        }
        
        if (now - lastToggleTime >= interval) {
            lastToggleTime = now;
            patternStep++;
            if (patternStep > 4) {
                patternStep = 0;
            }
            // Toggle LED for steps 0-3
            if (patternStep <= 3) {
                toggle();
            }
            patternActive = false;
        }
    }
    
    /**
     * @brief Triple blink pattern (blink three times, pause)
     */
    void updateTripleBlink(uint32_t now, uint32_t onMs, uint32_t offMs, uint32_t pauseMs) {
        // Pattern: ON-OFF-ON-OFF-ON-OFF-PAUSE (repeat)
        // Steps: 0=ON1, 1=OFF1, 2=ON2, 3=OFF2, 4=ON3, 5=OFF3, 6=PAUSE
        uint32_t interval;
        
        switch (patternStep) {
            case 0: case 2: case 4: // ON
                if (!patternActive) {
                    on();
                    patternActive = true;
                    lastToggleTime = now;
                }
                interval = onMs;
                break;
            case 1: case 3: case 5: // OFF
                interval = offMs;
                break;
            case 6: // PAUSE
                interval = pauseMs;
                break;
            default:
                patternStep = 0;
                return;
        }
        
        if (now - lastToggleTime >= interval) {
            lastToggleTime = now;
            patternStep++;
            if (patternStep > 6) {
                patternStep = 0;
            }
            // Toggle LED for steps 1,3,5 (turn off after ON)
            if (patternStep == 1 || patternStep == 3 || patternStep == 5) {
                off();
            }
            patternActive = false;
        }
    }
    
    /**
     * @brief Long-short blink pattern (SOS-like)
     */
    void updateLongShortBlink(uint32_t now, uint32_t longMs, uint32_t shortMs, uint32_t pauseMs) {
        // Pattern: LONG-OFF-SHORT-OFF-PAUSE (repeat)
        // Steps: 0=LONG, 1=OFF, 2=SHORT, 3=OFF2, 4=PAUSE
        uint32_t interval;
        
        switch (patternStep) {
            case 0: // LONG ON
                if (!patternActive) {
                    on();
                    patternActive = true;
                    lastToggleTime = now;
                }
                interval = longMs;
                break;
            case 1: // OFF after long
            case 3: // OFF after short
                interval = shortMs;
                break;
            case 2: // SHORT ON
                if (!patternActive) {
                    on();
                    patternActive = true;
                    lastToggleTime = now;
                }
                interval = shortMs;
                break;
            case 4: // PAUSE
                interval = pauseMs;
                break;
            default:
                patternStep = 0;
                return;
        }
        
        if (now - lastToggleTime >= interval) {
            lastToggleTime = now;
            patternStep++;
            if (patternStep > 4) {
                patternStep = 0;
            }
            // Turn off for OFF steps
            if (patternStep == 1 || patternStep == 3) {
                off();
            }
            patternActive = false;
        }
    }
};

#endif // LED_SERVICE_H
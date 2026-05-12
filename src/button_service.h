/**
 * @file button_service.h
 * @brief Button service for BT2 on GPIO2 - ESP32-C3 Hamownia
 * 
 * Only BT2 on GPIO2 exists as a physical button.
 * BT1 is ignored.
 */

#ifndef BUTTON_SERVICE_H
#define BUTTON_SERVICE_H

#include <Arduino.h>
#include "config.h"

/**
 * @brief Button event types
 */
enum class ButtonEvent {
    NONE,           // No event
    SHORT_PRESS,    // Short press detected
    LONG_PRESS      // Long press detected
};

/**
 * @brief Button callback function type
 */
typedef void (*ButtonCallback)(ButtonEvent event);

/**
 * @brief Button Service class - debounced button reading with short/long press detection
 */
class ButtonService {
public:
    ButtonService() : lastState(HIGH), currentState(HIGH), lastDebounceTime(0), 
                      pressStartTime(0), longPressTriggered(false), callback(nullptr) {}
    
    /**
     * @brief Initialize button pin
     */
    void begin() {
        pinMode(PIN_BT2, INPUT_PULLUP);  // Button connects to GND, active LOW
        lastState = digitalRead(PIN_BT2);
        DEBUG_PRINT("[BUTTON] Service initialized on GPIO" + String(PIN_BT2));
    }
    
    /**
     * @brief Set callback for button events
     */
    void setCallback(ButtonCallback cb) {
        callback = cb;
    }
    
    /**
     * @brief Update button state - call this in loop()
     * @return Button event if any
     */
    ButtonEvent update() {
        ButtonEvent event = ButtonEvent::NONE;
        int reading = digitalRead(PIN_BT2);
        uint32_t now = millis();
        
        // Check for state change (with debounce)
        if (reading != lastState) {
            lastDebounceTime = now;
        }
        
        // After debounce period, check if state has changed
        if ((now - lastDebounceTime) > BUTTON_DEBOUNCE_MS) {
            // If the button state has changed
            if (reading != currentState) {
                currentState = reading;
                
                if (currentState == LOW) {
                    // Button pressed (active LOW)
                    pressStartTime = now;
                    longPressTriggered = false;
                    DEBUG_PRINT("[BUTTON] Pressed");
                } else {
                    // Button released
                    if (!longPressTriggered) {
                        // Short press only if long press wasn't triggered
                        uint32_t pressDuration = now - pressStartTime;
                        if (pressDuration < BUTTON_LONG_PRESS_MS) {
                            event = ButtonEvent::SHORT_PRESS;
                            DEBUG_PRINT("[BUTTON] Short press detected");
                        }
                    }
                }
            }
        }
        
        // Check for long press while button is held
        if (currentState == LOW && !longPressTriggered) {
            if ((now - pressStartTime) >= BUTTON_LONG_PRESS_MS) {
                longPressTriggered = true;
                event = ButtonEvent::LONG_PRESS;
                DEBUG_PRINT("[BUTTON] Long press detected");
            }
        }
        
        // Call callback if event occurred
        if (event != ButtonEvent::NONE && callback != nullptr) {
            callback(event);
        }
        
        lastState = reading;
        return event;
    }
    
    /**
     * @brief Check if button is currently pressed
     */
    bool isPressed() {
        return digitalRead(PIN_BT2) == LOW;
    }

private:
    int lastState;
    int currentState;
    uint32_t lastDebounceTime;
    uint32_t pressStartTime;
    bool longPressTriggered;
    ButtonCallback callback;
};

#endif // BUTTON_SERVICE_H
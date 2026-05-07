/**
 * @file display_service.h
 * @brief OLED display service for ESP32-C3 Hamownia
 * 
 * OLED: HS96L03W2C03 (SSD1306 compatible)
 * I2C connections:
 * - GPIO3 = OLED_SCL
 * - GPIO4 = OLED_SDA
 */

#ifndef DISPLAY_SERVICE_H
#define DISPLAY_SERVICE_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "config.h"
#include "app_state.h"

/**
 * @brief Display Service class - OLED output
 */
class DisplayService {
public:
    DisplayService() : display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1), 
                       lastUpdateMs(0), initialized(false) 
    {
        strncpy(cachedIP, "0.0.0.0", sizeof(cachedIP) - 1);
    }
    
    /**
     * @brief Initialize OLED display
     */
    void begin() {
        // Initialize I2C with custom pins
        Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
        Wire.setClock(OLED_I2C_FREQUENCY);
        
        // Initialize display
        if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDRESS)) {
            DEBUG_PRINT("[DISPLAY] ERROR: SSD1306 initialization failed!");
            initialized = false;
            return;
        }
        
        initialized = true;
        DEBUG_PRINT("[DISPLAY] Service initialized");
        
        // Clear display
        display.clearDisplay();
        display.display();
        
        // Show boot screen
        showBootScreen();
    }
    
    /**
     * @brief Update display with current context
     */
    void update(const AppContext& ctx) {
        if (!initialized) return;
        
        uint32_t now = millis();
        
        // Update at defined interval
        if (now - lastUpdateMs < OLED_UPDATE_INTERVAL_MS) {
            return;
        }
        lastUpdateMs = now;
        
        // Clear display
        display.clearDisplay();
        
        // Draw content based on state
        drawStatusLine(ctx);
        drawForceDisplay(ctx);
        drawBatteryDisplay(ctx);
        drawStateIndicator(ctx);
        drawIPLine();
        
        // Display
        display.display();
    }
    
    /**
     * @brief Show boot screen
     */
    void showBootScreen() {
        if (!initialized) return;
        
        display.clearDisplay();
        display.setTextSize(2);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(20, 20);
        display.println("HAMOWNIA");
        display.setTextSize(1);
        display.setCursor(30, 45);
        display.println("ESP32-C3");
        display.display();
    }
    
    /**
     * @brief Show error message
     */
    void showError(const char* message) {
        if (!initialized) return;
        
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 0);
        display.println("ERROR:");
        display.setCursor(0, 16);
        display.println(message);
        display.display();
    }
    
    /**
     * @brief Store IP address and force display update (called on WiFi connect)
     */
    void showIPAddress(const char* ip, bool isSta) {
        if (!initialized) return;
        
        // Store IP for periodic redraw
        strncpy(cachedIP, ip, sizeof(cachedIP) - 1);
        cachedIsSta = isSta;
        
        // Draw immediately
        drawIPLine();
        display.display();
    }
    
    /**
     * @brief Alias for showIPAddress
     */
    void updateIPAddress(const char* ip, bool isSta) {
        showIPAddress(ip, isSta);
    }
    
    /**
     * @brief Check if display is initialized
     */
    bool isInitialized() const {
        return initialized;
    }

private:
    Adafruit_SSD1306 display;
    uint32_t lastUpdateMs;
    bool initialized;
    char cachedIP[16];
    bool cachedIsSta = false;
    
    /**
     * @brief Draw status line at top
     */
    void drawStatusLine(const AppContext& ctx) {
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 0);
        
        // State indicator
        display.print("[");
        display.print(stateToStringShort(ctx.currentState));
        display.print("]");
        
        // Recording indicator
        if (ctx.isRecording) {
            display.print(" REC");
        }
        
        // WiFi indicator
        if (ctx.wifiConnected || ctx.apMode) {
            display.print(" *");
        }
    }
    
    /**
     * @brief Draw force display (main area)
     */
    void drawForceDisplay(const AppContext& ctx) {
        // Current force (large)
        display.setTextSize(2);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 12);
        display.print(ctx.forceFiltered * HX711_SCALE_FACTOR, 1);
        display.print(" ");
        display.print(FORCE_UNIT);
        
        // Peak force (smaller)
        display.setTextSize(1);
        display.setCursor(0, 32);
        display.print("Peak: ");
        display.print(ctx.forcePeak * HX711_SCALE_FACTOR, 1);
        display.print(" ");
        display.print(FORCE_UNIT);
    }
    
    /**
     * @brief Draw battery display
     */
    void drawBatteryDisplay(const AppContext& ctx) {
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        
        // Logic battery
        display.setCursor(80, 0);
        display.print(ctx.logicBatteryVoltage, 1);
        display.print("V");
        
        // Heater battery
        display.setCursor(80, 32);
        display.print("H:");
        display.print(ctx.heaterBatteryVoltage, 1);
        display.print("V");
        
        // Low battery warning
        if (ctx.heaterBatteryLow) {
            display.setCursor(80, 42);
            display.print("LOW!");
        }
    }
    
    /**
     * @brief Draw state indicator
     */
    void drawStateIndicator(const AppContext& ctx) {
        // Heater status
        if (ctx.heaterRunning) {
            display.setCursor(0, 42);
            display.print("HEATER: ");
            if (ctx.currentState == AppState::COUNTDOWN) {
                display.print("CD:");
                display.print(ctx.heaterCountdownRemaining);
            } else {
                display.print("ON");
            }
        }
    }
    
    /**
     * @brief Draw IP address line at bottom (from cached values)
     */
    void drawIPLine() {
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 54);
        if (cachedIsSta) {
            display.print("STA:");
        } else {
            display.print("AP:");
        }
        // Only print if we have a valid IP
        if (strlen(cachedIP) > 0 && strcmp(cachedIP, "0.0.0.0") != 0) {
            display.println(cachedIP);
        } else {
            display.println("waiting...");
        }
    }
    
    /**
     * @brief Get short state string for display
     */
    const char* stateToStringShort(AppState state) {
        switch (state) {
            case AppState::BOOT:           return "BOOT";
            case AppState::IDLE:           return "IDLE";
            case AppState::READY:          return "RDY";
            case AppState::TARE_RUNNING:   return "TARE";
            case AppState::RECORDING:      return "REC";
            case AppState::SAVING:         return "SAV";
            case AppState::COUNTDOWN:      return "CD";
            case AppState::HEATER_ACTIVE:  return "HEAT";
            case AppState::ERROR:          return "ERR";
            case AppState::LOW_BAT_LOCKOUT: return "LOW";
            default:                       return "?";
        }
    }
};

#endif // DISPLAY_SERVICE_H
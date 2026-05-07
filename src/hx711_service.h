/**
 * @file hx711_service.h
 * @brief HX711 load cell amplifier service for ESP32-C3 Hamownia
 * 
 * HX711 connections:
 * - GPIO7 = HX711_DT (Data)
 * - GPIO8 = HX711_SCK (Clock)
 */

#ifndef HX711_SERVICE_H
#define HX711_SERVICE_H

#include <Arduino.h>
#include <HX711.h>
#include "config.h"
#include "app_state.h"

/**
 * @brief HX711 Service class - load cell reading with filtering
 */
class HX711Service {
public:
    HX711Service() : scale(), scaleFactor(HX711_SCALE_FACTOR), tareOffset(0.0f),
                     rawValue(0.0f), filteredValue(0.0f), lastSampleTime(0),
                     filterIndex(0), filterSum(0.0f), ready(false) {
        // Initialize filter buffer
        for (int i = 0; i < HX711_FILTER_WINDOW; i++) {
            filterBuffer[i] = 0.0f;
        }
    }
    
    /**
     * @brief Initialize HX711
     */
    void begin() {
        // Initialize HX711 with custom pins
        scale.begin(PIN_HX711_DT, PIN_HX711_SCK);
        
        // Wait for HX711 to be ready
        delay(100);
        
        if (scale.is_ready()) {
            ready = true;
            DEBUG_PRINT("[HX711] Service initialized");
            
            // Set scale factor
            scale.set_scale(scaleFactor);
            
            // Initial tare
            DEBUG_PRINT("[HX711] Performing initial tare...");
            tare();
        } else {
            ready = false;
            DEBUG_PRINT("[HX711] ERROR: Sensor not ready!");
        }
    }
    
    /**
     * @brief Perform tare operation
     */
    void tare() {
        if (!ready) return;
        
        DEBUG_PRINT("[HX711] Tare started");
        
        // Use HX711 library tare
        scale.tare(10);  // Average 10 readings
        
        // Update our offset
        tareOffset = scale.get_offset();
        
        // Reset filter
        filterSum = 0.0f;
        filterIndex = 0;
        for (int i = 0; i < HX711_FILTER_WINDOW; i++) {
            filterBuffer[i] = 0.0f;
        }
        
        DEBUG_PRINTF("[HX711] Tare complete, offset: %.2f\n", tareOffset);
    }
    
    /**
     * @brief Update readings - call in loop()
     */
    void update() {
        if (!ready) return;
        
        uint32_t now = millis();
        
        // Sample at defined interval
        if (now - lastSampleTime < HX711_SAMPLE_INTERVAL_MS) {
            return;
        }
        lastSampleTime = now;
        
        // Get raw value
        if (scale.is_ready()) {
            rawValue = scale.get_value(1);  // Single reading
            
            // Apply moving average filter
            applyFilter();
        }
    }
    
    /**
     * @brief Get raw force value (no filter)
     */
    float getRawForce() const {
        return rawValue;
    }
    
    /**
     * @brief Get filtered force value
     */
    float getFilteredForce() const {
        return filteredValue;
    }
    
    /**
     * @brief Get force in units (with scale factor applied)
     */
    float getForce() const {
        return filteredValue * scaleFactor;
    }
    
    /**
     * @brief Get raw ADC value
     */
    long getRawADC() {
        if (!ready) return 0;
        return scale.read();
    }
    
    /**
     * @brief Set scale factor (calibration)
     */
    void setScaleFactor(float factor) {
        scaleFactor = factor;
        scale.set_scale(factor);
        DEBUG_PRINTF("[HX711] Scale factor set to: %.6f\n", factor);
    }
    
    /**
     * @brief Get current scale factor
     */
    float getScaleFactor() const {
        return scaleFactor;
    }
    
    /**
     * @brief Check if HX711 is ready
     */
    bool isReady() {
        return ready && scale.is_ready();
    }
    
    /**
     * @brief Update AppContext with force values
     */
    void updateContext(AppContext& ctx) {
        ctx.forceRaw = rawValue;
        ctx.forceFiltered = filteredValue;
        ctx.forceTareOffset = tareOffset;
    }
    
    /**
     * @brief Calibrate with known weight
     * @param knownWeight The known weight in your units (e.g., kg)
     * Call this after placing known weight on scale
     */
    void calibrate(float knownWeight) {
        if (!ready) return;
        
        // Get current reading
        float currentReading = scale.get_value(10);  // Average 10 readings
        
        // Calculate new scale factor
        if (knownWeight != 0 && currentReading != 0) {
            scaleFactor = knownWeight / currentReading;
            scale.set_scale(scaleFactor);
            DEBUG_PRINTF("[HX711] Calibration complete. Scale factor: %.6f\n", scaleFactor);
        }
    }

private:
    HX711 scale;
    float scaleFactor;
    float tareOffset;
    float rawValue;
    float filteredValue;
    uint32_t lastSampleTime;
    
    // Moving average filter
    float filterBuffer[HX711_FILTER_WINDOW];
    int filterIndex;
    float filterSum;
    
    bool ready;
    
    /**
     * @brief Apply moving average filter
     */
    void applyFilter() {
        // Subtract oldest value from sum
        filterSum -= filterBuffer[filterIndex];
        
        // Add new value
        filterBuffer[filterIndex] = rawValue;
        filterSum += rawValue;
        
        // Update index
        filterIndex = (filterIndex + 1) % HX711_FILTER_WINDOW;
        
        // Calculate average
        filteredValue = filterSum / HX711_FILTER_WINDOW;
    }
};

#endif // HX711_SERVICE_H
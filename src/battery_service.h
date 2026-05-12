/**
 * @file battery_service.h
 * @brief Battery voltage measurement service for ESP32-C3 Hamownia
 * 
 * Measures two battery channels:
 * - GPIO0 (BAT_ADC): VIN / Logic battery via divider R1=100k, R2=33k
 * - GPIO1 (BAT_ADC2): BAT_IN / Heater battery via divider R7=100k, R8=33k
 */

#ifndef BATTERY_SERVICE_H
#define BATTERY_SERVICE_H

#include <Arduino.h>
#include "config.h"
#include "app_state.h"

/**
 * @brief Battery Service class - ADC-based voltage measurement
 */
class BatteryService {
public:
    BatteryService() : logicVoltage(0.0f), heaterVoltage(0.0f), 
                       logicLow(false), heaterLow(false), lastUpdateMs(0) {}
    
    /**
     * @brief Initialize ADC pins
     */
    void begin() {
        // Configure ADC pins as inputs
        pinMode(PIN_BAT_ADC, INPUT);
        pinMode(PIN_BAT_ADC2, INPUT);
        
        // Set ADC resolution to 12 bits (0-4095)
        analogReadResolution(12);
        
        // Set ADC attenuation for full 3.3V range
        analogSetAttenuation(ADC_11db);
        
        // Initial reading
        update();
        
        DEBUG_PRINT("[BATTERY] Service initialized");
        DEBUG_PRINTF("[BATTERY] Logic: %.2fV, Heater: %.2fV\n", logicVoltage, heaterVoltage);
    }
    
    /**
     * @brief Update battery readings - call periodically
     */
    void update() {
        uint32_t now = millis();
        
        // Update at most every 100ms to avoid excessive ADC reads
        if (now - lastUpdateMs < 100) {
            return;
        }
        lastUpdateMs = now;
        
        // Read and average multiple samples
        logicVoltage = readBatteryVoltage(PIN_BAT_ADC, ADC_CORRECTION_LOGIC_BAT);
        heaterVoltage = readBatteryVoltage(PIN_BAT_ADC2, ADC_CORRECTION_HEATER_BAT);
        
        // Check low voltage thresholds
        logicLow = logicVoltage < LOGIC_BATTERY_LOW_VOLTAGE;
        heaterLow = heaterVoltage < HEATER_BATTERY_LOW_VOLTAGE;
    }
    
    /**
     * @brief Force immediate update
     */
    void forceUpdate() {
        lastUpdateMs = 0;
        update();
    }
    
    /**
     * @brief Get logic battery voltage
     */
    float getLogicVoltage() const {
        return logicVoltage;
    }
    
    /**
     * @brief Get heater battery voltage
     */
    float getHeaterVoltage() const {
        return heaterVoltage;
    }
    
    /**
     * @brief Check if logic battery is low
     */
    bool isLogicBatteryLow() const {
        return logicLow;
    }
    
    /**
     * @brief Check if heater battery is low
     */
    bool isHeaterBatteryLow() const {
        return heaterLow;
    }
    
    /**
     * @brief Check if heater should be locked out due to low battery
     */
    bool isHeaterLockedOut() const {
        return heaterVoltage < HEATER_MIN_BATTERY_VOLTAGE;
    }
    
    /**
     * @brief Update AppContext with battery values
     */
    void updateContext(AppContext& ctx) {
        ctx.logicBatteryVoltage = logicVoltage;
        ctx.heaterBatteryVoltage = heaterVoltage;
        ctx.logicBatteryLow = logicLow;
        ctx.heaterBatteryLow = heaterLow;
    }

private:
    float logicVoltage;
    float heaterVoltage;
    bool logicLow;
    bool heaterLow;
    uint32_t lastUpdateMs;
    
    /**
     * @brief Read battery voltage from ADC pin with averaging
     * @param pin ADC pin to read
     * @param correctionOffset ADC voltage correction (additive, per config.h)
     * @return Battery voltage in volts
     */
    float readBatteryVoltage(int pin, float correctionOffset) {
        uint32_t sum = 0;

        // Take multiple samples and average
        for (int i = 0; i < BATTERY_ADC_SAMPLES; i++) {
            sum += analogRead(pin);
        }

        float avgAdc = (float)sum / BATTERY_ADC_SAMPLES;

        // Convert ADC value to voltage at the ADC pin
        float adcVoltage = avgAdc * (ADC_REFERENCE_VOLTAGE / ADC_RESOLUTION);

        // Apply ADC offset correction, then voltage divider multiplier
        float correctedAdc = adcVoltage + correctionOffset;
        float batteryVoltage = correctedAdc * BATTERY_DIVIDER_MULTIPLIER;

        return batteryVoltage;
    }
};

#endif // BATTERY_SERVICE_H
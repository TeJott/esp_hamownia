/**
 * @file config.h
 * @brief Central configuration for ESP32-C3 Hamownia Firmware
 * 
 * Hardware Mapping:
 * GPIO0  = BAT_ADC   = ADC for VIN measurement (divider R1=100k, R2=33k)
 * GPIO1  = BAT_ADC2  = ADC for BAT_IN measurement (divider R7=100k, R8=33k)
 * GPIO2  = BT2       = Physical button (active LOW, internal pull-up)
 * GPIO3  = OLED_SCL  = I2C clock for OLED
 * GPIO4  = OLED_SDA  = I2C data for OLED
 * GPIO5  = STATUS_LED = Green LED (active LOW)
 * GPIO6  = HEATER_EN = MOSFET gate control (LOW=OFF, HIGH=ON)
 * GPIO7  = HX711_DT  = Load cell data line
 * GPIO8  = HX711_SCK = Load cell clock line
 * GPIO9  = SD_CS     = SD card chip select
 * GPIO10 = SD_MOSI   = SPI MOSI
 * GPIO20 = SD_CLK    = SPI clock
 * GPIO21 = SD_MISO   = SPI MISO
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// =============================================================================
// GPIO PIN DEFINITIONS
// =============================================================================

// ADC pins for battery measurement
#define PIN_BAT_ADC         0   // GPIO0 - VIN / Logic battery
#define PIN_BAT_ADC2        1   // GPIO1 - BAT_IN / Heater battery

// Physical button (only BT2 exists, BT1 is ignored)
#define PIN_BUTTON          2   // GPIO2 - BT2, active LOW

// OLED I2C pins
#define PIN_OLED_SCL        3   // GPIO3
#define PIN_OLED_SDA        4   // GPIO4

// Status LED (active LOW - LED tied to 3.3V through 330ohm)
#define PIN_STATUS_LED      5   // GPIO5

// Heater MOSFET control (LOW=OFF, gate pulldown R6=10k)
#define PIN_HEATER_EN       6   // GPIO6

// HX711 load cell amplifier
#define PIN_HX711_DT        7   // GPIO7
#define PIN_HX711_SCK       8   // GPIO8

// SD card SPI pins
#define PIN_SD_CS           9   // GPIO9
#define PIN_SD_MOSI         10  // GPIO10
#define PIN_SD_CLK          20  // GPIO20
#define PIN_SD_MISO         21  // GPIO21

// =============================================================================
// BATTERY MEASUREMENT CONFIG
// =============================================================================

// Voltage divider: R_top = 100k, R_bottom = 33k
// V_bat = V_adc * (R_top + R_bottom) / R_bottom
// Multiplier = (100000 + 33000) / 33000 = 4.0303
#define BATTERY_DIVIDER_R_TOP       100000.0f
#define BATTERY_DIVIDER_R_BOTTOM    33000.0f
#define BATTERY_DIVIDER_MULTIPLIER  ((BATTERY_DIVIDER_R_TOP + BATTERY_DIVIDER_R_BOTTOM) / BATTERY_DIVIDER_R_BOTTOM)

// ADC reference voltage and resolution
#define ADC_REFERENCE_VOLTAGE       3.3f
#define ADC_RESOLUTION              4095.0f  // 12-bit ADC

// Number of samples for ADC averaging
#define BATTERY_ADC_SAMPLES         16

// Battery voltage thresholds
#define LOGIC_BATTERY_LOW_VOLTAGE   3.3f     // Low voltage warning for logic battery
#define HEATER_BATTERY_LOW_VOLTAGE  10.0f    // Low voltage lockout for heater battery

// ADC offset correction (ESP32-C3 ADC is inaccurate)
// Measured errors:
//   GPIO0 (logic batt): ADC reads ~0.60V too LOW   → add offset
//   GPIO1 (heater batt): ADC reads ~0.36V too HIGH  → subtract offset
#define ADC_CORRECTION_LOGIC_BAT    -0.33f   // Add to raw reading
#define ADC_CORRECTION_HEATER_BAT   -0.36f  // Subtract from raw reading

// =============================================================================
// HX711 LOAD CELL CONFIG
// =============================================================================

// HX711 gain setting (Channel A, Gain 128 is default)
#define HX711_GAIN                  128

// Calibration scale factor (units per count)
// User must calibrate with known weight
// Example: if 1000g reads 200000 counts, scale = 1000.0 / 200000.0 = 0.005
#define HX711_SCALE_FACTOR          0.005f

// Tare offset (will be updated at runtime)
#define HX711_TARE_OFFSET           0.0f

// Moving average filter window size
#define HX711_FILTER_WINDOW         10

// Sampling interval (ms)
#define HX711_SAMPLE_INTERVAL_MS    50

// Force unit string
#define FORCE_UNIT                  "kg"

// =============================================================================
// OLED DISPLAY CONFIG
// =============================================================================

// OLED I2C address (common: 0x3C for SSD1306)
#define OLED_I2C_ADDRESS            0x3C

// OLED dimensions
#define OLED_WIDTH                  128
#define OLED_HEIGHT                 64

// OLED I2C frequency
#define OLED_I2C_FREQUENCY          400000

// Display update interval (ms)
#define OLED_UPDATE_INTERVAL_MS     100

// =============================================================================
// SD CARD CONFIG
// =============================================================================

// SPI frequency for SD card
#define SD_SPI_FREQUENCY            4000000

// Maximum number of recordings to keep
#define MAX_RECORDINGS              3

// CSV file prefix
#define CSV_FILE_PREFIX             "/test_"

// CSV file extension
#define CSV_FILE_EXTENSION          ".csv"

// Metadata file
#define METADATA_FILE               "/metadata.json"

// =============================================================================
// HEATER CONTROL CONFIG
// =============================================================================

// Default PWM frequency (Hz)
#define HEATER_PWM_FREQUENCY        1000

// Default PWM duty cycle (%)
#define HEATER_DEFAULT_DUTY         50

// Maximum allowed heating duration (ms) - safety limit
#define HEATER_MAX_DURATION_MS      60000

// Default heating duration (ms)
#define HEATER_DEFAULT_DURATION_MS  1

// Default heating duration (seconds) - used for web UI input default
#define HEATER_DEFAULT_DURATION_S   (HEATER_DEFAULT_DURATION_MS / 1000)

// Default heater duty (%)
#define HEATER_DEFAULT_DUTY_PERCENT  100

// Default countdown duration (seconds)
#define HEATER_DEFAULT_COUNTDOWN_S  5

// Minimum heater battery voltage for operation
#define HEATER_MIN_BATTERY_VOLTAGE  10.0f

// PWM channel (ESP32 has multiple PWM channels)
#define HEATER_PWM_CHANNEL          0

// PWM resolution (bits)
#define HEATER_PWM_RESOLUTION       8

// =============================================================================
// LED BLINK PATTERNS (durations in ms)
// =============================================================================

// Boot: fast blink
#define LED_BOOT_ON_MS              100
#define LED_BOOT_OFF_MS             100

// Idle/Ready: slow heartbeat
#define LED_IDLE_ON_MS              200
#define LED_IDLE_OFF_MS             800

// Tare: quick double blink
#define LED_TARE_ON_MS              50
#define LED_TARE_OFF_MS             50
#define LED_TARE_PAUSE_MS           200

// Recording: medium blink
#define LED_RECORDING_ON_MS         250
#define LED_RECORDING_OFF_MS        250

// Countdown: fast blink
#define LED_COUNTDOWN_ON_MS         100
#define LED_COUNTDOWN_OFF_MS        100

// Heater active: very fast / mostly ON
#define LED_HEATER_ON_MS            150
#define LED_HEATER_OFF_MS           50

// Error: triple blink
#define LED_ERROR_ON_MS             100
#define LED_ERROR_OFF_MS            100
#define LED_ERROR_PAUSE_MS          500

// Low battery: long-short pattern
#define LED_LOWBAT_LONG_MS          400
#define LED_LOWBAT_SHORT_MS         100
#define LED_LOWBAT_PAUSE_MS         500

// AP mode: slow double blink
#define LED_AP_ON_MS                100
#define LED_AP_OFF_MS               100
#define LED_AP_PAUSE_MS             1000

// =============================================================================
// BUTTON CONFIG
// =============================================================================

// Debounce time (ms)
#define BUTTON_DEBOUNCE_MS          50

// Long press threshold (ms)
#define BUTTON_LONG_PRESS_MS        1000

// =============================================================================
// WI-FI CONFIG
// =============================================================================

// Wi-Fi credentials (replace with your network)
#define WIFI_SSID                   "PLACEHOLDER"
#define WIFI_PASSWORD               "PLACEHOLDER"

// AP mode credentials
#define AP_SSID                     "Hamownia"
#define AP_PASSWORD                 "hamownia123"

// Wi-Fi connection timeout (ms) - how long to wait for STA in setup
#define WIFI_CONNECT_TIMEOUT_MS     15000

// Wait for AP to start timeout (ms)
#define WIFI_AP_TIMEOUT_MS          5000

// Delay before WiFi init (ms) - allows power rail to stabilize
#define WIFI_BOOT_DELAY_MS          2000

// WiFi TX power level (ESP32-C3 enum values):
//   78 = WIFI_POWER_19_5dBm  (MAX - best range)
//   52 = WIFI_POWER_13_5dBm  (MODERATE - default)
//   26 = WIFI_POWER_7_5dBm   (LOW)
//    0 = WIFI_POWER_MINUS_1_5dBm (MIN)
#define WIFI_TX_POWER               78  // Maximum power for best range

// AP channel (1-11, default 1)
#define AP_CHANNEL                  1

// AP max connections
#define AP_MAX_CONNECTIONS          4

// Web server port
#define WEB_SERVER_PORT             80

// WebSocket port
#define WEBSOCKET_PORT              81

// =============================================================================
// WEB SERVER CONFIG
// =============================================================================

// Status update interval for WebSocket (ms)
#define WS_UPDATE_INTERVAL_MS       500

// =============================================================================
// RECORDING CONFIG
// =============================================================================

// Recording sample interval (ms)
#define RECORDING_SAMPLE_INTERVAL_MS    100

// =============================================================================
// DEBUG CONFIG
// =============================================================================

// Enable serial debug output
#define DEBUG_SERIAL                true

// Debug baud rate
#define DEBUG_BAUD_RATE             115200

// Debug macros
#if DEBUG_SERIAL
    #define DEBUG_PRINT(x)          Serial.println(x)
    #define DEBUG_PRINTF(fmt, ...)  Serial.printf(fmt, ##__VA_ARGS__)
#else
    #define DEBUG_PRINT(x)
    #define DEBUG_PRINTF(fmt, ...)
#endif

#endif // CONFIG_H
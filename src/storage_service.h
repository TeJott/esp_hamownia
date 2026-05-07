/**
 * @file storage_service.h
 * @brief SD card storage service for ESP32-C3 Hamownia
 * 
 * SD card connections (SPI):
 * - GPIO9  = SD_CS
 * - GPIO10 = SD_MOSI
 * - GPIO20 = SD_CLK
 * - GPIO21 = SD_MISO
 * 
 * CSV format: time_ms,force_raw,force_filtered,logic_batt_v,heater_batt_v,state
 * Keeps last 3 recordings
 */

#ifndef STORAGE_SERVICE_H
#define STORAGE_SERVICE_H

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include "config.h"
#include "app_state.h"

/**
 * @brief Recording metadata structure
 */
struct RecordingMetadata {
    int id;
    char filename[32];
    float peakForce;
    float avgForce;
    uint32_t duration;
    uint32_t sampleCount;
    float logicBattStart;
    float logicBattEnd;
    float heaterBattStart;
    float heaterBattEnd;
    bool valid;
};

/**
 * @brief Storage Service class - SD card logging
 */
class StorageService {
public:
    StorageService() : initialized(false), recordingFile(), currentRecordingId(0),
                       sampleCount(0), forceSum(0.0f), maxForce(0.0f),
                       recordingStartTime(0), logicBattStart(0.0f), heaterBattStart(0.0f) {
        // Initialize metadata array
        for (int i = 0; i < MAX_RECORDINGS; i++) {
            recordings[i].valid = false;
        }
    }
    
    /**
     * @brief Initialize SD card
     */
    bool begin() {
        // Initialize SPI with custom pins
        SPI.begin(PIN_SD_CLK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
        
        // Try to initialize SD card
        if (!SD.begin(PIN_SD_CS, SPI, SD_SPI_FREQUENCY)) {
            DEBUG_PRINT("[STORAGE] ERROR: SD card initialization failed!");
            initialized = false;
            return false;
        }
        
        uint8_t cardType = SD.cardType();
        if (cardType == CARD_NONE) {
            DEBUG_PRINT("[STORAGE] ERROR: No SD card attached!");
            initialized = false;
            return false;
        }
        
        initialized = true;
        DEBUG_PRINT("[STORAGE] Service initialized");
        DEBUG_PRINTF("[STORAGE] Card type: %d, Size: %llu MB\n", cardType, SD.totalBytes() / (1024 * 1024));
        
        // Load existing recordings metadata
        loadMetadata();
        
        return true;
    }
    
    /**
     * @brief Check if SD card is ready
     */
    bool isReady() const {
        return initialized;
    }
    
    /**
     * @brief Start a new recording
     */
    bool startRecording(AppContext& ctx) {
        if (!initialized) {
            DEBUG_PRINT("[STORAGE] Cannot start recording - SD not initialized");
            return false;
        }
        
        // Generate filename
        currentRecordingId = getNextRecordingId();
        snprintf(currentFilename, sizeof(currentFilename), "%s%03d%s", 
                 CSV_FILE_PREFIX, currentRecordingId, CSV_FILE_EXTENSION);
        
        // Open file for writing
        recordingFile = SD.open(currentFilename, FILE_WRITE);
        if (!recordingFile) {
            DEBUG_PRINTF("[STORAGE] ERROR: Cannot create file %s\n", currentFilename);
            return false;
        }
        
        // Write CSV header
        recordingFile.println("time_ms,force_raw,force_filtered,logic_batt_v,heater_batt_v,state");
        recordingFile.flush();
        
        // Initialize recording state
        sampleCount = 0;
        forceSum = 0.0f;
        maxForce = 0.0f;
        recordingStartTime = millis();
        logicBattStart = ctx.logicBatteryVoltage;
        heaterBattStart = ctx.heaterBatteryVoltage;
        
        // Update context
        ctx.isRecording = true;
        ctx.recordingStartTime = recordingStartTime;
        ctx.recordingSampleCount = 0;
        strncpy(ctx.currentRecordingFile, currentFilename, sizeof(ctx.currentRecordingFile) - 1);
        
        DEBUG_PRINTF("[STORAGE] Recording started: %s\n", currentFilename);
        return true;
    }
    
    /**
     * @brief Add sample to current recording
     */
    void addSample(const AppContext& ctx) {
        if (!initialized || !recordingFile) return;
        
        uint32_t elapsed = millis() - recordingStartTime;
        
        // Write CSV line
        recordingFile.printf("%lu,%.2f,%.2f,%.2f,%.2f,%s\n",
                            elapsed,
                            ctx.forceRaw,
                            ctx.forceFiltered,
                            ctx.logicBatteryVoltage,
                            ctx.heaterBatteryVoltage,
                            stateToString(ctx.currentState));
        
        // Update statistics
        sampleCount++;
        forceSum += ctx.forceFiltered;
        if (ctx.forceFiltered > maxForce) {
            maxForce = ctx.forceFiltered;
        }
        
        // Flush periodically (every 50 samples)
        if (sampleCount % 50 == 0) {
            recordingFile.flush();
        }
    }
    
    /**
     * @brief Stop current recording
     */
    void stopRecording(AppContext& ctx) {
        if (!initialized || !recordingFile) return;
        
        // Close file
        recordingFile.close();
        
        // Calculate statistics
        float avgForce = (sampleCount > 0) ? forceSum / sampleCount : 0.0f;
        uint32_t duration = millis() - recordingStartTime;
        
        // Save metadata
        saveRecordingMetadata(currentRecordingId, currentFilename, maxForce, avgForce,
                             duration, sampleCount, logicBattStart, ctx.logicBatteryVoltage,
                             heaterBattStart, ctx.heaterBatteryVoltage);
        
        // Update context
        ctx.isRecording = false;
        ctx.recordingSampleCount = sampleCount;
        
        DEBUG_PRINTF("[STORAGE] Recording stopped: %s, samples: %lu, peak: %.2f\n", 
                    currentFilename, sampleCount, maxForce);
        
        // Cleanup old recordings
        cleanupOldRecordings();
    }
    
    /**
     * @brief Get list of recordings
     */
    int getRecordingCount() const {
        int count = 0;
        for (int i = 0; i < MAX_RECORDINGS; i++) {
            if (recordings[i].valid) count++;
        }
        return count;
    }
    
    /**
     * @brief Get recording by index
     */
    const RecordingMetadata* getRecording(int index) const {
        if (index >= 0 && index < MAX_RECORDINGS && recordings[index].valid) {
            return &recordings[index];
        }
        return nullptr;
    }
    
    /**
     * @brief Get recording by ID
     */
    const RecordingMetadata* getRecordingById(int id) const {
        for (int i = 0; i < MAX_RECORDINGS; i++) {
            if (recordings[i].valid && recordings[i].id == id) {
                return &recordings[i];
            }
        }
        return nullptr;
    }
    
    /**
     * @brief Open recording file for download
     */
    File openRecordingFile(int id) {
        const RecordingMetadata* rec = getRecordingById(id);
        if (rec && initialized) {
            return SD.open(rec->filename, FILE_READ);
        }
        return File();
    }
    
    /**
     * @brief Delete a recording
     */
    bool deleteRecording(int id) {
        const RecordingMetadata* rec = getRecordingById(id);
        if (rec && initialized) {
            SD.remove(rec->filename);
            // Mark as invalid
            for (int i = 0; i < MAX_RECORDINGS; i++) {
                if (recordings[i].id == id) {
                    recordings[i].valid = false;
                    break;
                }
            }
            saveMetadata();
            return true;
        }
        return false;
    }

private:
    bool initialized;
    File recordingFile;
    int currentRecordingId;
    char currentFilename[32];
    
    // Recording statistics
    uint32_t sampleCount;
    float forceSum;
    float maxForce;
    uint32_t recordingStartTime;
    float logicBattStart;
    float heaterBattStart;
    
    // Recordings metadata
    RecordingMetadata recordings[MAX_RECORDINGS];
    
    /**
     * @brief Get next available recording ID
     */
    int getNextRecordingId() {
        int maxId = 0;
        for (int i = 0; i < MAX_RECORDINGS; i++) {
            if (recordings[i].valid && recordings[i].id > maxId) {
                maxId = recordings[i].id;
            }
        }
        return maxId + 1;
    }
    
    /**
     * @brief Save recording metadata
     */
    void saveRecordingMetadata(int id, const char* filename, float peak, float avg,
                               uint32_t duration, uint32_t samples,
                               float logicStart, float logicEnd,
                               float heaterStart, float heaterEnd) {
        // Find slot
        int slot = -1;
        
        // First, try to find an invalid slot
        for (int i = 0; i < MAX_RECORDINGS; i++) {
            if (!recordings[i].valid) {
                slot = i;
                break;
            }
        }
        
        // If no invalid slot, shift all recordings
        if (slot == -1) {
            // Delete oldest file
            if (recordings[0].valid) {
                SD.remove(recordings[0].filename);
            }
            
            // Shift all
            for (int i = 0; i < MAX_RECORDINGS - 1; i++) {
                recordings[i] = recordings[i + 1];
            }
            slot = MAX_RECORDINGS - 1;
        }
        
        // Save metadata
        recordings[slot].id = id;
        strncpy(recordings[slot].filename, filename, sizeof(recordings[slot].filename) - 1);
        recordings[slot].peakForce = peak;
        recordings[slot].avgForce = avg;
        recordings[slot].duration = duration;
        recordings[slot].sampleCount = samples;
        recordings[slot].logicBattStart = logicStart;
        recordings[slot].logicBattEnd = logicEnd;
        recordings[slot].heaterBattStart = heaterStart;
        recordings[slot].heaterBattEnd = heaterEnd;
        recordings[slot].valid = true;
        
        // Save to file
        saveMetadata();
    }
    
    /**
     * @brief Save metadata to SD card
     */
    void saveMetadata() {
        if (!initialized) return;
        
        File metaFile = SD.open(METADATA_FILE, FILE_WRITE);
        if (!metaFile) {
            DEBUG_PRINT("[STORAGE] Warning: Cannot save metadata");
            return;
        }
        
        metaFile.seek(0);
        metaFile.println("{");
        metaFile.println("  \"recordings\": [");
        
        bool first = true;
        for (int i = 0; i < MAX_RECORDINGS; i++) {
            if (recordings[i].valid) {
                if (!first) metaFile.println(",");
                first = false;
                
                metaFile.printf("    {\"id\":%d,\"filename\":\"%s\",\"peak\":%.2f,\"avg\":%.2f,\"duration\":%lu,\"samples\":%lu,\"logicStart\":%.2f,\"logicEnd\":%.2f,\"heaterStart\":%.2f,\"heaterEnd\":%.2f}",
                               recordings[i].id,
                               recordings[i].filename,
                               recordings[i].peakForce,
                               recordings[i].avgForce,
                               recordings[i].duration,
                               recordings[i].sampleCount,
                               recordings[i].logicBattStart,
                               recordings[i].logicBattEnd,
                               recordings[i].heaterBattStart,
                               recordings[i].heaterBattEnd);
            }
        }
        
        metaFile.println();
        metaFile.println("  ]");
        metaFile.println("}");
        metaFile.close();
    }
    
    /**
     * @brief Load metadata from SD card
     */
    void loadMetadata() {
        if (!initialized) return;
        
        // Simple approach: scan for existing CSV files
        File root = SD.open("/");
        if (!root) return;
        
        File file = root.openNextFile();
        while (file) {
            if (!file.isDirectory()) {
                String name = file.name();
                if (name.startsWith(CSV_FILE_PREFIX) && name.endsWith(CSV_FILE_EXTENSION)) {
                    // Parse ID from filename
                    int id = name.substring(5, 8).toInt();
                    if (id > 0) {
                        // Find slot
                        for (int i = 0; i < MAX_RECORDINGS; i++) {
                            if (!recordings[i].valid) {
                                recordings[i].id = id;
                                strncpy(recordings[i].filename, file.path(), sizeof(recordings[i].filename) - 1);
                                recordings[i].valid = true;
                                // Note: peak, avg, etc. will be 0 for loaded recordings
                                break;
                            }
                        }
                    }
                }
            }
            file = root.openNextFile();
        }
        
        DEBUG_PRINTF("[STORAGE] Loaded %d recordings\n", getRecordingCount());
    }
    
    /**
     * @brief Cleanup old recordings (keep only MAX_RECORDINGS)
     */
    void cleanupOldRecordings() {
        int count = getRecordingCount();
        if (count <= MAX_RECORDINGS) return;
        
        // Sort by ID and remove oldest
        // Simple approach: remove recordings with lowest IDs
        for (int i = 0; i < MAX_RECORDINGS && count > MAX_RECORDINGS; i++) {
            if (recordings[i].valid) {
                SD.remove(recordings[i].filename);
                recordings[i].valid = false;
                count--;
            }
        }
    }
};

#endif // STORAGE_SERVICE_H
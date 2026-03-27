// in src/storage_manager.cpp (FINAL VERSION)

#include "storage_manager.h"
#include <EEPROM.h>
#include <LittleFS.h>
#include <IPAddress.h>

// --- EEPROM Layout Definitions (Private to this module) ---
#define WIFI_CONFIG_START 0
#define EEPROM_TIMERS_COUNT_ADDRESS 100
#define EEPROM_SETTINGS_START_ADDRESS 105
#define ACTIVE_DAYS_ADDRESS 200
#define MQTT_IP_ADDRESS 230
#define TIMEZONE_OFFSET_ADDRESS 250
#define USE_DST_FLAG_ADDRESS 251
#define EEPROM_LAST_VALID_TIME_ADDRESS 260
#define MQTT_USER_ADDRESS 264   // 64 bytes: 264-327
#define MQTT_PASS_ADDRESS 328  // 64 bytes: 328-391
// ---

const int EEPROM_SIZE = 1300;

// Helper function
uint8_t calculateChecksum(const WiFiConfig& config) {
    uint8_t checksum = 0;
    const uint8_t* data = (const uint8_t*)&config;
    for (size_t i = 0; i < sizeof(WiFiConfig) - 1; i++) {
        checksum ^= data[i];
    }
    return checksum;
}

void storage_init() {
    EEPROM.begin(EEPROM_SIZE);
}

// --- Settings Functions (Unchanged) ---
void storage_save_settings(int numberOfTimers, TriggerTime triggerDays[], int days[]) {
    EEPROM.put(EEPROM_TIMERS_COUNT_ADDRESS, numberOfTimers);
    EEPROM.writeBytes(EEPROM_SETTINGS_START_ADDRESS, (const byte*)triggerDays, sizeof(TriggerTime) * MAX_TIMERS);

    
    // CORRECTED: Use writeBytes to save the array explicitly
    EEPROM.writeBytes(ACTIVE_DAYS_ADDRESS, days, sizeof(int) * DAYS_IN_WEEK);

    EEPROM.commit();
    Serial.println("Settings saved to EEPROM");
}

void storage_load_settings(int& numberOfTimers, TriggerTime triggerDays[], int days[]) {
    EEPROM.get(EEPROM_TIMERS_COUNT_ADDRESS, numberOfTimers);
    if (numberOfTimers < 1 || numberOfTimers > MAX_TIMERS) {
        numberOfTimers = 2; // Default
    }

    EEPROM.readBytes(EEPROM_SETTINGS_START_ADDRESS, (byte*)triggerDays, sizeof(TriggerTime) * MAX_TIMERS);

    // Validate and fix loaded timer settings
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (triggerDays[i].hour >= 24 || triggerDays[i].minute >= 60 ||
            triggerDays[i].activationDuration < 1 || triggerDays[i].delayBetweenActivations < 60 ||
            triggerDays[i].repetitions < 0) {
            // Set default values for invalid timer
            triggerDays[i].hour = 8;  // 8 AM
            triggerDays[i].minute = 0;
            triggerDays[i].second = 0;
            triggerDays[i].activationDuration = 60;  // 1 minute in seconds
            triggerDays[i].delayBetweenActivations = 3600;  // 1 hour in seconds
            triggerDays[i].repetitions = 1;
            Serial.printf("Timer %d had invalid settings, set to defaults\n", i);
        }
    }

    // CORRECTED: Use readBytes to load the array explicitly
    EEPROM.readBytes(ACTIVE_DAYS_ADDRESS, days, sizeof(int) * DAYS_IN_WEEK);

    // Validate days array
    for (int i = 0; i < DAYS_IN_WEEK; i++) {
        if (days[i] != 0 && days[i] != 1) {
            days[i] = (i < 5) ? 1 : 0;  // Mon-Fri enabled, Sat-Sun disabled
        }
    }

    Serial.println("Settings loaded from EEPROM");
}

void storage_save_number_of_timers(int count) {
    EEPROM.put(EEPROM_TIMERS_COUNT_ADDRESS, count);
    EEPROM.commit();
}

// --- WiFi Functions (Unchanged) ---
bool storage_save_wifi_config(const char* ssid, const char* password) {
    WiFiConfig config;
    memset(&config, 0, sizeof(WiFiConfig));
    config.configured = 1;
    strncpy(config.ssid, ssid, sizeof(config.ssid) - 1);
    strncpy(config.password, password, sizeof(config.password) - 1);
    config.checksum = calculateChecksum(config);
    EEPROM.put(WIFI_CONFIG_START, config);
    return EEPROM.commit();
}

bool storage_load_wifi_config(WiFiConfig& config) {
    EEPROM.get(WIFI_CONFIG_START, config);
    if (config.configured != 1) return false;
    return (calculateChecksum(config) == config.checksum);
}

void storage_clear_wifi_config() {
    WiFiConfig config;
    memset(&config, 0, sizeof(WiFiConfig));
    EEPROM.put(WIFI_CONFIG_START, config);
    EEPROM.commit();
}

// --- Timezone Functions (Unchanged) ---
void storage_save_timezone(int8_t tz, bool use_dst) {
    EEPROM.write(TIMEZONE_OFFSET_ADDRESS, tz);
    EEPROM.write(USE_DST_FLAG_ADDRESS, use_dst ? 1 : 0);
    EEPROM.commit();
}

void storage_load_timezone(int8_t& tz, bool& use_dst) {
    tz = (int8_t)EEPROM.read(TIMEZONE_OFFSET_ADDRESS);
    if (tz < -12 || tz > 14) tz = 0; // Validate
    use_dst = (EEPROM.read(USE_DST_FLAG_ADDRESS) == 1);
}

// --- MQTT Functions (Unchanged) ---
bool storage_save_mqtt_config(const char* ip, const char* user, const char* pass) {
    if (ip == nullptr || strlen(ip) >= MAX_IP_LENGTH) return false;

    IPAddress mqttIP;
    if (!mqttIP.fromString(ip)) {
        Serial.printf("storage_save_mqtt_config: invalid IP '%s'\n", ip);
        return false;
    }

    char buf[MAX_IP_LENGTH] = {};
    strncpy(buf, ip, MAX_IP_LENGTH - 1);
    EEPROM.writeBytes(MQTT_IP_ADDRESS, buf, MAX_IP_LENGTH);

    char userBuf[64] = {};
    if (user != nullptr) strncpy(userBuf, user, sizeof(userBuf) - 1);
    EEPROM.writeBytes(MQTT_USER_ADDRESS, userBuf, sizeof(userBuf));

    char passBuf[64] = {};
    if (pass != nullptr) strncpy(passBuf, pass, sizeof(passBuf) - 1);
    EEPROM.writeBytes(MQTT_PASS_ADDRESS, passBuf, sizeof(passBuf));

    bool ok = EEPROM.commit();
    Serial.printf("MQTT config saved: ip=%s user=%s (%s)\n", ip, userBuf, ok ? "OK" : "FAILED");
    return ok;
}

bool storage_load_mqtt_config(char* ip_buffer, int ip_size, char* user_buffer, int user_size, char* pass_buffer, int pass_size) {
    EEPROM.readBytes(MQTT_IP_ADDRESS, ip_buffer, ip_size);
    ip_buffer[ip_size - 1] = '\0';

    if (user_buffer && user_size > 0) {
        EEPROM.readBytes(MQTT_USER_ADDRESS, user_buffer, user_size);
        user_buffer[user_size - 1] = '\0';
    }
    if (pass_buffer && pass_size > 0) {
        EEPROM.readBytes(MQTT_PASS_ADDRESS, pass_buffer, pass_size);
        pass_buffer[pass_size - 1] = '\0';
    }

    IPAddress mqttIP;
    if (mqttIP.fromString(ip_buffer) && mqttIP[0] != 0) {
        Serial.printf("Loaded MQTT config: ip=%s user=%s\n", ip_buffer, user_buffer ? user_buffer : "");
        return true;
    }

    Serial.println("No valid MQTT IP found in EEPROM.");
    ip_buffer[0] = '\0';
    return false;
}

// --- NEW FILE-BASED LOGGING FUNCTIONS ---

void storage_log_to_file(const LogEntry& log) {
    File logFile = LittleFS.open("/activity.log", "a"); // "a" opens for appending
    if (!logFile) {
        Serial.println("Failed to open activity.log for writing");
        return;
    }
    logFile.write((uint8_t*)&log, sizeof(LogEntry));
    logFile.close();
}

int storage_load_logs_from_file(LogEntry logs[], int max_logs) {
    Serial.println("\n--- [storage_manager] DEBUG: Starting storage_load_logs_from_file ---");
    
    File logFile = LittleFS.open("/activity.log", "r");
    if (!logFile) {
        Serial.println("  -> DEBUG: open() failed. No /activity.log file found.");
        Serial.println("--- [storage_manager] DEBUG: End. Returning 0 logs. ---\n");
        return 0;
    }

    size_t file_size = logFile.size();
    Serial.printf("  -> DEBUG: File found. Size is %d bytes.\n", file_size);

    if (file_size == 0) {
        Serial.println("  -> DEBUG: Log file is empty.");
        logFile.close();
        Serial.println("--- [storage_manager] DEBUG: End. Returning 0 logs. ---\n");
        return 0;
    }

    if (file_size % sizeof(LogEntry) != 0) {
        Serial.printf("  -> DEBUG: WARNING! Log file size (%d) is not a multiple of LogEntry size (%d). File may be corrupt.\n", file_size, sizeof(LogEntry));
    }

    int total_entries = file_size / sizeof(LogEntry);
    int entries_to_read = min(total_entries, max_logs);
    Serial.printf("  -> DEBUG: Total entries in file: %d. Will read up to %d entries.\n", total_entries, entries_to_read);

    size_t start_position = (total_entries > max_logs) ? (file_size - (max_logs * sizeof(LogEntry))) : 0;
    Serial.printf("  -> DEBUG: Seeking to file position %d to read the last %d entries.\n", start_position, entries_to_read);
    
    logFile.seek(start_position, SeekSet);

    size_t bytes_to_read = entries_to_read * sizeof(LogEntry);
    Serial.printf("  -> DEBUG: Attempting to read %d bytes into RAM buffer.\n", bytes_to_read);

    size_t bytes_read = logFile.read((uint8_t*)logs, bytes_to_read);
    Serial.printf("  -> DEBUG: Actually read %d bytes.\n", bytes_read);

    logFile.close();

    if (bytes_read != bytes_to_read) {
        Serial.println("  -> DEBUG: ERROR! Number of bytes read does not match expected. Returning 0.");
        Serial.println("--- [storage_manager] DEBUG: End. Returning 0 logs. ---\n");
        return 0;
    }
    
    Serial.printf("  -> DEBUG: Successfully loaded %d entries into RAM.\n", entries_to_read);
    Serial.println("--- [storage_manager] DEBUG: End. Returning final count. ---\n");
    return entries_to_read;
}

void storage_verify_layout() {
    Serial.println("\nEEPROM Layout Verification:");
    Serial.printf("WiFiConfig size: %d bytes\n", sizeof(WiFiConfig));
    Serial.printf("WiFiConfig start: %d\n", WIFI_CONFIG_START);
    Serial.printf("WiFiConfig end: %d\n", WIFI_CONFIG_START + sizeof(WiFiConfig));
    Serial.printf("Gap until Timer settings: %d bytes\n", 
                 EEPROM_TIMERS_COUNT_ADDRESS - (WIFI_CONFIG_START + sizeof(WiFiConfig)));
    
    // Verify no overlap with timer settings
    if (WIFI_CONFIG_START + sizeof(WiFiConfig) > EEPROM_TIMERS_COUNT_ADDRESS) {
        Serial.println("ERROR: WiFi config overlaps with timer settings!");
    } else {
        Serial.println("EEPROM layout verified OK");
    }
}

void storage_save_time(unsigned long unix_time) {
    if (unix_time > 0) {
        EEPROM.put(EEPROM_LAST_VALID_TIME_ADDRESS, unix_time);
        EEPROM.commit();
    }
}

void storage_load_time(unsigned long& unix_time) {
    EEPROM.get(EEPROM_LAST_VALID_TIME_ADDRESS, unix_time);
    // Check if the loaded time is invalid or uninitialized (0xFFFFFFFF)
    if (unix_time == 0 || unix_time == 0xFFFFFFFF) {
        unix_time = 1704067200; // Default to Jan 1, 2024 if nothing is saved
        Serial.println("No valid time in EEPROM, using default.");
    }
}

#ifdef ENABLE_DEBUG_UTILS
void storage_dump_eeprom(int start, int end) {
    Serial.printf("EEPROM dump from %d to %d:\n", start, end);
    for (int i = start; i <= end; i++) {
        if ((i - start) % 16 == 0) Serial.println();
        Serial.printf("%02X ", EEPROM.read(i));
    }
    Serial.println();
}
#endif
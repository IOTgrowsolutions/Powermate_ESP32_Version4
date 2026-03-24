// in include/storage_manager.h (FINAL VERSION)

#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include <Arduino.h>
#include "project_types.h" // For LogEntry, TriggerTime, etc.

// --- Public Function Declarations ---

void storage_init();

// WiFi
bool storage_load_wifi_config(WiFiConfig& config);
bool storage_save_wifi_config(const char* ssid, const char* password);
void storage_clear_wifi_config();

// Timers & Settings
void storage_save_settings(int numberOfTimers, TriggerTime triggerDays[], int days[]);
void storage_load_settings(int& numberOfTimers, TriggerTime triggerDays[], int days[]);
void storage_save_number_of_timers(int count);
//TIME
void storage_save_time(unsigned long unix_time);
void storage_load_time(unsigned long& unix_time); 
// Timezone
void storage_save_timezone(int8_t tz, bool use_dst);
void storage_load_timezone(int8_t& tz, bool& use_dst);

// MQTT
bool storage_save_mqtt_config(const char* ip);
bool storage_load_mqtt_config(char* ip_buffer, int buffer_size);

// --- NEW FILE-BASED LOGGING FUNCTIONS ---
void storage_log_to_file(const LogEntry& log);
int storage_load_logs_from_file(LogEntry logs[], int max_logs);
void storage_verify_layout();

#endif // STORAGE_MANAGER_H
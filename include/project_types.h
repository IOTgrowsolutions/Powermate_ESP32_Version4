// in include/project_types.h (FINAL and COMPLETE version)

#ifndef PROJECT_TYPES_H
#define PROJECT_TYPES_H

#include <Arduino.h>

// --- SHARED CONSTANTS ---
const int MAX_TIMERS = 6;
const int MAX_SSID_LENGTH = 32;
const int MAX_PASSWORD_LENGTH = 64;
const int MAX_LOG_ENTRIES = 100;
const int MAX_IP_LENGTH = 16;

const int DAYS_IN_WEEK = 7;
// --- SHARED DATA STRUCTURES ---

#pragma pack(push, 1)

struct WiFiConfig {
    uint8_t configured;
    char ssid[32];
    char password[64];
    uint8_t checksum;
};

struct TriggerTime {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    unsigned long activationDuration;
    unsigned long delayBetweenActivations;
    int repetitions;
};

struct LogEntry {
    uint32_t unix_time;
    bool relayState;
};

#pragma pack(pop)

// This struct defines the RUNTIME state of a timer.
struct TriggerState {
    bool active;
    bool completedForCurrentTime;
    int activations;
    uint32_t startTime;
    uint32_t actionTime;
    uint32_t lastDeactivationTime;
    bool firstActivationPending;
    uint32_t lastCompletionTime;
};

#endif // PROJECT_TYPES_H
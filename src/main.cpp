#include <Arduino.h>
#include <ArduinoOTA.h>
#include <LittleFS.h>
#include <mqtt_client.h>
#include "wifi_manager.h"
#include "storage_manager.h"
#include "rtc_handler.h"
#include "web_server.h"
#include "project_types.h"
#include "esp_task_wdt.h" // Include the ESP32 Watchdog Timer library
#include "esp_timer.h"
#include "esp_err.h"

// Timers
int numberOfTimers = 2;
const int MIN_TIMERS = 1;
int safeNumberOfTimers() {
    return min(max(numberOfTimers, MIN_TIMERS), MAX_TIMERS);
}

// Relay settings
const int relayPin = 16; //ESP32_AC = 16 or 33

// Cached RTC time to avoid blocking I2C in timer callbacks
DateTime lastCachedTime = DateTime(2000, 1, 1, 0, 0, 0);  // Initialize to epoch (Jan 1, 2000)
bool rtcCacheValid = false;

TriggerTime triggerDays[MAX_TIMERS];
TriggerState triggerStates[MAX_TIMERS];

LogEntry relayLog[MAX_LOG_ENTRIES];

const char* getDayOfWeek(int dayIndex) {
    const char* days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    return days[dayIndex];
}

int days[7] = {0}; // 0 = Sunday, 1 = Monday, ..., 6 = Saturday
int logIndex = 0;
// Device control variables
bool deviceIsOn = false;
bool bypassModeEnabled = false;
const unsigned long CHECK_INTERVAL = 100; // Check every 100ms
volatile bool pendingTimerCheck = false;
volatile bool rtcCacheInitialized = false;

// MQTT Configuration
char mqttServer[MAX_IP_LENGTH] = "";
const int mqttPort = 1883;
char mqttUser[64] = "";     // Optional — leave empty if broker has no auth
char mqttPassword[64] = ""; // Optional — leave empty if broker has no auth
const char* mqttTopic = "pm/control"; // Topic to listen for "on" and "off" messages
bool shouldAttemptMqttConnection = false; // Flag to trigger MQTT connection attempts
bool mqttConnected = false; // Track MQTT connection status
esp_timer_handle_t relay_timer;

esp_mqtt_client_handle_t mqttClient = NULL;

// Function declarations

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
void syncDeviceState();
void setupOTA();
void resetTriggerState(int i);
bool turnDeviceOn();
bool turnDeviceOff();
void checkAndActivateTimers(const DateTime& now);
void resetActivationsOnDayChange(const DateTime& now);
bool readRelayState();
bool shouldActivateTimer(int idx, const DateTime& now);
bool shouldDeactivateTimer(int idx, const DateTime& now);
void setupMQTT();
void add_log_to_ram_buffer(const LogEntry& newLog);
void relayCheckCallback(void* arg);
void wifi_notify_got_ip() ;

void wifi_notify_got_ip() {
    Serial.println("IP confirmed — triggering NTP sync");
    if (rtc_sync_ntp()) {
        Serial.println("RTC updated from NTP successfully");
    } else {
        Serial.println("NTP sync failed — RTC keeps existing time");
    }
    shouldAttemptMqttConnection = true;
}

void add_log_to_ram_buffer(const LogEntry& newLog) {
    if (logIndex >= MAX_LOG_ENTRIES) {
        // Buffer is full, shift all entries one step to the left to make room
        // This discards the oldest entry (at index 0)
        for (int i = 0; i < MAX_LOG_ENTRIES - 1; i++) {
            relayLog[i] = relayLog[i + 1];
        }
        // Place the new log at the very end of the array
        relayLog[MAX_LOG_ENTRIES - 1] = newLog;
    } else {
        // Buffer has space, just add the new log and increment the count
        relayLog[logIndex] = newLog;
        logIndex++;
    }
}

void setup() {
    esp_task_wdt_init(30, true);  // 30s timeout, true = panic/reboot on trigger
    esp_task_wdt_add(NULL);       // Watch the current (main Arduino) task
    esp_log_level_set("*", ESP_LOG_WARN); // Show only warnings and errors
    Serial.begin(115200);
    pinMode(relayPin, OUTPUT);
    delay(50);
    digitalWrite(relayPin, LOW);
    Serial.println(digitalRead(relayPin));
    if (!LittleFS.begin()) {
        Serial.println("FATAL ERROR: Failed to mount LittleFS. Log and web page will not work.");
    } else {
        Serial.println("LittleFS mounted successfully.");
    }
    storage_init();         
    rtc_init();
    storage_load_settings(numberOfTimers, triggerDays, days);
    logIndex = storage_load_logs_from_file(relayLog, MAX_LOG_ENTRIES);
    storage_load_mqtt_config(mqttServer, MAX_IP_LENGTH, mqttUser, sizeof(mqttUser), mqttPassword, sizeof(mqttPassword));
    for (int i = 0; i < safeNumberOfTimers(); i++) {
        resetTriggerState(i);
    }
    
    Serial.printf("Number of timers: %d\n", safeNumberOfTimers());
    for (int i = 0; i < safeNumberOfTimers(); i++) {
        Serial.printf("Timer %d: %02d:%02d, Duration: %lu, Delay: %lu, Repetitions: %d\n",
                      i+1, triggerDays[i].hour, triggerDays[i].minute,
                      triggerDays[i].activationDuration, triggerDays[i].delayBetweenActivations,
                      triggerDays[i].repetitions);
    }

    Serial.print("Active days: ");
    for (int i = 0; i < 7; i++) {
        Serial.printf("%d ", days[i]);
    }
    Serial.println();

    wifi_setup();
    
    web_server_init(); 
    Serial.println("Web server created");
    Serial.println("Setup complete");

    const esp_timer_create_args_t timer_args = {
        .callback = &relayCheckCallback,
        .name = "relay_check"
    };
    esp_timer_create(&timer_args, &relay_timer);
    esp_timer_start_periodic(relay_timer, CHECK_INTERVAL * 1000); // µs
}

void loop() {
    esp_task_wdt_reset();
    wifi_check_status();  // Monitors connection, triggers reconnect if lost
    if (wifi_is_connected()) {
        ArduinoOTA.handle();
    }
    web_server_handle_client();


    // Process deferred relay actions from timer callback
    if (pendingTimerCheck) {
        pendingTimerCheck = false;
        checkAndActivateTimers(lastCachedTime);
    }
    
    if (shouldAttemptMqttConnection) {
        if (!wifi_is_connected()) {
            // Silently skip — will retry next loop iteration
        } else if (mqttServer == nullptr || strlen(mqttServer) == 0) {
            Serial.println("MQTT skipped: no valid server address configured");
            shouldAttemptMqttConnection = false; 
        } else {
            Serial.println("Starting MQTT connection...");
            setupMQTT();
            shouldAttemptMqttConnection = false;
        }
    } 
    
    // Update cached RTC time with timeout protection
    // Only attempt RTC read if enough time has passed since last update
    static unsigned long lastRtcReadTime = 0;
    
    if (!rtcCacheInitialized || (millis() - lastRtcReadTime > 1000)) {  // Update every 1 second instead of 5
        lastRtcReadTime = millis();
        rtcCacheInitialized = true;
        
        lastCachedTime = rtc_get_current_time();
        rtcCacheValid = rtc_is_ok() && lastCachedTime.isValid();
        
        if (rtcCacheValid) {
            esp_task_wdt_reset();
        } else {
            Serial.println("[WARNING] Failed to cache RTC time");
        }
    }
}   

void relayCheckCallback(void* arg) {
    // Safe to do: read cached state and set atomic flags only
    syncDeviceState();

    static unsigned long lastRtcUpdate = 0;
    unsigned long now_ms = millis();
    if (!rtcCacheInitialized || (now_ms - lastRtcUpdate > 1000)) {
        lastRtcUpdate = now_ms;
        lastCachedTime = rtc_get_current_time();
        rtcCacheValid = rtc_is_ok() && lastCachedTime.isValid();
        rtcCacheInitialized = true;
    }

    if (rtcCacheValid) {
        // Signal loop() to run full timer logic
        for (int i = 0; i < safeNumberOfTimers(); i++) {
            if (!triggerStates[i].active && shouldActivateTimer(i, lastCachedTime)) {
                pendingTimerCheck = true;
            } else if (triggerStates[i].active && shouldDeactivateTimer(i, lastCachedTime)) {
                pendingTimerCheck = true;
            }
        }
    }
}

void setupMQTT() {
    Serial.printf("Connecting to MQTT Server: %s:%d\n", mqttServer, mqttPort);

     esp_mqtt_client_config_t mqtt_cfg = {
        .host = mqttServer,
        .port = (uint32_t)mqttPort,
        .client_id = "powermateClient",
        .username = strlen(mqttUser) > 0 ? mqttUser : nullptr,
        .password = strlen(mqttPassword) > 0 ? mqttPassword : nullptr,
    };

    // Guard: destroy existing client before creating a new one
    if (mqttClient != NULL) {
        esp_mqtt_client_stop(mqttClient);
        esp_mqtt_client_destroy(mqttClient);
        mqttClient = NULL;
    }

    mqttClient = esp_mqtt_client_init(&mqtt_cfg);
    if (mqttClient == NULL) {
        Serial.println("MQTT ERROR: esp_mqtt_client_init failed — out of memory?");
        return;
    }

    esp_err_t err = esp_mqtt_client_register_event(mqttClient,
        (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    if (err != ESP_OK) {
        Serial.printf("MQTT ERROR: register_event failed: %d\n", err);
        esp_mqtt_client_destroy(mqttClient);
        mqttClient = NULL;
        return;
    }

    err = esp_mqtt_client_start(mqttClient);
    if (err != ESP_OK) {
        Serial.printf("MQTT ERROR: client_start failed: %d\n", err);
        esp_mqtt_client_destroy(mqttClient);
        mqttClient = NULL;
        return;
    }

    Serial.println("MQTT client started successfully");
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            Serial.println("Connected to MQTT broker");
            mqttConnected = true;
            if (mqttClient != NULL) {
                esp_mqtt_client_subscribe(mqttClient, mqttTopic, 0);
                Serial.printf("Subscribed to topic: %s\n", mqttTopic);
            }
            break;
        case MQTT_EVENT_DISCONNECTED:
            Serial.println("Disconnected from MQTT broker");
            mqttConnected = false;
            break;
        case MQTT_EVENT_DATA:
            {
                String topic = String(event->topic, event->topic_len);
                String message = String(event->data, event->data_len);
                Serial.printf("MQTT message received on topic %s: %s\n", topic.c_str(), message.c_str());

                if (topic == mqttTopic) {
                    if (message == "on") {
                        if (turnDeviceOn()) {
                            Serial.println("Device turned on via MQTT");
                        } else {
                            Serial.println("Failed to turn device on via MQTT");
                        }
                    } else if (message == "off") {
                        if (turnDeviceOff()) {
                            Serial.println("Device turned off via MQTT");
                        } else {
                            Serial.println("Failed to turn device off via MQTT");
                        }
                    } else {
                        Serial.println("Invalid MQTT message");
                    }
                }
            }
            break;
        default:
            break;
    }
}

void syncDeviceState() {
    bool actualState = readRelayState();
    if (deviceIsOn != actualState) {
        deviceIsOn = actualState;
        Serial.println("Device state synced. Actual state: " + String(actualState ? "ON" : "OFF"));
    }
}

bool readRelayState() {
    return digitalRead(relayPin) == HIGH;
}

bool shouldActivateTimer(int idx, const DateTime& now)
{
    //Serial.printf("[DEBUG] shouldActivateTimer called for Timer %d at time %02d:%02d\n", idx, now.hour(), now.minute());
    const TriggerTime& t = triggerDays[idx];
    TriggerState& s = triggerStates[idx];

    if (s.activations >= t.repetitions)
       // Serial.printf("[DEBUG] Timer %d: Max repetitions (%d) reached, skipping.\n", idx, t.repetitions);
        return false;

    if (s.firstActivationPending) {
        bool timeMatch = now.hour() == t.hour && now.minute() == t.minute;
        //Serial.printf("[DEBUG] Timer %d: First activation check - Time match: %s (Now: %02d:%02d, Scheduled: %02d:%02d)\n", 
        //              idx, timeMatch ? "YES" : "NO", now.hour(), now.minute(), t.hour, t.minute);
        return timeMatch;
    }

    // Subsequent activations: check delay
    if (s.lastDeactivationTime == 0) {
            //  Serial.printf("[DEBUG] Timer %d: No last deactivation time, skipping delay check.\n", idx);
                return false;
    }
        uint32_t nowu = now.unixtime();
        uint32_t last = s.lastDeactivationTime;
        uint32_t elapsed = nowu >= last ? nowu - last : (uint32_t)(nowu + (UINT32_MAX - last) + 1);
        bool delayElapsed = elapsed >= t.delayBetweenActivations;
        //Serial.printf("[DEBUG] Timer %d: Delay check - now=%lu, lastDeact=%lu, Elapsed: %lu sec, Delay: %lu sec, Delay elapsed: %s\n",
        //                            idx, nowu, last, elapsed, t.delayBetweenActivations, delayElapsed ? "YES" : "NO");
        return delayElapsed;
}

bool shouldDeactivateTimer(int idx, const DateTime& now)
{
    if (!triggerStates[idx].active) return false;

    // Prevent immediate deactivation if activationDuration is 0 or invalid
    uint32_t duration = triggerDays[idx].activationDuration;
    if (duration == 0) return false;  // Never deactivate if duration is 0

    uint32_t nowu = now.unixtime();
    uint32_t start = triggerStates[idx].startTime;
    uint32_t elapsed = nowu >= start ? nowu - start : (uint32_t)(nowu + (UINT32_MAX - start) + 1);
    //Serial.printf("[DEBUG] Timer %d deactivate check: now=%lu, start=%lu, elapsed=%lu, duration=%lu\n",
    //              idx, nowu, start, elapsed, duration);
    return elapsed >= duration;
}

void checkAndActivateTimers(const DateTime& now) {
    //Serial.println("Starting checkAndActivateTimers");

    if (bypassModeEnabled) {
        Serial.println("[DEBUG] Bypass mode enabled, skipping timer checks.");
        return;
    }
    if (!rtc_is_ok()) {
        Serial.println("[DEBUG] RTC not OK, skipping timer checks.");
        return;
    }
    if (!now.isValid()) {
        Serial.println("[DEBUG] Invalid RTC time, skipping.");
        return;
    }

    resetActivationsOnDayChange(now);

    int currentDay = now.dayOfTheWeek();
   
    
    //Serial.printf("[DEBUG] Current time: %02d:%02d:%02d, Day: %d\n", curHour, curMin, currentDay);

    for (int i  = 0; i < safeNumberOfTimers(); ++i) {
        //Serial.printf("[DEBUG] Checking Timer %d: Active=%s, Day enabled=%d\n", 
        //              i, triggerStates[i].active ? "YES" : "NO", days[currentDay]);
        
        if (days[currentDay] == 0) {
            Serial.printf("[DEBUG] Timer %d: Day not enabled, skipping.\n", i);
            continue;
        }

        if (!triggerStates[i].active) {
            bool shouldActivate = shouldActivateTimer(i, now);
            Serial.printf("[DEBUG] Timer %d: Should activate? %s\n", i, shouldActivate ? "YES" : "NO");
            if (shouldActivate) {
                Serial.println("Conditions met for activation.");
                if (turnDeviceOn()) {
                    triggerStates[i].active = true;
                    triggerStates[i].startTime = now.unixtime();
                    triggerStates[i].activations++;
                    triggerStates[i].firstActivationPending = false;
                    Serial.printf("Timer %d activated. Activation count: %d\n", i, triggerStates[i].activations);
                }
            }
        } else {
            uint32_t nowu = now.unixtime();
            uint32_t start = triggerStates[i].startTime;
            uint32_t activationDuration = nowu >= start ? nowu - start : (uint32_t)(nowu + (UINT32_MAX - start) + 1);
            uint32_t requiredDuration = triggerDays[i].activationDuration; // Stored in seconds
            Serial.printf("[DEBUG] Timer %d deactivation check: now=%lu, start=%lu, elapsed=%lu, required=%lu\n",
                         i, nowu, start, activationDuration, requiredDuration);
            if (activationDuration >= requiredDuration) {
                if (turnDeviceOff()) {
                    triggerStates[i].active = false;
                    triggerStates[i].lastDeactivationTime = now.unixtime();
                    Serial.printf("Timer %d deactivated after %lu seconds.\n", i, activationDuration);  }
            }
        }
    }
}

bool turnDeviceOn() {
    //Serial.printf("[DEBUG] Attempting to turn relay ON (pin %d)...\n", relayPin);
    digitalWrite(relayPin, HIGH);
    Serial.println(digitalRead(relayPin));
    delay(50);
    bool success = readRelayState() == true;
    if (success) {
      deviceIsOn = true;
      // CORRECTED: Create a log entry and pass it to the storage manager
      LogEntry newLog;
      newLog.unix_time = rtc_get_current_time().unixtime(); // Get current time
      newLog.relayState = true;
      strlcpy(newLog.action, "ON", sizeof(newLog.action));
      newLog.duration = 0; // Duration will be calculated when reading logs
      Serial.println("[main.cpp] Requesting to log ON action..."); 
      storage_log_to_file(newLog);// First, write the log to the permanent file
      add_log_to_ram_buffer(newLog);// THEN, update the live RAM buffer for the web page
    }
    return success;
}

bool turnDeviceOff() {
    digitalWrite(relayPin, LOW);
    Serial.println(digitalRead(relayPin));
    delay(50);
    bool success = readRelayState() == false;
    if (success) {
      deviceIsOn = false;
      // CORRECTED: Create a log entry and pass it to the storage manager
      LogEntry newLog;
      newLog.unix_time = rtc_get_current_time().unixtime(); // Get current time
      newLog.relayState = false;
      strlcpy(newLog.action, "OFF", sizeof(newLog.action));
      newLog.duration = 0; // Duration will be calculated when reading logs
      Serial.println("[main.cpp] Requesting to log OFF action..."); 
      storage_log_to_file(newLog); // First, write the log to the permanent file
      add_log_to_ram_buffer(newLog); // THEN, update the live RAM buffer for the web page
    } else
    {
      Serial.println("Failed to turn device off.");
    }
    return success;
}

void resetActivationsOnDayChange(const DateTime& now) {
    static int lastCheckedDay = -1;
    int currentDay = now.dayOfTheWeek();  // use passed-in time, not a fresh RTC read
    
    if (lastCheckedDay != currentDay && lastCheckedDay != -1) {
        Serial.printf("Day change detected. Old day: %d, New day: %d\n", lastCheckedDay, currentDay);
        for (int i = 0; i < safeNumberOfTimers(); i++) {
            triggerStates[i].activations = 0;
            triggerStates[i].firstActivationPending = true;
            triggerStates[i].lastDeactivationTime = 0;
            if (triggerStates[i].active) {
                turnDeviceOff();
                triggerStates[i].active = false;
                Serial.printf("Timer %d stopped due to day change.\n", i);
            }
        }
        lastCheckedDay = currentDay;
    } else if (lastCheckedDay == -1) {
        lastCheckedDay = currentDay; // initialise on first call
    }
}

void resetTriggerState(int i) {
    triggerStates[i].active = false;
    triggerStates[i].activations = 0;
    triggerStates[i].startTime = 0;
    triggerStates[i].lastDeactivationTime = 0;
    triggerStates[i].firstActivationPending = true;
    Serial.printf("Reset state for Timer %d\n", i);
}

void setupOTA() {
    static bool otaInitialised = false;
    if (otaInitialised) {
        Serial.println("OTA already initialised, skipping");
        return;
    }

    ArduinoOTA.setHostname("PowerMate-ESP32");
    ArduinoOTA.onStart([]() {
        String type = ArduinoOTA.getCommand() == U_FLASH ? "sketch" : "filesystem";
        Serial.println("OTA Start: " + type);
        // Stop MQTT cleanly before OTA overwrites firmware
        if (mqttClient != NULL) {
            esp_mqtt_client_stop(mqttClient);
        }
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("OTA End");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("OTA Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("OTA Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR)    Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR)   Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR)     Serial.println("End Failed");
    });
    ArduinoOTA.begin();
    otaInitialised = true;
    Serial.println("OTA initialised");
}

void wifi_notify_ap_ready() {
    Serial.println("AP ready — starting OTA");
    setupOTA();
}
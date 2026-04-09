#include "web_server.h"
#include "storage_manager.h"
#include "rtc_handler.h"
#include <WebServer.h>
#include <WiFi.h>      // For WiFi.localIP()
#include <Update.h>    // For firmware updates
#include <set>
#include "project_types.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "wifi_manager.h"
#include "RequestLimiter.h"
// --- Shared state owned by main.cpp ---
extern int numberOfTimers;
extern TriggerTime triggerDays[];
extern TriggerState triggerStates[];
extern int days[];
extern LogEntry relayLog[];
extern int logIndex;
extern bool deviceIsOn;
extern bool bypassModeEnabled;
extern bool shouldAttemptMqttConnection;
extern bool mqttConnected;
extern char mqttServer[16];
extern const int mqttPort;
extern const char* mqttTopic;
extern char mqttUser[64];
extern char mqttPassword[64];


// --- Functions owned by main.cpp ---
extern bool turnDeviceOn();
extern bool turnDeviceOff();
extern int safeNumberOfTimers();
extern bool readRelayState();
extern void resetTriggerState(int i);
extern const char* getDayOfWeek(int dayIndex);
extern void add_log_to_ram_buffer(const LogEntry& newLog);


void handleRoot();
void handleForm();
void handleInput();
void handleSetTimers();
void handleTimerConfig();
void handleRTCUpdate();
void handleTimeUpdate();
void handleCurrentTime();
void handleFirmwareUpdate();
void handleFirmwarePage();
void handleStatusJson();
void handleLogsJson();
void handleApiGetLogs();
void handleApiSystemInfo();
void handleApiGetConfig();
void handleApiUpdateConfig();
void handleApiGetTimers();
void handleApiCreateTimer();
void handleApiGetTimerById(int id);
void handleApiUpdateTimerById(int id);
void handleApiStatus();
void handleApiControl();

// The WebServer object now lives here
WebServer server(80);

void web_server_init() {    
    server.on("/", HTTP_GET, handleRoot);
    server.on("/handleInput", HTTP_GET, handleInput);
    server.on("/submit", HTTP_POST, handleForm);
    server.on("/turnDeviceOn", HTTP_GET, []() {
        if (turnDeviceOn()) {
            server.send(200, "text/plain", "OK");
        } else {
            server.send(500, "text/plain", "Failed to turn on device");
        }
    });
    server.on("/turnDeviceOff", HTTP_GET, []() {
        if (turnDeviceOff()) {
            server.send(200, "text/plain", "OK");
        } else {
            server.send(500, "text/plain", "Failed to turn off device");
        }
    });
    server.on("/timerConfig", HTTP_GET, handleTimerConfig);
    server.on("/setTimers", HTTP_GET, handleSetTimers);
    server.on("/rtc-update", HTTP_GET, handleRTCUpdate);
    server.on("/update-time", HTTP_POST, handleTimeUpdate);
    server.on("/mqtt", HTTP_GET, []() {
        File file = LittleFS.open("/mqtt.html", "r");
        if (!file) {
            server.send(404, "text/plain", "MQTT config page not found");
            return;
        }
        server.streamFile(file, "text/html");
        file.close();
    });
    server.on("/saveMQTT", HTTP_POST, []() {
        if (!server.hasArg("IP")) {
            server.send(400, "application/json",
            "{\"success\":false,\"message\":\"Missing required fields\"}");
        return;
    }

    // --- THIS IS THE CRUCIAL LINE ---
    // You must declare the variable and get the value from the form submission.
    String MQTTIpAddress = server.arg("IP");
    String MQTTUser = server.arg("user");
    String MQTTPass = server.arg("password");

    if (storage_save_mqtt_config(MQTTIpAddress.c_str(), MQTTUser.c_str(), MQTTPass.c_str())) {
        strlcpy(mqttUser, MQTTUser.c_str(), sizeof(mqttUser));
        strlcpy(mqttPassword, MQTTPass.c_str(), sizeof(mqttPassword));
        strlcpy(mqttServer, MQTTIpAddress.c_str(), sizeof(mqttServer));
        server.send(200, "application/json",
            "{\"success\":true,\"message\":\"MQTT settings saved successfully\"}");
        shouldAttemptMqttConnection = true; // Signal the main loop to try connecting again
    } else {
        server.send(500, "application/json",
            "{\"success\":false,\"message\":\"Failed to save settings\"}");
    }
});
    server.on("/status.json", HTTP_GET, handleStatusJson);

    server.on("/firmware", HTTP_GET, handleFirmwarePage);
 
    server.on("/update", HTTP_POST, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", Update.hasError() ? "FAIL" : "OK");
    delay(1000);
    ESP.restart();
    }, handleFirmwareUpdate);
    server.on("/wifi-config", HTTP_GET, []() {
        File file = LittleFS.open("/wifi.html", "r");
        if (!file) {
            server.send(404, "text/plain", "WiFi config page not found");
            return;
        }
        server.streamFile(file, "text/html");
        file.close();
    });
    server.on("/saveWiFi", HTTP_POST, []() {
        
            if (!server.hasArg("ssid") || !server.hasArg("password")) {
                server.send(400, "application/json", 
                    "{\"success\":false,\"message\":\"Missing required fields\"}");
                return;
            }

            String ssid = server.arg("ssid");
            String password = server.arg("password");

            if (ssid.length() > MAX_SSID_LENGTH - 1 || 
                password.length() > MAX_PASSWORD_LENGTH - 1) {
                server.send(400, "application/json", 
                    "{\"success\":false,\"message\":\"SSID or password too long\"}");
                return;
            }

            if (storage_save_wifi_config(ssid.c_str(), password.c_str())) {
                server.send(200, "application/json", 
                    "{\"success\":true,\"message\":\"WiFi settings saved successfully\"}");
            } else {
                server.send(500, "application/json", 
                    "{\"success\":false,\"message\":\"Failed to save settings\"}");
            }
        });
    server.on("/reboot", HTTP_GET, []() {
    server.send(200, "text/plain", "Rebooting...");
        delay(500);
        ESP.restart();
    });
    server.on("/log", HTTP_GET, []() {
        File file = LittleFS.open("/log.html", "r");
        if (!file) {
            server.send(404, "text/plain", "Log page not found");
            return;
        }
        server.streamFile(file, "text/html");
        file.close();
    });
    server.on("/logs.json", HTTP_GET, handleLogsJson);
    server.on("/current-time", HTTP_GET, handleCurrentTime);
    server.on("/getBypassStatus", HTTP_GET, []() {
    server.send(200, "application/json", String("{\"bypassMode\":") + (bypassModeEnabled ? "true" : "false") + "}");
    });
    server.on("/toggleBypass", HTTP_GET, []() {
        bypassModeEnabled = !bypassModeEnabled; // Toggle bypass mode
        server.send(200, "application/json", String("{\"bypassMode\":") + (bypassModeEnabled ? "true" : "false") + "}");
        Serial.printf("Bypass Mode toggled: %s\n", bypassModeEnabled ? "Enabled" : "Disabled");
    });

    // REST API Endpoints
    server.on("/api/v1/status", HTTP_GET, handleApiStatus);
    server.on("/api/v1/control", HTTP_POST, handleApiControl);
    server.on("/api/v1/timers", HTTP_GET, handleApiGetTimers);
    server.on("/api/v1/timers", HTTP_POST, handleApiCreateTimer);
    server.on("/api/v1/config", HTTP_GET, handleApiGetConfig);
    server.on("/api/v1/config", HTTP_PUT, handleApiUpdateConfig);
    server.on("/api/v1/system", HTTP_GET, handleApiSystemInfo);
    server.on("/api/v1/logs", HTTP_GET, handleApiGetLogs);

    // Handle timer-specific endpoints with ID
    server.on("/api/v1/timers/0", HTTP_GET, []() { handleApiGetTimerById(0); });
    server.on("/api/v1/timers/0", HTTP_PUT, []() { handleApiUpdateTimerById(0); });
    server.on("/api/v1/timers/1", HTTP_GET, []() { handleApiGetTimerById(1); });
    server.on("/api/v1/timers/1", HTTP_PUT, []() { handleApiUpdateTimerById(1); });
    server.on("/wdt-test", HTTP_GET, []() {
    server.send(200, "text/plain", "WDT test started — device will reset in 30s");
    // Simulate a hang — watchdog should fire and reboot
    while(true) { }  
    });
    server.onNotFound([]() {
        server.sendHeader("Location", "http://" + wifi_get_ap_ip() + "/");
        server.send(302, "text/plain", "");
    });

    server.begin();
    Serial.println("HTTP server started");
}

void handleRoot() {
    File file = LittleFS.open("/dashboard.html", "r");
    if (!file) {
        server.send(404, "text/plain", "Dashboard file not found");
        return;
    }
    server.streamFile(file, "text/html");
    file.close();
}

void handleForm() {
    if (server.method() != HTTP_POST) {
        server.send(405, "text/plain", "Method Not Allowed");
        return;
    }

    std::set<String> uniqueTimes;
    bool hasConflict = false;

    for (int i = 0; i < safeNumberOfTimers(); i++) {
        String time = server.arg("hour" + String(i));
        if (!time.isEmpty()) {
            if (uniqueTimes.find(time) != uniqueTimes.end()) {
                hasConflict = true;
                break;
            }
            uniqueTimes.insert(time);
        }

        int colonIndex = time.indexOf(':');
        triggerDays[i].hour = time.substring(0, colonIndex).toInt();
        triggerDays[i].minute = time.substring(colonIndex + 1).toInt();
        
        long activationDurationValue = server.arg("activationDuration" + String(i)).toInt();
        long activationDurationUnit = server.arg("activationDurationUnit" + String(i)).toInt();
        triggerDays[i].activationDuration = max(1UL, (unsigned long)(activationDurationValue * (activationDurationUnit / 1000)));  // Convert to seconds, minimum 1

        long delayValue = server.arg("delayBetweenActivations" + String(i)).toInt();
        long delayUnit = server.arg("delayUnit" + String(i)).toInt();
        triggerDays[i].delayBetweenActivations = max(60UL, (unsigned long)(delayValue * (delayUnit / 1000)));  // Convert to seconds, minimum 1
        
        triggerDays[i].repetitions = server.arg("repetitions" + String(i)).toInt();
        
        resetTriggerState(i);
        Serial.printf("Timer %d: Delay set to %lu seconds\n", i, triggerDays[i].delayBetweenActivations);
    }

    if (hasConflict) {
        server.send(400, "application/json", "{\"success\":false,\"message\":\"Multiple timers set for the same time. Please choose different times.\"}");
        return;
    }

    for (int i = 0; i < 7; i++) {
        days[i] = server.hasArg("day" + String(i)) ? 1 : 0;
    }

    storage_save_settings(numberOfTimers, triggerDays, days);
    
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
}

void handleInput() {
    File file = LittleFS.open("/triggerschedules.html", "r");
    if (!file) {
        server.send(404, "text/plain", "Trigger schedules page not found");
        return;
    }
    server.streamFile(file, "text/html");
    file.close();
}
void handleSetTimers() {
  if (server.hasArg("count")) {
    int newCount = server.arg("count").toInt();
    if (newCount >= 1 && newCount <= MAX_TIMERS) {
      numberOfTimers = newCount;
      storage_save_number_of_timers(numberOfTimers);
      Serial.printf("handleSetTimers: numberOfTimers updated to %d\n", numberOfTimers);

      server.send(200, "text/plain", "Number of timers updated");
    } else {
            Serial.printf("handleSetTimers: Invalid newCount = %d\n", newCount);
  
      server.send(400, "text/plain", "Invalid number of timers");
    }
  } else {
    server.send(400, "text/plain", "Missing count parameter");
  }
}

void handleTimerConfig() {
  File file = LittleFS.open("/timerConfig.html", "r");
  if (!file) {
      server.send(404, "text/plain", "Timer configuration page not found");
      return;
  }
  server.streamFile(file, "text/html");
  file.close();
}

void handleRTCUpdate() {
    File file = LittleFS.open("/clock.html", "r");
    if (!file) {
        server.send(404, "text/plain", "Clock page not found");
        return;
    }
    server.streamFile(file, "text/html");
    file.close();
}

void handleTimeUpdate() {
    if (server.method() != HTTP_POST) {
        server.send(405, "text/plain", "Method Not Allowed");
        return;
    }

    bool dstEnabled = server.hasArg("useDST");
    int timezone = server.arg("timezone").toInt();
    int year = server.arg("year").toInt();
    int month = server.arg("month").toInt();
    int day = server.arg("day").toInt();
    int hour = server.arg("hour").toInt();
    int minute = server.arg("minute").toInt();
    int second = server.arg("second").toInt();

    // Save the new timezone settings using our handler
    rtc_save_timezone_settings(timezone, dstEnabled);

    // Calculate the correct UTC time to set the clock
    DateTime userEnteredTime(year, month, day, hour, minute, second);
    int32_t totalOffset = timezone * 3600;
    if (dstEnabled && rtc_is_dst(userEnteredTime)) {
         totalOffset += 3600;
    }
    DateTime newTimeUTC(userEnteredTime.unixtime() - totalOffset);

    // Update the RTC clock with the calculated UTC time
    if (rtc_update_time(newTimeUTC)) {
        server.sendHeader("Location", "/rtc-update?success=true");
        server.send(303);
    } else {
        server.send(500, "text/plain", "Failed to set new date and time");
    }
}

void handleCurrentTime() {

    DateTime now = rtc_get_current_time();
    char datestring[20];
    snprintf_P(datestring, sizeof(datestring),
             PSTR("%04u/%02u/%02u %02u:%02u:%02u"),
             now.year(), now.month(), now.day(),
             now.hour(), now.minute(), now.second());
    server.send(200, "text/plain", String(datestring));
}

void handleLogsJson() {
    JsonDocument doc;
    JsonArray logs = doc.to<JsonArray>();

    for (int i = logIndex - 1; i >= 0; i--) {
        if (relayLog[i].unix_time == 0) {
            continue;
        }

        DateTime logTime(relayLog[i].unix_time);
        char datestring[20];
        snprintf(datestring, sizeof(datestring), "%04u/%02u/%02u %02u:%02u:%02u",
                 logTime.year(), logTime.month(), logTime.day(),
                 logTime.hour(), logTime.minute(), logTime.second());

        JsonObject log = logs.add<JsonObject>();
        log["timestamp"] = datestring;
        log["state"] = relayLog[i].relayState ? "ON" : "OFF";
        log["unix"] = relayLog[i].unix_time;
    }

    String jsonString;
    serializeJson(doc, jsonString);
    server.send(200, "application/json", jsonString);
}

void handleFirmwarePage() {
    File file = LittleFS.open("/firmware.html", "r");
    if (!file) {
        server.send(404, "text/plain", "File Not Found");
        return;
    }
    // Stream the file content to the browser
    server.streamFile(file, "text/html");
    file.close();
}

void handleFirmwareUpdate() {
  HTTPUpload& upload = server.upload();
  static bool updateStarted = false;
  static int updateCommand = U_FLASH; // Default to firmware update

  if (upload.status == UPLOAD_FILE_START) {
    Serial.printf("Update: %s\n", upload.filename.c_str());

    // Check which radio button was selected
    if (server.hasArg("update_type") && server.arg("update_type") == "filesystem") {
        updateCommand = U_SPIFFS; // Set command for filesystem
        Serial.println("Update type: Filesystem");
    } else {
        updateCommand = U_FLASH; // Set command for firmware
        Serial.println("Update type: Firmware");
    }
    
    // Start the update with the correct command
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, updateCommand)) {
      Update.printError(Serial);
      return server.send(400, "text/plain", "Not enough space");
    }
    updateStarted = true;
    
  } else if (upload.status == UPLOAD_FILE_WRITE && updateStarted) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
      // No server.send here, as it can disrupt the upload stream
    }
    
  } else if (upload.status == UPLOAD_FILE_END && updateStarted) {
    if (Update.end(true)) {
      Serial.printf("Update Success: %u bytes\n", upload.totalSize);
      // The success response is sent by the on-end handler now
    } else {
      Update.printError(Serial);
      // No server.send here either
    }
    updateStarted = false;
  }
}

void web_server_handle_client() {
    server.handleClient();
}

void handleStatusJson() {
    // Create a JSON document. Adjust size as needed.
    JsonDocument doc;

    // --- Time and System Info ---
    DateTime now = rtc_get_current_time();
    char timeString[20];
    snprintf(timeString, sizeof(timeString), "%04u/%02u/%02u %02u:%02u:%02u",
             now.year(), now.month(), now.day(),
             now.hour(), now.minute(), now.second());

    doc["currentTime"] = timeString;
    doc["currentDay"] = getDayOfWeek(now.dayOfTheWeek());
    doc["deviceState"] = readRelayState() ? "On" : "Off";
    doc["bypassMode"] = bypassModeEnabled;
    doc["localIp"] = WiFi.localIP().toString();
    doc["apIp"] = WiFi.softAPIP().toString();
    doc["mqttIp"] = mqttServer;
    doc["firmwareVersion"] = "4.1";
    doc["timezoneOffset"] = rtc_get_timezone_offset();
    doc["dstEnabled"] = rtc_is_dst_enabled();
    doc["rtcOk"] = rtc_is_ok();
    
    // --- Active Days Array ---
    JsonArray daysArray = doc["activeDays"].to<JsonArray>();
    for (int i = 0; i < 7; i++) {
        daysArray.add(days[i] == 1); // Add true if day is active, false otherwise
    }

    // --- Timers Array ---
    JsonArray timers = doc["timers"].to<JsonArray>();
    for (int i = 0; i < safeNumberOfTimers(); i++) {
        JsonObject timer = timers.add<JsonObject>();
        char timerTime[6];
        snprintf(timerTime, sizeof(timerTime), "%02d:%02d", triggerDays[i].hour, triggerDays[i].minute);
        
        timer["id"] = i + 1;
        timer["time"] = timerTime;
        timer["duration"] = triggerDays[i].activationDuration;
        timer["delay"] = triggerDays[i].delayBetweenActivations;
        timer["repetitions"] = triggerDays[i].repetitions;
        timer["activations"] = triggerStates[i].activations;
    }

    // --- Serialization and Sending ---
    String jsonString;
    serializeJson(doc, jsonString);
    server.send(200, "application/json", jsonString);
}

// REST API Handler Implementations

void handleApiStatus() {
    JsonDocument doc;
    
    DateTime now = rtc_get_current_time();
    char timeString[20];
    snprintf(timeString, sizeof(timeString), "%04u-%02u-%02uT%02u:%02u:%02u",
             now.year(), now.month(), now.day(),
             now.hour(), now.minute(), now.second());

    doc["timestamp"] = timeString;
    doc["device"]["state"] = readRelayState() ? "on" : "off";
    doc["device"]["bypassMode"] = bypassModeEnabled;
    doc["system"]["uptime"] = millis() / 1000;
    doc["system"]["freeHeap"] = ESP.getFreeHeap();
    doc["network"]["wifi"]["connected"] = WiFi.status() == WL_CONNECTED;
    doc["network"]["wifi"]["ip"] = WiFi.localIP().toString();
    doc["network"]["ap"]["ip"] = WiFi.softAPIP().toString();
    doc["mqtt"]["connected"] = mqttConnected;
    doc["rtc"]["ok"] = rtc_is_ok();

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void handleApiControl() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"No JSON body provided\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    
    if (error) {
        server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    String action = doc["action"];
    bool success = false;
    String message = "Unknown action";

    if (action == "on") {
        success = turnDeviceOn();
        message = success ? "Device turned on" : "Failed to turn device on";
    } else if (action == "off") {
        success = turnDeviceOff();
        message = success ? "Device turned off" : "Failed to turn device off";
    } else if (action == "bypass") {
        bypassModeEnabled = doc["enabled"] | false;
        success = true;
        message = String("Bypass mode ") + (bypassModeEnabled ? "enabled" : "disabled");
    } else {
        server.send(400, "application/json", "{\"error\":\"Invalid action\"}");
        return;
    }

    JsonDocument response;
    response["success"] = success;
    response["message"] = message;
    
    String jsonResponse;
    serializeJson(response, jsonResponse);
    server.send(success ? 200 : 500, "application/json", jsonResponse);
}

void handleApiGetTimers() {
    JsonDocument doc;
    JsonArray timers = doc["timers"].to<JsonArray>();
    
    for (int i = 0; i < safeNumberOfTimers(); i++) {
        JsonObject timer = timers.add<JsonObject>();
        timer["id"] = i;
        timer["hour"] = triggerDays[i].hour;
        timer["minute"] = triggerDays[i].minute;
        timer["duration"] = triggerDays[i].activationDuration;
        timer["delay"] = triggerDays[i].delayBetweenActivations;
        timer["repetitions"] = triggerDays[i].repetitions;
        timer["days"] = days[i];  // Use the global days array
        timer["enabled"] = true;  // Timers are enabled if they exist
        timer["activations"] = triggerStates[i].activations;
    }

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void handleApiCreateTimer() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"No JSON body provided\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    
    if (error) {
        server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    // For now, just update timer 0 as an example
    // In a full implementation, you'd create a new timer slot
    int timerId = 0;
    
    triggerDays[timerId].hour = doc["hour"] | 0;
    triggerDays[timerId].minute = doc["minute"] | 0;
    triggerDays[timerId].activationDuration = (doc["duration"] | 0) ;
    triggerDays[timerId].delayBetweenActivations = (doc["delay"] | 0);
    triggerDays[timerId].repetitions = doc["repetitions"] | 1;
    days[timerId] = doc["days"] | 0;  // Store in global days array
    // No individual enabled field - timers are enabled by default

    storage_save_settings(numberOfTimers, triggerDays, days);
    
    JsonDocument response;
    response["success"] = true;
    response["message"] = "Timer created";
    response["timerId"] = timerId;
    
    String jsonResponse;
    serializeJson(response, jsonResponse);
    server.send(201, "application/json", jsonResponse);
}

void handleApiGetTimerById(int id) {
    if (id < 0 || id >= safeNumberOfTimers()) {
        server.send(404, "application/json", "{\"error\":\"Timer not found\"}");
        return;
    }

    JsonDocument doc;
    doc["id"] = id;
    doc["hour"] = triggerDays[id].hour;
    doc["minute"] = triggerDays[id].minute;
    doc["duration"] = triggerDays[id].activationDuration ;
    doc["delay"] = triggerDays[id].delayBetweenActivations ;
    doc["repetitions"] = triggerDays[id].repetitions;
    doc["days"] = days[id];  // Use global days array
    doc["enabled"] = true;   // Timers are enabled if they exist
    doc["activations"] = triggerStates[id].activations;

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void handleApiUpdateTimerById(int id) {
    if (id < 0 || id >= safeNumberOfTimers()) {
        server.send(404, "application/json", "{\"error\":\"Timer not found\"}");
        return;
    }

    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"No JSON body provided\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    
    if (error) {
        server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    triggerDays[id].hour = doc["hour"] | triggerDays[id].hour;
    triggerDays[id].minute = doc["minute"] | triggerDays[id].minute;
    triggerDays[id].activationDuration = (doc["duration"] | (triggerDays[id].activationDuration));
    triggerDays[id].delayBetweenActivations = (doc["delay"] | (triggerDays[id].delayBetweenActivations));
    triggerDays[id].repetitions = doc["repetitions"] | triggerDays[id].repetitions;
    days[id] = doc["days"] | days[id];  // Update global days array
    // No individual enabled field to update

    storage_save_settings(numberOfTimers, triggerDays, days);

    JsonDocument response;
    response["success"] = true;
    response["message"] = "Timer updated";
    
    String jsonResponse;
    serializeJson(response, jsonResponse);
    server.send(200, "application/json", jsonResponse);
}

void handleApiGetConfig() {
    JsonDocument doc;
    
    doc["timers"]["count"] = safeNumberOfTimers();
    doc["mqtt"]["server"] = mqttServer;
    doc["mqtt"]["user"] = mqttUser;
    // Password intentionally omitted from GET response
    doc["mqtt"]["hasPassword"] = strlen(mqttPassword) > 0;
    doc["mqtt"]["port"] = 1883;
    doc["mqtt"]["topic"] = "pm/control";
    doc["rtc"]["dstStartMonth"] = rtc_get_dst_start_month();
    doc["rtc"]["dstEndMonth"]   = rtc_get_dst_end_month();
    doc["rtc"]["timezoneOffset"] = rtc_get_timezone_offset();
    doc["system"]["deviceName"] = "PowerMate ESP32";

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void handleApiUpdateConfig() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"No JSON body provided\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    if (error) {
        server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    bool changed = false;

    // Update MQTT config if provided
    if (!doc["mqtt"].isNull()) {
        if (doc["mqtt"]["server"].is<const char*>()) {
            strlcpy(mqttServer, doc["mqtt"]["server"], sizeof(mqttServer));
            changed = true;
        }
        if (doc["mqtt"]["user"].is<const char*>()) {
            strlcpy(mqttUser, doc["mqtt"]["user"], sizeof(mqttUser));
            changed = true;
        }
        if (doc["mqtt"]["password"].is<const char*>()) {
            strlcpy(mqttPassword, doc["mqtt"]["password"], sizeof(mqttPassword));
            changed = true;
        }
        if (changed) {
            storage_save_mqtt_config(mqttServer, mqttUser, mqttPassword);
            shouldAttemptMqttConnection = true;
        }
    }

    // Update timer count if provided
    if (doc["timers"]["count"].is<int>()) {
        int newCount = doc["timers"]["count"];
        if (newCount >= 1 && newCount <= MAX_TIMERS) {
            numberOfTimers = newCount;
            storage_save_number_of_timers(numberOfTimers);
            changed = true;
        } else {
            server.send(400, "application/json", "{\"error\":\"Timer count out of range (1–6)\"}");
            return;
        }
    }

    JsonDocument response;
    response["success"] = true;
    response["message"] = changed ? "Configuration updated" : "No changes applied";
    String jsonResponse;
    serializeJson(response, jsonResponse);
    server.send(200, "application/json", jsonResponse);
}

void handleApiSystemInfo() {
    JsonDocument doc;
    
    doc["firmware"]["version"] = "4.1";
    doc["hardware"]["chip"] = ESP.getChipModel();
    doc["hardware"]["revision"] = ESP.getChipRevision();
    doc["system"]["uptime"] = millis() / 1000;
    doc["system"]["freeHeap"] = ESP.getFreeHeap();
    doc["system"]["totalHeap"] = ESP.getHeapSize();
    doc["system"]["cpuFreq"] = ESP.getCpuFreqMHz();
    doc["network"]["wifi"]["mac"] = WiFi.macAddress();
    doc["network"]["ap"]["mac"] = WiFi.softAPmacAddress();
    doc["filesystem"]["totalBytes"] = LittleFS.totalBytes();
    doc["filesystem"]["usedBytes"] = LittleFS.usedBytes();

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void handleApiGetLogs() {
    JsonDocument doc;
    JsonArray logs = doc["logs"].to<JsonArray>();
    
    int startIndex = max(0, logIndex - MAX_LOG_ENTRIES);
    for (int i = 0; i < MAX_LOG_ENTRIES; i++) {
        int logPos = (startIndex + i) % MAX_LOG_ENTRIES;
        if (relayLog[logPos].unix_time > 0) {
            JsonObject logEntry = logs.add<JsonObject>();
            DateTime logTime = DateTime(relayLog[logPos].unix_time);
            char timeStr[20];
            snprintf(timeStr, sizeof(timeStr), "%04u-%02u-%02u %02u:%02u:%02u",
                     logTime.year(), logTime.month(), logTime.day(),
                     logTime.hour(), logTime.minute(), logTime.second());
            
            logEntry["timestamp"] = timeStr;
            logEntry["action"] = relayLog[logPos].action;
            logEntry["duration"] = relayLog[logPos].duration;
        }
    }

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}
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

String generateHTMLHeader();
String generateCSS();
String generateJavaScript();
String generateDashboardGrid();
String generateTriggerCard(int index);
String generateInfoItem(const String& label, const String& value);
String generateLogPage();
String generateWiFiConfigForm();
String generateMQTTConfigForm();
String generateUpdateTimeForm();
String generateTimeFormFields(); 
String generateFormValidation();
String generateSystemInfoCards();
String generateCurrentTimeCard();
String generateActiveDaysCard();
String generateMQTTConfigForm();

void sendChunked(const __FlashStringHelper* content);
void sendTriggerSection(int i);
void sendActiveDaysSection();
void sendSystemInfo();


// The WebServer object now lives here
WebServer server(80);

// 'extern' tells this file that these variables/objects exist in another file (main.cpp).
// This allows our handler functions to access the application's current state.
extern int numberOfTimers;
extern TriggerTime triggerDays[];
extern TriggerState triggerStates[];
extern int days[];
extern LogEntry relayLog[];
extern int logIndex;
extern const char* mqttServer;
extern bool bypassModeEnabled;
extern bool shouldAttemptMqttConnection;

// We also need to declare functions from main.cpp that are called by handlers.
extern bool turnDeviceOn();
extern bool turnDeviceOff();
extern int safeNumberOfTimers();
extern bool readRelayState();
extern void resetTriggerState(int i);
extern const char* getDayOfWeek(int dayIndex);


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

    // Now that the variable exists, you can use it.
    if (storage_save_mqtt_config(MQTTIpAddress.c_str())) {
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
    server.onNotFound([]() {
        server.sendHeader("Location", "http://192.168.4.1/");
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
        triggerDays[i].activationDuration = activationDurationValue * activationDurationUnit;
        
        long delayValue = server.arg("delayBetweenActivations" + String(i)).toInt();
        long delayUnit = server.arg("delayUnit" + String(i)).toInt();
        triggerDays[i].delayBetweenActivations = delayValue * delayUnit;
        
        triggerDays[i].repetitions = server.arg("repetitions" + String(i)).toInt();
        
        resetTriggerState(i);
        Serial.printf("Timer %d: Delay set to %lu milliseconds\n", i, triggerDays[i].delayBetweenActivations);
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
        snprintf_P(datestring, 
                 sizeof(datestring),
                 PSTR("%04u/%02u/%02u %02u:%02u:%02u"),
                 now.year(),
                 now.month(),
                 now.day(),
                 now.hour(),
                 now.minute(),
                 now.second());

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

String generateHTMLHeader() {
String header = R"=====(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <meta http-equiv="Cache-Control" content="no-cache, no-store, must-revalidate" />
    <meta http-equiv="Pragma" content="no-cache" />
    <meta http-equiv="Expires" content="0" />
    <title>Powermate Dashboard</title>
    <style>
)=====";
  header += generateCSS();
  header += R"=====(
    </style>
    <script>
)=====";
  header += generateJavaScript();
  header += R"=====(
    </script>
</head>
<body>
    <a href="/" class="back-button-icon" title="Back to Dashboard">←</a>
    <h1>Dashboard</h1>
    <div class='button-section'>
    <div style="display: flex; align-items: center; gap: 8px;">
        <span>Dark Mode</span>
        <label class="switch">
            <input type="checkbox" id="darkModeToggle">
            <span class="slider"></span>
        </label>
    </div></div>
    <script>
        document.addEventListener('DOMContentLoaded', function() {
            const darkModeToggle = document.getElementById('darkModeToggle');
            const body = document.body;
            const darkMode = localStorage.getItem('darkMode') === 'true';
            body.classList.toggle('dark-mode', darkMode);
            if(darkModeToggle) { 
                darkModeToggle.checked = darkMode;
                darkModeToggle.addEventListener('change', () => {
                    body.classList.toggle('dark-mode');
                    localStorage.setItem('darkMode', darkModeToggle.checked);
                });
            }
        });
    </script>
)=====";

   if (!rtc_is_ok()) {
    header += R"=====(
    <div class="alert alert-danger">
        <strong>Warning!</strong> There is an issue with the RTC. Time-based functions may not work correctly.
    </div>
)=====";
  }


  return header;
}

String generateDashboardGrid() {
  String dashboardContent = "<div class='dashboard-grid'>";
  for (int i = 0; i < safeNumberOfTimers(); i++) {
    dashboardContent += generateTriggerCard(i);
  }
  dashboardContent += "</div>";
  return dashboardContent;
}

String generateTriggerCard(int index) {
    String card = "<div class='card trigger-card'>";
    
    // Header with title and activation badge
    card += "<div class='card-header'>";
    card += "<h2>Trigger " + String(index + 1) + "</h2>";
    card += "<div class='activation-badge'>";
    card += "<span class='activation-count'>" + String(triggerStates[index].activations) + "</span>";
    card += "<span class='activation-label'>runs today</span>";
    card += "</div>";
    card += "</div>";
    
    // Hero time display
    String formattedTime = (triggerDays[index].hour < 10 ? "0" : "") + String(triggerDays[index].hour) + ":";
    formattedTime += (triggerDays[index].minute < 10 ? "0" : "") + String(triggerDays[index].minute);
    
    card += "<div class='hero-time'>" + formattedTime + "</div>";
    
    // Visual flow section
    card += "<div class='timing-flow'>";
    
    // Time icon and label
    card += "<div class='flow-item'>";
    card += "<div class='flow-icon'>🕐</div>";
    card += "<div class='flow-label'>Trigger Time</div>";
    card += "</div>";
    
    // Arrow
    card += "<div class='flow-arrow'>→</div>";
    
    // Delay
    String delayString;
    unsigned long delayMs = triggerDays[index].delayBetweenActivations;
    if (delayMs % 3600000 == 0) {
      delayString = String(delayMs / 3600000) + "h";
    } else if (delayMs % 60000 == 0) {
      delayString = String(delayMs / 60000) + "m";
    } else {
      delayString = String(delayMs / 1000) + "s";
    }
    
    card += "<div class='flow-item'>";
    card += "<div class='flow-icon'>⏳</div>";
    card += "<div class='flow-value'>" + delayString + "</div>";
    card += "<div class='flow-label'>Delay</div>";
    card += "</div>";
    
    // Arrow
    card += "<div class='flow-arrow'>→</div>";
    
    // Duration
    String durationString = String(triggerDays[index].activationDuration / 1000) + "s";
    
    card += "<div class='flow-item'>";
    card += "<div class='flow-icon'>▶️</div>";
    card += "<div class='flow-value'>" + durationString + "</div>";
    card += "<div class='flow-label'>Duration</div>";
    card += "</div>";
    
    card += "</div>"; // End timing-flow
    
    // Status section
    card += "<div class='status-section'>";
    card += "<span class='status-label'>Repeats:</span>";
    card += "<span class='status-value'>" + String(triggerDays[index].repetitions) + " times</span>";
    card += "</div>";
    
    card += "</div>"; // End trigger-card
    return card;
  }

String generateCSS() {
  return R"=====(
    body {
      font-family: 'Arial', sans-serif;
      line-height: 1.6;
      color: #333;
      max-width: 1200px;
      margin: 0 auto;
      padding: 20px;
      background-color: #f4f4f4;
    }

    h1, h2 {
      color: #2c3e50;
    }

    .dashboard-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
      gap: 20px;
      margin-bottom: 30px;
    }

    .card {
      background-color: #fff;
      border-radius: 8px;
      box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1);
      padding: 20px;
    }

    .card h2 {
      margin-top: 0;
      border-bottom: 2px solid #3498db;
      padding-bottom: 10px;
    }

    .info-grid {
      display: grid;
      grid-template-columns: auto 1fr;
      gap: 10px;
    }

    .info-label {
      font-weight: bold;
      color: #34495e;
    }

    .days-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(100px, 1fr));
      gap: 10px;
    }

    .day-item {
      background-color: #ecf0f1;
      border-radius: 4px;
      padding: 5px 10px;
      text-align: center;
    }

    .day-item.active {
      background-color: #2ecc71;
      color: white;
    }

    .button-section {
      margin-top: 20px;
      display: flex;
      flex-wrap: wrap;
      gap: 10px;
    }

    .button,
    .button-section button,
    input[type="submit"],
    button[type="button"] {
      display: inline-block;
      padding: 10px 20px;
      font-size: 16px;
      text-align: center;
      text-decoration: none;
      background-color: #3498db;
      color: white;
      border: none;
      border-radius: 4px;
      cursor: pointer;
      transition: background-color 0.3s ease;
      flex: 1;
      min-width: 120px;
    }

    .button:hover,
    .button-section button:hover,
    input[type="submit"]:hover,
    button[type="button"]:hover {
      background-color: #2980b9;
    }

    .alert {
      padding: 20px;
      background-color: #f44336;
      color: white;
      margin-bottom: 15px;
      border-radius: 4px;
    }

    .alert-danger {
      background-color: #f44336;
    }
    .button-pressed {
        background-color: #e74c3c !important;
    }
    .time-update-card {
        max-width: 600px;
        margin: 20px auto;
        padding: 25px;
        border-radius: 8px;
        background-color: #fff;
        box-shadow: 0 2px 4px rgba(0, 0, 0, 0.1);
    }

    .time-update-card h2 {
        margin: 0 0 20px;
        padding-bottom: 10px;
        border-bottom: 2px solid #3498db;
        color: #2c3e50;
    }

    .time-update-form {
        display: grid;
        grid-template-columns: repeat(1, 1fr);
        gap: 20px;
    }

    .form-group {
        display: flex;
        flex-direction: column;
    }

    .form-group label {
        margin-bottom: 8px;
        font-weight: 500;
        color: #34495e;
    }

    .form-group input {
        padding: 12px;
        border: 1px solid #ddd;
        border-radius: 4px;
        font-size: 16px;
        transition: border-color 0.3s ease;
    }

    .form-group input:focus {
        border-color: #3498db;
        outline: none;
        box-shadow: 0 0 0 2px rgba(52, 152, 219, 0.2);
    }

    .form-submit {
        grid-column: 1 / -1;
        margin-top: 20px;
    }

    .submit-button {
        width: 100%;
        padding: 12px;
        background-color: #3498db;
        color: white;
        border: none;
        border-radius: 4px;
        font-size: 16px;
        cursor: pointer;
        transition: background-color 0.3s ease;
    }

    .submit-button:hover {
        background-color: #2980b9;
    }

    .error-message {
        grid-column: 1 / -1;
        color: #e74c3c;
        font-size: 14px;
        margin-top: 10px;
        padding: 10px;
        border-radius: 4px;
        background-color: rgba(231, 76, 60, 0.1);
    }
    .switch {
        position: relative;
        display: inline-block;
        width: 60px;
        height: 34px;
    }
    .switch input {
        opacity: 0;
        width: 0;
        height: 0;
    }
    .slider {
        position: absolute;
        cursor: pointer;
        top: 0;
        left: 0;
        right: 0;
        bottom: 0;
        background-color: #ccc;
        transition: .4s;
        border-radius: 34px;
    }
    .slider:before {
        position: absolute;
        content: "";
        height: 26px;
        width: 26px;
        left: 4px;
        bottom: 4px;
        background-color: white;
        transition: .4s;
        border-radius: 50%;
    }
    input:checked + .slider {
        background-color: #2ecc71;
    }
    .dark-mode input:checked + .slider {
        background-color: #2ecc71;
    }
    input:checked + .slider:before {
        transform: translateX(26px);
    }
        /* Enhanced Trigger Card Styles */
    .trigger-card {
        position: relative;
        padding: 20px;
    }
    
    .trigger-card .card-header {
        display: flex;
        justify-content: space-between;
        align-items: center;
        margin-bottom: 15px;
        padding-bottom: 10px;
        border-bottom: 1px solid rgba(0, 0, 0, 0.1);
    }
    
    .trigger-card h2 {
        margin: 0;
        font-size: 18px;
        font-weight: 600;
        color: #2c3e50;
    }
    
    .activation-badge {
        display: flex;
        align-items: center;
        gap: 4px;
        background: #e8f5e8;
        border: 1px solid #4caf50;
        border-radius: 12px;
        padding: 4px 8px;
        font-size: 12px;
    }
    
    .activation-count {
        font-weight: 600;
        color: #2e7d32;
    }
    
    .activation-label {
        color: #4caf50;
    }
    
    .hero-time {
        font-size: 2.5em;
        font-weight: 700;
        text-align: center;
        color: #3498db;
        margin: 15px 0;
        font-family: 'Courier New', monospace;
        text-shadow: 0 1px 2px rgba(0, 0, 0, 0.1);
    }
    
    .timing-flow {
        display: flex;
        align-items: center;
        justify-content: center;
        gap: 8px;
        margin: 20px 0;
        padding: 15px;
        background: rgba(52, 152, 219, 0.05);
        border-radius: 8px;
        flex-wrap: wrap;
    }
    
    .flow-item {
        display: flex;
        flex-direction: column;
        align-items: center;
        gap: 4px;
        min-width: 60px;
    }
    
    .flow-icon {
        font-size: 24px;
        margin-bottom: 2px;
    }
    
    .flow-value {
        font-size: 16px;
        font-weight: 600;
        color: #2c3e50;
    }
    
    .flow-label {
        font-size: 11px;
        color: #888;
        font-weight: 500;
        text-transform: uppercase;
        letter-spacing: 0.5px;
    }
    
    .flow-arrow {
        font-size: 18px;
        color: #95a5a6;
        margin: 0 4px;
    }
    
    .status-section {
        display: flex;
        justify-content: center;
        align-items: center;
        gap: 8px;
        margin-top: 15px;
        padding-top: 10px;
        border-top: 1px solid rgba(0, 0, 0, 0.1);
    }
    
    .status-label {
        font-size: 13px;
        color: #888;
        font-weight: 500;
    }
    
    .status-value {
        font-size: 14px;
        font-weight: 600;
        color: #2c3e50;
    }
    
    /* Dark Mode Styles */
    body.dark-mode {
    background-color: #121212;
    color: #e0e0e0;
    }

    body.dark-mode h1, 
    body.dark-mode h2 {
        color: #ecf0f1;
    }

    body.dark-mode .card {
        background-color: #1e1e1e;
        box-shadow: 0 4px 6px rgba(0, 0, 0, 0.3);
    }
    body.dark-mode .button-pressed {
        background-color: #c0392b !important;
    }
    body.dark-mode .card h2 {
      border-bottom: 2px solid #3498db;
    }

    body.dark-mode .info-label {
      color: #b0b0b0;
    }

    body.dark-mode .day-item {
      background-color: #2c2c2c;
    }

    body.dark-mode .day-item.active {
      background-color: #27ae60;
    }

    body.dark-mode .button,
    body.dark-mode .button-section button,
    body.dark-mode input[type="submit"],
    body.dark-mode button[type="button"] {
      background-color: #2c3e50;
    }

    body.dark-mode .button:hover,
    body.dark-mode .button-section button:hover,
    body.dark-mode input[type="submit"]:hover,
    body.dark-mode button[type="button"]:hover {
      background-color: #34495e;
    }
    body.dark-mode .time-update-card {
        background-color: #1e1e1e;
        box-shadow: 0 2px 4px rgba(0, 0, 0, 0.2);
    }

    body.dark-mode .time-update-card h2 {
        color: #ecf0f1;
        border-bottom-color: #3498db;
    }

    body.dark-mode .form-group label {
        color: #bdc3c7;
    }

    body.dark-mode .form-group input {
        background-color: #2c2c2c;
        border-color: #444;
        color: #ecf0f1;
    }

    body.dark-mode .form-group input:focus {
        border-color: #3498db;
        box-shadow: 0 0 0 2px rgba(52, 152, 219, 0.2);
    }

    body.dark-mode .error-message {
        background-color: rgba(231, 76, 60, 0.2);
    }
    .back-button-icon {
        position: fixed;
        top: 20px;
        right: 20px;
        font-size: 24px;
        color: #3498db;
        text-decoration: none;
        z-index: 1000;
        padding: 8px;
        border-radius: 50%;
        background-color: rgba(255, 255, 255, 0.9);
        box-shadow: 0 2px 4px rgba(0, 0, 0, 0.1);
        transition: all 0.3s ease;
        width: 40px;
        height: 40px;
        display: flex;
        align-items: center;
        justify-content: center;
    }
    .back-button-icon:hover {
        background-color: #3498db;
        color: white;
        transform: scale(1.1);
    }
    body.dark-mode .back-button-icon {
        background-color: rgba(30, 30, 30, 0.9);
        color: #5dade2;
        box-shadow: 0 2px 4px rgba(0, 0, 0, 0.3);
    }
    body.dark-mode .back-button-icon:hover {
        background-color: #5dade2;
        color: #121212;
    }
    body.dark-mode input,
    body.dark-mode select,
    body.dark-mode textarea {
        background-color: #2c2c2c;
        color: #e0e0e0;
        border-color: #444;
    }
    .back-button-icon {
        position: fixed;
        top: 20px;
        right: 20px;
    }
        body.dark-mode .trigger-card h2 {
        color: #ecf0f1;
    }
    
    body.dark-mode .activation-badge {
        background: #1b5e20;
        border-color: #4caf50;
    }
    
    body.dark-mode .activation-count {
        color: #81c784;
    }
    
    body.dark-mode .activation-label {
        color: #4caf50;
    }
    
    body.dark-mode .hero-time {
        color: #5dade2;
        text-shadow: 0 1px 2px rgba(0, 0, 0, 0.3);
    }
    
    body.dark-mode .timing-flow {
        background: rgba(93, 173, 226, 0.1);
    }
    
    body.dark-mode .flow-value {
        color: #ecf0f1;
    }
    
    body.dark-mode .flow-label {
        color: #95a5a6;
    }
    
    body.dark-mode .status-label {
        color: #95a5a6;
    }
    
    body.dark-mode .status-value {
        color: #ecf0f1;
    }
    /* Mobile Responsive Styles */
    @media (max-width: 600px) {
        .time-update-card {
            margin: 10px;
            padding: 5px;
        }

        .time-update-form {
            grid-template-columns: 1fr;
            gap: 15px;
        }

        .form-group input {
            padding: 10px;
            font-size: 14px;
        }

        .submit-button {
            padding: 10px;
            font-size: 14px;
        }
        .back-button-icon {
            top: 15px;
            right: 15px;
        }
            .hero-time {
            font-size: 2em;
        }
        
        .timing-flow {
            gap: 6px;
            padding: 12px;
        }
        
        .flow-item {
            min-width: 50px;
        }
        
        .flow-icon {
            font-size: 20px;
        }
    }
  )=====";
}

String generateJavaScript() {
  return R"=====(
    const isInApp = window.navigator.userAgent.includes('PowerMateApp'); // You'll set this in your app's WebView
    function handleNavigation(url, event) {
        if (event) {
            event.preventDefault();
        }
        
        if (isInApp) {
            // Use AJAX for in-app navigation
            var xhr = new XMLHttpRequest();
            xhr.open('GET', url, true);
            xhr.onload = function() {
                if (xhr.status >= 200 && xhr.status < 300) {
                    document.body.innerHTML = xhr.responseText;
                    setupEventListeners();
                }
            };
            xhr.send();
        } else {
            // Regular navigation for browser users
            window.location.href = url;
        }
    }

    function setupEventListeners() {
        setupDarkMode();
        
        document.querySelectorAll('a[href]:not([target])').forEach(link => {
            link.addEventListener('click', function(e) {
                handleNavigation(this.getAttribute('href'), e);
            });
        });
    }

    function submitForm(formId, url) {
        event.preventDefault();
        var form = document.getElementById(formId);
        var formData = new FormData(form);
        
        var xhr = new XMLHttpRequest();
        xhr.open('POST', url, true);
        xhr.onload = function() {
            if (xhr.status >= 200 && xhr.status < 300) {
                handleNavigation('/');
            }
        };
        xhr.send(formData);
    }
    function toggleButton() {
        var button = document.getElementById('toggleButton');
        console.log('Button clicked, Current state:', button.textContent);
        if (button.textContent.includes('On')) {
            button.textContent = 'Turn Device Off';
            button.classList.add('button-pressed');
            console.log('Calling turnDeviceOn');
            turnDeviceOn();
        } else {
            button.textContent = 'Turn Device On';
            button.classList.remove('button-pressed');
            console.log('Calling turnDeviceOff');
            turnDeviceOff();
        }
    }

    function turnDeviceOn(section) {
        console.log('Sending request to turnDeviceOn for section:', section);
        var xhr = new XMLHttpRequest();
        xhr.open('GET', '/turnDeviceOn' + (section !== undefined ? '?section=' + section : ''), true);
        xhr.onerror = function(e) {
            console.error('XHR Error:', e);
            alert('Failed to send request: ' + e.message);
        };
        xhr.onload = function() {
            if (xhr.status >= 200 && xhr.status < 300) {
                console.log('Successfully turned on device:', xhr.responseText);
                var button = document.getElementById('toggleButton');
                button.textContent = 'Turn Device Off';
                button.classList.add('button-pressed');
            } else {
                console.error('Failed to turn on device. Status:', xhr.status);
                alert('Failed to turn on device: ' + xhr.status);
            }
        };
        xhr.send();
    }

    function turnDeviceOff(section) {
        console.log('Sending request to turnDeviceOff for section:', section);
        var xhr = new XMLHttpRequest();
        xhr.open('GET', '/turnDeviceOff' + (section !== undefined ? '?section=' + section : ''), true);
        xhr.onerror = function(e) {
            console.error('XHR Error:', e);
            alert('Failed to send request: ' + e.message);
        };
        xhr.onload = function() {
            if (xhr.status >= 200 && xhr.status < 300) {
                console.log('Successfully turned off device:', xhr.responseText);
                var button = document.getElementById('toggleButton');
                button.textContent = 'Turn Device On';
                button.classList.remove('button-pressed');
            } else {
                console.error('Failed to turn off device. Status:', xhr.status);
                alert('Failed to turn off device: ' + xhr.status);
            }
        };
        xhr.send();
    }

    function setupDarkMode() {
        console.log('Setting up dark mode');
        var toggleButton = document.getElementById('darkModeToggle');
        if (!toggleButton) {
            console.error('Dark mode toggle button not found');
            return;
        }

        var body = document.body;

        // Check for saved user preference
        var darkModeEnabled = localStorage.getItem('darkMode') === 'true';
        if (darkModeEnabled) {
            body.classList.add('dark-mode');
        }

        // Toggle dark mode on button click
        toggleButton.addEventListener('click', function() {
            console.log('Dark mode toggle clicked');
            body.classList.toggle('dark-mode');
            var isDarkMode = body.classList.contains('dark-mode');
            localStorage.setItem('darkMode', isDarkMode.toString()); // Save user preference
            console.log('Dark mode is now:', isDarkMode);
        });
    }
  )=====";
}

String generateLogPage() {
    // --- START OF DEBUGGING VERSION ---
    Serial.println("\n--- Debugging generateLogPage ---");
    Serial.printf("logIndex value at start of function: %d\n", logIndex);

    String page = F(
        "<!DOCTYPE html>"
        "<html lang='en'>"
        "<head>"
        "    <meta charset='UTF-8'>"
        "    <meta name='viewport' content='width=device-width, initial-scale=1.0'>"
        "    <title>Activity Log</title>"
        "    <style>"
        "        :root { --primary-color: #3498db; --border-radius: 8px; }"
        "        body { font-family: 'Arial', sans-serif; background-color: #f4f4f4; color: #333; max-width: 800px; margin: 0 auto; padding: 20px; transition: background-color 0.3s, color 0.3s; }"
        "        body.dark-mode { background-color: #121212; color: #e0e0e0; }"
        "        h1 { text-align: center; color: #2c3e50; }"
        "        body.dark-mode h1 { color: #ecf0f1; }"
        "        table { width: 100%; border-collapse: collapse; margin-top: 20px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); border-radius: var(--border-radius); overflow: hidden; }"
        "        body.dark-mode table { box-shadow: 0 2px 4px rgba(0,0,0,0.2); }"
        "        th, td { padding: 12px; text-align: left; border-bottom: 1px solid #ddd; }"
        "        body.dark-mode th, body.dark-mode td { border-bottom: 1px solid #444; }"
        "        th { background-color: #3498db; color: white; }"
        "        body.dark-mode th { background-color: #2c3e50; }"
        "        tr:nth-child(even) { background-color: #f8f9fa; }"
        "        body.dark-mode tr:nth-child(even) { background-color: #1e1e1e; }"
        "        .button { display: inline-flex; align-items: center; justify-content: center; padding: 10px 20px; background-color: var(--primary-color); color: white; text-decoration: none; border-radius: 4px; border: none; cursor: pointer; font-size: 15px; transition: background-color 0.3s; }"
        "        .button:hover { background-color: #2980b9; }"
        "        .header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 20px; }"
        "        .switch { position: relative; display: inline-block; width: 60px; height: 34px; }"
        "        .switch input { opacity: 0; width: 0; height: 0; }"
        "        .slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #ccc; transition: .4s; border-radius: 34px; }"
        "        .slider:before { position: absolute; content: ''; height: 26px; width: 26px; left: 4px; bottom: 4px; background-color: white; transition: .4s; border-radius: 50%; }"
        "        input:checked + .slider { background-color: var(--primary-color); }"
        "        input:checked + .slider:before { transform: translateX(26px); }"
        "        @media (max-width: 768px) { body { padding: 15px; } th, td { padding: 8px; } }"
        "    </style>"
        "</head>"
        "<body>"
        "    <div class='header'>"
        "        <a href='/' class='button'>Back to Dashboard</a>"
        "        <div style='display: flex; align-items: center; gap: 8px;'>"
        "            <span>Dark Mode</span>"
        "            <label class='switch'>"
        "                <input type='checkbox' id='darkModeToggle'>"
        "                <span class='slider'></span>"
        "            </label>"
        "        </div>"
        "    </div>"
        "    <h1>Activity Log</h1>"
        "    <table>"
        "        <thead><tr><th>Timestamp</th><th>Action</th></tr></thead>"
        "        <tbody>"
    );

    if (logIndex == 0) {
        Serial.println("  -> Loop not entered because logIndex is 0.");
    }

    for (int i = logIndex - 1; i >= 0; i--) {
        Serial.printf("  -> Processing loop for index %d. Timestamp: %lu, State: %d\n", i, relayLog[i].unix_time, relayLog[i].relayState);
        if (relayLog[i].unix_time != 0) {
            DateTime logTime(relayLog[i].unix_time);
            char datestring[20];
            snprintf(datestring, sizeof(datestring), "%04u/%02u/%02d %02u:%02u:%02u",
                     logTime.year(), logTime.month(), logTime.day(),
                     logTime.hour(), logTime.minute(), logTime.second());

            page += "<tr><td>" + String(datestring) + "</td>";
            page += "<td>" + String(relayLog[i].relayState ? "ON" : "OFF") + "</td></tr>";
        }
    }

    page += F(
        "        </tbody>"
        "    </table>"
        "    <script>"
        "        document.addEventListener('DOMContentLoaded', function() {"
        "            const darkModeToggle = document.getElementById('darkModeToggle');"
        "            const body = document.body;"
        "            const darkMode = localStorage.getItem('darkMode') === 'true';"
        "            body.classList.toggle('dark-mode', darkMode);"
        "            if(darkModeToggle) {"
        "                darkModeToggle.checked = darkMode;"
        "                darkModeToggle.addEventListener('change', () => {"
        "                    body.classList.toggle('dark-mode');"
        "                    localStorage.setItem('darkMode', darkModeToggle.checked);"
        "                });"
        "            }"
        "        });"
        "    </script>"
        "</body>"
        "</html>"
    );
    
    Serial.println("--- End of generateLogPage Debug ---");
    return page;
}

String generateWiFiConfigForm() {
    return R"=====(
    <div class='card'>
        <h2>WiFi Configuration</h2>
        <form id='wifiConfigForm' onsubmit='return submitWiFiConfig(event)'>
            <div class='form-group'>
                <label for='ssid'>WiFi SSID:</label>
                <input type='text' id='ssid' name='ssid' maxlength='31' required>
            </div>
            <div class='form-group'>
                <label for='password'>WiFi Password:</label>
                <input type='password' id='password' name='password' maxlength='63' required>
                <input type='checkbox' onclick='togglePassword()'> Show Password
            </div>
            <div class='button-grid'>
                <button type='submit' class='submit-button'>Save WiFi Settings</button>
            </div>
        </form>
        <div id='wifiStatus'></div>
        <div id='rebootSection' style='display: none;'>
            <hr style='margin: 20px 0;'>
            <button onclick='rebootDevice()' class='button'>Reboot Device</button>
        </div>
    </div>
    <script>
    function togglePassword() {
        var x = document.getElementById('password');
        x.type = x.type === 'password' ? 'text' : 'password';
    }
    
    function rebootDevice() {
        fetch('/reboot')
        .then(() => {
            document.getElementById('wifiStatus').innerHTML = 'Device rebooting...';
            setTimeout(() => {
                window.location.href = '/';
            }, 5000);
        });
    }
    
    function submitWiFiConfig(e) {
        e.preventDefault();
        const form = document.getElementById('wifiConfigForm');
        const status = document.getElementById('wifiStatus');
        const formData = new FormData(form);
        
        fetch('/saveWiFi', {
            method: 'POST',
            body: formData
        })
        .then(response => response.json())
        .then(data => {
            status.innerHTML = data.message;
            if (data.success) {
                status.style.color = 'green';
                document.getElementById('rebootSection').style.display = 'block';
            } else {
                status.style.color = 'red';
            }
        })
        .catch(error => {
            status.innerHTML = 'Error saving WiFi settings';
            status.style.color = 'red';
        });
        
        return false;
    }
    </script>
    )=====";
}

void sendTriggerSection(int i) {
  char buffer[1024];  // Increased buffer size to accommodate the entire content
  unsigned long activationDuration = triggerDays[i].activationDuration;
  int activationDurationValue;
  const char* selectedActivationUnit;
  
  if (activationDuration % 3600000 == 0) {
    activationDurationValue = activationDuration / 3600000;
    selectedActivationUnit = "3600000";
  } else if (activationDuration % 60000 == 0) {
    activationDurationValue = activationDuration / 60000;
    selectedActivationUnit = "60000";
  } else {
    activationDurationValue = activationDuration / 1000;
    selectedActivationUnit = "1000";
  }

  unsigned long delay = triggerDays[i].delayBetweenActivations;
  int delayValue;
  const char* selectedDelayUnit;
  
  if (delay % 3600000 == 0) {
    delayValue = delay / 3600000;
    selectedDelayUnit = "3600000";
  } else if (delay % 60000 == 0) {
    delayValue = delay / 60000;
    selectedDelayUnit = "60000";
  } else {
    delayValue = delay / 1000;
    selectedDelayUnit = "1000";
  }

  snprintf(buffer, sizeof(buffer),
    "<div class='trigger'>"
    "<h2>Trigger %d</h2>"
    "<label>Time: <input type='time' name='hour%d' value='%02d:%02d' required></label><br>"
    "<label>Activation Duration: <input type='number' name='activationDuration%d' value='%d' min='0'></label>"
    "<select name='activationDurationUnit%d'>"
    "<option value='1000' %s>Seconds</option>"
    "<option value='60000' %s>Minutes</option>"
    "<option value='3600000' %s>Hours</option>"
    "</select><br>"
    "<label>Repetitions: <input type='number' name='repetitions%d' value='%d' min='0'></label><br>"
    "<label for='delayBetweenActivations%d'>Delay Between Activations:</label>"
    "<div class='custom-number-input'>"
    "<input type='number' id='delayBetweenActivations%d' name='delayBetweenActivations%d' min='1' value='%d'>"
    "<select id='delayUnit%d' name='delayUnit%d'>"
    "<option value='1000' %s>Seconds</option>"
    "<option value='60000' %s>Minutes</option>"
    "<option value='3600000' %s>Hours</option>"
    "</select>"
    "</div>"
    "</div>",
    i+1, i, triggerDays[i].hour, triggerDays[i].minute, 
    i, activationDurationValue, i,
    strcmp(selectedActivationUnit, "1000") == 0 ? "selected" : "",
    strcmp(selectedActivationUnit, "60000") == 0 ? "selected" : "",
    strcmp(selectedActivationUnit, "3600000") == 0 ? "selected" : "",
    i, triggerDays[i].repetitions,  // Moved repetitions parameters here
    i, i, i, delayValue, i, i,      // Delay parameters after repetitions
    strcmp(selectedDelayUnit, "1000") == 0 ? "selected" : "",
    strcmp(selectedDelayUnit, "60000") == 0 ? "selected" : "",
    strcmp(selectedDelayUnit, "3600000") == 0 ? "selected" : ""
  );
  
  server.sendContent(buffer);
}

void sendActiveDaysSection() {
  server.sendContent("<h2>Active Days</h2>");
  const char* dayNames[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  for (int i = 0; i < 7; i++) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer),
      "<label><input type='checkbox' name='day%d'%s>%s</label> ",
      i, (days[i] == 1 ? " checked" : ""), dayNames[i]);
    server.sendContent(buffer);
  }
  server.sendContent("<br>");
}

void sendSystemInfo() {
  DateTime now = rtc_get_current_time();
  char buffer[128];
  snprintf(buffer, sizeof(buffer),
    "<p>System Time: %04u/%02u/%02u %02u:%02u:%02u</p>"
    "<p>Version: 4</p>",
    now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second()
  );
  server.sendContent(buffer);
}

void sendChunked(const __FlashStringHelper* content) {
  server.sendContent_P(reinterpret_cast<const char *>(content));
  yield();
}

String generateMQTTConfigForm() {
    return R"=====(
    <div class='card'>
        <h2>MQTT Configuration</h2>
        <form id='MQTTConfigForm' onsubmit='return submitMQTTConfig(event)'>
            <div class='form-group'>
                <label for='IP'>MQTT IP:</label>
                <input type="text" id="IP" name="IP" maxlength="15" 
                       pattern="^(?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$" 
                       placeholder="e.g., 192.168.1.1" required>
            </div>
           
            <div class='button-grid'>
                <button type='submit' class='submit-button'>Save MQTT Settings</button>
            </div>
        </form>
        <div id='mqttStatus'></div>
    </div>
    <script>
    function submitMQTTConfig(e) {
        e.preventDefault();
        const form = document.getElementById('MQTTConfigForm');
        const status = document.getElementById('mqttStatus');
        const formData = new FormData(form);
        
        fetch('/saveMQTT', {
            method: 'POST',
            body: formData
        })
        .then(response => response.json())
        .then(data => {
            status.innerHTML = data.message;
            if (data.success) {
                status.style.color = 'green';
            } else {
                status.style.color = 'red';
            }
        })
        .catch(error => {
            status.innerHTML = 'Error saving MQTT settings';
            status.style.color = 'red';
        });
        
        return false;
    }
    </script>
    )=====";
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
    doc["firmwareVersion"] = "4.0";
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
        timer["duration"] = triggerDays[i].activationDuration / 1000;
        timer["delay"] = triggerDays[i].delayBetweenActivations / 1000;
        timer["repetitions"] = triggerDays[i].repetitions;
        timer["activations"] = triggerStates[i].activations;
    }

    // --- Serialization and Sending ---
    String jsonString;
    serializeJson(doc, jsonString);
    server.send(200, "application/json", jsonString);
}
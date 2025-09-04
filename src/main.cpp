#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <set>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include "RequestLimiter.h"
#include <Wire.h>
#include <HTTPClient.h>
#include <RTClib.h>
#include <PubSubClient.h>
#include "wifi_manager.h"
#include "storage_manager.h"
#include "rtc_handler.h"
#include "web_server.h"
#include "project_types.h"

int numberOfTimers = 2;

const int I2C_SDA_PIN = 21;
const int I2C_SCL_PIN = 22;

// Timers
const int MIN_TIMERS = 1;
int safeNumberOfTimers() {
    return min(max(numberOfTimers, MIN_TIMERS), MAX_TIMERS);
}

// Relay settings
const int relayPin = 16; //ESP32_AC = 16 or 33


TriggerTime triggerDays[MAX_TIMERS];
TriggerState triggerStates[MAX_TIMERS];

LogEntry relayLog[MAX_LOG_ENTRIES];

unsigned long bootTime;
bool isI2CInitialized = false;
DateTime initialRtcTime;
unsigned long startupMillis;
const char* getDayOfWeek(int dayIndex) {
    const char* days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    return days[dayIndex];
}
// DST configuration
int dstStartMonth = 3;  // March
int dstEndMonth = 10;  // October
int dstStartDayOfWeek = 0;  // Sunday
int dstEndDayOfWeek = 0;  // Sunday
int dstStartWeek = 2;  // Second week
int dstEndWeek = 1;  // First week
const int dstOffset = 3600;  // 1 hour in seconds

int days[7] = {0}; // 0 = Sunday, 1 = Monday, ..., 6 = Saturday
int logIndex = 0;
bool timer0Active = false;
bool timer1Active = false;
// Device control variables
bool deviceIsOn = false;
bool bypassModeEnabled = false;
unsigned long lastCheckTime = 0;
const unsigned long CHECK_INTERVAL = 100; // Check every 100ms

// MQTT Configuration
const char* mqttServer = "0.0.0.0";
const int mqttPort = 1883;
//const char* mqttUser = "your-mqtt-username"; // Optional, if your broker requires authentication
//const char* mqttPassword = "your-mqtt-password"; // Optional
const char* mqttTopic = "pm/control"; // Topic to listen for "on" and "off" messages
bool shouldAttemptMqttConnection = false; // Flag to trigger MQTT connection attempts

WiFiClient powermateClient;
PubSubClient mqttClient(powermateClient);



// Function declarations

void mqttCallback(char* topic, byte* payload, unsigned int length);
void syncDeviceState();
void setupOTA();
void resetTriggerState(int i);
bool turnDeviceOn();
bool turnDeviceOff();
void checkAndActivateTimers();
void resetActivationsOnDayChange();
void sendChunked(const __FlashStringHelper* content);

void logRTCTime(const char* context) ;
void  logInitialRTCTime();
void logTimerState(int i);
bool readRelayState();
uint32_t getTimeDifference(DateTime current, DateTime previous);
void setupMQTT();
bool validateTimerSettings(const TriggerTime& timer) ;
bool validateTime(const DateTime& time) ;
void resetTimerState(int timerId);
void add_log_to_ram_buffer(const LogEntry& newLog);
// HTML generation functions
String generateInfoItem(const String& label, const String& value);
String generateCurrentTimeCard();
String deviceName;
String generateUpdateTimeForm();
String generateTimeFormFields(); 
String generateFirmwareVersionCard();

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

String generateInfoItem(const String& label, const String& value) {
  return "<span class='info-label'>" + label + ":</span><span>" + value + "</span>";
}

String generateActiveDaysCard() {
  String card = "<div class='card'><h2>Active Days</h2><div class='days-grid'>";
  const char* dayNames[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
  for (int i = 0; i < 7; i++) {
    card += "<div class='day-item" + String(days[i] == 1 ? " active" : "") + "'>" + dayNames[i] + "</div>";
  }
  card += "</div></div>";
  return card;
}

String generateSystemInfoCards() {
  String cards = "<div class='dashboard-grid'>";
  cards += generateCurrentTimeCard();
  cards += generateFirmwareVersionCard();
  
  cards += "</div>";
  return cards;
}

String generateCurrentTimeCard() {
    DateTime now = rtc_get_current_time();
    char datestring[20];
    snprintf_P(datestring, sizeof(datestring),
             PSTR("%04u/%02u/%02u %02u:%02u:%02u"),
             now.year(), now.month(), now.day(),
             now.hour(), now.minute(), now.second());
  
    // CORRECTED: Get timezone info from our RTC module, not directly from EEPROM
    int timezone = rtc_get_timezone_offset();
    String timezoneStr = timezone >= 0 ? "+" + String(timezone) : String(timezone);
    
    String card = "<div class='card'><h2>System Clock</h2><div class='info-grid'>";
    card += generateInfoItem("Current Day", getDayOfWeek(now.dayOfTheWeek()));
    card += generateInfoItem("Current Time", String(datestring));
    card += generateInfoItem("Timezone", "UTC" + timezoneStr);
    card += generateInfoItem("DST Active", rtc_is_dst(now) ? "Yes" : "No");
    card += "<div class='button-section'>";
    card += "<a href='/' class='button'>Refresh</a>";
    card += "</div>";
    card += "</div></div>";
    return card;
}

String generateFirmwareVersionCard() {
  String card = "<div class='card'><h2>System Information</h2><div class='info-grid'>";
  card += generateInfoItem("Firmware Version", "4.0");
    if (WiFi.status() == WL_CONNECTED) {
    card += generateInfoItem("Local IP", WiFi.localIP().toString());
  } else {
    card += generateInfoItem("Local IP", "Not Connected");
  }
  
  // Also show AP IP address since we're in AP+STA mode
  card += generateInfoItem("AP IP", WiFi.softAPIP().toString());
  card += "</div></div>";
  return card;
}

String generateFormValidation() {
    return F(
        "<script>"
            "function validateForm() {"
                "var errorMessage = '';"
                "var year = parseInt(document.getElementById('year').value);"
                "var month = parseInt(document.getElementById('month').value);"
                "var day = parseInt(document.getElementById('day').value);"
                "var hour = parseInt(document.getElementById('hour').value);"
                "var minute = parseInt(document.getElementById('minute').value);"
                "var second = parseInt(document.getElementById('second').value);"
                
                // Check for valid numeric inputs
                "if (isNaN(year) || isNaN(month) || isNaN(day) || isNaN(hour) || isNaN(minute) || isNaN(second)) {"
                    "errorMessage = 'Please enter valid numeric values for all fields.';"
                "} else if (year < 2000 || year > 2099) {"
                    "errorMessage = 'Invalid year. Please enter a year between 2000 and 2099.';"
                "} else if (month < 1 || month > 12) {"
                    "errorMessage = 'Invalid month. Please enter a month between 1 and 12.';"
                "} else if (day < 1 || day > getDaysInMonth(month, year)) {"
                    "errorMessage = 'Invalid day for the selected month and year.';"
                "} else if (hour < 0 || hour > 23) {"
                    "errorMessage = 'Invalid hour. Please enter an hour between 0 and 23.';"
                "} else if (minute < 0 || minute > 59) {"
                    "errorMessage = 'Invalid minute. Please enter a minute between 0 and 59.';"
                "} else if (second < 0 || second > 59) {"
                    "errorMessage = 'Invalid second. Please enter a second between 0 and 59.';"
                "}"
                
                "var errorElement = document.getElementById('errorMessage');"
                "var submitButton = document.querySelector('.submit-button');"
                
                "if (errorMessage) {"
                    "if (errorElement) errorElement.innerHTML = errorMessage;"
                    "if (submitButton) submitButton.disabled = false;"
                    "return false;"
                "}"
                
                "if (errorElement) errorElement.innerHTML = 'Updating clock...';"
                "if (submitButton) submitButton.disabled = true;"
                
                "const form = document.getElementById('updateTimeForm');"
                "if (!form) {"
                    "if (errorElement) errorElement.innerHTML = 'Form not found.';"
                    "if (submitButton) submitButton.disabled = false;"
                    "return false;"
                "}"
                
                "const formData = new FormData(form);"
                
                "fetch('/update-time', {"
                    "method: 'POST',"
                    "body: formData,"
                    "signal: AbortSignal.timeout(10000)" // 10-second timeout
                "})"
                ".then(response => {"
                    "if (!response.ok) {"
                        "throw new Error('Failed to update clock: ' + response.statusText);"
                    "}"
                    "return response.text();"
                "})"
                ".then(() => {"
                    "window.location.href = '/rtc-update?success=true';"
                "})"
                ".catch(error => {"
                    "if (errorElement) errorElement.innerHTML = error.message;"
                    "if (submitButton) submitButton.disabled = false;"
                "});"
                
                "return false;"
            "}"
            
            // Helper function to calculate days in a month
            "function getDaysInMonth(month, year) {"
                "if (month === 2) {"
                    "if ((year % 4 === 0 && year % 100 !== 0) || (year % 400 === 0)) {"
                        "return 29;" // Leap year
                    "}"
                    "return 28;"
                "}"
                "const daysInMonth = [31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31];"
                "return daysInMonth[month - 1];"
            "}"
        "</script>"
    );
}

String generateUpdateTimeForm() {
    String html = F("<div class='card time-update-card'>");
    html += F("<h2>Update System Clock</h2>");
    html += generateTimeFormFields();
    html += generateFormValidation();
    html += F("</div>");
    return html;
}

// In main.cpp
String getDeviceName() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char deviceName[15];
    snprintf(deviceName, sizeof(deviceName), "PowerMate-%02X%02X", mac[4], mac[5]);
    return String(deviceName);
}


void dumpEEPROM(int start, int end) {
    Serial.printf("EEPROM dump from %d to %d:\n", start, end);
    for (int i = start; i <= end; i++) {
        if ((i - start) % 16 == 0) Serial.println();
        Serial.printf("%02X ", EEPROM.read(i));
    }
    Serial.println();
}

void verifyEEPROMWrite(int address, TriggerTime& timer) {
    TriggerTime readTimer;
    EEPROM.get(address, readTimer);
    if (readTimer.hour != timer.hour ||
        readTimer.minute != timer.minute ||
        readTimer.second != timer.second ||
        readTimer.activationDuration != timer.activationDuration ||
        readTimer.delayBetweenActivations != timer.delayBetweenActivations ||
        readTimer.repetitions != timer.repetitions) {
        Serial.printf("EEPROM write verification failed at address %d\n", address);
        Serial.printf("Expected: Hour=%d, Minute=%d, ActDur=%lu, Delay=%lu, Reps=%d\n",
                      timer.hour, timer.minute, timer.activationDuration,
                      timer.delayBetweenActivations, timer.repetitions);
        Serial.printf("Read: Hour=%d, Minute=%d, ActDur=%lu, Delay=%lu, Reps=%d\n",
                      readTimer.hour, readTimer.minute, readTimer.activationDuration,
                      readTimer.delayBetweenActivations, readTimer.repetitions);
    }
}

void setup() {
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

    for (int i = 0; i < safeNumberOfTimers(); i++) {
        resetTimerState(i);
    }
    startupMillis = millis();
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

    setupOTA();
    Serial.println("OTA setup complete");
    //logRTCTime("Setup");
    Serial.println("Setup complete");

    shouldAttemptMqttConnection = true; // Trigger MQTT connection attempt on boot

}

void loop() {
    static int reconnectCount = 0;
    static unsigned long lastReconnectAttempt = 0;
    const int MAX_RECONNECT_ATTEMPTS = 3;
    const unsigned long RECONNECT_RESET_INTERVAL = 300000; // 5 minutes

    wifi_check_status();  // This handles NTP sync when connecting to external AP
    ArduinoOTA.handle();
    web_server_handle_client();
    
    if (shouldAttemptMqttConnection) {
        // If already connected, disconnect first to use new settings
        if (mqttClient.connected()) {
            mqttClient.disconnect();
            Serial.println("Disconnected existing MQTT connection.");
        }
        
        Serial.println("Starting MQTT connection attempts...");
        setupMQTT(); // This function already contains the 3-attempt logic
        shouldAttemptMqttConnection = false; // Reset the flag so it only runs once per trigger
    }
    if (mqttClient.connected()) {
        mqttClient.loop();
    }  
    DateTime now = rtc_get_current_time();
     
    uint32_t currentTime = now.unixtime();
   
    static uint32_t lastCheckTime = 0;
    if (currentTime - lastCheckTime >= CHECK_INTERVAL / 1000) {  // Convert CHECK_INTERVAL to seconds
        lastCheckTime = currentTime;
        syncDeviceState();
        checkAndActivateTimers();
    }

    }

void logRTCTime(const char* context) {
    DateTime now = rtc_get_current_time();
    Serial.printf("[%s] Local time: %04d-%02d-%02d %02d:%02d:%02d\n", 
                  context, now.year(), now.month(), now.day(),
                  now.hour(), now.minute(), now.second());
}

void startAPMode() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("PowerMate-ESP32", "powermate123");
    Serial.println("AP Mode started");
    Serial.print("AP IP address: ");
    Serial.println(WiFi.softAPIP());
}

void setupMQTT() {
    // Ensure the MQTT IP is loaded from EEPROM
    if (mqttServer == nullptr || strlen(mqttServer) == 0) {
        Serial.println("No valid MQTT IP loaded. Using default.");
        mqttServer = "192.168.0.27"; // Default MQTT IP
    }

    Serial.printf("Using MQTT Server: %s\n", mqttServer);

    mqttClient.setServer(mqttServer, mqttPort);
    mqttClient.setCallback(mqttCallback);

    int retryCount = 0;
    while (!mqttClient.connected() && retryCount < 3) {
        Serial.printf("Connecting to MQTT broker... (Attempt %d/3)\n", retryCount + 1);
        if (mqttClient.connect("powermateClient")) {
            Serial.println("Connected to MQTT broker");
            mqttClient.subscribe(mqttTopic); // Subscribe to the control topic
            Serial.printf("Subscribed to topic: %s\n", mqttTopic);
            break;
        } else {
            Serial.printf("Failed to connect to MQTT broker, rc=%d. Retrying in 5 seconds...\n", mqttClient.state());
            retryCount++;
            if (retryCount < 3) {
                delay(5000);
            }
        }
    }
    if (retryCount >= 3) {
        Serial.println("Failed to connect to MQTT broker after 3 attempts. Will try again later.");
    }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String message;
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }

    Serial.printf("MQTT message received on topic %s: %s\n", topic, message.c_str());

    if (String(topic) == mqttTopic) {
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

String generateTimeFormFields() {
    int timezone = rtc_get_timezone_offset();
    String html = "<form id='updateTimeForm' class='time-update-form' action='/update-time' method='POST' onsubmit='return validateForm()'>";
    
    // DST Checkbox
    html += "<div class='form-group' style='grid-column: 1 / -1; flex-direction: row; align-items: center;'>";
    html += "<label for='useDST' style='margin-right: 10px;'>Enable Daylight Saving Time:</label>";
    html += "<input type='checkbox' id='useDST' name='useDST' value='1'";
    if (rtc_is_dst_enabled()) {
        html += " checked";
    }
    html += "></div>";
    
    // Timezone Select
    html +=  "<div class='form-group'>"
             "<label for='timezone'>Timezone:</label>"
             "<select id='timezone' name='timezone' required>";
        
    // Add all timezone options
    const char* cities[] = {
        " (Baker Island)", " (American Samoa)", " (Hawaii)", " (Alaska)", 
        " (Los Angeles)", " (Denver)", " (Chicago)", " (New York)",
        " (Santiago)", " (São Paulo)", " (South Georgia)", " (Azores)",
        " (London)", " (Paris, Berlin)", " (Cairo, Kiev)", " (Moscow)",
        " (Dubai)", " (Karachi)", " (Dhaka)", " (Bangkok)",
        " (Singapore, Beijing)", " (Tokyo)", " (Sydney)", 
        " (Solomon Islands)", " (Auckland)"
    };
 
    for (int i = -12; i <= 12; i++) {
        html += "<option value='" + String(i) + "'";
        if (i == timezone) {
            html += " selected";
        }
        html += ">UTC" + (i >= 0 ? "+" + String(i) : String(i));
        html += cities[i + 12];
        html += "</option>";
    }
 
    html += "</select></div>";

    // Date and Time Input Fields
    html += "<div class='form-group'>"
            "<label for='year'>Year:</label>"
            "<input type='number' id='year' name='year' placeholder='Year' min='2024' max='2099' required>"
            "</div>"
            "<div class='form-group'>"
            "<label for='month'>Month:</label>"
            "<input type='number' id='month' name='month' placeholder='Month' min='1' max='12' required>"
            "</div>"
            "<div class='form-group'>"
            "<label for='day'>Day:</label>"
            "<input type='number' id='day' name='day' placeholder='Day' min='1' max='31' required>"
            "</div>"
            "<div class='form-group'>"
            "<label for='hour'>Hour:</label>"
            "<input type='number' id='hour' name='hour' placeholder='Hour' min='0' max='23' required>"
            "</div>"
            "<div class='form-group'>"
            "<label for='minute'>Minute:</label>"
            "<input type='number' id='minute' name='minute' placeholder='Minute' min='0' max='59' required>"
            "</div>"
            "<div class='form-group'>"
            "<label for='second'>Second:</label>"
            "<input type='number' id='second' name='second' placeholder='Second' min='0' max='59' required>"
            "</div>";

    // Buttons
    html += "<div class='button-grid'>"
            "<button type='submit' class='submit-button'>Update Clock</button>"
            "<a href='/' class='button'>Back to Dashboard</a>"
            "</div>"
            "</form>"
            "<div id='errorMessage' style='color: red;'></div>";

    return html;
}

bool validateTime(const DateTime& time) {
    return time.isValid() && 
           time.year() >= 2024 && time.year() <= 2099 &&
           time.hour() < 24 && time.minute() < 60 && time.second() < 60;
}

bool validateTimerSettings(const TriggerTime& timer) {

    return timer.hour < 24 && 
           timer.minute < 60 && 
           timer.activationDuration > 0 &&
           timer.delayBetweenActivations > 0 &&
           timer.repetitions >= 0;
}

bool shouldActivateTimer(int timerIndex, DateTime now) {
    if (triggerStates[timerIndex].activations >= triggerDays[timerIndex].repetitions) {
        return false;
    }

    uint32_t delayInSeconds = triggerDays[timerIndex].delayBetweenActivations / 1000;
    uint32_t timeSinceLastDeactivation = getTimeDifference(now, 
        DateTime(triggerStates[timerIndex].lastDeactivationTime));

    return (triggerDays[timerIndex].hour == now.hour() && 
            triggerDays[timerIndex].minute == now.minute()) ||
           (triggerStates[timerIndex].lastDeactivationTime != 0 &&
            timeSinceLastDeactivation >= delayInSeconds);
}

bool shouldDeactivateTimer(int timerIndex, DateTime now) {
    if (!triggerStates[timerIndex].active) return false;
    
    uint32_t activationDuration = getTimeDifference(now, 
        DateTime(triggerStates[timerIndex].startTime));
        
    return activationDuration >= triggerDays[timerIndex].activationDuration / 1000;
}

void updateTimerStateForActivation(int timerIndex, DateTime now) {
    triggerStates[timerIndex].active = true;
    triggerStates[timerIndex].startTime = now.unixtime();
    triggerStates[timerIndex].activations++;
    triggerStates[timerIndex].firstActivationPending = false;
}

void updateTimerStateForDeactivation(int timerIndex, DateTime now) {
    triggerStates[timerIndex].active = false;
    triggerStates[timerIndex].lastDeactivationTime = now.unixtime();
}

bool handleTimerStateChange(int timerIndex, DateTime now) {
    if (!triggerStates[timerIndex].active) {
        // Device is OFF - check if it should turn ON
        if (shouldActivateTimer(timerIndex, now)) {
            if (!turnDeviceOn()) {
                resetTriggerState(timerIndex);
                return false;
            }
            updateTimerStateForActivation(timerIndex, now);
        }
    } else {
        // Device is ON - check if it should turn OFF
        if (shouldDeactivateTimer(timerIndex, now)) {
            if (!turnDeviceOff()) {
                resetTriggerState(timerIndex);
                return false;
            }
            updateTimerStateForDeactivation(timerIndex, now);
        }
    }
    return true;
}

uint32_t getTimeDifference(DateTime current, DateTime previous) {   //Function to convert unix time to avoid unixtimestamp overflow
    if (!current.isValid() || !previous.isValid()) return 0;
    
    uint32_t currentSeconds = 
        current.hour() * 3600 + 
        current.minute() * 60 + 
        current.second();
        
    uint32_t previousSeconds = 
        previous.hour() * 3600 + 
        previous.minute() * 60 + 
        previous.second();
        
    return currentSeconds >= previousSeconds ? 
           currentSeconds - previousSeconds : 
           (24 * 3600) - (previousSeconds - currentSeconds);
}

uint32_t getTimeSinceLastDeactivation(const DateTime& now, uint32_t lastDeactivationTime) {
    if (lastDeactivationTime == 0) return 0;
    return now.unixtime() - lastDeactivationTime;
}

void checkAndActivateTimers() {
    //Serial.println("Starting checkAndActivateTimers");

    if (bypassModeEnabled) {
        Serial.println("Bypass mode is enabled. Skipping timer checks.");
        return; // Skip all timer checks
    }

    if (!rtc_is_ok()) {
        Serial.println("RTC validation failed, skipping timer checks");
        return;
    }
    
    DateTime now = rtc_get_current_time();
    if (!now.isValid()) {
        Serial.println("Invalid time detected, skipping timer checks");
        return;
    }

    resetActivationsOnDayChange();

    int currentDay = now.dayOfTheWeek(); // Get the current day of the week (0 = Sunday, 6 = Saturday)
    byte currentHour = now.hour();
    byte currentMinute = now.minute();
    
    for (int i = 0; i < safeNumberOfTimers(); i++) {
        // Check if the current day is active
        if (days[currentDay] == 0) {
           // Serial.printf("Timer %d skipped because today is not an active day\n", i);
            continue; // Skip this timer if the current day is not active
        }

        uint32_t delayInSeconds = triggerDays[i].delayBetweenActivations / 1000;
        uint32_t timeSinceLastDeactivation = getTimeSinceLastDeactivation(now, triggerStates[i].lastDeactivationTime);
        
        bool isCorrectTime = (triggerDays[i].hour == currentHour && 
                            triggerDays[i].minute == currentMinute) ||
                            (triggerStates[i].lastDeactivationTime != 0 && 
                             timeSinceLastDeactivation >= delayInSeconds);

        if (!triggerStates[i].active) {
            if (triggerStates[i].activations >= triggerDays[i].repetitions) {
                //Serial.printf("All repetitions (%d) completed for today\n", triggerDays[i].repetitions);
                continue;
            }

            if (isCorrectTime && (triggerStates[i].firstActivationPending || timeSinceLastDeactivation >= delayInSeconds)) {
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
            uint32_t activationDuration = now.unixtime() - triggerStates[i].startTime;
            uint32_t requiredDuration = triggerDays[i].activationDuration / 1000; // Convert to seconds
            //Serial.printf("Timer %d: Current duration %lu seconds, Required duration %lu seconds\n", 
            //             i, activationDuration, requiredDuration);
            if (activationDuration >= requiredDuration) {
                if (turnDeviceOff()) {
                    triggerStates[i].active = false;
                    triggerStates[i].lastDeactivationTime = now.unixtime();
                    Serial.printf("Timer %d deactivated. Next activation in %d seconds.\n", i, delayInSeconds);
                }
            }
        }
    }
}

bool turnDeviceOn() {
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
      Serial.println("[main.cpp] Requesting to log OFF action..."); 
      storage_log_to_file(newLog); // First, write the log to the permanent file
      add_log_to_ram_buffer(newLog); // THEN, update the live RAM buffer for the web page
    }
    return success;
}

void resetActivationsOnDayChange() {
    static int lastCheckedDay = -1;
    DateTime now = rtc_get_current_time();
    int currentDay = now.dayOfTheWeek();
    
    if (lastCheckedDay != currentDay) {
        Serial.printf("Day change detected. Old day: %d, New day: %d\n", lastCheckedDay, currentDay);        
        for (int i = 0; i < safeNumberOfTimers(); i++) {
            triggerStates[i].activations = 0;
            triggerStates[i].firstActivationPending = true;
            triggerStates[i].lastDeactivationTime = 0;
            
            if (triggerStates[i].active) {
                turnDeviceOff();
                triggerStates[i].active = false;
                Serial.printf("Timer %d was active and stopped due to day change.\n", i);
            }
            
            Serial.printf("Reset state for Timer %d\n", i);
        }
        
        lastCheckedDay = currentDay;
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
    ArduinoOTA.setHostname("PowerMate-ESP32");
    ArduinoOTA.onStart([]() {
        String type = ArduinoOTA.getCommand() == U_FLASH ? "sketch" : "filesystem";
        Serial.println("Start updating " + type);
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("\nEnd");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });
    ArduinoOTA.begin();
}

void resetTimerState(int timerId) {
    if (timerId == 0) {
        timer0Active = false; // Or some state reset
        Serial.println("Reset state for Timer 0");
    } else if (timerId == 1) {
        timer1Active = false;
        Serial.println("Reset state for Timer 1");
    } else {
        Serial.printf("Invalid timerId: %d\n", timerId);
    }
}

void logInitialRTCTime() {
    char datestring[20];
    snprintf_P(datestring, 
               sizeof(datestring),
               PSTR("%04u/%02u/%02u %02u:%02u:%02u"),
               initialRtcTime.year(),
               initialRtcTime.month(),
               initialRtcTime.day(),
               initialRtcTime.hour(),
               initialRtcTime.minute(),
               initialRtcTime.second());
    Serial.print("Initial RTC time: ");
    Serial.println(datestring);
}

void logTimerState(int i) {
    Serial.printf("Timer %d State:\n", i);
    Serial.printf("  Set Time: %02d:%02d\n", triggerDays[i].hour, triggerDays[i].minute);
    Serial.printf("  Activation Duration: %lu ms (%lu seconds)\n", 
                  triggerDays[i].activationDuration, 
                  triggerDays[i].activationDuration / 1000UL);
    Serial.printf("  Delay Between Activations: %lu ms (%lu seconds)\n", 
                  triggerDays[i].delayBetweenActivations, 
                  triggerDays[i].delayBetweenActivations / 1000UL);
    Serial.printf("  Repetitions: %d\n", triggerDays[i].repetitions);
    Serial.printf("  Current Activations: %d\n", triggerStates[i].activations);
    Serial.printf("  Is Active: %s\n", triggerStates[i].active ? "Yes" : "No");
    Serial.printf("  Last Deactivation Time: %lu\n", triggerStates[i].lastDeactivationTime);
    Serial.printf("  First Activation Pending: %s\n", triggerStates[i].firstActivationPending ? "Yes" : "No");
}


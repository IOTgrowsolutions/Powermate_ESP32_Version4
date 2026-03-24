#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "web_interface.h"
#include "web_server.h"      // Include to get the 'extern WebServer server'
#include "storage_manager.h" 
#include "project_types.h"

// External global variables from main.cpp that we need to modify
extern TriggerTime triggerDays[MAX_TIMERS];
extern int days[7];
extern int numberOfTimers;

// --- Forward Declarations for Local Functions ---
void handleCalculateSchedule();
String getContentType(String filename);
bool handleFileRead(String path);

/**
 * @brief Registers all calculator-related endpoints to the main web server.
 */
void register_calculator_endpoints() {
    server.on("/crop-steering.html", HTTP_GET, []() { handleFileRead("/crop-steering.html"); });
    server.on("/styles.css", HTTP_GET, []() { handleFileRead("/styles.css"); });
    server.on("/irrigation-calculator.js", HTTP_GET, []() { handleFileRead("/irrigation-calculator.js"); });
    server.on("/calculate-schedule", HTTP_POST, handleCalculateSchedule);
    Serial.println("Calculator endpoints registered.");
}


/**
 * @brief Handles the POST request from the calculator form.
 * This function reads the JSON and configures the FIRST trigger
 * with repetitions and delays.
 */
void handleCalculateSchedule() {
    if (!server.hasArg("plain")) {
        server.send(400, "text/plain", "Bad Request: Missing body");
        return;
    }

    String body = server.arg("plain");
    StaticJsonDocument<1024> doc; 
    DeserializationError error = deserializeJson(doc, body);

    if (error) {
        server.send(400, "text/plain", "Invalid JSON format");
        return;
    }

    // --- Get final schedule parameters (always present) ---
    const char* startTimeStr = doc["startTime"] | "08:00";
    int frequency = doc["frequency"];
    int durationPerSession = doc["durationPerSession"];
    int delayBetweenActivations = doc["delayBetweenActivations"]; // in minutes

    int startHour = 8;
    int startMinute = 0;
    sscanf(startTimeStr, "%d:%d", &startHour, &startMinute);

    Serial.println("Received new schedule from calculator:");
    Serial.printf("Final Schedule -> Start: %02d:%02d, Reps: %d, Duration: %d sec, Delay: %d min\n", 
                  startHour, startMinute, frequency, durationPerSession, delayBetweenActivations);

    // --- NEW: Check for and log advanced parameters if they exist ---
    if (doc["mode"] == "advanced") {
        const char* substrate = doc["substrateType"] | "N/A";
        const char* growthStage = doc["growthStage"] | "N/A";
        float temp = doc["ambientTemperature"] | -1.0;
        float humidity = doc["humidityLevel"] | -1.0;
        
        Serial.println("Advanced Parameters Received:");
        Serial.printf("  - Substrate: %s\n", substrate);
        Serial.printf("  - Growth Stage: %s\n", growthStage);
        Serial.printf("  - Temperature: %.1f C\n", temp);
        Serial.printf("  - Humidity: %.1f %%\n", humidity);
    } else {
        const char* profile = doc["plantProfile"] | "N/A";
        Serial.printf("Simple Mode Profile: %s\n", profile);
    }


    // --- Apply final schedule parameters to the first trigger ---
    numberOfTimers = 1;

    triggerDays[0].hour = startHour;
    triggerDays[0].minute = startMinute;
    triggerDays[0].second = 0;
    triggerDays[0].repetitions = frequency;
    triggerDays[0].activationDuration = durationPerSession * 1000UL; // Convert seconds to milliseconds
    triggerDays[0].delayBetweenActivations = delayBetweenActivations * 60000UL; // Convert minutes to milliseconds

    // Clear settings for all other timers
    for (int i = 1; i < MAX_TIMERS; i++) {
        triggerDays[i] = {0, 0, 0, 0, 0, 0};
    }
    
    // Activate for all days of the week
    for(int i=0; i<7; i++) { days[i] = 1; }

    storage_save_settings(numberOfTimers, triggerDays, days);

    server.send(200, "application/json", "{\"status\":\"success\", \"message\":\"Schedule updated successfully!\"}");
}

// --- File Serving Utilities ---
String getContentType(String filename) {
    if (filename.endsWith(".html")) return "text/html";
    if (filename.endsWith(".css")) return "text/css";
    if (filename.endsWith(".js")) return "application/javascript";
    return "text/plain";
}

bool handleFileRead(String path) {
    if (LittleFS.exists(path)) {
        File file = LittleFS.open(path, "r");
        server.streamFile(file, getContentType(path));
        file.close();
        return true;
    }
    Serial.println("File Not Found: " + path);
    server.send(404, "text/plain", "Not Found");
    return false;
}

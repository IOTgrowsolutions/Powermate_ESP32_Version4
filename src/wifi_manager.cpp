#include "wifi_manager.h"
#include "storage_manager.h" // We will create this file next
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
// These variables are now "private" to this module.
DNSServer dnsServer;
const byte DNS_PORT = 53;
const char* password = "powerm8te";

// Forward declaration for the helper function that is only used in this file.
String getLastFourOfMAC(); 

// --- IMPLEMENTATION ---

// Renamed from setupWiFi to match the header file.
void wifi_setup() {
    String uniqueSSID = wifi_get_unique_ssid(); // Use the new function name
    Serial.println("Setting up WiFi with SSID: " + uniqueSSID);

    // Set WiFi mode to AP+STA
    if (!WiFi.mode(WIFI_AP_STA)) {
        Serial.println("Failed to set WiFi mode to AP+STA");
        return;
    }

    // Start AP with unique SSID
    bool apSuccess = WiFi.softAP(uniqueSSID.c_str(), password);
    if (apSuccess) {
        Serial.println("AP setup successful");
    } else {
        Serial.println("AP setup failed");
        return;
    }
    delay(200); // Increased delay for AP initialization

    // Get and print AP IP
    IPAddress AP_IP = WiFi.softAPIP();
    if (!AP_IP) {
        Serial.println("Failed to get AP IP address");
        return;
    }
    Serial.print("AP IP address: ");
    Serial.println(AP_IP);

    // Try to load and connect to saved WiFi if exists
    WiFiConfig config;
    if (storage_load_wifi_config(config)) { // This function will be moved to storage_manager
        if (strlen(config.ssid) == 0 || strlen(config.password) == 0) {
            Serial.println("Invalid Wi-Fi credentials");
            // No return here, just proceed to start DNS server
        } else {
            Serial.printf("Attempting to connect to saved WiFi: %s\n", config.ssid);
            WiFi.begin(config.ssid, config.password);

            // Wait up to 10 seconds for connection
            int attempts = 0;
            while (WiFi.status() != WL_CONNECTED && attempts < 20) {
                delay(500);
                yield(); // Allow background tasks to run
                Serial.print(".");
                attempts++;
            }

            if (WiFi.status() == WL_CONNECTED) {
                Serial.println("\nConnected to WiFi");
                Serial.print("STA IP Address: ");
                Serial.println(WiFi.localIP());
                String hostname = "powermate-" + WiFi.macAddress();
                hostname.replace(":", "");

                if (MDNS.begin(hostname.c_str())) {
                    Serial.println("mDNS responder started");
                    
                    // Add service with more detailed information
                    MDNS.addService("powermate", "tcp", 80);
                    
                    // Add device information as TXT records
                    MDNS.addServiceTxt("powermate", "tcp", "version", "4.1"); // Updated version from your spec
                    MDNS.addServiceTxt("powermate", "tcp", "mac", WiFi.macAddress());
                    MDNS.addServiceTxt("powermate", "tcp", "ip", WiFi.localIP().toString());
                    MDNS.addServiceTxt("powermate", "tcp", "hostname", hostname);

                } else {
                    Serial.println("Error setting up mDNS responder!");
                }
            } else {
                Serial.println("\nFailed to connect to saved WiFi");
                // Stay in AP mode if connection fails
                WiFi.disconnect();
            }
        }
    } else {
        Serial.println("No saved WiFi credentials found");
    }

    // Start DNS server for captive portal
    if (!dnsServer.start(DNS_PORT, "*", AP_IP)) {
        Serial.println("Failed to start DNS server");
        return;
    }
}

// Renamed from checkWiFiStatus to match the header file
void wifi_check_status() {
    static unsigned long lastWiFiCheck = 0;
    static bool wasConnected = false;

    if (millis() - lastWiFiCheck > 30000) {  // Check every 30 seconds
        lastWiFiCheck = millis();

        if (WiFi.status() == WL_CONNECTED) {
            if (!wasConnected) {
                Serial.println("WiFi connected to external AP");
                Serial.print("IP address: ");
                Serial.println(WiFi.localIP());
                wasConnected = true;
            }
        } else {
            if (wasConnected) {
                Serial.println("WiFi connection lost");
                wasConnected = false;

                // Try to reconnect if we have credentials
                WiFiConfig config;
                if (storage_load_wifi_config(config)) { // This function will be moved to storage_manager
                    Serial.println("Attempting to reconnect...");
                    WiFi.begin(config.ssid, config.password);
                }
            }
        }
    }
    
    // The DNS server needs to be processed in the main loop or here.
    // Let's keep it here for now to ensure the captive portal works.
    dnsServer.processNextRequest();
}

// Renamed from generateUniqueSSID to match the header file
String wifi_get_unique_ssid() {
  String baseName = "PowerMate-";
  String uniquePart = getLastFourOfMAC();
  return baseName + uniquePart;
}


// This is a "private" helper function. It's not in the .h file.
String getLastFourOfMAC() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char macStr[5];
  snprintf(macStr, sizeof(macStr), "%02X%02X", mac[4], mac[5]);
  return String(macStr);
}

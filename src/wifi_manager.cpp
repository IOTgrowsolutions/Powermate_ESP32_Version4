#include "wifi_manager.h"
#include "storage_manager.h" // We will create this file next
#include <WiFi.h>  // Keep for compatibility
#include <DNSServer.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <mdns.h>
// These variables are now "private" to this module.
DNSServer dnsServer;
const byte DNS_PORT = 53;
const char* password = "powerm8te";

// Forward declaration for the helper function that is only used in this file.
String getLastFourOfMAC(); 
static void _wifi_setup_mdns();
// --- IMPLEMENTATION ---

// Forward-declare event handler so it's easier to find in the file.
// ESP-IDF event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
void wifi_notify_got_ip() __attribute__((weak));
inline void wifi_notify_got_ip() {}
void wifi_notify_ap_ready() __attribute__((weak));
inline void wifi_notify_ap_ready() {}

// Renamed from setupWiFi to match the header file.
void wifi_setup() {
    String uniqueSSID = wifi_get_unique_ssid();
    Serial.println("Setting up WiFi with SSID: " + uniqueSSID);

    // Let Arduino own the stack init — this prevents double-init of
    // esp_netif/event loop that causes WiFiUdp parsePacket errors.
    WiFi.mode(WIFI_AP_STA);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);

    // AP setup via Arduino (internally calls esp_wifi_set_config)
    WiFi.softAP(uniqueSSID.c_str(), password);
    delay(100); // AP netif needs a moment to assign IP

    IPAddress AP_IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(AP_IP);

    // mDNS via ESP-IDF is safe to init after Arduino brings up the stack
    mdns_init();

    // Load saved credentials and connect STA
    WiFiConfig config;
    if (storage_load_wifi_config(config)) {
        if (strlen(config.ssid) == 0 || strlen(config.password) == 0) {
            Serial.println("Invalid Wi-Fi credentials");
        } else {
            Serial.printf("Attempting to connect to saved WiFi: %s\n", config.ssid);

            // esp_wifi_set_config is safe here — Arduino already called esp_wifi_init
            wifi_config_t sta_config = {};
            strncpy((char*)sta_config.sta.ssid, config.ssid, sizeof(sta_config.sta.ssid) - 1);
            strncpy((char*)sta_config.sta.password, config.password, sizeof(sta_config.sta.password) - 1);
            esp_wifi_set_config(WIFI_IF_STA, &sta_config);
            esp_wifi_connect();

            int attempts = 0;
            while (WiFi.status() != WL_CONNECTED && attempts < 20) {
                delay(500);
                Serial.print(".");
                attempts++;
            }

            if (WiFi.status() == WL_CONNECTED) {
                Serial.println("\nConnected to WiFi");
                Serial.println(WiFi.localIP());
                _wifi_setup_mdns(); // see Issue 3
            } else {
                Serial.println("\nFailed to connect to saved WiFi");
                esp_wifi_disconnect();
            }
        }
    } else {
        Serial.println("No saved WiFi credentials found");
    }

    if (!dnsServer.start(DNS_PORT, "*", AP_IP)) {
        Serial.println("Failed to start DNS server");
    }
    wifi_notify_ap_ready(); // AP is up — safe to start OTA
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
                 _wifi_setup_mdns();
                wasConnected = true;
            }
        } else {
            if (wasConnected) {
                // we just went from connected to disconnected
                Serial.println("WiFi connection lost");
                Serial.printf("Status: %d\n", WiFi.status());
                wasConnected = false;

                // Try to reconnect if we have credentials
                WiFiConfig config;
                if (storage_load_wifi_config(config)) { // This function will be moved to storage_manager
                    Serial.println("Attempting to reconnect...");
                    // Reconfigure and connect
                    wifi_config_t sta_config = {};
                    strncpy((char*)sta_config.sta.ssid, config.ssid, sizeof(sta_config.sta.ssid) - 1);
                    strncpy((char*)sta_config.sta.password, config.password, sizeof(sta_config.sta.password) - 1);
                    
                    esp_wifi_set_config(WIFI_IF_STA, &sta_config);
                    esp_wifi_connect();
                   
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


static void _wifi_setup_mdns() {
    String hostname = WiFi.softAPSSID();
    hostname.toLowerCase();

    mdns_hostname_set(hostname.c_str());
    mdns_instance_name_set("PowerMate Device");

    // Only add the service if not already registered
    esp_err_t err = mdns_service_add(NULL, "_powermate", "_tcp", 80, NULL, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_ARG) {
        Serial.printf("mDNS service_add error: %d\n", err);
        return;
    }

    // Always update TXT records — IP changes on each reconnect
    mdns_service_txt_item_set("_powermate", "_tcp", "version", "4.1");
    mdns_service_txt_item_set("_powermate", "_tcp", "mac", WiFi.macAddress().c_str());
    mdns_service_txt_item_set("_powermate", "_tcp", "ip", WiFi.localIP().toString().c_str());
    mdns_service_txt_item_set("_powermate", "_tcp", "hostname", hostname.c_str());
    Serial.println("mDNS responder started");
}

bool wifi_is_connected() {
    return WiFi.status() == WL_CONNECTED;
}

String wifi_get_local_ip() {
    return WiFi.localIP().toString();
}

String wifi_get_ap_ip() {
    return WiFi.softAPIP().toString();
}

// Event handler implementation for ESP-IDF
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*) event_data;
                Serial.printf("WiFi event: STA disconnected (reason %d)\n", event->reason);
                break;
            }
            case WIFI_EVENT_STA_CONNECTED:
                Serial.println("WiFi event: STA connected");
                break;
            case WIFI_EVENT_AP_START:
                Serial.println("WiFi event: AP started");
                break;
            default:
                break;
        }
    
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
            Serial.printf("Got IP: " IPSTR "\n", IP2STR(&event->ip_info.ip));
            wifi_notify_got_ip(); // safe to sync NTP now
        }
    
}
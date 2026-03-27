#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>

// Initializes and starts the WiFi in AP + STA mode.
void wifi_setup();

// Checks the WiFi connection status and handles reconnections.
void wifi_check_status();

// Generates the unique "PowerMate-XXXX" SSID.
String wifi_get_unique_ssid();

void wifi_notify_ap_ready();
void wifi_notify_got_ip();

// Read-only status accessors — safe to call from main.cpp
bool wifi_is_connected();
String wifi_get_local_ip();
String wifi_get_ap_ip();

#endif // WIFI_MANAGER_H
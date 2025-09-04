# PowerMate ESP32 - Smart Power Management System

[![PlatformIO](https://img.shields.io/badge/PlatformIO-IDE-blue)](https://platformio.org/)
[![ESP32](https://img.shields.io/badge/ESP32-DevKit-green)](https://www.espressif.com/en/products/som/esp32)
[![Arduino](https://img.shields.io/badge/Framework-Arduino-orange)](https://www.arduino.cc/)

PowerMate is an advanced ESP32-based power management device that provides automated timer-based control of electrical devices through a relay system. It features a comprehensive web interface, MQTT integration, and robust time management capabilities.

## 🚀 Features

### Core Functionality
- **Automated Power Control**: Up to 6 configurable timers with programmable activation/deactivation schedules
- **Relay Control**: High-power relay switching for controlling external devices
- **Real-Time Clock**: DS3231 RTC with NTP synchronization and DST support
- **Activity Logging**: Comprehensive logging system with persistent storage

### Connectivity & Communication
- **Dual WiFi Modes**: Access Point (AP) + Station (STA) mode for flexible networking
- **Web Interface**: Modern, responsive dashboard for configuration and monitoring
- **MQTT Integration**: Remote control and monitoring via MQTT protocol
- **OTA Updates**: Over-the-Air firmware updates for seamless upgrades

### Advanced Features
- **Daylight Saving Time**: Automatic DST adjustment with configurable rules
- **Timezone Support**: Global timezone support from UTC-12 to UTC+12
- **Request Limiting**: Built-in rate limiting for security
- **Persistent Storage**: EEPROM-based configuration with LittleFS for web assets
- **Device Discovery**: mDNS support for easy network discovery

## 🛠️ Hardware Requirements

- **ESP32 Development Board** (ESP32-DevKitC recommended)
- **DS3231 RTC Module** (I2C interface)
- **Relay Module** (connected to GPIO 16)
- **Power Supply** (appropriate for your relay and ESP32)
- **Optional**: External antenna for better WiFi range

### Pin Configuration
```cpp
// I2C Pins
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22

// Relay Pin
#define relayPin 16  // ESP32_AC = 16 or 33
```

## 📦 Software Dependencies

This project uses PlatformIO with the following libraries:
- `LittleFS` - File system for web assets
- `RTClib@^2.1.4` - Real-time clock management
- `PubSubClient@^2.8` - MQTT client
- `ArduinoJson@7.2.0` - JSON parsing and generation

## 🚀 Installation & Setup

### 1. Clone the Repository
```bash
git clone https://github.com/yourusername/Powermate_ESP32_Version4.git
cd Powermate_ESP32_Version4
```

### 2. Install PlatformIO
```bash
# Install PlatformIO CLI
pip install platformio

# Or install PlatformIO IDE for VS Code
# https://platformio.org/install/ide
```

### 3. Configure PlatformIO
The `platformio.ini` file is pre-configured for ESP32 development:
```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
upload_speed = 115200
board_build.filesystem = littlefs
monitor_speed = 115200
```

### 4. Build and Upload
```bash
# Build the project
pio run

# Upload to ESP32
pio run --target upload

# Upload filesystem (web assets)
pio run --target uploadfs

# Monitor serial output
pio device monitor
```

## 🔧 Configuration

### Initial Setup
1. Power on the ESP32 - it will create an Access Point named "PowerMate-ESP32"
2. Connect to the AP using password: `powermate123`
3. Open a browser and navigate to `http://192.168.4.1`
4. Configure WiFi settings and device preferences

### Timer Configuration
- **Schedule**: Set activation times (hour/minute)
- **Duration**: Define how long the device should stay active
- **Repetitions**: Number of times to repeat the activation
- **Delay**: Time between repetitions
- **Active Days**: Select which days of the week to run the timer

### MQTT Setup
```cpp
const char* mqttServer = "YOUR_MQTT_BROKER_IP";
const int mqttPort = 1883;
const char* mqttTopic = "pm/control";
```

Send MQTT messages:
- `"on"` - Turn device on
- `"off"` - Turn device off

## 🌐 Web Interface

The PowerMate dashboard provides:
- **System Status**: Real-time clock, device state, network information
- **Timer Management**: Configure and monitor all timers
- **Activity Logs**: View device activation history
- **Network Settings**: WiFi and MQTT configuration
- **System Information**: Firmware version, uptime, and diagnostics

### Access Points
- **Dashboard**: `/` - Main control interface
- **Timer Settings**: `/timers` - Configure timer schedules
- **Network Setup**: `/wifi` - WiFi and MQTT configuration
- **Logs**: `/logs` - View activity history
- **System Info**: `/info` - System status and information

## 📊 Data Structures

### Timer Configuration
```cpp
struct TriggerTime {
    uint8_t hour;                    // Activation hour (0-23)
    uint8_t minute;                  // Activation minute (0-59)
    uint8_t second;                  // Activation second (0-59)
    unsigned long activationDuration;    // How long to stay active (ms)
    unsigned long delayBetweenActivations; // Delay between repeats (ms)
    int repetitions;                 // Number of repetitions per day
};
```

### Log Entry
```cpp
struct LogEntry {
    uint32_t unix_time;  // Timestamp
    bool relayState;     // true = ON, false = OFF
};
```

## 🔍 API Endpoints

### Timer Management
- `GET /timers` - Get current timer configuration
- `POST /update-timer` - Update timer settings
- `POST /toggle-timer` - Enable/disable specific timer

### Device Control
- `GET /toggle` - Toggle device state
- `GET /status` - Get current device status

### System Configuration
- `POST /update-time` - Update system clock
- `POST /wifi-config` - Update WiFi settings
- `GET /logs` - Retrieve activity logs

## 🐛 Troubleshooting

### Common Issues

1. **WiFi Connection Problems**
   - Check SSID and password configuration
   - Verify network compatibility (2.4GHz only)
   - Check signal strength and antenna placement

2. **RTC Time Issues**
   - Ensure DS3231 is properly connected to I2C pins
   - Check battery backup on RTC module
   - Verify NTP server accessibility

3. **MQTT Connection Issues**
   - Verify broker IP address and port
   - Check network firewall settings
   - Ensure MQTT broker is running and accessible

4. **Web Interface Not Loading**
   - Clear browser cache
   - Check LittleFS filesystem upload
   - Verify ESP32 is in AP+STA mode

### Debug Logging
Enable verbose logging by modifying `esp_log_level_set()` in `main.cpp`:
```cpp
esp_log_level_set("*", ESP_LOG_DEBUG);
```

## 🤝 Contributing

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

## 📝 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## 📞 Support

For questions, issues, or contributions:
- Open an issue on GitHub
- Check the troubleshooting section above
- Review the code comments for implementation details

## 🔄 Version History

### Version 4.0
- Complete rewrite with modular architecture
- Enhanced web interface with modern design
- Improved MQTT integration
- DST support and timezone management
- Persistent logging system
- Request limiting for security
- OTA update capability

---

**Device Name**: PowerMate-{MAC_LAST_4}
**Default AP**: PowerMate-ESP32
**Default Password**: powermate123
**Firmware Version**: 4.0
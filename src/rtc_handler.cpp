#include "rtc_handler.h"
#include "storage_manager.h"
#include <Wire.h>

// --- Private variables for the RTC handler ---
RTC_DS3231 Rtc;
TwoWire I2C_BUS = TwoWire(0);

// These variables are now properly hidden inside this module
bool rtcStatus = false;
unsigned long lastKnownValidTime = 0;
int8_t tz = 0;
bool useDST = false;

// --- Private Helper Functions (not in .h file) ---
static bool isLeapyear(uint16_t year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static int getLastSunday(int year, int month) {
    const int daysInMonth[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int days = daysInMonth[month];
    if (month == 2 && isLeapyear(year)) {
        days = 29;
    }
    DateTime lastDay(year, month, days);
    int dayOfWeek = lastDay.dayOfTheWeek();
    return days - dayOfWeek;
}

// --- Public Function Implementations ---

void rtc_init() {
    storage_load_timezone(tz, useDST);
    storage_load_time(lastKnownValidTime);
    Serial.print("Loaded tz: "); Serial.println(tz);
    Serial.print("Loaded useDST: "); Serial.println(useDST);
    Serial.print("Loaded lastKnownValidTime: "); Serial.println(lastKnownValidTime);
    if (!I2C_BUS.begin(21, 22)) {
        Serial.println("I2C bus initialization failed!");
        rtcStatus = false;
        return;
    }

    if (!Rtc.begin(&I2C_BUS)) {
        Serial.println("RTC init failed. Time will be based on last saved value.");
        rtcStatus = false;
        return;
    }

    if (Rtc.lostPower()) {
        Serial.println("RTC lost power, setting time to last known good value.");
        Rtc.adjust(DateTime(lastKnownValidTime));
    }

    rtcStatus = true;
    Serial.println("RTC init successful.");
}

bool rtc_is_ok() {
    return rtcStatus;
}

DateTime rtc_get_current_time() {
    if (!rtcStatus) {
        DateTime fallback(lastKnownValidTime + (millis() / 1000));
        // If RTC is bad, return time based on last known good value + millis()
        Serial.println("RTC is bad, returning time based on last known good value.");
        return fallback;
    }
    DateTime utc = Rtc.now();
    int32_t totalOffset = tz * 3600;
    
    if (useDST && rtc_is_dst(utc)) {
        totalOffset += 3600;
    }
    DateTime result(utc.unixtime() + totalOffset);
    //Serial.print("Final time: "); Serial.println(result.unixtime());
    return result;    
}

bool rtc_update_time(const DateTime& new_time_utc) {
    if (!new_time_utc.isValid()) return false;
    
    Rtc.adjust(new_time_utc);
    delay(50); // Give RTC time to settle
    
    // Verify the time was set correctly
    DateTime now = Rtc.now();
    if (abs((long)now.unixtime() - (long)new_time_utc.unixtime()) < 2) {
        lastKnownValidTime = new_time_utc.unixtime();
        storage_save_time(lastKnownValidTime); // Save the good time to EEPROM
        Serial.println("RTC time updated successfully.");
        return true;
    }
    
    Serial.println("Failed to verify RTC time update.");
    return false;
}

void rtc_save_timezone_settings(int8_t new_tz, bool new_use_dst) {
    tz = new_tz;
    useDST = new_use_dst;
    storage_save_timezone(tz, useDST); // Save to EEPROM using our storage manager
}

int8_t rtc_get_timezone_offset() {
    return tz;
}

bool rtc_is_dst(const DateTime& dt) {
    if (!useDST) return false;
    int month = dt.month();
    int day = dt.day();
    if (month < 3 || month > 10) return false;
    if (month > 3 && month < 10) return true;
    
    int startDay = getLastSunday(dt.year(), 3);
    int endDay = getLastSunday(dt.year(), 10);

    if (month == 3) return day >= startDay;
    if (month == 10) return day < endDay;
    
    return false;
}

bool rtc_is_dst_enabled() {
    return useDST; // 'useDST' is the private variable inside this module.
}
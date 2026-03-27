// In rtc_handler.h

#ifndef RTC_HANDLER_H
#define RTC_HANDLER_H

#include <RTClib.h>

// Initializes the I2C bus and the RTC module.
void rtc_init();

// Returns true if the RTC is functioning correctly.
bool rtc_is_ok();
bool rtc_is_dst_enabled();
// Gets the current time, adjusted for timezone and DST.
DateTime rtc_get_current_time();

int8_t rtc_get_timezone_offset();

void rtc_save_timezone_settings(int8_t new_tz, bool use_dst);
// Updates the RTC with a new time.
bool rtc_update_time(const DateTime& new_time);

// Checks if a given DateTime is within the DST period.
bool rtc_is_dst(const DateTime& dt);

bool rtc_sync_ntp();

int rtc_get_dst_start_month();
int rtc_get_dst_end_month();

#endif // RTC_HANDLER_H
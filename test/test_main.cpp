#include <unity.h>
#include <algorithm>

// Mock definitions for native testing
const int MIN_TIMERS = 1;
const int MAX_TIMERS = 6;
int numberOfTimers = 2;

const char* getDayOfWeek(int dayIndex) {
    const char* days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    return days[dayIndex];
}

int safeNumberOfTimers() {
    return std::min(std::max(numberOfTimers, MIN_TIMERS), MAX_TIMERS);
}

// Mock relay state
static bool relay_state = false;
static uint8_t scheduled_on_hour = 8;
static uint8_t scheduled_off_hour = 18;

void relay_set(bool state) {
    relay_state = state;
}

bool relay_get(void) {
    return relay_state;
}

void relay_update_schedule(uint8_t hour) {
    if (hour == scheduled_on_hour) {
        relay_set(true);
    } else if (hour == scheduled_off_hour) {
        relay_set(false);
    }
}

// Test functions
void test_basic_assertions(void) {
    TEST_ASSERT_EQUAL(2, 1 + 1);
}

void test_getDayOfWeek(void) {
    TEST_ASSERT_EQUAL_STRING("Sunday", getDayOfWeek(0));
    TEST_ASSERT_EQUAL_STRING("Monday", getDayOfWeek(1));
    TEST_ASSERT_EQUAL_STRING("Saturday", getDayOfWeek(6));
}

void test_safeNumberOfTimers(void) {
    numberOfTimers = 0;
    TEST_ASSERT_EQUAL(1, safeNumberOfTimers());
    numberOfTimers = 10;
    TEST_ASSERT_EQUAL(6, safeNumberOfTimers());
    numberOfTimers = 3;
    TEST_ASSERT_EQUAL(3, safeNumberOfTimers());
}

void test_relay_turns_on_at_8am(void) {
    relay_set(false);
    relay_update_schedule(8);
    TEST_ASSERT_TRUE(relay_get());
}

void test_relay_turns_off_at_6pm(void) {
    relay_set(true);
    relay_update_schedule(18);
    TEST_ASSERT_FALSE(relay_get());
}

void test_relay_stays_on_during_active_hours(void) {
    relay_set(true);
    for (uint8_t h = 9; h < 18; h++) {
        relay_update_schedule(h);
        TEST_ASSERT_TRUE(relay_get());
    }
}

void test_relay_stays_off_during_inactive_hours(void) {
    relay_set(false);
    for (uint8_t h = 19; h < 24; h++) {
        relay_update_schedule(h);
        TEST_ASSERT_FALSE(relay_get());
    }
}

void test_24hour_relay_cycle(void) {
    relay_set(false);
    relay_update_schedule(8);
    TEST_ASSERT_TRUE(relay_get());
    relay_update_schedule(18);
    TEST_ASSERT_FALSE(relay_get());
}


typedef struct {
    uint8_t start_hour;
    uint8_t start_minute;
    uint16_t on_duration_sec;    // How long relay stays ON
    uint16_t interval_sec;       // Time between each pulse
    uint8_t repetitions;         // Number of pulses
    uint8_t current_rep;         // Current repetition count
    bool is_active;              // Timer is currently running
} Timer;

Timer timers[2];

void timer_init(uint8_t timer_id, uint8_t hour, uint8_t minute, 
                uint16_t on_sec, uint16_t interval_sec, uint8_t reps) {
    timers[timer_id].start_hour = hour;
    timers[timer_id].start_minute = minute;
    timers[timer_id].on_duration_sec = on_sec;
    timers[timer_id].interval_sec = interval_sec;
    timers[timer_id].repetitions = reps;
    timers[timer_id].current_rep = 0;
    timers[timer_id].is_active = false;
}

void timer_start(uint8_t timer_id) {
    timers[timer_id].is_active = true;
    timers[timer_id].current_rep = 0;
}

void timer_pulse(uint8_t timer_id) {
    if (timers[timer_id].is_active && timers[timer_id].current_rep < timers[timer_id].repetitions) {
        timers[timer_id].current_rep++;
        relay_set(true);  // Turn ON
    }
}

bool timer_is_complete(uint8_t timer_id) {
    return timers[timer_id].current_rep >= timers[timer_id].repetitions;
}

// Test: Timer 1 - 8 AM, 10 sec ON, every 5 min, 5 reps
void test_timer1_8am_10sec_5min_interval_5reps(void) {
    timer_init(0, 8, 0, 10, 300, 5);  // 300 sec = 5 min
    timer_start(0);
    
    for (int i = 0; i < 5; i++) {
        timer_pulse(0);
        TEST_ASSERT_EQUAL(i + 1, timers[0].current_rep);
    }
    TEST_ASSERT_TRUE(timer_is_complete(0));
}

// Test: Timer 2 - 2 PM, 5 sec ON, every 10 min, 3 reps
void test_timer2_2pm_5sec_10min_interval_3reps(void) {
    timer_init(1, 14, 0, 5, 600, 3);  // 600 sec = 10 min
    timer_start(1);
    
    for (int i = 0; i < 3; i++) {
        timer_pulse(1);
        TEST_ASSERT_EQUAL(i + 1, timers[1].current_rep);
    }
    TEST_ASSERT_TRUE(timer_is_complete(1));
}

// Test: Both timers run independently
void test_both_timers_independent(void) {
    timer_init(0, 8, 0, 10, 300, 5);
    timer_init(1, 14, 0, 5, 600, 3);
    timer_start(0);
    timer_start(1);
    
    TEST_ASSERT_EQUAL(8, timers[0].start_hour);
    TEST_ASSERT_EQUAL(14, timers[1].start_hour);
    TEST_ASSERT_EQUAL(5, timers[0].repetitions);
    TEST_ASSERT_EQUAL(3, timers[1].repetitions);
    TEST_ASSERT_TRUE(timers[0].is_active);
    TEST_ASSERT_TRUE(timers[1].is_active);
}

// Test: Timer 1 completes before Timer 2
void test_timer1_completes_before_timer2(void) {
    timer_init(0, 8, 0, 10, 300, 5);
    timer_init(1, 14, 0, 5, 600, 3);
    timer_start(0);
    timer_start(1);
    
    // Simulate Timer 1 completing
    for (int i = 0; i < 5; i++) {
        timer_pulse(0);
    }
    
    TEST_ASSERT_TRUE(timer_is_complete(0));
    TEST_ASSERT_FALSE(timer_is_complete(1));
    TEST_ASSERT_EQUAL(0, timers[1].current_rep);
}

// Add to main()
int main() {
    UNITY_BEGIN();
    RUN_TEST(test_basic_assertions);
    RUN_TEST(test_getDayOfWeek);
    RUN_TEST(test_safeNumberOfTimers);
    RUN_TEST(test_relay_turns_on_at_8am);
    RUN_TEST(test_relay_turns_off_at_6pm);
    RUN_TEST(test_relay_stays_on_during_active_hours);
    RUN_TEST(test_relay_stays_off_during_inactive_hours);
    RUN_TEST(test_24hour_relay_cycle);
    RUN_TEST(test_timer1_8am_10sec_5min_interval_5reps);
    RUN_TEST(test_timer2_2pm_5sec_10min_interval_3reps);
    RUN_TEST(test_both_timers_independent);
    RUN_TEST(test_timer1_completes_before_timer2);
    UNITY_END();
    return 0;
}
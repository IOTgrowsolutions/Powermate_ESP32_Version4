#include "unity.h"
#include <stdint.h>
#include <stdbool.h>

// Mock relay state
static bool relay_state = false;
static uint8_t scheduled_on_hour = 8;
static uint8_t scheduled_off_hour = 18;

// Mock functions
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

void setUp(void) {
    relay_state = false;
}

void tearDown(void) {
    relay_state = false;
}

// Test relay turns ON at scheduled time
void test_relay_turns_on_at_8am(void) {
    relay_update_schedule(8);
    TEST_ASSERT_TRUE(relay_get());
}

// Test relay turns OFF at scheduled time
void test_relay_turns_off_at_6pm(void) {
    relay_set(true);  // Start with relay ON
    relay_update_schedule(18);
    TEST_ASSERT_FALSE(relay_get());
}

// Test relay stays ON during active period
void test_relay_stays_on_during_active_hours(void) {
    relay_set(true);
    for (uint8_t h = 9; h < 18; h++) {
        relay_update_schedule(h);
        TEST_ASSERT_TRUE_MESSAGE(relay_get(), "Relay should remain ON");
    }
}

// Test relay stays OFF during inactive period
void test_relay_stays_off_during_inactive_hours(void) {
    relay_set(false);
    for (uint8_t h = 19; h < 24; h++) {
        relay_update_schedule(h);
        TEST_ASSERT_FALSE_MESSAGE(relay_get(), "Relay should remain OFF");
    }
}

// Test 24-hour cycle
void test_24hour_relay_cycle(void) {
    relay_update_schedule(8);
    TEST_ASSERT_TRUE(relay_get());
    
    relay_update_schedule(18);
    TEST_ASSERT_FALSE(relay_get());
}
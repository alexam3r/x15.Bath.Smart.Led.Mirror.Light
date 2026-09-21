// Task 1 tests: Config, Log, Types, ColorMath, Frame (168-pixel virtual ring).
// Runs on the `native` PlatformIO environment (host, no Arduino/FreeRTOS).
#include <unity.h>

#include "Config.h"
#include "Log.h"
#include "Types.h"

void setUp(void) {}
void tearDown(void) {}

// --- Config.h ---------------------------------------------------------

static void test_config_derived_constants(void) {
    TEST_ASSERT_EQUAL_UINT16(168, cfg::TOTAL_LEDS);
    TEST_ASSERT_EQUAL_UINT16(95, cfg::SLIDE_MAX_RADIUS);
}

// --- Types.h ------------------------------------------------------------

static void test_state_snapshot_equality(void) {
    StateSnapshot a;
    StateSnapshot b;
    TEST_ASSERT_TRUE(a == b);

    b.brightness = 100;
    TEST_ASSERT_FALSE(a == b);
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_config_derived_constants);
    RUN_TEST(test_state_snapshot_equality);
    return UNITY_END();
}

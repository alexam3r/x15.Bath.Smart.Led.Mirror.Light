// PersistedFlags (v1.2.1): automation, night mode and glitch survive a
// software restart (network watchdog, panic, task watchdog, brownout) in RTC
// memory, but not a power cycle. The memory is not initialised at power-on,
// so whatever is there must be rejected unless it is a valid record.
#include <unity.h>

#include <cstring>

#include "PersistedFlags.h"

void setUp(void) {}
void tearDown(void) {}

static void test_roundtrip_all_combinations(void) {
    for (int bits = 0; bits < 8; ++bits) {
        const bool a = bits & 1, n = bits & 2, g = bits & 4;
        const PersistedFlags p = packFlags(a, n, g);
        bool ra = !a, rn = !n, rg = !g;
        TEST_ASSERT_TRUE(unpackFlags(p, ra, rn, rg));
        TEST_ASSERT_EQUAL(a, ra);
        TEST_ASSERT_EQUAL(n, rn);
        TEST_ASSERT_EQUAL(g, rg);
    }
}

static void test_rejects_uninitialised_memory(void) {
    bool a = true, n = false, g = true;
    PersistedFlags zero;
    std::memset(&zero, 0, sizeof(zero));
    TEST_ASSERT_FALSE(unpackFlags(zero, a, n, g));
    PersistedFlags ones;
    std::memset(&ones, 0xFF, sizeof(ones));
    TEST_ASSERT_FALSE(unpackFlags(ones, a, n, g));
    // Rejected records leave the outputs untouched.
    TEST_ASSERT_TRUE(a);
    TEST_ASSERT_FALSE(n);
    TEST_ASSERT_TRUE(g);
}

static void test_rejects_any_corrupted_byte(void) {
    const PersistedFlags good = packFlags(false, true, false);
    for (size_t i = 0; i < sizeof(PersistedFlags); ++i) {
        PersistedFlags bad = good;
        reinterpret_cast<uint8_t*>(&bad)[i] ^= 0x04;
        bool a = true, n = false, g = true;
        TEST_ASSERT_FALSE_MESSAGE(unpackFlags(bad, a, n, g), "a corrupted record was accepted");
    }
}

// esp_reset_reason_t (ESP-IDF 4.4): restore after faults and software
// restarts only — never after power-on or the reset button.
static void test_restore_only_after_software_and_fault_resets(void) {
    TEST_ASSERT_FALSE(shouldRestoreFlags(0));   // UNKNOWN
    TEST_ASSERT_FALSE(shouldRestoreFlags(1));   // POWERON
    TEST_ASSERT_FALSE(shouldRestoreFlags(2));   // EXT (reset button)
    TEST_ASSERT_TRUE(shouldRestoreFlags(3));    // SW (network watchdog ESP.restart)
    TEST_ASSERT_TRUE(shouldRestoreFlags(4));    // PANIC
    TEST_ASSERT_TRUE(shouldRestoreFlags(5));    // INT_WDT
    TEST_ASSERT_TRUE(shouldRestoreFlags(6));    // TASK_WDT
    TEST_ASSERT_TRUE(shouldRestoreFlags(7));    // WDT
    TEST_ASSERT_FALSE(shouldRestoreFlags(8));   // DEEPSLEEP (never used)
    TEST_ASSERT_TRUE(shouldRestoreFlags(9));    // BROWNOUT
    TEST_ASSERT_FALSE(shouldRestoreFlags(10));  // SDIO
    TEST_ASSERT_FALSE(shouldRestoreFlags(200));
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_roundtrip_all_combinations);
    RUN_TEST(test_rejects_uninitialised_memory);
    RUN_TEST(test_rejects_any_corrupted_byte);
    RUN_TEST(test_restore_only_after_software_and_fault_resets);
    return UNITY_END();
}

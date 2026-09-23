// NetWatchdog (v1.2.1): when a lost network warrants a restart. WiFi down for
// WATCHDOG_TIMEOUT_MS -> restart, but never while the mirror is lit (the
// restart waits until it goes dark). MQTT is not an input at all: a dead
// broker is not fixed by rebooting, the task just keeps reconnecting.
#include <unity.h>

#include "Config.h"
#include "NetWatchdog.h"

void setUp(void) {}
void tearDown(void) {}

// Feeds update() every `stepMs` for `ms`; true if a restart was requested.
static bool feed(NetWatchdog& w, uint32_t& now, uint32_t ms, uint32_t stepMs, bool wifiUp, bool lit) {
    bool restart = false;
    const uint32_t end = now + ms;
    while (now != end) {
        const uint32_t step = (uint32_t)(end - now) < stepMs ? (uint32_t)(end - now) : stepMs;
        now += step;
        restart |= w.update(now, wifiUp, lit);
    }
    return restart;
}

static void test_wifi_down_restarts_after_the_timeout_when_dark(void) {
    NetWatchdog w;
    uint32_t now = 1000;
    w.update(now, true, false);
    TEST_ASSERT_FALSE(feed(w, now, cfg::WATCHDOG_TIMEOUT_MS - 1000, 10, false, false));
    TEST_ASSERT_TRUE_MESSAGE(feed(w, now, 2000, 10, false, false), "no restart after 5 min without WiFi");
}

static void test_restart_waits_while_the_mirror_is_lit(void) {
    NetWatchdog w;
    uint32_t now = 0;
    w.update(now, true, true);
    TEST_ASSERT_FALSE_MESSAGE(feed(w, now, cfg::WATCHDOG_TIMEOUT_MS + 30UL * 60 * 1000, 10, false, true),
                              "restarted while someone was using the mirror");
    TEST_ASSERT_TRUE_MESSAGE(w.update(now + 10, false, false), "no restart once the mirror went dark");
}

static void test_wifi_back_before_the_timeout_resets_the_count(void) {
    NetWatchdog w;
    uint32_t now = 0;
    w.update(now, true, false);
    feed(w, now, cfg::WATCHDOG_TIMEOUT_MS - 10000, 10, false, false);
    feed(w, now, 100, 10, true, false);  // back for a moment
    TEST_ASSERT_FALSE_MESSAGE(feed(w, now, cfg::WATCHDOG_TIMEOUT_MS - 1000, 10, false, false),
                              "downtime before the reconnect was still counted");
}

// NetworkTask blocks for up to ~10 s at a time while reconnecting; that
// time must count, but only the time the link was really down.
static void test_long_gaps_between_updates_count(void) {
    NetWatchdog w;
    uint32_t now = 0;
    w.update(now, true, false);
    TEST_ASSERT_FALSE(feed(w, now, cfg::WATCHDOG_TIMEOUT_MS - 5000, 10000, false, false));
    TEST_ASSERT_TRUE(feed(w, now, 10000, 10000, false, false));
}

static void test_first_update_only_starts_the_clock(void) {
    NetWatchdog w;
    TEST_ASSERT_FALSE_MESSAGE(w.update(0x80000000u, false, false), "the first call must not count from 0");
    TEST_ASSERT_EQUAL_UINT32(0, w.wifiDownMs());
}

static void test_survives_millis_wraparound(void) {
    NetWatchdog w;
    uint32_t now = 0xFFFFFFFFu - 60000;
    w.update(now, true, false);
    TEST_ASSERT_FALSE(feed(w, now, cfg::WATCHDOG_TIMEOUT_MS - 1000, 10, false, false));  // crosses 0
    TEST_ASSERT_TRUE(feed(w, now, 2000, 10, false, false));
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_wifi_down_restarts_after_the_timeout_when_dark);
    RUN_TEST(test_restart_waits_while_the_mirror_is_lit);
    RUN_TEST(test_wifi_back_before_the_timeout_resets_the_count);
    RUN_TEST(test_long_gaps_between_updates_count);
    RUN_TEST(test_first_update_only_starts_the_clock);
    RUN_TEST(test_survives_millis_wraparound);
    return UNITY_END();
}

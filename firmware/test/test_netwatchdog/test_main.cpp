// NetWatchdog (v1.2.1): what a lost network warrants.
//   - WiFi down for WATCHDOG_TIMEOUT_MS -> restart the ESP, but only once the
//     mirror has been quiet (dark, no motion) for RESTART_QUIET_MS — never
//     while it is lit, and not right after it went dark (a reboot would wipe
//     the 15 s PIR cooldown and relight it behind the person leaving).
//   - WiFi up but MQTT down for MQTT_DOWN_REJOIN_MS -> re-join WiFi (no
//     restart: a dead broker is not fixed by rebooting, but a stale lease or
//     an IP conflict after a router swap is fixed by re-joining), also only
//     when quiet.
#include <unity.h>

#include "Config.h"
#include "NetWatchdog.h"

void setUp(void) {}
void tearDown(void) {}

struct Net {
    bool wifi;
    bool mqtt;
    bool quiet;
};

// Feeds update() every `stepMs` for `ms`; returns the first non-None action.
static NetAction feed(NetWatchdog& w, uint32_t& now, uint32_t ms, uint32_t stepMs, Net n) {
    NetAction first = NetAction::None;
    const uint32_t end = now + ms;
    while (now != end) {
        const uint32_t step = (uint32_t)(end - now) < stepMs ? (uint32_t)(end - now) : stepMs;
        now += step;
        const NetAction a = w.update(now, n.wifi, n.mqtt, n.quiet);
        if (first == NetAction::None) first = a;
    }
    return first;
}

static const Net kDarkNoWifi{false, false, true};

static void test_wifi_down_restarts_after_the_timeout_when_quiet(void) {
    NetWatchdog w;
    uint32_t now = 1000;
    w.update(now, true, true, true);
    TEST_ASSERT_TRUE(NetAction::None == feed(w, now, cfg::WATCHDOG_TIMEOUT_MS - 1000, 10, kDarkNoWifi));
    TEST_ASSERT_TRUE_MESSAGE(NetAction::Restart == feed(w, now, 2000, 10, kDarkNoWifi),
                             "no restart after 5 min without WiFi");
}

static void test_restart_waits_while_the_mirror_is_in_use(void) {
    NetWatchdog w;
    uint32_t now = 0;
    w.update(now, true, true, false);
    TEST_ASSERT_TRUE_MESSAGE(NetAction::None == feed(w, now, cfg::WATCHDOG_TIMEOUT_MS + 30UL * 60 * 1000, 10,
                                                     Net{false, false, false}),
                             "restarted while someone was using the mirror");
}

// The mirror just went dark: the restart must wait RESTART_QUIET_MS, or it
// would wipe the PIR cooldown and relight the mirror behind the person.
static void test_restart_waits_for_a_quiet_spell_after_going_dark(void) {
    NetWatchdog w;
    uint32_t now = 0;
    w.update(now, true, true, false);
    feed(w, now, cfg::WATCHDOG_TIMEOUT_MS + 60000, 10, Net{false, false, false});  // lit, WiFi long gone
    TEST_ASSERT_TRUE_MESSAGE(NetAction::None == feed(w, now, cfg::RESTART_QUIET_MS - 1000, 10, kDarkNoWifi),
                             "restarted right after the mirror went dark");
    TEST_ASSERT_TRUE(NetAction::Restart == feed(w, now, 2000, 10, kDarkNoWifi));
}

static void test_motion_restarts_the_quiet_spell(void) {
    NetWatchdog w;
    uint32_t now = 0;
    w.update(now, true, true, true);
    feed(w, now, cfg::WATCHDOG_TIMEOUT_MS + 60000, 10, Net{false, false, false});
    feed(w, now, cfg::RESTART_QUIET_MS - 5000, 10, kDarkNoWifi);
    feed(w, now, 10, 10, Net{false, false, false});  // PIR blip
    TEST_ASSERT_TRUE(NetAction::None == feed(w, now, cfg::RESTART_QUIET_MS - 1000, 10, kDarkNoWifi));
}

// Without WiFi the task calls update() only every ~10 s (reconnect loop).
// The gap before the first quiet observation may have been lit, so the quiet
// spell must start at that observation, not include the gap.
static void test_quiet_spell_starts_at_the_first_quiet_observation(void) {
    NetWatchdog w;
    uint32_t now = 0;
    w.update(now, true, true, false);
    feed(w, now, cfg::WATCHDOG_TIMEOUT_MS + 60000, 10000, Net{false, false, false});
    TEST_ASSERT_TRUE_MESSAGE(NetAction::None == feed(w, now, cfg::RESTART_QUIET_MS, 10000, kDarkNoWifi),
                             "the lit gap before going dark was counted as quiet");
    TEST_ASSERT_TRUE(NetAction::Restart == feed(w, now, 10000, 10000, kDarkNoWifi));
}

static void test_wifi_back_before_the_timeout_resets_the_count(void) {
    NetWatchdog w;
    uint32_t now = 0;
    w.update(now, true, true, true);
    feed(w, now, cfg::WATCHDOG_TIMEOUT_MS - 10000, 10, kDarkNoWifi);
    feed(w, now, 100, 10, Net{true, true, true});  // back for a moment
    TEST_ASSERT_TRUE_MESSAGE(NetAction::None == feed(w, now, cfg::WATCHDOG_TIMEOUT_MS - 1000, 10, kDarkNoWifi),
                             "downtime before the reconnect was still counted");
}

// NetworkTask blocks for up to ~10 s at a time while reconnecting; that
// time counts.
static void test_long_gaps_between_updates_count(void) {
    NetWatchdog w;
    uint32_t now = 0;
    w.update(now, true, true, true);
    TEST_ASSERT_TRUE(NetAction::None == feed(w, now, cfg::WATCHDOG_TIMEOUT_MS - 5000, 10000, kDarkNoWifi));
    TEST_ASSERT_TRUE(NetAction::Restart == feed(w, now, 10000, 10000, kDarkNoWifi));
}

static void test_mqtt_only_outage_never_restarts_but_rejoins_wifi(void) {
    NetWatchdog w;
    uint32_t now = 0;
    w.update(now, true, true, true);
    const Net brokerGone{true, false, true};
    TEST_ASSERT_TRUE_MESSAGE(NetAction::None == feed(w, now, cfg::MQTT_DOWN_REJOIN_MS - 1000, 10, brokerGone),
                             "acted on an MQTT-only outage too early");
    TEST_ASSERT_TRUE_MESSAGE(NetAction::RejoinWifi == feed(w, now, 2000, 10, brokerGone),
                             "no WiFi re-join after a long MQTT-only outage");
    // One re-join per outage period, never a restart.
    TEST_ASSERT_TRUE(NetAction::None == feed(w, now, cfg::MQTT_DOWN_REJOIN_MS - 1000, 10, brokerGone));
    TEST_ASSERT_TRUE(NetAction::RejoinWifi == feed(w, now, 2000, 10, brokerGone));
}

static void test_mqtt_rejoin_waits_while_the_mirror_is_in_use(void) {
    NetWatchdog w;
    uint32_t now = 0;
    w.update(now, true, true, false);
    TEST_ASSERT_TRUE(NetAction::None == feed(w, now, cfg::MQTT_DOWN_REJOIN_MS + 60000, 10, Net{true, false, false}));
}

static void test_first_update_only_starts_the_clock(void) {
    NetWatchdog w;
    TEST_ASSERT_TRUE(NetAction::None == w.update(0x80000000u, false, false, true));
    TEST_ASSERT_EQUAL_UINT32(0, w.wifiDownMs());
}

static void test_survives_millis_wraparound(void) {
    NetWatchdog w;
    uint32_t now = 0xFFFFFFFFu - 60000;
    w.update(now, true, true, true);
    TEST_ASSERT_TRUE(NetAction::None == feed(w, now, cfg::WATCHDOG_TIMEOUT_MS - 1000, 10, kDarkNoWifi));  // crosses 0
    TEST_ASSERT_TRUE(NetAction::Restart == feed(w, now, 2000, 10, kDarkNoWifi));
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_wifi_down_restarts_after_the_timeout_when_quiet);
    RUN_TEST(test_restart_waits_while_the_mirror_is_in_use);
    RUN_TEST(test_restart_waits_for_a_quiet_spell_after_going_dark);
    RUN_TEST(test_motion_restarts_the_quiet_spell);
    RUN_TEST(test_quiet_spell_starts_at_the_first_quiet_observation);
    RUN_TEST(test_wifi_back_before_the_timeout_resets_the_count);
    RUN_TEST(test_long_gaps_between_updates_count);
    RUN_TEST(test_mqtt_only_outage_never_restarts_but_rejoins_wifi);
    RUN_TEST(test_mqtt_rejoin_waits_while_the_mirror_is_in_use);
    RUN_TEST(test_first_update_only_starts_the_clock);
    RUN_TEST(test_survives_millis_wraparound);
    return UNITY_END();
}

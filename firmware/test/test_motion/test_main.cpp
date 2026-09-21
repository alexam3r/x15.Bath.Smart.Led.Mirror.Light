// Task 3 tests: Countdown (overflow-safe timer, Ruling R7) and MotionGate
// (PIR automation gating: automation switch, night mode, 15s cooldown
// after manual OFF, 2s blackout after slide-out) — ARCHITECTURE.md 4.4 /
// 4.5 / 8.1. Runs on the `native` PlatformIO environment (host, no
// Arduino/FreeRTOS dependency).
#include <unity.h>

#include "Config.h"
#include "Countdown.h"
#include "MotionGate.h"

void setUp(void) {}
void tearDown(void) {}

// --- Countdown (R7) ---------------------------------------------------------

// Ruling R7: a Countdown left `active` after expiry would incorrectly
// report running() again ~49.7 days later when (now - startMs) wraps back
// into [0, durationMs). update() must retire an expired countdown so that
// running() stays false once the uint32_t clock has wrapped all the way
// around back near startMs.
static void test_countdown_does_not_refire_after_wrap(void) {
    Countdown c;
    const uint32_t t = 1000;

    c.start(t, 15000);
    TEST_ASSERT_TRUE(c.running(t + 14999));

    // Retire the countdown well after real expiry, still within the same
    // 2^32 ms cycle (no wraparound involved yet).
    c.update(t + 20000);
    TEST_ASSERT_FALSE(c.active);
    TEST_ASSERT_FALSE(c.running(t + 20000));

    // Simulate "now" having wrapped a full 2^32 ms cycle: the uint32_t
    // value coincides with t + 5 again (t + 2^32 + 5 truncated to 32
    // bits). Without update() having retired the countdown, running()
    // would compute (uint32_t)(t + 5 - t) == 5 < 15000 and incorrectly
    // report true here.
    const uint32_t wrapped_now = t + 5;
    TEST_ASSERT_FALSE(c.running(wrapped_now));
}

// Selectivity half of R7: update() must NOT blindly deactivate — a
// countdown that has not yet expired must stay active and running(). A
// broken `update()` that just sets `active = false` unconditionally would
// pass test_countdown_does_not_refire_after_wrap above (it only ever
// calls update() after real expiry) but must fail here.
static void test_countdown_update_does_not_clear_running_countdown(void) {
    Countdown c;
    const uint32_t t = 1000;

    c.start(t, 15000);

    c.update(t + 14999);  // one ms before expiry: still running
    TEST_ASSERT_TRUE(c.active);
    TEST_ASSERT_TRUE(c.running(t + 14999));

    c.update(t + 15000);  // exactly at expiry: now retired
    TEST_ASSERT_FALSE(c.active);
    TEST_ASSERT_FALSE(c.running(t + 15000));
}

// Makes the R7 refire hazard concrete: two identical countdowns, both
// fully expired; only one has update() called on it before the uint32_t
// clock wraps a full 2^32 ms cycle back near startMs. The un-retired one
// incorrectly reports running() == true again; the retired one does not.
static void test_countdown_refire_hazard_without_update(void) {
    Countdown notUpdated;
    Countdown updated;
    const uint32_t t = 1000;

    notUpdated.start(t, 15000);
    updated.start(t, 15000);

    updated.update(t + 20000);  // only this one gets retired past expiry

    const uint32_t wrapped_now = t + 5;  // coincides with t + 2^32 + 5
    TEST_ASSERT_TRUE(notUpdated.running(wrapped_now));   // hazard: refires
    TEST_ASSERT_FALSE(updated.running(wrapped_now));     // R7 fix: does not
}

// --- MotionGate ---------------------------------------------------------

static void test_auto_on_allowed_by_default(void) {
    MotionGate gate;

    TEST_ASSERT_TRUE(gate.automation());
    TEST_ASSERT_FALSE(gate.nightMode());
    TEST_ASSERT_TRUE(gate.automationActive());
    TEST_ASSERT_TRUE(gate.canAutoOn(0));
    TEST_ASSERT_TRUE(gate.canAutoOn(1000000));
}

static void test_manual_off_blocks_for_15s(void) {
    MotionGate gate;
    const uint32_t t0 = 1000;

    gate.onManualOff(t0);
    TEST_ASSERT_FALSE(gate.canAutoOn(t0 + 14999));  // still within cooldown
    TEST_ASSERT_TRUE(gate.canAutoOn(t0 + 15000));   // cooldown elapsed
}

static void test_blackout_blocks_for_2s(void) {
    MotionGate gate;
    const uint32_t t0 = 500;

    gate.onOffReached(t0);
    TEST_ASSERT_FALSE(gate.canAutoOn(t0 + 1999));  // still within blackout
    TEST_ASSERT_TRUE(gate.canAutoOn(t0 + 2000));   // blackout elapsed
}

static void test_automation_off_blocks_until_on(void) {
    MotionGate gate;
    const uint32_t t0 = 0;
    const uint32_t oneHourMs = 3600UL * 1000;

    gate.setAutomation(false);
    TEST_ASSERT_FALSE(gate.automation());
    TEST_ASSERT_FALSE(gate.automationActive());
    TEST_ASSERT_FALSE(gate.canAutoOn(t0 + oneHourMs));  // blocked indefinitely

    gate.setAutomation(true);
    TEST_ASSERT_TRUE(gate.automation());
    TEST_ASSERT_TRUE(gate.automationActive());
    TEST_ASSERT_TRUE(gate.canAutoOn(t0 + oneHourMs));  // allowed right after
}

static void test_night_mode_blocks_until_cleared(void) {
    MotionGate gate;
    const uint32_t t0 = 0;
    const uint32_t oneHourMs = 3600UL * 1000;

    gate.setNightMode(true);
    TEST_ASSERT_TRUE(gate.nightMode());
    TEST_ASSERT_TRUE(gate.automation());       // untouched by night mode
    TEST_ASSERT_FALSE(gate.automationActive());
    TEST_ASSERT_FALSE(gate.canAutoOn(t0 + oneHourMs));  // blocked indefinitely

    gate.setNightMode(false);
    TEST_ASSERT_FALSE(gate.nightMode());
    TEST_ASSERT_TRUE(gate.automationActive());
    TEST_ASSERT_TRUE(gate.canAutoOn(t0 + oneHourMs));  // allowed right after
}

static void test_power_on_clears_night_mode_and_cooldown(void) {
    MotionGate gate;
    const uint32_t t0 = 1000;

    gate.setNightMode(true);
    gate.onManualOff(t0);
    gate.onOffReached(t0);
    TEST_ASSERT_FALSE(gate.canAutoOn(t0 + 1));  // blocked by all three

    gate.onPowerOn();
    TEST_ASSERT_FALSE(gate.nightMode());
    TEST_ASSERT_TRUE(gate.canAutoOn(t0 + 1));  // nightMode, cooldown, blackout all cleared
}

static void test_automation_on_clears_cooldown(void) {
    MotionGate gate;
    const uint32_t t0 = 2000;

    gate.onManualOff(t0);
    TEST_ASSERT_FALSE(gate.canAutoOn(t0 + 100));  // cooldown running

    gate.setAutomation(true);  // re-affirm ON (was already true): still clears cooldown
    TEST_ASSERT_TRUE(gate.canAutoOn(t0 + 100));
}

static void test_cooldown_survives_millis_wraparound(void) {
    MotionGate gate;
    const uint32_t t0 = 0xFFFFF000u;  // close to millis() wraparound

    gate.onManualOff(t0);
    TEST_ASSERT_FALSE(gate.canAutoOn(t0 + 14999));  // wraps past 0, still blocked
    TEST_ASSERT_TRUE(gate.canAutoOn(t0 + 15000));   // wraps past 0, cooldown elapsed
}

// R7 at the MotionGate level: tick() must retire an expired cooldown so
// that a later call whose (now - start) arithmetic coincidentally lands
// back inside the cooldown window (after a full 2^32 ms wrap) is not
// blocked again.
static void test_motion_gate_tick_retires_expired_cooldown(void) {
    MotionGate gate;
    const uint32_t t0 = 1000;

    gate.onManualOff(t0);
    TEST_ASSERT_FALSE(gate.canAutoOn(t0 + 14999));  // still within cooldown

    gate.tick(t0 + 20000);  // retire the expired cooldown

    // Simulate wraparound back near t0 (see the Countdown-level test above
    // for why this coincides numerically with a fresh, unexpired window).
    TEST_ASSERT_TRUE(gate.canAutoOn(t0 + 5));
}

// Selectivity half of R7 through MotionGate::tick(): ticking while the
// cooldown is still legitimately running must leave it blocking. A broken
// tick() that unconditionally clears cooldown/blackout would pass
// test_motion_gate_tick_retires_expired_cooldown above (it only ticks
// after real expiry) but must fail here.
static void test_motion_gate_tick_does_not_clear_running_cooldown(void) {
    MotionGate gate;
    const uint32_t t0 = 1000;

    gate.onManualOff(t0);

    gate.tick(t0 + 10000);  // well before the 15s cooldown elapses
    TEST_ASSERT_FALSE(gate.canAutoOn(t0 + 10000));

    gate.tick(t0 + 15000);  // cooldown has now elapsed
    TEST_ASSERT_TRUE(gate.canAutoOn(t0 + 15000));
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_countdown_does_not_refire_after_wrap);
    RUN_TEST(test_countdown_update_does_not_clear_running_countdown);
    RUN_TEST(test_countdown_refire_hazard_without_update);
    RUN_TEST(test_auto_on_allowed_by_default);
    RUN_TEST(test_manual_off_blocks_for_15s);
    RUN_TEST(test_blackout_blocks_for_2s);
    RUN_TEST(test_automation_off_blocks_until_on);
    RUN_TEST(test_night_mode_blocks_until_cleared);
    RUN_TEST(test_power_on_clears_night_mode_and_cooldown);
    RUN_TEST(test_automation_on_clears_cooldown);
    RUN_TEST(test_cooldown_survives_millis_wraparound);
    RUN_TEST(test_motion_gate_tick_retires_expired_cooldown);
    RUN_TEST(test_motion_gate_tick_does_not_clear_running_cooldown);
    return UNITY_END();
}

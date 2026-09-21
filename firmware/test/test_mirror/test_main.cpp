// Task 5 tests: Mirror — the single-owner state machine (ARCHITECTURE.md
// section 4 / 8.1). Runs on the `native` PlatformIO environment (host, no
// Arduino/FreeRTOS dependency). Driven with a fake clock via run(), which
// calls tick() every 5 ms (a PlatformIO test filter runs only this suite:
// `pio test -e native -f test_mirror`).
//
// Grown test-first in slices; see task-5-report.md for RED/GREEN evidence.
#include <unity.h>

#include "Config.h"
#include "Frame.h"
#include "Mirror.h"
#include "Types.h"

void setUp(void) {}
void tearDown(void) {}

// Deterministic RandomFn: always returns 0 (index 0 -> CENTERS[0] == 9,
// randomEffect() -> Dark, rollAutoEffectDelay() -> AUTO_EFFECT_MIN_MS).
static uint32_t zeroRandom(uint32_t /*bound*/) { return 0; }

// Advances the fake clock `now` by `ms`, calling tick() every 5 ms — the
// task-5-context.md test harness convention.
static void run(Mirror& m, uint32_t& now, uint32_t ms) {
    uint32_t end = now + ms;
    while (now < end) {
        now += 5;
        m.tick(now);
    }
}

static ButtonEvent click(uint8_t n) {
    ButtonEvent ev;
    ev.type = ButtonEventType::Click;
    ev.clicks = n;
    return ev;
}

static uint16_t litPixelCount(const Frame& f) {
    uint16_t n = 0;
    for (uint16_t i = 0; i < Frame::kSize; ++i) {
        const Rgbw& p = f[i];
        if (p.r != 0 || p.g != 0 || p.b != 0 || p.w != 0) ++n;
    }
    return n;
}

// A full slide (either direction) takes SLIDE_MAX_RADIUS (95) steps *
// SLIDE_STEP_MS (28ms) =~ 2660ms; 3200ms of ticking leaves comfortable
// margin without being so long tests are slow.
static const uint32_t kSlideMs = 3200;

// --- Slice 1: power & slide (table 4.1: powerOn Off/SlideOff rows,
// powerOff SlideOn/On rows, "slide finished" row) --------------------------

static void test_begins_off(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    TEST_ASSERT_TRUE(PowerState::Off == m.power());
}

// Table 4.1: powerOn() / OFF -> SLIDE_ON, radius 0.
static void test_click1_from_off_starts_slide_on(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);

    m.onButton(click(1), now);
    TEST_ASSERT_TRUE(PowerState::SlideOn == m.power());
}

// Table 4.1: "slide finished" / SLIDE_ON -> ON (no pending effect).
static void test_slide_on_completes_to_on(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    m.onButton(click(1), now);

    run(m, now, kSlideMs);
    TEST_ASSERT_TRUE(PowerState::On == m.power());
}

// Table 4.1: powerOff(manual) / ON -> SLIDE_OFF, radius max.
// "slide finished" / SLIDE_OFF -> OFF; applyDefaults(); blackout.start().
static void test_click1_from_on_slides_off_to_off(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    m.onButton(click(1), now);
    run(m, now, kSlideMs);
    TEST_ASSERT_TRUE(PowerState::On == m.power());

    m.onButton(click(1), now);
    TEST_ASSERT_TRUE(PowerState::SlideOff == m.power());

    run(m, now, kSlideMs);
    TEST_ASSERT_TRUE(PowerState::Off == m.power());
}

// Table 4.1: powerOn() / SLIDE_OFF -> SLIDE_ON, radius kept (reversible);
// "радиус < 0 -> OFF" never fires early. Rule (§11.1 item 7): reversing
// must not jump the radius to 0 — the frame must never go fully dark
// mid-reversal (v27 used to jump to 0 on this path).
static void test_power_on_during_slide_off_reverses(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    m.onButton(click(1), now);
    run(m, now, kSlideMs);  // -> On, ring fully lit

    m.onButton(click(1), now);  // -> SlideOff
    TEST_ASSERT_TRUE(PowerState::SlideOff == m.power());
    run(m, now, 500);  // partial slide-out, still well short of radius 0

    // Reverse mid-slide-out.
    m.onButton(click(1), now);
    TEST_ASSERT_TRUE(PowerState::SlideOn == m.power());

    // Keep ticking through the reversal and beyond: the ring must never
    // go fully dark (no jump-to-zero), and power must end up On.
    uint32_t end = now + kSlideMs;
    bool everBlank = false;
    while (now < end) {
        now += 5;
        m.tick(now);
        if (litPixelCount(m.frame()) == 0) everBlank = true;
    }
    TEST_ASSERT_FALSE_MESSAGE(everBlank, "slide reversal jumped the radius to 0 (went fully dark)");
    TEST_ASSERT_TRUE(PowerState::On == m.power());
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_begins_off);
    RUN_TEST(test_click1_from_off_starts_slide_on);
    RUN_TEST(test_slide_on_completes_to_on);
    RUN_TEST(test_click1_from_on_slides_off_to_off);
    RUN_TEST(test_power_on_during_slide_off_reverses);
    return UNITY_END();
}

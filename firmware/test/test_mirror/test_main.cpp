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

// --- Slice 2: button, PIR, commands ----------------------------------------

static const Rgbw kSolidDefault{cfg::DEFAULT_R, cfg::DEFAULT_G, cfg::DEFAULT_B, 0};
static const Rgbw kMakeupColor{0, 0, 0, 255};

static ButtonEvent holdStart(void) {
    ButtonEvent ev;
    ev.type = ButtonEventType::HoldStart;
    return ev;
}
static ButtonEvent holdTick(void) {
    ButtonEvent ev;
    ev.type = ButtonEventType::HoldTick;
    return ev;
}
static ButtonEvent holdEnd(void) {
    ButtonEvent ev;
    ev.type = ButtonEventType::HoldEnd;
    return ev;
}

static bool allPixelsEqual(const Frame& f, Rgbw c) {
    for (uint16_t i = 0; i < Frame::kSize; ++i) {
        if (!(f[i] == c)) return false;
    }
    return true;
}

static Command lightState(int8_t state) {
    Command c;
    c.type = CommandType::Light;
    c.light.state = state;
    return c;
}
static Command lightBrightness(int16_t brightness) {
    Command c;
    c.type = CommandType::Light;
    c.light.brightness = brightness;
    return c;
}
static Command lightColor(int16_t r, int16_t g, int16_t b) {
    Command c;
    c.type = CommandType::Light;
    c.light.r = r;
    c.light.g = g;
    c.light.b = b;
    return c;
}
static Command automationCmd(bool on) {
    Command c;
    c.type = CommandType::Automation;
    c.flag = on;
    return c;
}
static Command makeupCmd(bool on) {
    Command c;
    c.type = CommandType::Makeup;
    c.flag = on;
    return c;
}
static Command nightModeCmd(bool on) {
    Command c;
    c.type = CommandType::NightMode;
    c.flag = on;
    return c;
}
static Command randomEffectCmd(void) {
    Command c;
    c.type = CommandType::RandomEffect;
    return c;
}

// --- Click(2): table 4.3 ------------------------------------------------

static void test_click2_from_off_makeup_and_powers_on(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);

    m.onButton(click(2), now);
    TEST_ASSERT_TRUE(PowerState::SlideOn == m.power());
    run(m, now, kSlideMs);
    TEST_ASSERT_TRUE(PowerState::On == m.power());
    TEST_ASSERT_TRUE(allPixelsEqual(m.frame(), kMakeupColor));
}

static void test_click2_toggles_base_when_on(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    m.onButton(click(1), now);
    run(m, now, kSlideMs);
    TEST_ASSERT_TRUE(allPixelsEqual(m.frame(), kSolidDefault));

    m.onButton(click(2), now);
    run(m, now, 10);
    TEST_ASSERT_TRUE(allPixelsEqual(m.frame(), kMakeupColor));

    m.onButton(click(2), now);
    run(m, now, 10);
    TEST_ASSERT_TRUE(allPixelsEqual(m.frame(), kSolidDefault));
}

// --- Click(3) / startEffect: table 4.1 "startEffect(id)" + "effect finished" rows ---

static void test_click3_from_off_ignored(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);

    m.onButton(click(3), now);
    TEST_ASSERT_TRUE(PowerState::Off == m.power());

    // A mutant that started the effect anyway (ignoring the OFF guard)
    // would only reveal itself once the mirror actually powers on and
    // finishes sliding in -- confirm no effect is running there.
    m.onButton(click(1), now);
    run(m, now, kSlideMs);
    TEST_ASSERT_TRUE(PowerState::On == m.power());
    TEST_ASSERT_TRUE_MESSAGE(EffectId::None == m.snapshot().effect,
                              "click(3) while OFF started an effect that surfaced after power-on");
    run(m, now, 10);  // let the static fill settle
    TEST_ASSERT_TRUE(allPixelsEqual(m.frame(), kSolidDefault));
}

static void test_click3_during_slide_off_ignored(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    m.onButton(click(1), now);
    run(m, now, kSlideMs);
    m.onButton(click(1), now);  // -> SlideOff
    TEST_ASSERT_TRUE(PowerState::SlideOff == m.power());

    m.onButton(click(3), now);  // must be ignored: no pending_ leak
    run(m, now, kSlideMs);
    TEST_ASSERT_TRUE(PowerState::Off == m.power());

    m.onButton(click(1), now);
    run(m, now, kSlideMs);
    TEST_ASSERT_TRUE(PowerState::On == m.power());
    run(m, now, 90);  // long enough for 2 effect steps, if one had leaked in
    TEST_ASSERT_TRUE_MESSAGE(allPixelsEqual(m.frame(), kSolidDefault),
                              "a pending effect leaked through a SlideOff click(3)");
}

// Table 4.1: startEffect(id) / SLIDE_ON -> pending_ = id; "slide finished" /
// SLIDE_ON -> ON picks up pending_ and starts it (§11.1 item 7).
static void test_effect_during_slide_on_is_deferred(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);

    m.onButton(click(1), now);           // -> SlideOn
    m.onButton(click(3), now);           // pending_ = Dark (deferred, not started)
    TEST_ASSERT_TRUE(PowerState::SlideOn == m.power());

    run(m, now, kSlideMs);               // slide finishes -> On, pending starts
    TEST_ASSERT_TRUE(PowerState::On == m.power());

    run(m, now, 90);                     // >= 2 DarkSnake steps (40ms each)
    TEST_ASSERT_FALSE_MESSAGE(allPixelsEqual(m.frame(), kSolidDefault),
                               "pending effect never started after slide-on finished");
}

// Table 4.1: startEffect(id) / ON -> effect_ = id (replaces current).
static void test_click3_replaces_running_effect(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    m.onButton(click(1), now);
    run(m, now, kSlideMs);

    m.onButton(click(3), now);           // start Dark
    run(m, now, 400);                    // progress ~10 steps into it
    TEST_ASSERT_FALSE(allPixelsEqual(m.frame(), kSolidDefault));

    m.onButton(click(3), now);           // replace: begin() called again, step counter resets
    run(m, now, 45);                     // exactly one fresh step (step 0, alpha 0)
    TEST_ASSERT_TRUE_MESSAGE(allPixelsEqual(m.frame(), kSolidDefault),
                              "click(3) did not restart the effect from step 0");
}

// Table 4.1: "effect finished" / ON -> effect_ = None, static fill, lastIdle_ = now.
static void test_effect_finishes_returns_to_static(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    m.onButton(click(1), now);
    run(m, now, kSlideMs);

    m.onButton(click(3), now);
    run(m, now, 229 * 40 + 200);         // DarkSnake: 229 steps * 40ms + margin
    TEST_ASSERT_TRUE(PowerState::On == m.power());
    TEST_ASSERT_TRUE(allPixelsEqual(m.frame(), kSolidDefault));
}

// --- Hold: table 4.3 -----------------------------------------------------

// §11.1 item 6: hold from OFF slides in; dimming only starts once ON.
static void test_hold_from_off_slides_then_dims(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);

    m.onButton(holdStart(), now);
    TEST_ASSERT_TRUE(PowerState::SlideOn == m.power());

    // HoldTicks while still sliding must be ignored (no dimming).
    for (int i = 0; i < 10; ++i) {
        now += 30;
        m.onButton(holdTick(), now);
        m.tick(now);
    }
    TEST_ASSERT_TRUE(PowerState::SlideOn == m.power());

    run(m, now, kSlideMs);
    TEST_ASSERT_TRUE(PowerState::On == m.power());

    // First tick after reaching On hits the DIM_MAX cap and flips direction;
    // the second applies the first real decrement.
    m.onButton(holdTick(), now);
    m.tick(now);
    m.onButton(holdTick(), now);
    m.tick(now);
    TEST_ASSERT_EQUAL_UINT8(cfg::DEFAULT_BRIGHTNESS - cfg::DIM_STEP, m.frame()[0].r);
}

// Bug A regression: hold started while in night mode must ONLY clear night
// mode (and lock dimming for the rest of the hold) — it must never power on
// and never dim. Night mode is cleared the instant HoldStart fires, so if
// the user is already standing in front of the mirror, PIR can power it on
// *during the same hold* (still holding, no HoldEnd yet) — holdLocked_ must
// keep blocking dimming even once power_ reaches On. A version of this test
// that never leaves OFF during the hold cannot tell the fix apart from a
// missing `holdLocked_ = true;`: the `power_ == On` half of HoldTick's guard
// already blocks dimming on its own while OFF/SLIDE_ON, so the lock is only
// load-bearing once ON is reached mid-hold.
static void test_hold_in_night_mode_only_clears_night_mode(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    m.apply(nightModeCmd(true), now);

    m.onButton(holdStart(), now);
    TEST_ASSERT_TRUE(PowerState::Off == m.power());

    // Night mode is now clear; PIR can auto-on while the hold is still in
    // progress (no HoldEnd yet).
    now += 10;
    m.onPir(true, now);
    TEST_ASSERT_TRUE(PowerState::SlideOn == m.power());

    // Keep holding through the slide-in. These HoldTicks are separately
    // ignored by HoldTick's own `power_ == On` guard while still sliding
    // (regardless of the lock) — checking pixel values against a "should be
    // untouched" constant here would be invalid anyway, since the slide's
    // edge-fade legitimately renders partial-strength colour during the
    // animation. Just drive the clock forward until ON is reached.
    uint32_t slideDeadline = now + kSlideMs;
    while (m.power() != PowerState::On && now < slideDeadline) {
        now += 30;
        m.onButton(holdTick(), now);
        m.tick(now);
    }
    TEST_ASSERT_TRUE(PowerState::On == m.power());

    // This is where a missing `holdLocked_ = true` actually shows up: still
    // holding (no HoldEnd yet), and now power_ == On, so an unlocked
    // HoldTick's dimming branch would fire. Keep holding and confirm
    // brightness never moves for the rest of this hold.
    for (int i = 0; i < 10; ++i) {
        now += 30;
        m.onButton(holdTick(), now);
        m.tick(now);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(cfg::DEFAULT_BRIGHTNESS, m.frame()[0].r,
                                         "a night-mode hold dimmed after power reached ON");
    }

    m.onButton(holdEnd(), now);
    TEST_ASSERT_EQUAL_UINT8(cfg::DEFAULT_BRIGHTNESS, m.frame()[0].r);

    // A brand-new hold (not started in night mode, power already On) must
    // dim normally — the lock does not leak past HoldEnd.
    m.onButton(holdStart(), now);
    now += 30;
    m.onButton(holdTick(), now);
    m.tick(now);
    now += 30;
    m.onButton(holdTick(), now);
    m.tick(now);
    TEST_ASSERT_EQUAL_UINT8(cfg::DEFAULT_BRIGHTNESS - cfg::DIM_STEP, m.frame()[0].r);
}

static void test_dim_bounces_5_255(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    m.onButton(click(1), now);
    run(m, now, kSlideMs);

    bool sawMin = false, sawMax = false;
    for (int i = 0; i < 115; ++i) {
        now += 30;
        m.onButton(holdTick(), now);
        m.tick(now);
        uint8_t v = m.frame()[0].r;
        TEST_ASSERT_TRUE(v >= cfg::DIM_MIN && v <= cfg::DIM_MAX);
        if (v == cfg::DIM_MIN) sawMin = true;
        if (i > 2 && v == cfg::DIM_MAX) sawMax = true;
    }
    TEST_ASSERT_TRUE_MESSAGE(sawMin, "brightness never bounced down to DIM_MIN (5)");
    TEST_ASSERT_TRUE_MESSAGE(sawMax, "brightness never bounced back up to DIM_MAX (255)");
}

static void test_click_ge4_ignored(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);

    m.onButton(click(4), now);
    TEST_ASSERT_TRUE(PowerState::Off == m.power());
    m.onButton(click(5), now);
    TEST_ASSERT_TRUE(PowerState::Off == m.power());
}

// --- Commands: apply() ----------------------------------------------------

static void test_brightness_zero_turns_off(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    m.onButton(click(1), now);
    run(m, now, kSlideMs);

    m.apply(lightBrightness(0), now);
    TEST_ASSERT_TRUE(PowerState::SlideOff == m.power());
}

static void test_color_switches_to_solid(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    m.onButton(click(1), now);
    run(m, now, kSlideMs);
    m.onButton(click(2), now);
    run(m, now, 10);
    TEST_ASSERT_TRUE(allPixelsEqual(m.frame(), kMakeupColor));

    m.apply(lightColor(10, 20, 30), now);
    run(m, now, 10);
    TEST_ASSERT_TRUE(allPixelsEqual(m.frame(), Rgbw{10, 20, 30, 0}));
}

// applyDefaults() only takes effect once SLIDE_OFF -> OFF completes; a
// custom color/brightness must survive the whole slide-out.
static void test_apply_defaults_only_after_slide_off(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    m.onButton(click(1), now);
    run(m, now, kSlideMs);

    m.apply(lightColor(10, 20, 30), now);
    m.apply(lightBrightness(100), now);
    run(m, now, 10);
    Rgbw custom = scale(Rgbw{10, 20, 30, 0}, 100);
    TEST_ASSERT_TRUE(allPixelsEqual(m.frame(), custom));

    m.onButton(click(1), now);  // -> SlideOff
    run(m, now, 100);           // well short of finishing; center pixel still lit
    TEST_ASSERT_TRUE_MESSAGE(custom == m.frame()[9],
                              "custom color/brightness reset before SLIDE_OFF finished");

    run(m, now, kSlideMs);
    TEST_ASSERT_TRUE(PowerState::Off == m.power());

    m.onButton(click(1), now);
    run(m, now, kSlideMs);
    TEST_ASSERT_TRUE(allPixelsEqual(m.frame(), kSolidDefault));
}

static void test_makeup_command_powers_on_from_off(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);

    m.apply(makeupCmd(true), now);
    TEST_ASSERT_TRUE(PowerState::SlideOn == m.power());
    run(m, now, kSlideMs);
    TEST_ASSERT_TRUE(allPixelsEqual(m.frame(), kMakeupColor));

    m.apply(makeupCmd(false), now);
    run(m, now, 10);
    TEST_ASSERT_TRUE(PowerState::On == m.power());
    TEST_ASSERT_TRUE(allPixelsEqual(m.frame(), kSolidDefault));
}

static void test_night_mode_command_turns_off_when_on(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    m.onButton(click(1), now);
    run(m, now, kSlideMs);

    m.apply(nightModeCmd(true), now);
    TEST_ASSERT_TRUE(PowerState::SlideOff == m.power());
}

static void test_random_effect_command_starts_effect(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    m.onButton(click(1), now);
    run(m, now, kSlideMs);

    m.apply(randomEffectCmd(), now);
    run(m, now, 90);
    TEST_ASSERT_FALSE(allPixelsEqual(m.frame(), kSolidDefault));
}

// Table 4.1 "—" (no-op) cells, only reachable through Light's unconditional
// state=1/0 handling (every other caller guards on power_ first).
//
// Comparing lit counts immediately before/after apply() (with no tick() in
// between) can't tell a no-op from a reset-to-0: apply() itself never
// renders, so the frame is untouched either way until the next tick(). This
// version advances well into the slide, then lets *one more* real slide
// step render after the (expected) no-op, and checks the lit count kept
// growing from where it was rather than dropping back toward 0.
static void test_power_on_noop_while_slide_on(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    m.onButton(click(1), now);
    run(m, now, 500);  // well into the slide-on (radius > 0)
    uint16_t litBefore = litPixelCount(m.frame());
    TEST_ASSERT_TRUE_MESSAGE(litBefore > 0, "sanity: slide-on has not progressed");

    m.apply(lightState(1), now);  // powerOn() while SlideOn -> must no-op
    TEST_ASSERT_TRUE(PowerState::SlideOn == m.power());

    run(m, now, cfg::SLIDE_STEP_MS + 5);  // let (at least) one more slide step render
    uint16_t litAfter = litPixelCount(m.frame());
    TEST_ASSERT_TRUE_MESSAGE(litAfter >= litBefore,
                              "powerOn() while SLIDE_ON reset the radius to 0 (lit count dropped)");
}

static void test_power_on_noop_while_on(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    m.onButton(click(1), now);
    run(m, now, kSlideMs);

    m.apply(lightState(1), now);
    TEST_ASSERT_TRUE(PowerState::On == m.power());
    TEST_ASSERT_TRUE(allPixelsEqual(m.frame(), kSolidDefault));
}

static void test_power_off_noop_while_off(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);

    m.apply(lightState(0), now);  // powerOff() while Off -> must no-op, no cooldown started
    TEST_ASSERT_TRUE(PowerState::Off == m.power());

    now += 10;
    m.onPir(true, now);
    TEST_ASSERT_TRUE_MESSAGE(PowerState::SlideOn == m.power(),
                              "powerOff() while OFF incorrectly started a PIR cooldown");
}

static void test_power_off_noop_while_slide_off(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    m.onButton(click(1), now);
    run(m, now, kSlideMs);
    m.onButton(click(1), now);  // -> SlideOff, starts the 15s cooldown at this `now`
    uint32_t cooldownStart = now;
    run(m, now, 200);

    m.apply(lightState(0), now);  // powerOff() while SlideOff -> must no-op (no cooldown restart)
    TEST_ASSERT_TRUE(PowerState::SlideOff == m.power());
    run(m, now, kSlideMs);
    TEST_ASSERT_TRUE(PowerState::Off == m.power());

    now = cooldownStart + cfg::PIR_COOLDOWN_MS + 10;
    m.onPir(true, now);
    TEST_ASSERT_TRUE_MESSAGE(PowerState::SlideOn == m.power(),
                              "powerOff() while SLIDE_OFF restarted the PIR cooldown");
}

// Table 4.1: powerOff(manual) / SLIDE_ON -> SLIDE_OFF, radius kept (continues
// from the current radius: neither jumps to max nor to 0); manual=true still
// starts the 15s cooldown.
static void test_power_off_during_slide_on(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    m.onButton(click(1), now);  // -> SlideOn
    run(m, now, 500);           // partway into the slide-on
    uint16_t litBefore = litPixelCount(m.frame());
    TEST_ASSERT_TRUE_MESSAGE(litBefore > 0, "sanity: slide-on has not progressed");

    m.onButton(click(1), now);  // powerOff(manual) while SlideOn -> reverses direction
    TEST_ASSERT_TRUE(PowerState::SlideOff == m.power());
    uint32_t cooldownStart = now;

    // The very next render still reflects the (unchanged) radius from the
    // moment of the call — the internal radius_ has already advanced one
    // step past the last *rendered* frame, so a single-step comparison
    // against litBefore is off by one and not a reliable signal either way.
    // Several steps out, though, the direction is unambiguous: continuing
    // to decrease from "current" gives a strictly lower lit count than
    // litBefore; a bug that kept turningOn_ true (radius still climbing)
    // or reset to SLIDE_MAX_RADIUS would instead give a higher one.
    run(m, now, cfg::SLIDE_STEP_MS * 5);
    uint16_t litAfter = litPixelCount(m.frame());
    TEST_ASSERT_TRUE_MESSAGE(
        litAfter < litBefore,
        "powerOff() while SLIDE_ON did not reverse direction from the current radius");

    run(m, now, kSlideMs);
    TEST_ASSERT_TRUE(PowerState::Off == m.power());

    // Manual off started the 15s cooldown at the moment powerOff() was
    // called (not e.g. only once OFF was actually reached).
    now = cooldownStart + cfg::PIR_COOLDOWN_MS - 100;
    m.onPir(true, now);
    TEST_ASSERT_TRUE(PowerState::Off == m.power());
    now = cooldownStart + cfg::PIR_COOLDOWN_MS + 50;
    m.onPir(true, now);
    TEST_ASSERT_TRUE(PowerState::SlideOn == m.power());
}

// Table 4.1: powerOff(manual) / SLIDE_ON also drops any pending_ effect.
// Checked by reversing back to SLIDE_ON *before* the slide-out ever reaches
// OFF: applyDefaults() (which also happens to clear pending_, as part of
// resetting everything for the next power-on) only runs on the SLIDE_OFF ->
// OFF transition, so skipping OFF entirely isolates powerOff()'s own
// cancellation — a version of this test that goes all the way through OFF
// cannot tell the two apart and would pass even if powerOff() itself forgot
// to clear pending_.
static void test_power_off_during_slide_on_drops_pending_effect(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    m.onButton(click(1), now);  // -> SlideOn
    m.onButton(click(3), now);  // pending_ = Dark
    run(m, now, 500);

    m.onButton(click(1), now);  // powerOff(manual) while SlideOn
    TEST_ASSERT_TRUE(PowerState::SlideOff == m.power());
    run(m, now, 200);           // still well short of reaching OFF

    m.onButton(click(1), now);  // powerOn (reverse) -- never reaches OFF
    TEST_ASSERT_TRUE(PowerState::SlideOn == m.power());
    run(m, now, kSlideMs);
    TEST_ASSERT_TRUE(PowerState::On == m.power());
    TEST_ASSERT_TRUE_MESSAGE(EffectId::None == m.snapshot().effect,
                              "a pending_ effect survived powerOff() during SLIDE_ON");
}

// Table 4.1: powerOff(manual) / ON -> SLIDE_OFF, radius max; a running
// effect is cancelled immediately (not just eventually finished).
static void test_power_off_cancels_running_effect(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    m.onButton(click(1), now);
    run(m, now, kSlideMs);      // -> On
    m.onButton(click(3), now);  // start Dark
    run(m, now, 400);           // well into the effect
    TEST_ASSERT_TRUE(EffectId::Dark == m.snapshot().effect);

    m.onButton(click(1), now);  // powerOff(manual) -> effect must cancel immediately
    TEST_ASSERT_TRUE(PowerState::SlideOff == m.power());
    TEST_ASSERT_TRUE_MESSAGE(EffectId::None == m.snapshot().effect,
                              "powerOff() while ON did not cancel the running effect");

    // Power back on while still mid-slide-out (reverse): no effect resumes.
    run(m, now, 500);
    m.onButton(click(1), now);  // powerOn (reverse)
    TEST_ASSERT_TRUE(PowerState::SlideOn == m.power());
    run(m, now, kSlideMs);
    TEST_ASSERT_TRUE(PowerState::On == m.power());
    TEST_ASSERT_TRUE_MESSAGE(
        EffectId::None == m.snapshot().effect,
        "an effect resumed after reversing a slide-out that had cancelled it");
    run(m, now, 10);
    TEST_ASSERT_TRUE(allPixelsEqual(m.frame(), kSolidDefault));
}

static void test_automation_flag_gates_pir(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);

    m.apply(automationCmd(false), now);
    m.onPir(true, now);
    TEST_ASSERT_TRUE(PowerState::Off == m.power());

    m.apply(automationCmd(true), now);
    now += 10;
    m.onPir(true, now);
    TEST_ASSERT_TRUE(PowerState::SlideOn == m.power());
}

// --- PIR: §4.4 auto-on rules -----------------------------------------------

static void test_pir_turns_on_when_allowed(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);

    m.onPir(true, now);
    TEST_ASSERT_TRUE(PowerState::SlideOn == m.power());
}

static void test_pir_blocked_during_manual_off_cooldown(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    m.onButton(click(1), now);
    run(m, now, kSlideMs);
    m.onButton(click(1), now);  // -> SlideOff, cooldown starts at this `now`
    uint32_t t0 = now;
    run(m, now, kSlideMs);      // -> Off

    now = t0 + cfg::PIR_COOLDOWN_MS - 100;
    m.onPir(true, now);
    TEST_ASSERT_TRUE(PowerState::Off == m.power());

    now = t0 + cfg::PIR_COOLDOWN_MS + 50;
    m.onPir(true, now);
    TEST_ASSERT_TRUE(PowerState::SlideOn == m.power());
}

static void test_pir_blocked_when_automation_off(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    m.apply(automationCmd(false), now);

    m.onPir(true, now);
    TEST_ASSERT_TRUE(PowerState::Off == m.power());
}

static void test_pir_blocked_in_night_mode(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    m.apply(nightModeCmd(true), now);

    m.onPir(true, now);
    TEST_ASSERT_TRUE(PowerState::Off == m.power());
}

// --- Slice 3: automation (tick step 2) + snapshot + frame dirty -----------

// §4.4: auto-off after 15 min idle, automation && !nightMode, power == On,
// strict >. Also isolates the 2s blackout after slide-out: auto-off is
// manual=false, so it must NOT start the 15s manual cooldown.
static void test_auto_off_after_15min_idle(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    m.onButton(click(1), now);
    run(m, now, kSlideMs);  // -> On, lastActivity_ == 0 (set at click(1) time)

    // Lower bound: still ON right up to (but not past) the 15-minute mark.
    // Deliberately hardcodes the ARCHITECTURE.md §4.4 spec value (15 min)
    // instead of referencing cfg::AUTO_OFF_MS: a bound expressed in terms of
    // the constant itself would scale right along with a mutated/shortened
    // AUTO_OFF_MS and could never catch that class of change — it would only
    // ever catch a threshold-comparison bug in Mirror's own code that
    // diverges from whatever the constant says.
    while (now < 15UL * 60 * 1000 - 50) {
        now += 5;
        m.tick(now);
    }
    TEST_ASSERT_TRUE_MESSAGE(PowerState::On == m.power(),
                              "auto-off fired before the 15-minute idle threshold");

    // Tick in fine (5ms) steps until auto-off has fired AND the resulting
    // slide-out has fully completed, capturing the exact moment (tOff)
    // blackout starts — a single coarse run() risks overshooting past that
    // moment, which would throw off the blackout-boundary check below.
    uint32_t safetyLimit = cfg::AUTO_OFF_MS + kSlideMs + 5000;
    while (m.power() != PowerState::Off && now < safetyLimit) {
        now += 5;
        m.tick(now);
    }
    TEST_ASSERT_TRUE_MESSAGE(PowerState::Off == m.power(),
                              "auto-off never completed within the expected window");

    uint32_t tOff = now;
    now = tOff + cfg::PIR_BLACKOUT_MS - 200;
    m.onPir(true, now);
    TEST_ASSERT_TRUE(PowerState::Off == m.power());
    now = tOff + cfg::PIR_BLACKOUT_MS + 50;
    m.onPir(true, now);
    TEST_ASSERT_TRUE_MESSAGE(PowerState::SlideOn == m.power(),
                              "auto-off incorrectly started a manual-off-style 15s cooldown");
}

// §4.4 activity rule: `pir == HIGH && power ∈ {SLIDE_ON, ON}` resets the
// idle clock, postponing auto-off. A PIR pulse at ~10 minutes must push the
// 15-minute deadline out to ~25 minutes, so the mirror must still be ON at
// 20 minutes (which would already be well past a *from-start* 15-minute
// deadline). Catches a mutant that drops the `markActivity(now)` call in
// onPir()'s "power in {SlideOn, On}" branch.
static void test_pir_activity_postpones_auto_off(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    m.onButton(click(1), now);
    run(m, now, kSlideMs);  // -> On, lastActivity_ == 0

    while (now < 10UL * 60 * 1000) {
        now += 5;
        m.tick(now);
    }
    TEST_ASSERT_TRUE(PowerState::On == m.power());
    m.onPir(true, now);  // marks activity -> lastActivity_ resets to ~10 min

    while (now < 20UL * 60 * 1000) {
        now += 5;
        m.tick(now);
        TEST_ASSERT_TRUE_MESSAGE(PowerState::On == m.power(),
                                  "PIR activity did not postpone auto-off");
    }
}

// §4.3: "any button event resets activity timers." A HoldStart/HoldEnd pair
// while already ON (not in night mode) has no other observable effect, so it
// isolates the activity-marking rule the same way the PIR test isolates its
// own. Catches a mutant that drops the `markActivity(now)` call at the top
// of onButton().
static void test_button_event_postpones_auto_off(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    m.onButton(click(1), now);
    run(m, now, kSlideMs);  // -> On, lastActivity_ == 0

    while (now < 10UL * 60 * 1000) {
        now += 5;
        m.tick(now);
    }
    TEST_ASSERT_TRUE(PowerState::On == m.power());
    m.onButton(holdStart(), now);
    m.onButton(holdEnd(), now);

    while (now < 20UL * 60 * 1000) {
        now += 5;
        m.tick(now);
        TEST_ASSERT_TRUE_MESSAGE(PowerState::On == m.power(),
                                  "a button event did not postpone auto-off");
    }
}

static void test_no_auto_off_when_automation_off(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    m.onButton(click(1), now);
    run(m, now, kSlideMs);
    m.apply(automationCmd(false), now);

    run(m, now, cfg::AUTO_OFF_MS + 1000);
    TEST_ASSERT_TRUE(PowerState::On == m.power());
}

// §4.4: auto-effect only in Solid; nextAutoEffect rerolled to AUTO_EFFECT_MIN_MS
// by zeroRandom (bound 0 offset).
static void test_auto_effect_starts_in_solid(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    m.onButton(click(1), now);
    run(m, now, kSlideMs);

    run(m, now, cfg::AUTO_EFFECT_MIN_MS + 1000);
    TEST_ASSERT_FALSE_MESSAGE(allPixelsEqual(m.frame(), kSolidDefault),
                               "auto-effect never started after the idle window");
}

static void test_auto_effect_not_in_makeup(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    m.onButton(click(2), now);  // Makeup, powers on
    run(m, now, kSlideMs);
    TEST_ASSERT_TRUE(allPixelsEqual(m.frame(), kMakeupColor));

    run(m, now, cfg::AUTO_EFFECT_MIN_MS + 1000);
    TEST_ASSERT_TRUE_MESSAGE(allPixelsEqual(m.frame(), kMakeupColor),
                              "auto-effect incorrectly started while in Makeup");
}

// Ruling R9: enabling automation must never cause an instant auto-off, even
// though lastActivity_ went stale while automation was off.
static void test_enabling_automation_does_not_auto_off_immediately(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    m.apply(automationCmd(false), now);
    m.onButton(click(1), now);
    run(m, now, kSlideMs);

    run(m, now, cfg::AUTO_OFF_MS * 2);  // go very stale while automation is off
    TEST_ASSERT_TRUE(PowerState::On == m.power());

    m.apply(automationCmd(true), now);
    run(m, now, 100);
    TEST_ASSERT_TRUE_MESSAGE(PowerState::On == m.power(),
                              "enabling automation caused an instant auto-off");
}

// --- snapshot() -------------------------------------------------------------

static void test_snapshot_reports_running_effect(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    TEST_ASSERT_TRUE(EffectId::None == m.snapshot().effect);

    m.onButton(click(1), now);
    run(m, now, kSlideMs);
    TEST_ASSERT_TRUE(EffectId::None == m.snapshot().effect);

    m.onButton(click(3), now);
    TEST_ASSERT_TRUE(EffectId::Dark == m.snapshot().effect);

    run(m, now, 229 * 40 + 200);
    TEST_ASSERT_TRUE(EffectId::None == m.snapshot().effect);
}

static void test_snapshot_hides_pending_effect(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    m.onButton(click(1), now);
    m.onButton(click(3), now);  // pending_ = Dark
    TEST_ASSERT_TRUE(EffectId::None == m.snapshot().effect);

    run(m, now, kSlideMs);
    TEST_ASSERT_TRUE(EffectId::Dark == m.snapshot().effect);
}

static void test_snapshot_fields(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    StateSnapshot s0 = m.snapshot();
    TEST_ASSERT_FALSE(s0.on);
    TEST_ASSERT_EQUAL_UINT8(cfg::DEFAULT_BRIGHTNESS, s0.brightness);
    TEST_ASSERT_EQUAL_UINT8(cfg::DEFAULT_R, s0.r);
    TEST_ASSERT_EQUAL_UINT8(cfg::DEFAULT_G, s0.g);
    TEST_ASSERT_EQUAL_UINT8(cfg::DEFAULT_B, s0.b);
    TEST_ASSERT_TRUE(BaseMode::Solid == s0.base);
    TEST_ASSERT_TRUE(s0.automation);
    TEST_ASSERT_FALSE(s0.nightMode);
    TEST_ASSERT_FALSE(s0.pir);

    m.onPir(true, now);
    StateSnapshot s1 = m.snapshot();
    TEST_ASSERT_TRUE(s1.on);   // SLIDE_ON counts as "on"
    TEST_ASSERT_TRUE(s1.pir);
}

static void test_snapshot_reflects_night_mode_and_automation(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);

    m.apply(nightModeCmd(true), now);
    TEST_ASSERT_TRUE(m.snapshot().nightMode);
    m.apply(nightModeCmd(false), now);
    TEST_ASSERT_FALSE(m.snapshot().nightMode);

    m.apply(automationCmd(false), now);
    TEST_ASSERT_FALSE(m.snapshot().automation);
    m.apply(automationCmd(true), now);
    TEST_ASSERT_TRUE(m.snapshot().automation);
}

// --- frame dirty / brightness -----------------------------------------------

static void test_frame_dirty_only_on_change(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    TEST_ASSERT_TRUE(m.takeFrameDirty());   // begin() dirties the (cleared) frame
    TEST_ASSERT_FALSE(m.takeFrameDirty());  // consumed; nothing changed since

    m.onButton(click(1), now);
    run(m, now, kSlideMs);
    TEST_ASSERT_TRUE(m.takeFrameDirty());   // slide + settle produced dirty frames
    TEST_ASSERT_FALSE(m.takeFrameDirty());  // consumed

    for (int i = 0; i < 50; ++i) {
        now += 100;
        m.tick(now);
        TEST_ASSERT_FALSE_MESSAGE(m.takeFrameDirty(), "frame marked dirty with no state change");
    }
}

static void test_frame_brightness_applied(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    m.onButton(click(1), now);
    run(m, now, kSlideMs);

    m.apply(lightBrightness(128), now);
    run(m, now, 10);
    TEST_ASSERT_TRUE(allPixelsEqual(m.frame(), scale(kSolidDefault, 128)));
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_begins_off);
    RUN_TEST(test_click1_from_off_starts_slide_on);
    RUN_TEST(test_slide_on_completes_to_on);
    RUN_TEST(test_click1_from_on_slides_off_to_off);
    RUN_TEST(test_power_on_during_slide_off_reverses);

    RUN_TEST(test_click2_from_off_makeup_and_powers_on);
    RUN_TEST(test_click2_toggles_base_when_on);
    RUN_TEST(test_click3_from_off_ignored);
    RUN_TEST(test_click3_during_slide_off_ignored);
    RUN_TEST(test_effect_during_slide_on_is_deferred);
    RUN_TEST(test_click3_replaces_running_effect);
    RUN_TEST(test_effect_finishes_returns_to_static);
    RUN_TEST(test_hold_from_off_slides_then_dims);
    RUN_TEST(test_hold_in_night_mode_only_clears_night_mode);
    RUN_TEST(test_dim_bounces_5_255);
    RUN_TEST(test_click_ge4_ignored);

    RUN_TEST(test_brightness_zero_turns_off);
    RUN_TEST(test_color_switches_to_solid);
    RUN_TEST(test_apply_defaults_only_after_slide_off);
    RUN_TEST(test_makeup_command_powers_on_from_off);
    RUN_TEST(test_night_mode_command_turns_off_when_on);
    RUN_TEST(test_random_effect_command_starts_effect);
    RUN_TEST(test_power_on_noop_while_slide_on);
    RUN_TEST(test_power_on_noop_while_on);
    RUN_TEST(test_power_off_noop_while_off);
    RUN_TEST(test_power_off_noop_while_slide_off);
    RUN_TEST(test_power_off_during_slide_on);
    RUN_TEST(test_power_off_during_slide_on_drops_pending_effect);
    RUN_TEST(test_power_off_cancels_running_effect);
    RUN_TEST(test_automation_flag_gates_pir);

    RUN_TEST(test_pir_turns_on_when_allowed);
    RUN_TEST(test_pir_blocked_during_manual_off_cooldown);
    RUN_TEST(test_pir_blocked_when_automation_off);
    RUN_TEST(test_pir_blocked_in_night_mode);

    RUN_TEST(test_auto_off_after_15min_idle);
    RUN_TEST(test_no_auto_off_when_automation_off);
    RUN_TEST(test_pir_activity_postpones_auto_off);
    RUN_TEST(test_button_event_postpones_auto_off);
    RUN_TEST(test_auto_effect_starts_in_solid);
    RUN_TEST(test_auto_effect_not_in_makeup);
    RUN_TEST(test_enabling_automation_does_not_auto_off_immediately);

    RUN_TEST(test_snapshot_reports_running_effect);
    RUN_TEST(test_snapshot_hides_pending_effect);
    RUN_TEST(test_snapshot_fields);
    RUN_TEST(test_snapshot_reflects_night_mode_and_automation);
    RUN_TEST(test_frame_dirty_only_on_change);
    RUN_TEST(test_frame_brightness_applied);
    return UNITY_END();
}

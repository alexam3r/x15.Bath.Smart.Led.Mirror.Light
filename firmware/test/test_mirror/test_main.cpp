// Mirror tests: the single-owner state machine (ARCHITECTURE.md
// section 4 / 8.1). Runs on the `native` PlatformIO environment (host, no
// Arduino/FreeRTOS dependency). Driven with a fake clock via run(), which
// calls tick() every 5 ms (a PlatformIO test filter runs only this suite:
// `pio test -e native -f test_mirror`).
#include <unity.h>

#include <vector>

#include "Config.h"
#include "Frame.h"
#include "Mirror.h"
#include "Types.h"
#include "effects/Glitch.h"

void setUp(void) {}
void tearDown(void) {}

// Deterministic RandomFn: always returns 0 (index 0 -> CENTERS[0] == 9,
// randomEffect() -> Dark, rollAutoEffectDelay() -> AUTO_EFFECT_MIN_MS).
static uint32_t zeroRandom(uint32_t /*bound*/) { return 0; }

// Advances the fake clock `now` by `ms`, calling tick() every 5 ms — the
// test harness convention.
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

// Long enough for a full slide in either direction: at most
// SLIDE_MAX_RADIUS + 1 steps; with the 5 ms fake-clock tick a step lands every
// SLIDE_STEP_MS rounded up to the next 5 ms. Derived from the constants so
// that tuning the slide does not silently break the waits below.
static const uint32_t kSlideMs =
    (cfg::SLIDE_MAX_RADIUS + 2) * ((cfg::SLIDE_STEP_MS + 4) / 5 * 5) + 200;

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
// v1.0.1: the power-on slide is ~20% slower than v27 (97 steps * 33 ms =
// ~3.2 s nominal, v27: 95 * 28 ms = ~2.7 s). With the 5 ms fake-clock tick each
// step lands 35 ms apart, so SLIDE_ON -> ON takes 97 * 35 = 3395 ms here.
static void test_slide_on_duration_is_20_percent_longer_than_v27(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    m.onButton(click(1), now);
    TEST_ASSERT_TRUE(PowerState::SlideOn == m.power());

    const uint32_t start = now;
    while (m.power() != PowerState::On) {
        now += 5;
        m.tick(now);
        TEST_ASSERT_TRUE_MESSAGE(now - start < 10000, "slide-on never finished");
    }
    const uint32_t took = now - start;
    TEST_ASSERT_TRUE_MESSAGE(took >= 3350 && took <= 3450,
                              "slide-on duration is not ~97 steps of 33 ms (35 ms with 5 ms ticks)");
}

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

// Ruling R17: switching off within ~30 ms of switching on (before the
// slide-on rendered its first step, radius 0) must not flash the ring:
// the slide-out continues from radius 0 — dark — and reaches OFF within a
// step or two, instead of lighting all 168 pixels and playing a full
// ~2.7 s slide-out from the maximum radius.
static void test_power_off_before_first_slide_step_stays_dark(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);

    m.onButton(click(1), now);  // -> SlideOn, radius 0
    m.onButton(click(1), now);  // -> SlideOff before any slide step
    TEST_ASSERT_TRUE(PowerState::SlideOff == m.power());

    uint16_t maxLit = 0;
    uint32_t offAt = 0;
    const uint32_t end = now + 500;
    while (now < end) {
        now += 5;
        m.tick(now);
        const uint16_t lit = litPixelCount(m.frame());
        if (lit > maxLit) maxLit = lit;
        if (offAt == 0 && m.power() == PowerState::Off) offAt = now;
    }
    TEST_ASSERT_TRUE_MESSAGE(maxLit <= 3, "power-off right after power-on lit up the ring");
    TEST_ASSERT_TRUE_MESSAGE(offAt != 0, "never reached OFF");
    TEST_ASSERT_TRUE_MESSAGE(offAt <= 2 * cfg::SLIDE_STEP_MS + 5,
                              "slide-out from radius 0 took more than a couple of steps");
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
static Command lightEffect(EffectRequest req, EffectId id = EffectId::None) {
    Command c;
    c.type = CommandType::Light;
    c.light.effect = req;
    c.light.effectId = id;
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
    run(m, now, cfg::TRANSITION_MS + 50);  // let the colour fade finish
    TEST_ASSERT_TRUE(allPixelsEqual(m.frame(), kMakeupColor));

    m.onButton(click(2), now);
    run(m, now, cfg::TRANSITION_MS + 50);  // let the colour fade finish
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

// Bug A regression + Ruling R14: a hold started while in night mode must
// ONLY clear night mode — the light must not come on, neither from the hold
// itself nor from the PIR. Clearing night mode this way starts the same 15 s
// PIR cooldown as a manual OFF, so a user already standing in front of the
// mirror does not relight it in the very same loop iteration (main.cpp
// calls onPir() right after onButton()), nor while still holding, nor right
// after releasing. Once the cooldown has run out, the PIR works again.
static void test_hold_in_night_mode_only_clears_night_mode(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    m.apply(nightModeCmd(true), now);
    TEST_ASSERT_TRUE(m.snapshot().nightMode);

    now += 100;
    const uint32_t holdStartAt = now;
    m.onButton(holdStart(), now);
    TEST_ASSERT_FALSE(m.snapshot().nightMode);
    TEST_ASSERT_TRUE(PowerState::Off == m.power());

    // Same loop iteration: PIR already HIGH (the user is standing there).
    m.onPir(true, now);
    m.tick(now);
    TEST_ASSERT_TRUE_MESSAGE(PowerState::Off == m.power(),
                              "PIR relit the mirror in the same iteration as a night-mode HoldStart");

    // Keep holding for 2 s with the PIR HIGH the whole time.
    for (int i = 0; i < 2000 / 30; ++i) {
        now += 30;
        m.onButton(holdTick(), now);
        m.onPir(true, now);
        m.tick(now);
        TEST_ASSERT_TRUE_MESSAGE(PowerState::Off == m.power(),
                                  "PIR relit the mirror during a night-mode hold");
    }
    m.onButton(holdEnd(), now);

    // Released: the PIR stays ignored until 15 s after HoldStart.
    while (now < holdStartAt + cfg::PIR_COOLDOWN_MS - 50) {
        now += 5;
        m.onPir(true, now);
        m.tick(now);
        TEST_ASSERT_TRUE_MESSAGE(PowerState::Off == m.power(),
                                  "PIR relit the mirror within 15 s of a night-mode hold");
    }

    // Cooldown over: the PIR auto-on works again.
    now = holdStartAt + cfg::PIR_COOLDOWN_MS + 50;
    m.tick(now);
    m.onPir(true, now);
    TEST_ASSERT_TRUE(PowerState::SlideOn == m.power());
}

// A night-mode button hold starts a 15 s PIR cooldown. A redundant command
// that would only re-affirm the current state must not cancel it (R18):
// motion_disable/set OFF when night mode is already off, or motion/set ON
// when automation is already on.
static void assertRedundantCommandKeepsHoldCooldown(const Command& redundant) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    m.apply(nightModeCmd(true), now);

    now += 100;
    const uint32_t holdStartAt = now;
    m.onButton(holdStart(), now);
    now += 600;
    m.onButton(holdEnd(), now);
    TEST_ASSERT_FALSE(m.snapshot().nightMode);
    TEST_ASSERT_TRUE(m.snapshot().automation);

    now += 550;
    m.apply(redundant, now);

    while (now < holdStartAt + cfg::PIR_COOLDOWN_MS - 50) {
        now += 5;
        m.onPir(true, now);
        m.tick(now);
        TEST_ASSERT_TRUE_MESSAGE(PowerState::Off == m.power(),
                                  "redundant command let the PIR relight the mirror within 15 s of the hold");
    }

    now = holdStartAt + cfg::PIR_COOLDOWN_MS + 50;
    m.tick(now);
    m.onPir(true, now);
    TEST_ASSERT_TRUE(PowerState::SlideOn == m.power());
}

static void test_redundant_night_mode_off_keeps_hold_cooldown(void) {
    assertRedundantCommandKeepsHoldCooldown(nightModeCmd(false));
}

static void test_redundant_automation_on_keeps_hold_cooldown(void) {
    assertRedundantCommandKeepsHoldCooldown(automationCmd(true));
}

// Bug A lock: a hold started in night mode must not dim for the rest of
// that hold, even if the mirror reaches ON before it is released. The PIR
// can no longer do that (see above), so ON is driven here by an explicit
// Light state:1 command mid-hold. A version of this test that never leaves
// OFF during the hold cannot tell the lock apart from a missing
// `holdLocked_ = true;`: the `power_ == On` half of HoldTick's guard already
// blocks dimming on its own while OFF/SLIDE_ON, so the lock is only
// load-bearing once ON is reached mid-hold.
static void test_night_mode_hold_never_dims(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    m.apply(nightModeCmd(true), now);

    m.onButton(holdStart(), now);
    TEST_ASSERT_TRUE(PowerState::Off == m.power());

    // Still holding (no HoldEnd yet): HA/Alice turns the light on.
    now += 10;
    m.apply(lightState(1), now);
    TEST_ASSERT_TRUE(PowerState::SlideOn == m.power());

    // Keep holding through the slide-in. These HoldTicks are separately
    // ignored by HoldTick's own `power_ == On` guard while still sliding
    // (regardless of the lock) — checking pixel values against a "should be
    // untouched" constant here would be invalid anyway, since the slide's
    // edge-fade legitimately renders partial-strength colour during the
    // animation. Just drive the clock forward until ON is reached: 5 ms
    // loop ticks like the other tests, a HoldTick every 30 ms like the
    // real Button (ticking only every 30 ms would stretch the slide
    // whenever SLIDE_STEP_MS is not a divisor of 30).
    uint32_t slideDeadline = now + kSlideMs;
    uint32_t lastHoldTick = now;
    while (m.power() != PowerState::On && now < slideDeadline) {
        now += 5;
        if (now - lastHoldTick >= 30) {
            lastHoldTick = now;
            m.onButton(holdTick(), now);
        }
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
    run(m, now, cfg::TRANSITION_MS + 50);  // let the colour fade finish
    TEST_ASSERT_TRUE(allPixelsEqual(m.frame(), kMakeupColor));

    m.apply(lightColor(10, 20, 30), now);
    run(m, now, cfg::TRANSITION_MS + 50);  // let the colour fade finish
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
    run(m, now, cfg::TRANSITION_MS + 50);  // let the colour fade finish
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
    run(m, now, cfg::TRANSITION_MS + 50);  // let the colour fade finish
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

// §5.2 step 3 / Ruling R15: a named temporary effect arrives as
// EffectRequest::Temporary plus the registry's EffectId, and Mirror starts
// exactly that effect — Mirror has no per-effect table. A second request
// replaces the running one (table 4.1, ON row). Wave/Rainbow are used
// because zeroRandom's random pick is Dark, so a request mistakenly routed
// through the random path would show up as Dark.
static void test_light_named_effect_starts_that_effect(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    m.onButton(click(1), now);
    run(m, now, kSlideMs);

    m.apply(lightEffect(EffectRequest::Temporary, EffectId::Wave), now);
    TEST_ASSERT_TRUE(EffectId::Wave == m.snapshot().effect);

    run(m, now, 100);
    m.apply(lightEffect(EffectRequest::Temporary, EffectId::Rainbow), now);
    TEST_ASSERT_TRUE(EffectId::Rainbow == m.snapshot().effect);

    // A Temporary request without an id (never produced by Protocol) is a
    // no-op rather than a crash, and leaves the running effect alone.
    m.apply(lightEffect(EffectRequest::Temporary, EffectId::None), now);
    TEST_ASSERT_TRUE(EffectId::Rainbow == m.snapshot().effect);
    run(m, now, 100);
    TEST_ASSERT_TRUE(PowerState::On == m.power());
}

// "random" in the JSON Light picks from the registry (zeroRandom -> the
// first kEffects[] entry, Dark).
static void test_light_random_effect_starts_random_effect(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    m.onButton(click(1), now);
    run(m, now, kSlideMs);

    m.apply(lightEffect(EffectRequest::Random), now);
    TEST_ASSERT_TRUE(EffectId::Dark == m.snapshot().effect);
}

// {"state":"ON","effect":"rainbow"} from OFF: the power-on is applied first
// and the effect is deferred until the slide-in finishes (§5.2 order,
// table 4.1 SLIDE_ON row).
static void test_light_on_with_named_effect_defers_it(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);

    Command c = lightEffect(EffectRequest::Temporary, EffectId::Rainbow);
    c.light.state = 1;
    m.apply(c, now);
    TEST_ASSERT_TRUE(PowerState::SlideOn == m.power());
    TEST_ASSERT_TRUE(EffectId::None == m.snapshot().effect);

    run(m, now, kSlideMs);
    TEST_ASSERT_TRUE(PowerState::On == m.power());
    TEST_ASSERT_TRUE(EffectId::Rainbow == m.snapshot().effect);
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
    run(m, now, cfg::TRANSITION_MS + 50);  // let the colour fade finish
    TEST_ASSERT_TRUE(allPixelsEqual(m.frame(), scale(kSolidDefault, 128)));
}

// --- Glitch overlay ("neon failure", v1.1.0) --------------------------------
//
// zeroRandom: every interval is GLITCH_INTERVAL_MIN_MS (45 s), the core
// starts at pixel 0 with GLITCH_LEN_MIN (3) pixels and GLITCH_EDGE_MIN (2)
// edge pixels each side, lasts 300 ms, and every neon level pick is
// kGlitchLevels[0] (off) -> pixels 0..2 go black, 3,4 and 167,166 fade.

static Command glitchCmd(bool on) {
    Command c;
    c.type = CommandType::Glitch;
    c.flag = on;
    return c;
}

// Powers on and ticks (5 ms) until SLIDE_ON -> ON; returns that moment.
static uint32_t powerOnSettled(Mirror& m, uint32_t& now) {
    m.onButton(click(1), now);
    while (m.power() != PowerState::On) {
        now += 5;
        m.tick(now);
    }
    return now;
}

// Ticks (5 ms) until `until`; true if any frame differed from plain `base`.
static bool glitchSeenUntil(Mirror& m, uint32_t& now, uint32_t until, Rgbw base) {
    bool seen = false;
    while (now < until) {
        now += 5;
        m.tick(now);
        if (!allPixelsEqual(m.frame(), base)) seen = true;
    }
    return seen;
}

// Latest interval, 6-pixel segment from pixel 167 (wraps), level "off".
static uint32_t lateRandom(uint32_t bound) {
    if (bound == kGlitchLevelCount) return 0;
    return bound - 1;
}

static void test_glitch_first_fires_45s_after_power_on(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    const uint32_t onAt = powerOnSettled(m, now);
    const StateSnapshot before = m.snapshot();

    TEST_ASSERT_FALSE_MESSAGE(glitchSeenUntil(m, now, onAt + cfg::GLITCH_INTERVAL_MIN_MS - 5, kSolidDefault),
                              "glitch before 45 s");
    now = onAt + cfg::GLITCH_INTERVAL_MIN_MS;
    m.tick(now);
    const Rgbw off = scale(kSolidDefault, kGlitchLevels[0]);
    TEST_ASSERT_TRUE(off == m.frame()[0]);
    TEST_ASSERT_TRUE(off == m.frame()[2]);
    TEST_ASSERT_TRUE(scale(kSolidDefault, 85) == m.frame()[3]);    // soft edge, 1/3 of the way
    TEST_ASSERT_TRUE(scale(kSolidDefault, 85) == m.frame()[167]);  // left edge wraps below 0
    TEST_ASSERT_TRUE(kSolidDefault == m.frame()[5]);
    TEST_ASSERT_TRUE(kSolidDefault == m.frame()[165]);
    TEST_ASSERT_TRUE_MESSAGE(before == m.snapshot(), "glitch changed the reported state");

    run(m, now, cfg::GLITCH_DURATION_MIN_MS + cfg::GLITCH_STEP_MS);
    TEST_ASSERT_TRUE_MESSAGE(allPixelsEqual(m.frame(), kSolidDefault), "base not restored after the glitch");
}

static void test_glitch_interval_upper_bound_is_90s(void) {
    Mirror m(lateRandom);
    uint32_t now = 0;
    m.begin(now);
    const uint32_t onAt = powerOnSettled(m, now);

    TEST_ASSERT_FALSE(glitchSeenUntil(m, now, onAt + cfg::GLITCH_INTERVAL_MAX_MS - 5, kSolidDefault));
    now = onAt + cfg::GLITCH_INTERVAL_MAX_MS;
    m.tick(now);
    const Rgbw off = scale(kSolidDefault, kGlitchLevels[0]);
    TEST_ASSERT_TRUE(off == m.frame()[167]);
    TEST_ASSERT_TRUE(off == m.frame()[4]);  // 6-pixel core: 167, 0..4
    TEST_ASSERT_TRUE(scale(kSolidDefault, 63) == m.frame()[5]);    // 3-pixel edges: 5..7, 166..164
    TEST_ASSERT_TRUE(scale(kSolidDefault, 191) == m.frame()[7]);
    TEST_ASSERT_TRUE(scale(kSolidDefault, 191) == m.frame()[164]);
    TEST_ASSERT_TRUE(kSolidDefault == m.frame()[8]);
    TEST_ASSERT_TRUE(kSolidDefault == m.frame()[163]);
}

static void test_glitch_never_in_makeup(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    const uint32_t onAt = powerOnSettled(m, now);
    m.apply(makeupCmd(true), now);
    run(m, now, cfg::TRANSITION_MS + 50);  // let the colour fade finish
    TEST_ASSERT_FALSE(glitchSeenUntil(m, now, onAt + 2 * cfg::GLITCH_INTERVAL_MAX_MS, kMakeupColor));
}

static void test_glitch_never_when_automation_off(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    const uint32_t onAt = powerOnSettled(m, now);
    m.apply(automationCmd(false), now);
    TEST_ASSERT_FALSE(glitchSeenUntil(m, now, onAt + 2 * cfg::GLITCH_INTERVAL_MAX_MS, kSolidDefault));
}

static void test_glitch_switch_disables_and_reenables(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    TEST_ASSERT_TRUE(m.snapshot().glitch);  // on by default
    const uint32_t onAt = powerOnSettled(m, now);

    m.apply(glitchCmd(false), now);
    TEST_ASSERT_FALSE(m.snapshot().glitch);
    TEST_ASSERT_FALSE(glitchSeenUntil(m, now, onAt + 2 * cfg::GLITCH_INTERVAL_MAX_MS, kSolidDefault));

    m.apply(glitchCmd(true), now);
    TEST_ASSERT_TRUE(m.snapshot().glitch);
    TEST_ASSERT_TRUE(glitchSeenUntil(m, now, now + cfg::GLITCH_INTERVAL_MAX_MS, kSolidDefault));
}

static void test_glitch_skipped_while_button_held(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    const uint32_t onAt = powerOnSettled(m, now);

    run(m, now, cfg::GLITCH_INTERVAL_MIN_MS - 1000);
    m.onButton(holdStart(), now);  // held across the due moment, no dimming ticks
    TEST_ASSERT_FALSE(glitchSeenUntil(m, now, onAt + cfg::GLITCH_INTERVAL_MIN_MS + 1000, kSolidDefault));
    m.onButton(holdEnd(), now);

    // Skipped, not deferred: the next one comes one interval later.
    TEST_ASSERT_FALSE(glitchSeenUntil(m, now, onAt + 2 * cfg::GLITCH_INTERVAL_MIN_MS - 5, kSolidDefault));
    TEST_ASSERT_TRUE(glitchSeenUntil(m, now, onAt + 2 * cfg::GLITCH_INTERVAL_MIN_MS + 5, kSolidDefault));
}

static uint32_t frameHash(const Frame& f) {
    uint32_t h = 2166136261u;  // FNV-1a over every channel of every pixel
    for (uint16_t i = 0; i < Frame::kSize; ++i) {
        const uint8_t ch[4] = {f[i].r, f[i].g, f[i].b, f[i].w};
        for (uint8_t c : ch) h = (h ^ c) * 16777619u;
    }
    return h;
}

// Powers on, starts a random effect ~1 s before the first glitch is due and
// records a hash of every frame while it runs (effect instances are shared
// registry singletons, so the two runs below must be sequential, not
// interleaved).
static void recordEffectFrames(bool glitchOn, std::vector<uint32_t>& hashes, Mirror& m, uint32_t& now) {
    m.begin(now);
    m.apply(glitchCmd(glitchOn), now);
    const uint32_t onAt = powerOnSettled(m, now);
    run(m, now, cfg::GLITCH_INTERVAL_MIN_MS - 1000 - (now - onAt));
    m.apply(randomEffectCmd(), now);  // dark snake, ~9 s: running at the due moment
    const uint32_t end = now + 12000;
    while (now < end) {
        now += 5;
        m.tick(now);
        hashes.push_back(frameHash(m.frame()));
    }
}

// A glitch due while an effect runs is skipped, not deferred, and never
// paints over the effect: the frames are identical to a run with the glitch
// switched off.
static void test_glitch_skipped_during_effect(void) {
    std::vector<uint32_t> ref, got;
    Mirror refMirror(zeroRandom);
    uint32_t refNow = 0;
    recordEffectFrames(false, ref, refMirror, refNow);

    Mirror m(zeroRandom);
    uint32_t now = 0;
    recordEffectFrames(true, got, m, now);
    TEST_ASSERT_EQUAL_UINT32(ref.size(), got.size());
    for (size_t i = 0; i < ref.size(); ++i) {
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(ref[i], got[i], "glitch painted over a running effect");
    }

    TEST_ASSERT_TRUE(EffectId::None == m.snapshot().effect);
    TEST_ASSERT_FALSE_MESSAGE(glitchSeenUntil(m, now, now + 30000, kSolidDefault),
                              "a glitch due during an effect was deferred instead of skipped");
}

static void test_glitch_cancelled_by_color_change(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    const uint32_t onAt = powerOnSettled(m, now);
    now = onAt + cfg::GLITCH_INTERVAL_MIN_MS;
    m.tick(now);
    TEST_ASSERT_FALSE(allPixelsEqual(m.frame(), kSolidDefault));  // glitch running

    m.apply(lightColor(0, 0, 255), now);
    const Rgbw blue{0, 0, 255, 0};
    for (int i = 0; i < 20; ++i) {  // mid-fade, and the 300 ms glitch would still be running
        now += 5;
        m.tick(now);
        TEST_ASSERT_TRUE_MESSAGE(allPixelsEqual(m.frame(), m.frame()[100]), "glitch pixels after a colour change");
    }
    run(m, now, cfg::TRANSITION_MS);  // let the colour fade finish
    TEST_ASSERT_TRUE(allPixelsEqual(m.frame(), blue));
    TEST_ASSERT_FALSE_MESSAGE(glitchSeenUntil(m, now, now + cfg::GLITCH_DURATION_MAX_MS, blue),
                              "the glitch kept running after a colour change");
}

static void test_glitch_does_not_delay_auto_effect(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    m.onButton(click(1), now);  // activity at t=0 -> auto effect after AUTO_EFFECT_MIN_MS

    bool glitched = false;
    while (now < cfg::AUTO_EFFECT_MIN_MS - 50) {
        now += 5;
        m.tick(now);
        if (m.power() == PowerState::On && !allPixelsEqual(m.frame(), kSolidDefault)) glitched = true;
        TEST_ASSERT_TRUE(EffectId::None == m.snapshot().effect);
    }
    TEST_ASSERT_TRUE_MESSAGE(glitched, "no glitch in the first 4 minutes");
    run(m, now, 100);
    TEST_ASSERT_TRUE_MESSAGE(EffectId::None != m.snapshot().effect, "glitches delayed the auto effect");
}

// --- Transitions (v1.2.0): colour/brightness/mode changes from commands and
// the double click fade over cfg::TRANSITION_MS; button dimming and power-on
// from OFF apply at once. ----------------------------------------------------

static bool channelBetween(uint8_t v, uint8_t lo, uint8_t hi) { return v >= lo && v <= hi; }

static void test_color_command_transitions_over_500ms(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    powerOnSettled(m, now);

    m.apply(lightColor(0, 0, 255), now);
    TEST_ASSERT_EQUAL_UINT8(0, m.snapshot().r);  // state reports the target at once
    TEST_ASSERT_EQUAL_UINT8(255, m.snapshot().b);
    run(m, now, cfg::TRANSITION_MS / 2);
    const Rgbw mid = m.frame()[0];
    TEST_ASSERT_TRUE_MESSAGE(allPixelsEqual(m.frame(), mid), "transition frame is not uniform");
    TEST_ASSERT_TRUE_MESSAGE(channelBetween(mid.r, 110, 145), "red did not fade halfway at 250 ms");
    TEST_ASSERT_TRUE_MESSAGE(channelBetween(mid.b, 130, 170), "blue did not rise halfway at 250 ms");
    run(m, now, cfg::TRANSITION_MS / 2 + 50);
    TEST_ASSERT_TRUE(allPixelsEqual(m.frame(), Rgbw{0, 0, 255, 0}));
}

static void test_brightness_command_transitions(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    powerOnSettled(m, now);

    m.apply(lightBrightness(55), now);
    TEST_ASSERT_EQUAL_UINT8(55, m.snapshot().brightness);
    run(m, now, cfg::TRANSITION_MS / 2);
    TEST_ASSERT_TRUE(channelBetween(m.frame()[0].r, scale8(255, 140), scale8(255, 170)));
    run(m, now, cfg::TRANSITION_MS / 2 + 50);
    TEST_ASSERT_TRUE(allPixelsEqual(m.frame(), scale(kSolidDefault, 55)));
}

static void test_makeup_toggle_transitions(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    powerOnSettled(m, now);

    m.onButton(click(2), now);  // solid -> makeup
    run(m, now, cfg::TRANSITION_MS / 2);
    const Rgbw mid = m.frame()[0];
    TEST_ASSERT_TRUE(channelBetween(mid.r, 110, 145));
    TEST_ASSERT_TRUE(channelBetween(mid.w, 110, 145));
    run(m, now, cfg::TRANSITION_MS / 2 + 50);
    TEST_ASSERT_TRUE(allPixelsEqual(m.frame(), kMakeupColor));
}

// A new command mid-transition fades on from the colour currently shown.
static void test_command_mid_transition_continues_from_shown_colour(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    powerOnSettled(m, now);

    m.apply(lightColor(0, 0, 255), now);
    run(m, now, cfg::TRANSITION_MS / 2);
    const Rgbw mid = m.frame()[0];
    m.apply(lightColor(0, 255, 0), now);
    run(m, now, 5);
    const Rgbw after = m.frame()[0];
    TEST_ASSERT_TRUE_MESSAGE(channelBetween(after.r, mid.r - 10, mid.r), "restarted from the old target");
    run(m, now, cfg::TRANSITION_MS + 50);
    TEST_ASSERT_TRUE(allPixelsEqual(m.frame(), Rgbw{0, 255, 0, 0}));
}

static void test_hold_dimming_stays_immediate(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    powerOnSettled(m, now);

    m.onButton(holdStart(), now);
    // v27: from DIM_MAX the first tick clamps and turns downwards, the second dims.
    for (int i = 0; i < 2; ++i) {
        now += cfg::DIM_PERIOD_MS;
        m.onButton(holdTick(), now);
    }
    m.tick(now);
    TEST_ASSERT_TRUE(allPixelsEqual(m.frame(), scale(kSolidDefault, cfg::DIM_MAX - cfg::DIM_STEP)));
    m.onButton(holdEnd(), now);
}

static void test_power_on_from_off_uses_new_colour_at_once(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    m.apply(makeupCmd(true), now);  // from OFF: base = makeup and power on, no fade
    run(m, now, cfg::SLIDE_STEP_MS * 12);
    bool sawLit = false;
    for (uint16_t i = 0; i < Frame::kSize; ++i) {
        const Rgbw p = m.frame()[i];
        if (p.w > 0) sawLit = true;
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, p.r, "slide-in painted the old solid colour");
    }
    TEST_ASSERT_TRUE(sawLit);
}

// A double click while sliding off reverses the slide in makeup: the new base
// shows at once and stays (nothing may keep the old solid colour on screen).
static void test_click2_during_slide_off_shows_makeup(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    powerOnSettled(m, now);
    m.onButton(click(1), now);
    run(m, now, cfg::SLIDE_STEP_MS * 5);
    TEST_ASSERT_TRUE(PowerState::SlideOff == m.power());

    m.onButton(click(2), now);
    TEST_ASSERT_TRUE(PowerState::SlideOn == m.power());
    run(m, now, kSlideMs);
    TEST_ASSERT_TRUE(PowerState::On == m.power());
    TEST_ASSERT_TRUE(allPixelsEqual(m.frame(), kMakeupColor));
}

// --- Pre-auto-off warning (v1.2.0): a minute before auto-off the rendered
// brightness fades to 50 %; any activity restores it and restarts the timer.
// The glitch is switched off in these tests so a random overlay cannot make
// the frame non-uniform at the moment it is sampled. ------------------------

static void test_warning_dims_to_half_one_minute_before_auto_off(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    powerOnSettled(m, now);  // activity at the click (t = 0)
    m.apply(glitchCmd(false), now);

    run(m, now, cfg::AUTO_OFF_MS - cfg::AUTO_OFF_WARN_MS - 10 - now);
    TEST_ASSERT_TRUE_MESSAGE(allPixelsEqual(m.frame(), kSolidDefault), "dimmed too early");
    run(m, now, 10 + cfg::WARN_FADE_IN_MS + 100);
    TEST_ASSERT_TRUE_MESSAGE(allPixelsEqual(m.frame(), scale(kSolidDefault, cfg::WARN_DIM_LEVEL)),
                             "not dimmed to 50 % two seconds into the warning");
    TEST_ASSERT_EQUAL_UINT8(cfg::DEFAULT_BRIGHTNESS, m.snapshot().brightness);  // HA setting untouched
    TEST_ASSERT_TRUE(PowerState::On == m.power());
}

static void test_activity_during_warning_restores_and_postpones(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    powerOnSettled(m, now);
    m.apply(glitchCmd(false), now);
    run(m, now, cfg::AUTO_OFF_MS - cfg::AUTO_OFF_WARN_MS + 5000 - now);
    TEST_ASSERT_FALSE_MESSAGE(allPixelsEqual(m.frame(), kSolidDefault), "warning did not start");

    m.onPir(true, now);
    m.onPir(false, now);
    run(m, now, cfg::WARN_FADE_OUT_MS + 100);
    TEST_ASSERT_TRUE_MESSAGE(allPixelsEqual(m.frame(), kSolidDefault), "brightness not restored after activity");

    run(m, now, cfg::AUTO_OFF_MS - cfg::AUTO_OFF_WARN_MS - 5000);  // past the old auto-off moment
    TEST_ASSERT_TRUE_MESSAGE(PowerState::On == m.power(), "auto-off timer was not restarted");
    TEST_ASSERT_TRUE(allPixelsEqual(m.frame(), kSolidDefault));
}

static void test_no_warning_when_automation_off(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    powerOnSettled(m, now);
    m.apply(automationCmd(false), now);
    run(m, now, cfg::AUTO_OFF_MS + 60000);
    TEST_ASSERT_TRUE(PowerState::On == m.power());
    TEST_ASSERT_TRUE(allPixelsEqual(m.frame(), kSolidDefault));
}

// Switching automation off mid-warning must not leave the light at 50 % for good.
static void test_automation_off_during_warning_restores_brightness(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    powerOnSettled(m, now);
    m.apply(glitchCmd(false), now);
    run(m, now, cfg::AUTO_OFF_MS - cfg::AUTO_OFF_WARN_MS + cfg::WARN_FADE_IN_MS + 100 - now);
    TEST_ASSERT_TRUE(allPixelsEqual(m.frame(), scale(kSolidDefault, cfg::WARN_DIM_LEVEL)));

    m.apply(automationCmd(false), now);
    run(m, now, cfg::WARN_FADE_OUT_MS + 100);
    TEST_ASSERT_TRUE_MESSAGE(allPixelsEqual(m.frame(), kSolidDefault), "still dimmed after automation was switched off");
}

// Auto effect after 280 s of idle (and the dark snake, pixel 0, +1): the
// auto effects then land at ~280 s, ~569 s and ~858 s — the last one inside
// the warning minute (840..900 s). An effect started by a command would not
// do: a command is activity and ends the warning (v1.2.1).
static uint32_t autoEffectEvery280s(uint32_t bound) {
    return bound == cfg::AUTO_EFFECT_MAX_MS - cfg::AUTO_EFFECT_MIN_MS ? 40000 : 0;
}

static void test_warning_scales_a_running_effect(void) {
    Mirror m(autoEffectEvery280s);
    uint32_t now = 0;
    m.begin(now);
    powerOnSettled(m, now);
    m.apply(glitchCmd(false), now);
    run(m, now, cfg::AUTO_OFF_MS - cfg::AUTO_OFF_WARN_MS + cfg::WARN_FADE_IN_MS + 100 - now);
    while (m.snapshot().effect == EffectId::None && now < cfg::AUTO_OFF_MS) {
        now += 5;
        m.tick(now);
    }
    TEST_ASSERT_TRUE_MESSAGE(EffectId::None != m.snapshot().effect, "no auto effect inside the warning minute");
    run(m, now, cfg::SNAKE_STEP_MS + 5);
    // Pixel 100 is far behind the head for the first steps -> plain base, dimmed.
    TEST_ASSERT_TRUE(scale(kSolidDefault, cfg::WARN_DIM_LEVEL) == m.frame()[100]);
}

// --- Makeup keeps the light for 45 minutes (v1.2.0) -------------------------
// The limit follows the base mode, and a mode change counts as activity: the
// idle time spent in makeup must not switch the light off the moment the user
// goes back to solid. No auto-effects or glitches run in makeup, so the frame
// checks below are stable.

static void test_makeup_auto_off_after_45_minutes(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    powerOnSettled(m, now);
    m.apply(makeupCmd(true), now);

    run(m, now, cfg::AUTO_OFF_MS + 5UL * 60 * 1000);  // 20 min idle: solid would be off by now
    TEST_ASSERT_TRUE_MESSAGE(PowerState::On == m.power(), "makeup switched off before 45 min");
    run(m, now, cfg::AUTO_OFF_MAKEUP_MS - (cfg::AUTO_OFF_MS + 5UL * 60 * 1000) - 60000);
    TEST_ASSERT_TRUE(PowerState::On == m.power());  // 44 min
    run(m, now, 60000 + 100 + kSlideMs);
    TEST_ASSERT_TRUE_MESSAGE(PowerState::Off == m.power(), "makeup did not auto-off after 45 min");
}

static void test_makeup_warning_comes_a_minute_before_its_own_limit(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    powerOnSettled(m, now);
    m.apply(makeupCmd(true), now);  // activity: the 45 min are counted from here

    run(m, now, cfg::AUTO_OFF_MAKEUP_MS - cfg::AUTO_OFF_WARN_MS - 10);
    TEST_ASSERT_TRUE_MESSAGE(allPixelsEqual(m.frame(), kMakeupColor), "dimmed before the 44th minute");
    run(m, now, 10 + cfg::WARN_FADE_IN_MS + 100);
    TEST_ASSERT_TRUE_MESSAGE(allPixelsEqual(m.frame(), scale(kMakeupColor, cfg::WARN_DIM_LEVEL)),
                             "no warning a minute before the makeup limit");
}

static void test_leaving_makeup_restarts_the_idle_timer(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    powerOnSettled(m, now);
    m.apply(makeupCmd(true), now);
    run(m, now, 30UL * 60 * 1000);  // 30 min idle in makeup

    m.apply(makeupCmd(false), now);  // back to solid, whose limit is 15 min
    run(m, now, cfg::AUTO_OFF_MS - 60000);
    TEST_ASSERT_TRUE_MESSAGE(PowerState::On == m.power(), "mode change did not count as activity");
    run(m, now, 60000 + 100 + kSlideMs);
    TEST_ASSERT_TRUE(PowerState::Off == m.power());
}

// The same, through the double click (it goes through onButton, not apply).
static void test_click2_into_makeup_extends_the_limit(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    powerOnSettled(m, now);
    m.onButton(click(2), now);  // solid -> makeup
    run(m, now, cfg::AUTO_OFF_MS + 5UL * 60 * 1000);
    TEST_ASSERT_TRUE(PowerState::On == m.power());
}

// The JSON Light path (effect: makeup / solid) must restart the timer too —
// it is a different branch of apply() than the makeup/set switch.
static void test_light_effect_solid_after_long_makeup_restarts_the_timer(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    powerOnSettled(m, now);
    m.apply(lightEffect(EffectRequest::Makeup), now);
    run(m, now, 30UL * 60 * 1000);  // 30 min in makeup: past the solid limit

    m.apply(lightEffect(EffectRequest::Solid), now);
    run(m, now, cfg::AUTO_OFF_MS - 60000);
    TEST_ASSERT_TRUE_MESSAGE(PowerState::On == m.power(), "JSON mode change did not count as activity");
    run(m, now, 60000 + 100 + kSlideMs);
    TEST_ASSERT_TRUE(PowerState::Off == m.power());
}

// --- Commands to a lit mirror count as activity (v1.2.1) ----------------------
// Someone the PIR cannot see (in the shower) talking to Alice is still there:
// a command must end the pre-auto-off warning and restart the timer.

static void test_ha_command_during_warning_restores_and_postpones(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    powerOnSettled(m, now);
    m.apply(glitchCmd(false), now);
    run(m, now, cfg::AUTO_OFF_MS - cfg::AUTO_OFF_WARN_MS + cfg::WARN_FADE_IN_MS + 100 - now);
    TEST_ASSERT_TRUE(allPixelsEqual(m.frame(), scale(kSolidDefault, cfg::WARN_DIM_LEVEL)));

    m.apply(lightBrightness(200), now);  // "Alice, mirror brightness 80 %"
    run(m, now, cfg::TRANSITION_MS + cfg::WARN_FADE_OUT_MS + 100);
    TEST_ASSERT_TRUE_MESSAGE(allPixelsEqual(m.frame(), scale(kSolidDefault, 200)),
                             "a command did not end the warning");
    run(m, now, cfg::AUTO_OFF_MS - cfg::AUTO_OFF_WARN_MS - 5000);  // past the old auto-off moment
    TEST_ASSERT_TRUE_MESSAGE(PowerState::On == m.power(), "a command did not restart the auto-off timer");
}

static void test_effect_command_postpones_auto_off(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    powerOnSettled(m, now);  // activity at t = 0
    run(m, now, 10UL * 60 * 1000 - now);
    m.apply(randomEffectCmd(), now);  // the effect/set button in HA at 10 min
    run(m, now, cfg::AUTO_OFF_MS - 60000);  // 24 min: off at 15 min without the fix
    TEST_ASSERT_TRUE_MESSAGE(PowerState::On == m.power(), "an effect command did not count as activity");
}

// The automatic effect every 4-5 min must NOT count, or the mirror would
// never switch itself off.
static void test_auto_effect_does_not_postpone_auto_off(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    powerOnSettled(m, now);
    run(m, now, cfg::AUTO_OFF_MS + kSlideMs + 1000 - now);
    TEST_ASSERT_TRUE_MESSAGE(PowerState::Off == m.power(), "auto-effects kept the mirror on");
}

// --- Stuck PIR (v1.2.1) -------------------------------------------------------
// A PIR stuck HIGH for over PIR_STUCK_MS stops counting as activity and stops
// switching the mirror on, until it goes LOW. Its raw level still reaches HA.

// Like run(), but feeds the PIR level on every 5 ms pass, as loop() does.
static void runWithPir(Mirror& m, uint32_t& now, uint32_t ms, bool pir) {
    const uint32_t end = now + ms;
    while (now < end) {
        now += 5;
        m.onPir(pir, now);
        m.tick(now);
    }
}

static void test_stuck_pir_stops_holding_the_light_on(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    runWithPir(m, now, 10000, true);  // auto-on, then HIGH for good
    TEST_ASSERT_TRUE(PowerState::On == m.power());
    runWithPir(m, now, cfg::PIR_STUCK_MS - 20000, true);
    TEST_ASSERT_TRUE_MESSAGE(PowerState::On == m.power(), "a busy PIR must keep the light on");

    runWithPir(m, now, 20000 + cfg::AUTO_OFF_MS + kSlideMs + 1000, true);
    TEST_ASSERT_TRUE_MESSAGE(PowerState::Off == m.power(), "a PIR stuck HIGH kept the mirror on forever");
    TEST_ASSERT_TRUE_MESSAGE(m.snapshot().pir, "the raw PIR level must still be reported");
    runWithPir(m, now, 30UL * 60 * 1000, true);
    TEST_ASSERT_TRUE_MESSAGE(PowerState::Off == m.power(), "a stuck PIR switched the mirror back on");
}

static void test_pir_works_again_after_going_low(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    runWithPir(m, now, cfg::PIR_STUCK_MS + cfg::AUTO_OFF_MS + kSlideMs + 10000, true);
    TEST_ASSERT_TRUE(PowerState::Off == m.power());

    runWithPir(m, now, 1000, false);  // the sensor recovers
    runWithPir(m, now, 100, true);
    TEST_ASSERT_TRUE_MESSAGE(PowerState::SlideOn == m.power() || PowerState::On == m.power(),
                             "the PIR stayed ignored after it went LOW");
}

// A PIR that is HIGH most of the time but drops now and then is not stuck:
// the limit counts from the last rising edge.
static void test_pir_with_short_drops_is_not_stuck(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    for (int i = 0; i < 3; ++i) {
        runWithPir(m, now, cfg::PIR_STUCK_MS - 60000, true);
        runWithPir(m, now, 500, false);
    }
    TEST_ASSERT_TRUE_MESSAGE(PowerState::On == m.power(), "a busy PIR with short drops was treated as stuck");
}

// --- Choosing solid/makeup in HA stops a running effect (v1.2.1) --------------

static void test_choosing_solid_stops_a_running_effect(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    powerOnSettled(m, now);
    m.apply(lightEffect(EffectRequest::Temporary, EffectId::Candle), now);
    run(m, now, 200);
    TEST_ASSERT_TRUE(EffectId::Candle == m.snapshot().effect);

    m.apply(lightEffect(EffectRequest::Solid), now);  // picked "solid" in the HA effect list
    TEST_ASSERT_TRUE_MESSAGE(EffectId::None == m.snapshot().effect, "the running effect kept going");
    run(m, now, cfg::TRANSITION_MS + 50);
    TEST_ASSERT_TRUE(allPixelsEqual(m.frame(), kSolidDefault));
}

static void test_choosing_makeup_stops_a_running_effect(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    powerOnSettled(m, now);
    m.apply(lightEffect(EffectRequest::Temporary, EffectId::Comet), now);
    run(m, now, 200);
    m.apply(lightEffect(EffectRequest::Makeup), now);
    TEST_ASSERT_TRUE(EffectId::None == m.snapshot().effect);
    run(m, now, cfg::TRANSITION_MS + 50);
    TEST_ASSERT_TRUE(allPixelsEqual(m.frame(), kMakeupColor));
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_begins_off);
    RUN_TEST(test_click1_from_off_starts_slide_on);
    RUN_TEST(test_slide_on_completes_to_on);
    RUN_TEST(test_slide_on_duration_is_20_percent_longer_than_v27);
    RUN_TEST(test_click1_from_on_slides_off_to_off);
    RUN_TEST(test_power_on_during_slide_off_reverses);
    RUN_TEST(test_power_off_before_first_slide_step_stays_dark);

    RUN_TEST(test_click2_from_off_makeup_and_powers_on);
    RUN_TEST(test_click2_toggles_base_when_on);
    RUN_TEST(test_click3_from_off_ignored);
    RUN_TEST(test_click3_during_slide_off_ignored);
    RUN_TEST(test_effect_during_slide_on_is_deferred);
    RUN_TEST(test_click3_replaces_running_effect);
    RUN_TEST(test_effect_finishes_returns_to_static);
    RUN_TEST(test_hold_from_off_slides_then_dims);
    RUN_TEST(test_hold_in_night_mode_only_clears_night_mode);
    RUN_TEST(test_night_mode_hold_never_dims);
    RUN_TEST(test_redundant_night_mode_off_keeps_hold_cooldown);
    RUN_TEST(test_redundant_automation_on_keeps_hold_cooldown);
    RUN_TEST(test_dim_bounces_5_255);
    RUN_TEST(test_click_ge4_ignored);

    RUN_TEST(test_brightness_zero_turns_off);
    RUN_TEST(test_color_switches_to_solid);
    RUN_TEST(test_apply_defaults_only_after_slide_off);
    RUN_TEST(test_makeup_command_powers_on_from_off);
    RUN_TEST(test_night_mode_command_turns_off_when_on);
    RUN_TEST(test_random_effect_command_starts_effect);
    RUN_TEST(test_light_named_effect_starts_that_effect);
    RUN_TEST(test_light_random_effect_starts_random_effect);
    RUN_TEST(test_light_on_with_named_effect_defers_it);
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
    RUN_TEST(test_glitch_first_fires_45s_after_power_on);
    RUN_TEST(test_glitch_interval_upper_bound_is_90s);
    RUN_TEST(test_glitch_never_in_makeup);
    RUN_TEST(test_glitch_never_when_automation_off);
    RUN_TEST(test_glitch_switch_disables_and_reenables);
    RUN_TEST(test_glitch_skipped_while_button_held);
    RUN_TEST(test_glitch_skipped_during_effect);
    RUN_TEST(test_glitch_cancelled_by_color_change);
    RUN_TEST(test_glitch_does_not_delay_auto_effect);
    RUN_TEST(test_color_command_transitions_over_500ms);
    RUN_TEST(test_brightness_command_transitions);
    RUN_TEST(test_makeup_toggle_transitions);
    RUN_TEST(test_command_mid_transition_continues_from_shown_colour);
    RUN_TEST(test_hold_dimming_stays_immediate);
    RUN_TEST(test_power_on_from_off_uses_new_colour_at_once);
    RUN_TEST(test_click2_during_slide_off_shows_makeup);
    RUN_TEST(test_warning_dims_to_half_one_minute_before_auto_off);
    RUN_TEST(test_activity_during_warning_restores_and_postpones);
    RUN_TEST(test_no_warning_when_automation_off);
    RUN_TEST(test_automation_off_during_warning_restores_brightness);
    RUN_TEST(test_warning_scales_a_running_effect);
    RUN_TEST(test_makeup_auto_off_after_45_minutes);
    RUN_TEST(test_makeup_warning_comes_a_minute_before_its_own_limit);
    RUN_TEST(test_leaving_makeup_restarts_the_idle_timer);
    RUN_TEST(test_click2_into_makeup_extends_the_limit);
    RUN_TEST(test_light_effect_solid_after_long_makeup_restarts_the_timer);
    RUN_TEST(test_ha_command_during_warning_restores_and_postpones);
    RUN_TEST(test_effect_command_postpones_auto_off);
    RUN_TEST(test_auto_effect_does_not_postpone_auto_off);
    RUN_TEST(test_stuck_pir_stops_holding_the_light_on);
    RUN_TEST(test_pir_works_again_after_going_low);
    RUN_TEST(test_pir_with_short_drops_is_not_stuck);
    RUN_TEST(test_choosing_solid_stops_a_running_effect);
    RUN_TEST(test_choosing_makeup_stops_a_running_effect);
    return UNITY_END();
}

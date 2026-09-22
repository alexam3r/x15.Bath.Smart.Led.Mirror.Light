// Task 4 tests: effect interface, SlideAnimation, snakes (dark/rainbow),
// wave, EffectRegistry. Runs on the `native` PlatformIO environment.
//
// Golden frames are computed OFFLINE from the v27 formulas (see the comment
// above each block naming the v27 function) with a throwaway host C++ port
// using `float` (matching v27's float usage bit-for-bit — a double-precision
// first pass rounded one wave value differently), at full brightness
// (globalScale=255, where v27's scale8(x,255)==x makes the omitted
// brightness step a no-op) — not re-implemented in this file.
#include <unity.h>

#include "Config.h"
#include "Frame.h"
#include "Types.h"
#include "effects/Effect.h"
#include "effects/EffectRegistry.h"
#include "effects/Breathe.h"
#include "effects/Snake.h"
#include "effects/SlideAnimation.h"
#include "effects/Wave.h"

void setUp(void) {}
void tearDown(void) {}

static const Rgbw kSolid{255, 140, 50, 0};
static const Rgbw kMakeup{0, 0, 0, 255};
static const Rgbw kBlack{0, 0, 0, 0};

static Rgbw rgbw(uint8_t r, uint8_t g, uint8_t b, uint8_t w) { return Rgbw{r, g, b, w}; }

// Deterministic RandomFn: always 0 -> startPos_ = 0, direction_ = +1
// (random(2)==0), wave center = CENTERS[0]. Used for reproducible goldens.
static uint32_t zeroRandom(uint32_t /*bound*/) { return 0; }

// --- SlideAnimation (Ruling R3) ---------------------------------------------

static void test_slide_on_takes_97_steps(void) {
    SlideAnimation slide;
    Frame f;
    slide.startOn(9);

    int calls = 0;
    bool more = true;
    while (more) {
        more = slide.step(f, kSolid);
        ++calls;
        TEST_ASSERT_TRUE_MESSAGE(calls <= 200, "slide-on did not finish in time");
    }
    TEST_ASSERT_EQUAL_INT(97, calls);
}

// Runs a slide-on to completion (radius_ == SLIDE_MAX_RADIUS), i.e. the
// state SlideAnimation is in whenever Mirror is ON.
static void completeSlideOn(SlideAnimation& slide, Frame& f, uint16_t center) {
    slide.startOn(center);
    while (slide.step(f, kSolid)) {}
}

static void test_slide_off_takes_98_steps(void) {
    SlideAnimation slide;
    Frame f;
    completeSlideOn(slide, f, 9);
    slide.startOff();  // from ON (radius_ == SLIDE_MAX_RADIUS) -> starts at SLIDE_MAX_RADIUS
    TEST_ASSERT_EQUAL_INT16(97, slide.radius());

    int calls = 0;
    bool more = true;
    while (more) {
        more = slide.step(f, kSolid);
        ++calls;
        TEST_ASSERT_TRUE_MESSAGE(calls <= 200, "slide-off did not finish in time");
    }
    TEST_ASSERT_EQUAL_INT(98, calls);

    // Finished while turning off -> v27 clears the strips.
    for (uint16_t i = 0; i < Frame::kSize; ++i) {
        TEST_ASSERT_TRUE(kBlack == f[i]);
    }
}

static void test_slide_reverse_keeps_radius(void) {
    SlideAnimation slide;
    Frame f;

    // startOff() mid-slide-on keeps the current radius (v27 triggerPowerOff:
    // `if (slideRadius < slideMaxRadius && slideRadius > 0) /* keep */`).
    slide.startOn(9);
    slide.step(f, kSolid);  // radius_: 0 -> 1
    slide.step(f, kSolid);  // radius_: 1 -> 2
    TEST_ASSERT_EQUAL_INT16(2, slide.radius());
    slide.startOff();
    TEST_ASSERT_EQUAL_INT16(2, slide.radius());
    TEST_ASSERT_FALSE(slide.turningOn());

    // reverseToOn() flips direction but keeps radius (Ruling R3: reversible
    // mid slide-out).
    slide.step(f, kSolid);  // radius_: 2 -> 1 (turning off)
    TEST_ASSERT_EQUAL_INT16(1, slide.radius());
    slide.reverseToOn();
    TEST_ASSERT_TRUE(slide.turningOn());
    TEST_ASSERT_EQUAL_INT16(1, slide.radius());
}

// Ruling R17: powering off before the slide-on rendered its first step
// (radius_ still 0) keeps radius 0 instead of jumping to SLIDE_MAX_RADIUS —
// otherwise the whole ring would light up and play a full ~3.2 s slide-out.
// The one remaining step renders radius 0 (dark everywhere) and finishes.
static void test_slide_off_from_radius_0_stays_dark(void) {
    SlideAnimation slide;
    Frame f;
    slide.startOn(9);
    slide.startOff();
    TEST_ASSERT_EQUAL_INT16(0, slide.radius());
    TEST_ASSERT_FALSE(slide.turningOn());

    bool more = slide.step(f, kSolid);
    TEST_ASSERT_FALSE_MESSAGE(more, "slide-off from radius 0 did not finish after one step");
    for (uint16_t i = 0; i < Frame::kSize; ++i) {
        TEST_ASSERT_TRUE(kBlack == f[i]);
    }

    // Already past the end (radius_ < 0 after a finished slide-out):
    // startOff() restarts from SLIDE_MAX_RADIUS, as before.
    TEST_ASSERT_TRUE(slide.radius() < 0);
    slide.startOff();
    TEST_ASSERT_EQUAL_INT16(97, slide.radius());
}

// Golden frames: the v27 processAnimation() MODE_ANIM_ON/MODE_ANIM_OFF formula
// (main.cpp lines 946-1013 @ d4421dd) with the v1.0.1 soft edge SLIDE_EDGE = 11
// (v27 used 9), computed offline for center=9. Frame N below is the output of
// the Nth step() call (renders radius N-1 while turning on).
static void test_slide_golden_frames(void) {
    SlideAnimation slide;
    Frame f;
    slide.startOn(9);

    // Call 1 -> radius 0: edgeFade==0 everywhere (dist<=radius-EDGE never
    // holds at radius 0), so the whole ring is still black.
    slide.step(f, kSolid);
    TEST_ASSERT_TRUE(kBlack == f[9]);
    TEST_ASSERT_TRUE(kBlack == f[0]);

    // Advance to call 10 -> renders radius 9. radius < EDGE: no pixel is at
    // full brightness yet, the centre itself is still on the fading edge.
    for (int i = 0; i < 9; ++i) slide.step(f, kSolid);
    TEST_ASSERT_EQUAL_INT16(10, slide.radius());  // already advanced past the rendered radius
    TEST_ASSERT_TRUE(rgbw(208, 114, 40, 0) == f[9]);  // dist 0 -> edgeFade 9*255/11 = 208
    TEST_ASSERT_TRUE(rgbw(92, 50, 18, 0) == f[4]);    // dist 5 -> edgeFade 4*255/11 = 92
    TEST_ASSERT_TRUE(kBlack == f[0]);                 // dist 9 == radius -> edgeFade 0

    SlideAnimation slideMakeup;
    Frame fm;
    slideMakeup.startOn(9);
    for (int i = 0; i < 10; ++i) slideMakeup.step(fm, kMakeup);  // -> radius 9 rendered on 10th call
    TEST_ASSERT_TRUE(rgbw(0, 0, 0, 92) == fm[4]);  // makeup base, dist 5 -> edgeFade 92

    // Advance a fresh slide to call 50 -> renders radius 49.
    SlideAnimation slide49;
    Frame f49;
    slide49.startOn(9);
    for (int i = 0; i < 50; ++i) slide49.step(f49, kSolid);
    TEST_ASSERT_TRUE(rgbw(255, 140, 50, 0) == f49[47]);   // dist 38 == radius-EDGE -> full
    TEST_ASSERT_TRUE(rgbw(115, 63, 22, 0) == f49[133]);   // dist 44 -> edgeFade 5*255/11 = 115
    TEST_ASSERT_TRUE(kBlack == f49[69]);                  // dist 60 > radius -> black

    // Call 95 -> renders radius 94: the far point (dist 84) is still on the
    // edge (84 > radius-EDGE = 83).
    SlideAnimation slide94;
    Frame f94;
    slide94.startOn(9);
    for (int i = 0; i < 95; ++i) slide94.step(f94, kSolid);
    TEST_ASSERT_TRUE(rgbw(231, 126, 45, 0) == f94[93]);  // dist 84 -> edgeFade 10*255/11 = 231

    // Call 97 (last) -> renders radius 96: max ringDist (84) <= radius-EDGE
    // (85), so the whole ring is lit (-> MODE_ON edge).
    SlideAnimation slide96;
    Frame f96;
    slide96.startOn(9);
    for (int i = 0; i < 97; ++i) slide96.step(f96, kSolid);
    TEST_ASSERT_TRUE(rgbw(255, 140, 50, 0) == f96[0]);
    TEST_ASSERT_TRUE(rgbw(255, 140, 50, 0) == f96[93]);

    // First slide-off frame from a fresh ON (radius == SLIDE_MAX_RADIUS ==
    // 97): max ringDist (84) <= radius-EDGE (86), whole ring lit too.
    SlideAnimation slideOff;
    Frame fOff;
    completeSlideOn(slideOff, fOff, 9);
    slideOff.startOff();
    TEST_ASSERT_EQUAL_INT16(97, slideOff.radius());
    slideOff.step(fOff, kSolid);
    TEST_ASSERT_TRUE(rgbw(255, 140, 50, 0) == fOff[0]);
    TEST_ASSERT_TRUE(rgbw(255, 140, 50, 0) == fOff[93]);
}

// --- Snakes (SnakeBase + DarkSnake, RainbowSnake) ---------------------------

static void test_snake_finishes_after_229_steps(void) {
    DarkSnake snake;
    Frame f;
    EffectContext ctx{kSolid, zeroRandom};
    snake.begin(ctx);

    int calls = 0;
    bool more = true;
    while (more) {
        more = snake.step(f, ctx);
        ++calls;
        TEST_ASSERT_TRUE_MESSAGE(calls <= 400, "snake did not finish in time");
    }
    TEST_ASSERT_EQUAL_INT(229, calls);
}

static void test_dark_snake_head_is_black_at_full_alpha(void) {
    DarkSnake snake;
    Frame f;
    EffectContext ctx{kSolid, zeroRandom};
    snake.begin(ctx);

    // startPos_=0, direction_=+1 -> call N renders step N-1, head = step.
    // Call 101 renders step 100 (alpha==1, since 30 <= 100 <= 198) with
    // head==100 -> dist 0 < SNAKE_HEAD_SOLID -> snakeFactor 0 -> black.
    for (int i = 0; i < 101; ++i) snake.step(f, ctx);
    TEST_ASSERT_TRUE(kBlack == f[100]);
}

static void test_rainbow_snake_blends_with_base(void) {
    RainbowSnake snake;
    Frame f;
    EffectContext ctx{kSolid, zeroRandom};
    snake.begin(ctx);

    // Call 1 renders step 0 -> alpha 0 -> frame == base everywhere.
    snake.step(f, ctx);
    for (uint16_t i = 0; i < Frame::kSize; ++i) {
        TEST_ASSERT_TRUE_MESSAGE(kSolid == f[i], "step 0 (alpha 0) must equal base everywhere");
    }

    // Continue to call 101 (renders step 100, alpha 1); head==100, dist 0 ->
    // hue 0 -> hsv(0) == pure red.
    for (int i = 0; i < 100; ++i) snake.step(f, ctx);
    TEST_ASSERT_TRUE(rgbw(255, 0, 0, 0) == f[100]);
}

// Golden frames vs v27 processAnimation() MODE_EFFECT_SNAKE branch (main.cpp
// lines 1019-1074 @ d4421dd), computed offline for startPos=0, direction=+1
// (zeroRandom), base = solid. Call N renders step N-1.
static void test_dark_snake_golden_frames(void) {
    DarkSnake snake;
    Frame f;
    EffectContext ctx{kSolid, zeroRandom};
    snake.begin(ctx);

    snake.step(f, ctx);  // call 1 -> step 0, alpha 0 -> base everywhere
    TEST_ASSERT_TRUE(kSolid == f[0]);

    DarkSnake s15;
    Frame f15;
    s15.begin(ctx);
    for (int i = 0; i < 16; ++i) s15.step(f15, ctx);  // call 16 -> step 15, alpha 0.5, head 15
    TEST_ASSERT_TRUE(rgbw(127, 70, 25, 0) == f15[15]);

    DarkSnake s100;
    Frame f100;
    s100.begin(ctx);
    for (int i = 0; i < 101; ++i) s100.step(f100, ctx);  // call 101 -> step 100, alpha 1, head 100
    TEST_ASSERT_TRUE(kBlack == f100[100]);  // dist 0 -> solid head -> black
    TEST_ASSERT_TRUE(kSolid == f100[130]);  // dist 138 (head-i+168) >= SNAKE_SIZE -> unaffected base
    TEST_ASSERT_TRUE(kSolid == f100[0]);    // dist 100 >= SNAKE_SIZE -> unaffected base

    DarkSnake s200;
    Frame f200;
    s200.begin(ctx);
    for (int i = 0; i < 201; ++i) s200.step(f200, ctx);  // call 201 -> step 200, alpha 0.9333, head 32 (wrapped)
    TEST_ASSERT_TRUE(rgbw(16, 9, 3, 0) == f200[32]);
}

static void test_rainbow_snake_golden_frames(void) {
    RainbowSnake s100;
    Frame f100;
    EffectContext ctx{kSolid, zeroRandom};
    s100.begin(ctx);
    for (int i = 0; i < 101; ++i) s100.step(f100, ctx);  // call 101 -> step 100, alpha 1, head 100
    TEST_ASSERT_TRUE(rgbw(255, 0, 0, 0) == f100[100]);    // dist 0 -> hue 0 -> hsv(0)
    TEST_ASSERT_TRUE(rgbw(0, 255, 255, 0) == f100[70]);   // dist 30 -> hue 32767 -> hsv(32767)
    TEST_ASSERT_TRUE(kSolid == f100[0]);                  // dist 100 >= SNAKE_SIZE -> base

    RainbowSnake s15;
    Frame f15;
    s15.begin(ctx);
    for (int i = 0; i < 16; ++i) s15.step(f15, ctx);  // call 16 -> step 15, alpha 0.5, head 15
    TEST_ASSERT_TRUE(rgbw(255, 70, 25, 0) == f15[15]);

    RainbowSnake s200;
    Frame f200;
    s200.begin(ctx);
    for (int i = 0; i < 201; ++i) s200.step(f200, ctx);  // call 201 -> step 200, alpha 0.9333, head 32
    TEST_ASSERT_TRUE(rgbw(255, 9, 3, 0) == f200[32]);
}

// --- Wave (dark pulse) ------------------------------------------------------

static float waveDarkFactor(uint16_t i, uint16_t center) {
    // v27 getDarkFactor (main.cpp lines 1169-1175 @ d4421dd), using
    // MirrorCore's own already-tested ringDist(). Only used by the
    // meeting-point test below, to check rule #6 (max, never sum)
    // independently of the golden hardcoded frames.
    uint16_t d = ringDist(i, center);
    if (d <= cfg::WAVE_CORE) return 1.0f;
    if (d <= cfg::WAVE_RADIUS) {
        return 1.0f - static_cast<float>(d - cfg::WAVE_CORE) /
                           static_cast<float>(cfg::WAVE_RADIUS - cfg::WAVE_CORE);
    }
    return 0.0f;
}

static void test_wave_finishes_after_103_steps(void) {
    Wave wave;
    Frame f;
    EffectContext ctx{kSolid, zeroRandom};
    wave.begin(ctx);

    int calls = 0;
    bool more = true;
    while (more) {
        more = wave.step(f, ctx);
        ++calls;
        TEST_ASSERT_TRUE_MESSAGE(calls <= 400, "wave did not finish in time");
    }
    TEST_ASSERT_EQUAL_INT(103, calls);
}

static void test_wave_fades_back_to_base(void) {
    Wave wave;
    Frame f;
    EffectContext ctx{kSolid, zeroRandom};
    wave.begin(ctx);

    bool more = true;
    while (more) {
        more = wave.step(f, ctx);
    }
    // Last rendered frame is step 102 (limit + WAVE_RADIUS): fadeOut == 0.
    for (uint16_t i = 0; i < Frame::kSize; ++i) {
        TEST_ASSERT_TRUE(kSolid == f[i]);
    }
}

// Rule #6: the two waves (CW/CCW) combine with MAX, never a sum, so no pixel
// can be darker than either single wave and no channel can exceed base
// (which a summed/overflowed darkening would violate). Checked by
// recomputing the expected pixel independently in this test from ringDist
// for steps limit-2..limit+2 (center = CENTERS[0] = 9, from the
// deterministic zeroRandom) rather than a single hardcoded value.
static void test_wave_meeting_point_not_darker_than_single_wave(void) {
    EffectContext ctx{kSolid, zeroRandom};
    const uint16_t center = 9;
    const int limit = cfg::TOTAL_LEDS / 2;  // 84

    for (int targetStep = limit - 2; targetStep <= limit + 2; ++targetStep) {
        Wave wave;
        Frame f;
        wave.begin(ctx);
        for (int i = 0; i <= targetStep; ++i) wave.step(f, ctx);  // call targetStep+1 -> step targetStep

        int eff = (targetStep < limit) ? targetStep : limit;
        float fadeOut = 1.0f;
        if (targetStep > limit) {
            fadeOut = 1.0f - static_cast<float>(targetStep - limit) / static_cast<float>(cfg::WAVE_RADIUS);
            if (fadeOut < 0.0f) fadeOut = 0.0f;
        }
        int cw = center + eff;
        while (cw >= static_cast<int>(cfg::TOTAL_LEDS)) cw -= cfg::TOTAL_LEDS;
        int ccw = center - eff;
        while (ccw < 0) ccw += cfg::TOTAL_LEDS;

        for (uint16_t i = 0; i < Frame::kSize; ++i) {
            float dCw = waveDarkFactor(i, static_cast<uint16_t>(cw));
            float dCcw = waveDarkFactor(i, static_cast<uint16_t>(ccw));
            float dark = (dCw > dCcw ? dCw : dCcw) * fadeOut;
            uint8_t bm = static_cast<uint8_t>((1.0f - dark) * 255.0f);
            Rgbw expected = scale(kSolid, bm);
            TEST_ASSERT_TRUE_MESSAGE(expected == f[i], "wave meeting-point pixel mismatch");

            // No channel may exceed base -> proves no double-darkening/wrap.
            TEST_ASSERT_TRUE(f[i].r <= kSolid.r);
            TEST_ASSERT_TRUE(f[i].g <= kSolid.g);
            TEST_ASSERT_TRUE(f[i].b <= kSolid.b);
            TEST_ASSERT_TRUE(f[i].w <= kSolid.w);
        }
    }
}

// Golden frames vs v27 processAnimation() MODE_EFFECT_WAVE branch + v27
// getDarkFactor() (main.cpp lines 1076-1136 and 1169-1175 @ d4421dd),
// computed offline for center=CENTERS[0]=9 (zeroRandom), base = solid.
// Call N renders step N-1.
static void test_wave_golden_frames(void) {
    EffectContext ctx{kSolid, zeroRandom};

    Wave w0;
    Frame f0;
    w0.begin(ctx);
    w0.step(f0, ctx);  // call 1 -> step 0
    TEST_ASSERT_TRUE(kBlack == f0[9]);                  // dist 0 to center -> core -> black
    TEST_ASSERT_TRUE(rgbw(101, 55, 19, 0) == f0[0]);     // dist 9 -> partial darkening
    TEST_ASSERT_TRUE(kSolid == f0[100]);                 // far away -> untouched base

    Wave w50;
    Frame f50;
    w50.begin(ctx);
    for (int i = 0; i < 51; ++i) w50.step(f50, ctx);  // call 51 -> step 50
    TEST_ASSERT_TRUE(kBlack == f50[59]);                 // at CW center -> black
    TEST_ASSERT_TRUE(rgbw(50, 27, 9, 0) == f50[65]);      // near CW center -> partial

    Wave w84;
    Frame f84;
    w84.begin(ctx);
    for (int i = 0; i < 85; ++i) w84.step(f84, ctx);  // call 85 -> step 84 (meeting point)
    TEST_ASSERT_TRUE(kBlack == f84[93]);                 // CW==CCW==93 -> black
    TEST_ASSERT_TRUE(kBlack == f84[90]);                  // still within core of the merged wave
    TEST_ASSERT_TRUE(rgbw(170, 93, 33, 0) == f84[80]);    // partial darkening
}

// --- EffectRegistry ----------------------------------------------------------

// --- Breathe (v1.2.0): whole ring, cosine 100 % -> 60 % -> 100 %, 3 cycles ---

static bool ringUniform(const Frame& f) {
    for (uint16_t i = 1; i < Frame::kSize; ++i) {
        if (!(f[i] == f[0])) return false;
    }
    return true;
}

static void test_breathe_finishes_after_three_cycles(void) {
    Breathe fx;
    Frame f;
    EffectContext ctx{kSolid, zeroRandom};
    fx.begin(ctx);
    TEST_ASSERT_EQUAL_UINT16(cfg::BREATHE_STEP_MS, fx.stepIntervalMs());
    int calls = 0;
    while (fx.step(f, ctx)) {
        ++calls;
        TEST_ASSERT_TRUE_MESSAGE(calls < 1000, "breathe did not finish");
    }
    TEST_ASSERT_EQUAL_INT(cfg::BREATHE_CYCLE_STEPS * cfg::BREATHE_CYCLES, calls + 1);
}

static void test_breathe_starts_full_and_dips_at_half_cycle(void) {
    Breathe fx;
    Frame f;
    EffectContext ctx{kSolid, zeroRandom};
    fx.begin(ctx);
    fx.step(f, ctx);  // renders step 0
    TEST_ASSERT_TRUE(ringUniform(f));
    TEST_ASSERT_TRUE(kSolid == f[0]);
    for (int i = 0; i < cfg::BREATHE_CYCLE_STEPS / 2; ++i) fx.step(f, ctx);  // renders step 100
    TEST_ASSERT_TRUE_MESSAGE(scale(kSolid, cfg::BREATHE_MIN_LEVEL) == f[0], "not at 60 % halfway");
    TEST_ASSERT_TRUE(ringUniform(f));
    for (int i = 0; i < cfg::BREATHE_CYCLE_STEPS / 2; ++i) fx.step(f, ctx);  // renders step 200
    TEST_ASSERT_TRUE_MESSAGE(kSolid == f[0], "did not come back to full at the cycle end");
}

// Monotone down for the first half-cycle, monotone up for the second: the
// curve must be a smooth breath, not a saw or a jump.
static void test_breathe_moves_smoothly_and_never_below_the_floor(void) {
    Breathe fx;
    Frame f;
    EffectContext ctx{kMakeup, zeroRandom};
    fx.begin(ctx);
    const uint8_t floorW = scale8(255, cfg::BREATHE_MIN_LEVEL);
    fx.step(f, ctx);
    uint8_t prev = f[0].w;
    for (int i = 1; i <= cfg::BREATHE_CYCLE_STEPS / 2; ++i) {
        fx.step(f, ctx);
        TEST_ASSERT_TRUE_MESSAGE(f[0].w <= prev, "brightness rose during the first half-cycle");
        prev = f[0].w;
    }
    for (int i = 0; i < cfg::BREATHE_CYCLE_STEPS / 2; ++i) {
        fx.step(f, ctx);
        TEST_ASSERT_TRUE_MESSAGE(f[0].w >= prev, "brightness fell during the second half-cycle");
        prev = f[0].w;
    }
    fx.begin(ctx);
    while (fx.step(f, ctx)) {
        TEST_ASSERT_TRUE_MESSAGE(f[0].w >= floorW, "dipped below BREATHE_MIN_LEVEL");
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, f[0].r, "makeup base must stay on the white channel");
    }
}

static void test_registry_names_roundtrip(void) {
    const EffectId ids[] = {EffectId::Dark, EffectId::Rainbow, EffectId::Wave, EffectId::Breathe};
    const char* expectedNames[] = {"dark", "rainbow", "wave", "breathe"};
    for (int i = 0; i < 4; ++i) {
        const char* name = effectName(ids[i]);
        TEST_ASSERT_NOT_NULL(name);
        TEST_ASSERT_EQUAL_STRING(expectedNames[i], name);
        TEST_ASSERT_TRUE(ids[i] == effectIdFromName(name));
        TEST_ASSERT_NOT_NULL(effectInstance(ids[i]));
    }

    // None: no name, no instance; unknown/wrong-case names resolve to None.
    TEST_ASSERT_NULL(effectName(EffectId::None));
    TEST_ASSERT_NULL(effectInstance(EffectId::None));
    TEST_ASSERT_TRUE(EffectId::None == effectIdFromName("bogus"));
    TEST_ASSERT_TRUE(EffectId::None == effectIdFromName("Dark"));  // case-sensitive

    TEST_ASSERT_EQUAL_STRING("solid", baseModeName(BaseMode::Solid));
    TEST_ASSERT_EQUAL_STRING("makeup", baseModeName(BaseMode::Makeup));
}

// Deterministic RandomFn cycling 0,1,2 (kEffects has exactly 3 entries).
static uint32_t s_cycleIdx = 0;
static uint32_t cyclingRandom(uint32_t bound) {
    uint32_t v = s_cycleIdx % bound;
    ++s_cycleIdx;
    return v;
}

static void test_random_effect_covers_all(void) {
    s_cycleIdx = 0;
    bool sawDark = false, sawRainbow = false, sawWave = false, sawBreathe = false;
    for (int i = 0; i < 4; ++i) {
        EffectId id = randomEffect(cyclingRandom);
        if (id == EffectId::Dark) sawDark = true;
        else if (id == EffectId::Rainbow) sawRainbow = true;
        else if (id == EffectId::Wave) sawWave = true;
        else if (id == EffectId::Breathe) sawBreathe = true;
    }
    TEST_ASSERT_TRUE_MESSAGE(sawDark, "randomEffect never returned Dark");
    TEST_ASSERT_TRUE_MESSAGE(sawRainbow, "randomEffect never returned Rainbow");
    TEST_ASSERT_TRUE_MESSAGE(sawWave, "randomEffect never returned Wave");
    TEST_ASSERT_TRUE_MESSAGE(sawBreathe, "randomEffect never returned Breathe");
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_slide_on_takes_97_steps);
    RUN_TEST(test_slide_off_takes_98_steps);
    RUN_TEST(test_slide_reverse_keeps_radius);
    RUN_TEST(test_slide_off_from_radius_0_stays_dark);
    RUN_TEST(test_slide_golden_frames);
    RUN_TEST(test_snake_finishes_after_229_steps);
    RUN_TEST(test_dark_snake_head_is_black_at_full_alpha);
    RUN_TEST(test_rainbow_snake_blends_with_base);
    RUN_TEST(test_dark_snake_golden_frames);
    RUN_TEST(test_rainbow_snake_golden_frames);
    RUN_TEST(test_wave_finishes_after_103_steps);
    RUN_TEST(test_wave_fades_back_to_base);
    RUN_TEST(test_wave_meeting_point_not_darker_than_single_wave);
    RUN_TEST(test_wave_golden_frames);
    RUN_TEST(test_breathe_finishes_after_three_cycles);
    RUN_TEST(test_breathe_starts_full_and_dips_at_half_cycle);
    RUN_TEST(test_breathe_moves_smoothly_and_never_below_the_floor);
    RUN_TEST(test_registry_names_roundtrip);
    RUN_TEST(test_random_effect_covers_all);
    return UNITY_END();
}

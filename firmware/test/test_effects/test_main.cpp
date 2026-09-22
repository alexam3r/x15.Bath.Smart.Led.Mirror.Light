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
#include "effects/Candle.h"
#include "effects/Embers.h"
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

// --- Embers (v1.2.0): per-pixel smouldering between EMBERS_MIN_LEVEL and
// full, ~20 s with a fade in and out. ---------------------------------------

// Deterministic stand-in for esp_random: an LCG, so targets and indices vary
// but the run is reproducible.
static uint32_t s_lcg = 12345;
static uint32_t lcgRandom(uint32_t bound) {
    s_lcg = s_lcg * 1103515245u + 12345u;
    return (s_lcg >> 16) % bound;
}

static void test_embers_finishes_after_its_steps_and_starts_on_base(void) {
    Embers fx;
    Frame f;
    EffectContext ctx{kSolid, zeroRandom};
    fx.begin(ctx);
    TEST_ASSERT_EQUAL_UINT16(cfg::EMBERS_STEP_MS, fx.stepIntervalMs());
    TEST_ASSERT_TRUE(fx.step(f, ctx));  // step 0: alpha 0 -> plain base
    for (uint16_t i = 0; i < Frame::kSize; ++i) TEST_ASSERT_TRUE(kSolid == f[i]);
    int calls = 1;
    while (fx.step(f, ctx)) {
        ++calls;
        TEST_ASSERT_TRUE_MESSAGE(calls < 1000, "embers did not finish");
    }
    TEST_ASSERT_EQUAL_INT(cfg::EMBERS_STEPS, calls + 1);
}

// zeroRandom always picks pixel 0 and the lowest target, so exactly one pixel
// cools down to EMBERS_MIN_LEVEL and the rest stay on the plain base.
static void test_embers_only_retargeted_pixels_change(void) {
    Embers fx;
    Frame f;
    EffectContext ctx{kSolid, zeroRandom};
    fx.begin(ctx);
    for (int i = 0; i <= cfg::EMBERS_FADE_STEPS; ++i) fx.step(f, ctx);  // past the fade-in
    uint8_t prev = f[0].r;
    for (int i = 0; i < 40; ++i) {
        fx.step(f, ctx);
        TEST_ASSERT_TRUE_MESSAGE(f[0].r <= prev, "pixel 0 did not cool down");
        prev = f[0].r;
        for (uint16_t k = 1; k < Frame::kSize; ++k) {
            TEST_ASSERT_TRUE_MESSAGE(kSolid == f[k], "an untouched pixel left the base colour");
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(scale(kSolid, cfg::EMBERS_MIN_LEVEL) == f[0], "pixel 0 did not reach 40 %");
}

// In makeup the white channel equals the raw level, so the frame shows each
// pixel's level directly: no jumps bigger than EMBERS_SLEW, never below the floor.
static void test_embers_moves_by_at_most_slew_and_stays_in_range(void) {
    s_lcg = 12345;
    Embers fx;
    Frame f;
    EffectContext ctx{kMakeup, lcgRandom};
    fx.begin(ctx);
    for (int i = 0; i <= cfg::EMBERS_FADE_STEPS; ++i) fx.step(f, ctx);  // full alpha from here
    uint8_t prev[Frame::kSize];
    for (uint16_t i = 0; i < Frame::kSize; ++i) prev[i] = f[i].w;
    bool sawChange = false;
    for (int step = 0; step < 200; ++step) {
        fx.step(f, ctx);
        for (uint16_t i = 0; i < Frame::kSize; ++i) {
            const uint8_t now = f[i].w;
            const int delta = static_cast<int>(now) - static_cast<int>(prev[i]);
            TEST_ASSERT_TRUE_MESSAGE(delta <= cfg::EMBERS_SLEW && -delta <= cfg::EMBERS_SLEW,
                                     "a pixel jumped by more than EMBERS_SLEW in one step");
            TEST_ASSERT_TRUE_MESSAGE(now >= scale8(255, cfg::EMBERS_MIN_LEVEL), "pixel dropped below the floor");
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, f[i].r, "makeup base must stay on the white channel");
            if (delta != 0) sawChange = true;
            prev[i] = now;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(sawChange, "nothing smouldered at all");
    // EMBERS_CHANGES_PER_STEP pixels are retargeted every step, so after a few
    // hundred steps practically the whole ring has left full brightness. With
    // only a handful of retargets per step most pixels would still sit at 255.
    uint16_t below = 0;
    for (uint16_t i = 0; i < Frame::kSize; ++i) {
        if (f[i].w < 255) ++below;
    }
    TEST_ASSERT_TRUE_MESSAGE(below >= Frame::kSize - 8, "too much of the ring never smouldered");
}

// --- Candle (v1.2.0): the whole ring trembles at 89..100 %, with rare dips
// towards 80 %. ------------------------------------------------------------

// zeroRandom makes every retarget roll a dip (rnd(CANDLE_DIP_CHANCE) == 0)
// and take the lowest dip target, so the ring settles at CANDLE_DIP_LEVEL.
// noDipRandom never rolls a dip, so it stays in the 89..100 % band.
static uint32_t noDipRandom(uint32_t bound) { return bound == cfg::CANDLE_DIP_CHANCE ? 1 : 0; }

static void test_candle_finishes_after_its_steps_on_base(void) {
    Candle fx;
    Frame f;
    EffectContext ctx{kSolid, zeroRandom};
    fx.begin(ctx);
    TEST_ASSERT_EQUAL_UINT16(cfg::CANDLE_STEP_MS, fx.stepIntervalMs());
    TEST_ASSERT_TRUE(fx.step(f, ctx));
    TEST_ASSERT_TRUE_MESSAGE(kSolid == f[0], "step 0 must render the plain base (alpha 0)");
    int calls = 1;
    while (fx.step(f, ctx)) {
        ++calls;
        TEST_ASSERT_TRUE_MESSAGE(calls < 2000, "candle did not finish");
    }
    TEST_ASSERT_EQUAL_INT(cfg::CANDLE_STEPS, calls + 1);
}

static void test_candle_dips_smoothly_and_uniformly(void) {
    Candle fx;
    Frame f;
    EffectContext ctx{kSolid, zeroRandom};
    fx.begin(ctx);
    for (int i = 0; i <= cfg::CANDLE_FADE_STEPS; ++i) fx.step(f, ctx);  // past the fade-in
    uint8_t prev = f[0].r;
    for (int i = 0; i < 20; ++i) {
        fx.step(f, ctx);
        TEST_ASSERT_TRUE_MESSAGE(ringUniform(f), "the flame must light the whole ring evenly");
        TEST_ASSERT_TRUE_MESSAGE(f[0].r <= prev && prev - f[0].r <= cfg::CANDLE_SLEW, "the dip is not smooth");
        prev = f[0].r;
    }
    TEST_ASSERT_TRUE(scale(kSolid, cfg::CANDLE_DIP_LEVEL) == f[0]);
}

static void test_candle_without_dips_stays_above_the_band_floor(void) {
    Candle fx;
    Frame f;
    EffectContext ctx{kMakeup, noDipRandom};
    fx.begin(ctx);
    const uint8_t floorW = scale8(255, cfg::CANDLE_MIN_LEVEL);
    while (fx.step(f, ctx)) {
        TEST_ASSERT_TRUE_MESSAGE(f[0].w >= floorW, "dipped below CANDLE_MIN_LEVEL without rolling a dip");
        TEST_ASSERT_TRUE(ringUniform(f));
    }
}

// With varied targets the flame still moves by at most CANDLE_SLEW per step
// and never leaves [CANDLE_DIP_LEVEL, 255].
static void test_candle_moves_by_at_most_slew(void) {
    s_lcg = 999;
    Candle fx;
    Frame f;
    EffectContext ctx{kMakeup, lcgRandom};
    fx.begin(ctx);
    for (int i = 0; i <= cfg::CANDLE_FADE_STEPS; ++i) fx.step(f, ctx);
    uint8_t prev = f[0].w;
    bool sawChange = false;
    for (int i = 0; i < 300; ++i) {
        fx.step(f, ctx);
        const int delta = static_cast<int>(f[0].w) - static_cast<int>(prev);
        TEST_ASSERT_TRUE_MESSAGE(delta <= cfg::CANDLE_SLEW && -delta <= cfg::CANDLE_SLEW,
                                 "the flame jumped by more than CANDLE_SLEW");
        TEST_ASSERT_TRUE_MESSAGE(f[0].w >= scale8(255, cfg::CANDLE_DIP_LEVEL), "dropped below the dip floor");
        TEST_ASSERT_TRUE(ringUniform(f));
        if (delta != 0) sawChange = true;
        prev = f[0].w;
    }
    TEST_ASSERT_TRUE_MESSAGE(sawChange, "the flame never flickered");
}

// Counts how often an effect asks for randomness, to pin the retarget cadence.
static uint32_t s_randomCalls = 0;
static uint32_t countingRandom(uint32_t bound) {
    ++s_randomCalls;
    return bound == cfg::CANDLE_DIP_CHANCE ? 1 : 0;  // never a dip
}

// The flame picks a new target every CANDLE_RETARGET_STEPS steps, not every
// step: two rolls (dip chance + level) per retarget and none in between.
static void test_candle_retargets_every_third_step(void) {
    Candle fx;
    Frame f;
    EffectContext ctx{kSolid, countingRandom};
    fx.begin(ctx);
    s_randomCalls = 0;
    const int steps = 30;
    for (int i = 0; i < steps; ++i) fx.step(f, ctx);
    TEST_ASSERT_EQUAL_UINT32(2 * (steps / cfg::CANDLE_RETARGET_STEPS), s_randomCalls);
}

// --- Shared effect helpers (Effect.h, v1.2.0) -------------------------------

static void test_fade_alpha_ramps_in_and_out(void) {
    TEST_ASSERT_EQUAL_UINT8(0, fadeAlpha(0, 100, 10));
    TEST_ASSERT_EQUAL_UINT8(127, fadeAlpha(5, 100, 10));
    TEST_ASSERT_EQUAL_UINT8(255, fadeAlpha(10, 100, 10));
    TEST_ASSERT_EQUAL_UINT8(255, fadeAlpha(90, 100, 10));
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(127, fadeAlpha(95, 100, 10), "no fade-out towards the end");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, fadeAlpha(100, 100, 10), "the last frame must be the plain base");
}

static void test_slew_towards_caps_the_step(void) {
    TEST_ASSERT_EQUAL_UINT8(108, slewTowards(100, 200, 8));
    TEST_ASSERT_EQUAL_UINT8(92, slewTowards(100, 50, 8));
    TEST_ASSERT_EQUAL_UINT8(100, slewTowards(100, 100, 8));
    TEST_ASSERT_EQUAL_UINT8(103, slewTowards(100, 103, 8));  // gap smaller than the cap
    TEST_ASSERT_EQUAL_UINT8(98, slewTowards(100, 98, 8));
}

static void test_registry_names_roundtrip(void) {
    const EffectId ids[] = {EffectId::Dark, EffectId::Rainbow, EffectId::Wave, EffectId::Breathe,
                            EffectId::Embers, EffectId::Candle};
    const char* expectedNames[] = {"dark", "rainbow", "wave", "breathe", "embers", "candle"};
    for (int i = 0; i < 6; ++i) {
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
    bool sawDark = false, sawRainbow = false, sawWave = false, sawBreathe = false, sawEmbers = false,
         sawCandle = false;
    for (int i = 0; i < 6; ++i) {
        EffectId id = randomEffect(cyclingRandom);
        if (id == EffectId::Dark) sawDark = true;
        else if (id == EffectId::Rainbow) sawRainbow = true;
        else if (id == EffectId::Wave) sawWave = true;
        else if (id == EffectId::Breathe) sawBreathe = true;
        else if (id == EffectId::Embers) sawEmbers = true;
        else if (id == EffectId::Candle) sawCandle = true;
    }
    TEST_ASSERT_TRUE_MESSAGE(sawDark, "randomEffect never returned Dark");
    TEST_ASSERT_TRUE_MESSAGE(sawRainbow, "randomEffect never returned Rainbow");
    TEST_ASSERT_TRUE_MESSAGE(sawWave, "randomEffect never returned Wave");
    TEST_ASSERT_TRUE_MESSAGE(sawBreathe, "randomEffect never returned Breathe");
    TEST_ASSERT_TRUE_MESSAGE(sawEmbers, "randomEffect never returned Embers");
    TEST_ASSERT_TRUE_MESSAGE(sawCandle, "randomEffect never returned Candle");
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
    RUN_TEST(test_embers_finishes_after_its_steps_and_starts_on_base);
    RUN_TEST(test_embers_only_retargeted_pixels_change);
    RUN_TEST(test_embers_moves_by_at_most_slew_and_stays_in_range);
    RUN_TEST(test_candle_finishes_after_its_steps_on_base);
    RUN_TEST(test_candle_dips_smoothly_and_uniformly);
    RUN_TEST(test_candle_without_dips_stays_above_the_band_floor);
    RUN_TEST(test_candle_moves_by_at_most_slew);
    RUN_TEST(test_candle_retargets_every_third_step);
    RUN_TEST(test_fade_alpha_ramps_in_and_out);
    RUN_TEST(test_slew_towards_caps_the_step);
    RUN_TEST(test_registry_names_roundtrip);
    RUN_TEST(test_random_effect_covers_all);
    return UNITY_END();
}

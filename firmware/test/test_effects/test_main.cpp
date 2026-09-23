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
#include "effects/Comet.h"
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
// The lit arc grows from the centre in both directions, one pixel per step,
// with a soft edge of SLIDE_EDGE - 1 lit pixels on each front. Since v1.3.2
// the edge is 16 steps (15 lit pixels; v1.0.1: 11/10) in even perceived
// steps (CIE 1931) — before it was linear in PWM, the v27 look.

// The slide's multiplier for a pixel `back` pixels inside the front
// (radius - dist): 0 at the front, full from SLIDE_EDGE inwards.
static Rgbw slidePixel(Rgbw base, int back) {
    if (back <= 0) return kBlack;
    if (back >= static_cast<int>(cfg::SLIDE_EDGE)) return base;
    return scale(base, cie8(static_cast<uint8_t>(back * 255 / cfg::SLIDE_EDGE)));
}

static void test_slide_edge_is_15_pixels(void) {
    TEST_ASSERT_EQUAL_UINT16(16, cfg::SLIDE_EDGE);
    TEST_ASSERT_EQUAL_UINT16(cfg::TOTAL_LEDS / 2 + cfg::SLIDE_EDGE + 2, cfg::SLIDE_MAX_RADIUS);  // 102
}

static void test_slide_on_takes_max_radius_steps(void) {
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
    TEST_ASSERT_EQUAL_INT(cfg::SLIDE_MAX_RADIUS, calls);
}

// Runs a slide-on to completion (radius_ == SLIDE_MAX_RADIUS), i.e. the
// state SlideAnimation is in whenever Mirror is ON.
static void completeSlideOn(SlideAnimation& slide, Frame& f, uint16_t center) {
    slide.startOn(center);
    while (slide.step(f, kSolid)) {}
}

static void test_slide_off_takes_max_radius_plus_one_steps(void) {
    SlideAnimation slide;
    Frame f;
    completeSlideOn(slide, f, 9);
    slide.startOff();  // from ON (radius_ == SLIDE_MAX_RADIUS) -> starts at SLIDE_MAX_RADIUS
    TEST_ASSERT_EQUAL_INT16(cfg::SLIDE_MAX_RADIUS, slide.radius());

    int calls = 0;
    bool more = true;
    while (more) {
        more = slide.step(f, kSolid);
        ++calls;
        TEST_ASSERT_TRUE_MESSAGE(calls <= 200, "slide-off did not finish in time");
    }
    TEST_ASSERT_EQUAL_INT(cfg::SLIDE_MAX_RADIUS + 1, calls);

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
// otherwise the whole ring would light up and play a full slide-out.
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
    TEST_ASSERT_EQUAL_INT16(cfg::SLIDE_MAX_RADIUS, slide.radius());
}

// Frames of a slide-on from centre 9: frame N is the output of the Nth
// step() call and renders radius N - 1.
static void test_slide_frames_follow_the_perceptual_edge(void) {
    // Call 1 -> radius 0: nothing lit yet.
    SlideAnimation slide;
    Frame f;
    slide.startOn(9);
    slide.step(f, kSolid);
    for (uint16_t i = 0; i < Frame::kSize; ++i) TEST_ASSERT_TRUE(kBlack == f[i]);

    // Call 51 -> radius 50: both fronts at distance 50 from the centre, each
    // with its soft edge; full inside, black outside.
    SlideAnimation s50;
    Frame f50;
    s50.startOn(9);
    for (int i = 0; i < 51; ++i) s50.step(f50, kSolid);
    for (uint16_t i = 0; i < Frame::kSize; ++i) {
        TEST_ASSERT_TRUE_MESSAGE(slidePixel(kSolid, 50 - ringDist(i, 9)) == f50[i], "frame differs from the edge formula");
    }
    int lit = 0;  // partially lit pixels on the clockwise front
    for (int d = 0; d <= 60; ++d) {
        const Rgbw p = f50[(9 + d) % cfg::TOTAL_LEDS];
        if (!(p == kBlack) && !(p == kSolid)) ++lit;
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(15, lit, "the soft edge must be 15 pixels");

    // Last slide-on frame and the first slide-off frame: the whole ring lit.
    SlideAnimation sOn;
    Frame fOn;
    completeSlideOn(sOn, fOn, 9);
    for (uint16_t i = 0; i < Frame::kSize; ++i) TEST_ASSERT_TRUE(kSolid == fOn[i]);
    sOn.startOff();
    sOn.step(fOn, kSolid);
    for (uint16_t i = 0; i < Frame::kSize; ++i) TEST_ASSERT_TRUE(kSolid == fOn[i]);
}

// The edge must look even: equal perceived steps from the front inwards, not
// the v27 linear-PWM edge whose dark half the eye barely saw.
static void test_slide_edge_is_even_to_the_eye(void) {
    SlideAnimation slide;
    Frame f;
    slide.startOn(9);
    for (int i = 0; i < 51; ++i) slide.step(f, kMakeup);  // radius 50; w channel = multiplier
    int prev = -1, prevStep = -1;
    for (int back = 1; back < static_cast<int>(cfg::SLIDE_EDGE); ++back) {
        const int l = lightness8(f[(9 + 50 - back) % cfg::TOTAL_LEDS].w);
        if (prev >= 0) {
            const int step = l - prev;
            TEST_ASSERT_TRUE_MESSAGE(step > 0, "the edge must brighten inwards");
            if (prevStep >= 0) TEST_ASSERT_INT_WITHIN_MESSAGE(4, prevStep, step, "uneven perceived steps");
            prevStep = step;
        }
        prev = l;
    }
    TEST_ASSERT_TRUE_MESSAGE(f[(9 + 49) % cfg::TOTAL_LEDS].w <= 3, "the outermost edge pixel must be barely lit");
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
// Since v1.3.0 the curve is in perceived brightness (CIE 1931): the floor is
// 60 % to the eye, not 60 % PWM (which looked like 82 %).

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
    TEST_ASSERT_TRUE_MESSAGE(scale(kSolid, cie8(cfg::BREATHE_MIN_LEVEL)) == f[0], "not at 60 % (to the eye) halfway");
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
    const uint8_t floorW = cie8(cfg::BREATHE_MIN_LEVEL);
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

// --- Embers (v1.3.0; deeper with soft edges in v1.3.1): sparse coals --------
// About one spot per twelve pixels at any moment: its centre dims to 10..30 %
// (to the eye) and back along a cosine over 1..2.5 s, with a soft edge of
// EMBERS_EDGE pixels each side (2/3, 1/3 of the depth), every coal on its own
// clock. Levels are perceived brightness (CIE 1931); in makeup the white
// channel is the multiplier itself, so lightness8(f[i].w) reads a pixel's
// perceived level directly.

// Deterministic stand-in for esp_random.
static uint32_t s_lcg = 12345;
static uint32_t lcgRandom(uint32_t bound) {
    s_lcg = s_lcg * 1103515245u + 12345u;
    return (s_lcg >> 16) % bound;
}

// Exactly one coal, at pixel 50, as deep and as short as allowed.
static uint32_t s_spawnRolls = 0;
static uint32_t oneCoalRandom(uint32_t bound) {
    if (bound == 1000) return (s_spawnRolls++ == 0) ? 0 : 999;  // spawn once, never again
    if (bound == cfg::TOTAL_LEDS) return 50;
    return 0;  // deepest floor, shortest dip
}

static void test_embers_starts_and_ends_on_the_plain_base(void) {
    s_lcg = 12345;
    Embers fx;
    Frame f;
    EffectContext ctx{kSolid, lcgRandom};
    fx.begin(ctx);
    TEST_ASSERT_EQUAL_UINT16(cfg::EMBERS_STEP_MS, fx.stepIntervalMs());
    TEST_ASSERT_TRUE(fx.step(f, ctx));
    for (uint16_t i = 0; i < Frame::kSize; ++i) TEST_ASSERT_TRUE_MESSAGE(kSolid == f[i], "not base at the start");
    int calls = 1;
    while (fx.step(f, ctx)) {
        ++calls;
        TEST_ASSERT_TRUE_MESSAGE(calls < 2000, "embers did not finish");
    }
    TEST_ASSERT_EQUAL_INT(cfg::EMBERS_STEPS, calls + 1);
    for (uint16_t i = 0; i < Frame::kSize; ++i) {
        TEST_ASSERT_TRUE_MESSAGE(kSolid == f[i], "a coal was still dimmed on the last frame");
    }
}

static void test_embers_coal_dips_to_10_percent_with_soft_edges(void) {
    s_spawnRolls = 0;
    Embers fx;
    Frame f;
    EffectContext ctx{kMakeup, oneCoalRandom};
    fx.begin(ctx);
    int minAt[2 * cfg::EMBERS_EDGE + 1];
    for (int& m : minAt) m = 255;
    bool rising = false;
    int prevCentre = 255;
    while (fx.step(f, ctx)) {
        const int centre = lightness8(f[50].w);
        if (centre > prevCentre) rising = true;
        TEST_ASSERT_TRUE_MESSAGE(!(rising && centre < prevCentre), "the dip went down twice");
        prevCentre = centre;
        for (int k = -cfg::EMBERS_EDGE; k <= cfg::EMBERS_EDGE; ++k) {
            const int l = lightness8(f[50 + k].w);
            if (l < minAt[k + cfg::EMBERS_EDGE]) minAt[k + cfg::EMBERS_EDGE] = l;
        }
        for (uint16_t i = 0; i < Frame::kSize; ++i) {
            if (i < 50 - cfg::EMBERS_EDGE || i > 50 + cfg::EMBERS_EDGE) {
                TEST_ASSERT_TRUE_MESSAGE(kMakeup == f[i], "only the coal and its edge dim");
            }
        }
    }
    TEST_ASSERT_EQUAL_UINT8(26, cfg::EMBERS_FLOOR_MIN);  // 10 % to the eye
    const int depth = 255 - cfg::EMBERS_FLOOR_MIN;
    TEST_ASSERT_INT_WITHIN_MESSAGE(3, cfg::EMBERS_FLOOR_MIN, minAt[cfg::EMBERS_EDGE], "the coal did not reach 10 %");
    // Soft edge: even perceived steps from the centre out to the base.
    for (int k = 1; k <= cfg::EMBERS_EDGE; ++k) {
        const int want = 255 - depth * (cfg::EMBERS_EDGE + 1 - k) / (cfg::EMBERS_EDGE + 1);
        TEST_ASSERT_INT_WITHIN_MESSAGE(4, want, minAt[cfg::EMBERS_EDGE + k], "uneven soft edge (right)");
        TEST_ASSERT_INT_WITHIN_MESSAGE(4, want, minAt[cfg::EMBERS_EDGE - k], "uneven soft edge (left)");
    }
    TEST_ASSERT_TRUE(kMakeup == f[50]);  // back to base at the end
}

// Not the whole ring and not a handful: roughly one coal per eight pixels,
// each at its own phase.
static void test_embers_are_sparse_and_staggered(void) {
    s_lcg = 777;
    Embers fx;
    Frame f;
    EffectContext ctx{kMakeup, lcgRandom};
    fx.begin(ctx);
    for (int i = 0; i < 150; ++i) fx.step(f, ctx);  // past the ramp-up
    long dimmedTotal = 0;
    int samples = 0, maxDistinct = 0;
    for (int step = 0; step < 300; ++step) {
        fx.step(f, ctx);
        bool seen[256] = {false};
        int dimmed = 0, distinct = 0;
        for (uint16_t i = 0; i < Frame::kSize; ++i) {
            const uint8_t l = lightness8(f[i].w);
            if (l < 255) {
                ++dimmed;
                if (!seen[l]) { seen[l] = true; ++distinct; }
            }
        }
        TEST_ASSERT_TRUE_MESSAGE(dimmed <= (2 * cfg::EMBERS_EDGE + 1) * cfg::EMBERS_MAX_COALS,
                                 "more pixels dimmed than coals allow");
        dimmedTotal += dimmed;
        ++samples;
        if (distinct > maxDistinct) maxDistinct = distinct;
    }
    const long average = dimmedTotal / samples;
    TEST_ASSERT_TRUE_MESSAGE(average >= 20, "too few coals: the effect is barely visible");
    TEST_ASSERT_TRUE_MESSAGE(average <= 90, "too many coals: the whole ring dims");
    TEST_ASSERT_TRUE_MESSAGE(maxDistinct >= 10, "the coals dim in step instead of on their own clocks");
}

static void test_embers_move_smoothly_and_never_below_10_percent(void) {
    s_lcg = 4242;
    Embers fx;
    Frame f;
    EffectContext ctx{kMakeup, lcgRandom};
    fx.begin(ctx);
    // The steepest a cosine dip can move per step, plus rounding.
    const int maxStep = static_cast<int>(3.1416f * (255 - cfg::EMBERS_FLOOR_MIN) / cfg::EMBERS_DIP_MIN_STEPS) + 2;
    uint8_t prev[Frame::kSize];
    for (uint16_t i = 0; i < Frame::kSize; ++i) prev[i] = 255;
    while (fx.step(f, ctx)) {
        for (uint16_t i = 0; i < Frame::kSize; ++i) {
            const int l = lightness8(f[i].w);
            // Compared in PWM: at 10 % to the eye a PWM step spans several
            // lightness units, so a round trip through lightness8 undershoots.
            TEST_ASSERT_TRUE_MESSAGE(f[i].w >= cie8(cfg::EMBERS_FLOOR_MIN), "dipped below 10 %");
            TEST_ASSERT_INT_WITHIN_MESSAGE(maxStep, prev[i], l, "a coal jumped instead of fading");
            TEST_ASSERT_EQUAL_UINT8(0, f[i].r);
            prev[i] = static_cast<uint8_t>(l);
        }
    }
}

// --- Comet (v1.2.0): a white head with a fading tail flies one lap ----------
// Since v1.3.0 the tail fades evenly to the eye (linear in perceived
// brightness, CIE 1931) instead of the hand-made quadratic PWM curve.

static const Rgbw kWhite{255, 255, 255, 255};

static void test_comet_finishes_after_one_lap_plus_tail(void) {
    Comet fx;
    Frame f;
    EffectContext ctx{kSolid, zeroRandom};  // start at 0, direction +1
    fx.begin(ctx);
    TEST_ASSERT_EQUAL_UINT16(cfg::COMET_STEP_MS, fx.stepIntervalMs());
    TEST_ASSERT_TRUE(fx.step(f, ctx));
    for (uint16_t i = 0; i < Frame::kSize; ++i) {
        TEST_ASSERT_TRUE_MESSAGE(kSolid == f[i], "step 0 must render the plain base (alpha 0)");
    }
    int calls = 1;
    while (fx.step(f, ctx)) {
        ++calls;
        TEST_ASSERT_TRUE_MESSAGE(calls < 1000, "comet did not finish");
    }
    TEST_ASSERT_EQUAL_INT(cfg::TOTAL_LEDS + cfg::COMET_TAIL + 1, calls + 1);  // one lap plus the tail
}

static void test_comet_head_is_white_with_a_tail_even_to_the_eye(void) {
    Comet fx;
    Frame f;
    EffectContext ctx{kSolid, zeroRandom};
    fx.begin(ctx);
    for (int i = 0; i <= 100; ++i) fx.step(f, ctx);  // renders step 100: head at pixel 100, full alpha
    TEST_ASSERT_TRUE_MESSAGE(kWhite == f[100], "the head must be white");
    // 20 LEDs behind the head: halfway to the eye, t = 255 * (40-20) / 40
    TEST_ASSERT_TRUE(lerpPerceptual(kSolid, kWhite, 127) == f[80]);
    // 10 LEDs behind: t = 255 * 30 / 40 — brighter than further back
    TEST_ASSERT_TRUE(lerpPerceptual(kSolid, kWhite, 191) == f[90]);
    TEST_ASSERT_TRUE_MESSAGE(kSolid == f[60], "the tail must end after COMET_TAIL pixels");
    TEST_ASSERT_TRUE_MESSAGE(kSolid == f[101], "nothing may light up ahead of the head");
}

// The tail must fall off monotonically, and the whole comet must be a single
// contiguous run behind the head (it wraps around the ring end).
static void test_comet_tail_falls_off_monotonically_and_wraps(void) {
    Comet fx;
    Frame f;
    EffectContext ctx{kSolid, zeroRandom};
    fx.begin(ctx);
    for (int i = 0; i <= 10; ++i) fx.step(f, ctx);  // head at 10, tail wraps past pixel 0
    uint8_t prev = 255;
    for (int back = 0; back < static_cast<int>(cfg::COMET_TAIL); ++back) {
        const uint16_t idx = static_cast<uint16_t>((10 - back + cfg::TOTAL_LEDS) % cfg::TOTAL_LEDS);
        const uint8_t v = f[idx].w;
        TEST_ASSERT_TRUE_MESSAGE(v <= prev, "the tail got brighter further from the head");
        prev = v;
    }
    // Everything further back than COMET_TAIL is plain base — the comet is one
    // contiguous run of exactly COMET_TAIL pixels, nothing smeared elsewhere.
    for (int back = cfg::COMET_TAIL; back < static_cast<int>(cfg::TOTAL_LEDS); ++back) {
        const uint16_t idx = static_cast<uint16_t>((10 - back + 2 * cfg::TOTAL_LEDS) % cfg::TOTAL_LEDS);
        TEST_ASSERT_TRUE_MESSAGE(kSolid == f[idx], "a pixel outside the tail is not plain base");
    }
}

static void test_comet_adds_light_in_makeup(void) {
    Comet fx;
    Frame f;
    EffectContext ctx{kMakeup, zeroRandom};
    fx.begin(ctx);
    for (int i = 0; i <= 100; ++i) fx.step(f, ctx);
    TEST_ASSERT_TRUE(kWhite == f[100]);
    // RGB rises on top of the white channel instead of replacing it.
    TEST_ASSERT_TRUE(f[90].r > 0 && f[90].w == 255);
    TEST_ASSERT_TRUE(kMakeup == f[60]);
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

// A quarter of the way into a breath the cosine is at half its swing: the
// eye must see the midpoint between full and the floor.
static void test_breathe_curve_is_even_to_the_eye(void) {
    Breathe fx;
    Frame f;
    EffectContext ctx{kMakeup, zeroRandom};
    fx.begin(ctx);
    for (int i = 0; i <= cfg::BREATHE_CYCLE_STEPS / 4; ++i) fx.step(f, ctx);  // renders step 50
    const int midpoint = cfg::BREATHE_MIN_LEVEL + (255 - cfg::BREATHE_MIN_LEVEL) / 2;
    TEST_ASSERT_INT_WITHIN_MESSAGE(2, midpoint, lightness8(f[0].w), "the breath is not even to the eye");
}

static void test_registry_names_roundtrip(void) {
    const EffectId ids[] = {EffectId::Dark, EffectId::Rainbow, EffectId::Wave, EffectId::Breathe,
                            EffectId::Embers, EffectId::Comet};
    const char* expectedNames[] = {"dark", "rainbow", "wave", "breathe", "embers", "comet"};
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
         sawComet = false;
    for (int i = 0; i < 6; ++i) {
        EffectId id = randomEffect(cyclingRandom);
        if (id == EffectId::Dark) sawDark = true;
        else if (id == EffectId::Rainbow) sawRainbow = true;
        else if (id == EffectId::Wave) sawWave = true;
        else if (id == EffectId::Breathe) sawBreathe = true;
        else if (id == EffectId::Embers) sawEmbers = true;
        else if (id == EffectId::Comet) sawComet = true;
    }
    TEST_ASSERT_TRUE_MESSAGE(sawDark, "randomEffect never returned Dark");
    TEST_ASSERT_TRUE_MESSAGE(sawRainbow, "randomEffect never returned Rainbow");
    TEST_ASSERT_TRUE_MESSAGE(sawWave, "randomEffect never returned Wave");
    TEST_ASSERT_TRUE_MESSAGE(sawBreathe, "randomEffect never returned Breathe");
    TEST_ASSERT_TRUE_MESSAGE(sawEmbers, "randomEffect never returned Embers");
    TEST_ASSERT_TRUE_MESSAGE(sawComet, "randomEffect never returned Comet");
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_slide_edge_is_15_pixels);
    RUN_TEST(test_slide_on_takes_max_radius_steps);
    RUN_TEST(test_slide_off_takes_max_radius_plus_one_steps);
    RUN_TEST(test_slide_reverse_keeps_radius);
    RUN_TEST(test_slide_off_from_radius_0_stays_dark);
    RUN_TEST(test_slide_frames_follow_the_perceptual_edge);
    RUN_TEST(test_slide_edge_is_even_to_the_eye);
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
    RUN_TEST(test_breathe_curve_is_even_to_the_eye);
    RUN_TEST(test_embers_starts_and_ends_on_the_plain_base);
    RUN_TEST(test_embers_coal_dips_to_10_percent_with_soft_edges);
    RUN_TEST(test_embers_are_sparse_and_staggered);
    RUN_TEST(test_embers_move_smoothly_and_never_below_10_percent);
    RUN_TEST(test_fade_alpha_ramps_in_and_out);
    RUN_TEST(test_comet_finishes_after_one_lap_plus_tail);
    RUN_TEST(test_comet_head_is_white_with_a_tail_even_to_the_eye);
    RUN_TEST(test_comet_tail_falls_off_monotonically_and_wraps);
    RUN_TEST(test_comet_adds_light_in_makeup);
    RUN_TEST(test_registry_names_roundtrip);
    RUN_TEST(test_random_effect_covers_all);
    return UNITY_END();
}

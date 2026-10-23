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
#include "effects/Comet.h"
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

// Deterministic stand-in for esp_random.
static uint32_t s_lcg = 12345;
static uint32_t lcgRandom(uint32_t bound) {
    s_lcg = s_lcg * 1103515245u + 12345u;
    return (s_lcg >> 16) % bound;
}

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

// --- Rainbow colour order (v1.4.2) --------------------------------------------
// Every start shuffles the six key colours (red, yellow, green, cyan, blue,
// magenta), 10 pixels apart along the snake; between two keys the colour
// runs the short way round the wheel, and the tail runs back to the head
// colour. With a zero RandomFn the order stays the v27 one (goldens above).

// Key colour c (0 red .. 5 magenta) and any point of the 60-unit wheel.
static Rgbw wheelColour(int wheel) {
    return hsv(static_cast<uint16_t>(((wheel % 60 + 60) % 60) * 65535L / 60));
}
static Rgbw keyColour(int c) { return wheelColour(10 * c); }

// Queued values in order, then 0.
static uint32_t s_script[8];
static int s_scriptLen = 0, s_scriptPos = 0;
static uint32_t scriptedRandom(uint32_t bound) {
    const uint32_t v = s_scriptPos < s_scriptLen ? s_script[s_scriptPos++] : 0;
    TEST_ASSERT_TRUE_MESSAGE(v < bound, "scripted value out of range");
    return v;
}

// Start 0 and direction +1 (the first two draws of begin), the shuffle from
// the LCG.
static int s_draws = 0;
static uint32_t shuffleOnlyRandom(uint32_t bound) { return (s_draws++ < 2) ? 0 : lcgRandom(bound); }

// Renders the snake at step 100: alpha 1, head at pixel 100, so the pixel
// `d` behind the head is f[100 - d].
static void rainbowAtFullAlpha(RandomFn random, Frame& f) {
    RainbowSnake snake;
    EffectContext ctx{kSolid, random};
    snake.begin(ctx);
    for (int i = 0; i < 101; ++i) snake.step(f, ctx);
}

static void test_rainbow_snake_shuffles_its_six_colours(void) {
    // Start 0, direction +1, then the shuffle draws 5, 3, 1, 0, 1: blue,
    // magenta, cyan, green, yellow, red from the head.
    const uint32_t script[] = {0, 0, 5, 3, 1, 0, 1};
    for (int i = 0; i < 7; ++i) s_script[i] = script[i];
    s_scriptLen = 7;
    s_scriptPos = 0;
    Frame f;
    rainbowAtFullAlpha(scriptedRandom, f);
    const int want[6] = {4, 5, 3, 2, 1, 0};
    for (int k = 0; k < 6; ++k) {
        TEST_ASSERT_TRUE_MESSAGE(keyColour(want[k]) == f[100 - 10 * k], "a key colour is not where the shuffle put it");
    }
    // blue (40) -> magenta (50), neighbours: one wheel unit per pixel.
    TEST_ASSERT_TRUE(wheelColour(45) == f[100 - 5]);
    // magenta (50) -> cyan (30) the short way, through blue (40).
    TEST_ASSERT_TRUE_MESSAGE(wheelColour(40) == f[100 - 15], "not the short way round the wheel");
    // The tail runs back to the head colour: red (0) -> blue (40) through magenta.
    TEST_ASSERT_TRUE_MESSAGE(wheelColour(50) == f[100 - 55], "the tail does not run back to the head colour");
}

// Every start draws a fresh order: over 200 starts most orders differ and
// every colour leads the snake now and then.
static void test_rainbow_snake_order_changes_every_start(void) {
    s_lcg = 2024;
    static bool seen[6 * 6 * 6 * 6 * 6 * 6];
    for (bool& b : seen) b = false;
    int distinct = 0;
    int leads[6] = {0};
    for (int run = 0; run < 200; ++run) {
        s_draws = 0;
        Frame f;
        rainbowAtFullAlpha(shuffleOnlyRandom, f);
        int code = 0;
        bool used[6] = {false};
        for (int k = 0; k < 6; ++k) {
            int c = -1;
            for (int t = 0; t < 6; ++t) {
                if (keyColour(t) == f[100 - 10 * k]) c = t;
            }
            TEST_ASSERT_TRUE_MESSAGE(c >= 0, "a key pixel is not one of the six colours");
            TEST_ASSERT_FALSE_MESSAGE(used[c], "a colour appears twice");
            used[c] = true;
            if (k == 0) ++leads[c];
            code = code * 6 + c;
        }
        if (!seen[code]) {
            seen[code] = true;
            ++distinct;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(distinct >= 150, "the order barely changes between starts");
    for (int c = 0; c < 6; ++c) {
        TEST_ASSERT_TRUE_MESSAGE(leads[c] >= 15, "a colour (almost) never leads the snake");  // ~33 each
    }
}

// Saturated colours all along the snake and no jumps: neighbours are at most
// three wheel units (~77 per channel) apart.
static void test_rainbow_snake_is_smooth_and_saturated(void) {
    s_lcg = 99;
    for (int run = 0; run < 50; ++run) {
        s_draws = 0;
        Frame f;
        rainbowAtFullAlpha(shuffleOnlyRandom, f);
        for (int d = 0; d < static_cast<int>(cfg::SNAKE_SIZE); ++d) {
            const Rgbw p = f[100 - d];
            const uint8_t hi = p.r > p.g ? (p.r > p.b ? p.r : p.b) : (p.g > p.b ? p.g : p.b);
            const uint8_t lo = p.r < p.g ? (p.r < p.b ? p.r : p.b) : (p.g < p.b ? p.g : p.b);
            TEST_ASSERT_TRUE_MESSAGE(hi == 255 && lo == 0 && p.w == 0, "a washed-out colour on the snake");
            if (d == 0) continue;
            const Rgbw q = f[100 - d + 1];
            TEST_ASSERT_INT_WITHIN_MESSAGE(80, q.r, p.r, "the colour jumps between neighbours");
            TEST_ASSERT_INT_WITHIN_MESSAGE(80, q.g, p.g, "the colour jumps between neighbours");
            TEST_ASSERT_INT_WITHIN_MESSAGE(80, q.b, p.b, "the colour jumps between neighbours");
        }
    }
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

// --- Comet (v1.6.0): a yellow head with a fading tail flies one lap ---------
// Since v1.3.0 the tail fades evenly to the eye (linear in perceived
// brightness, CIE 1931) instead of the hand-made quadratic PWM curve.

static const Rgbw kYellow{255, 255, 0, 0};

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

static void test_comet_head_is_yellow_with_a_tail_even_to_the_eye(void) {
    Comet fx;
    Frame f;
    EffectContext ctx{kSolid, zeroRandom};
    fx.begin(ctx);
    for (int i = 0; i <= 100; ++i) fx.step(f, ctx);  // renders step 100: head at pixel 100, full alpha
    TEST_ASSERT_TRUE_MESSAGE(kYellow == f[100], "the head must be saturated yellow");
    // 20 LEDs behind the head: halfway to the eye, t = 255 * (40-20) / 40
    TEST_ASSERT_TRUE(lerpPerceptual(kSolid, kYellow, 127) == f[80]);
    // 10 LEDs behind: t = 255 * 30 / 40 — brighter than further back
    TEST_ASSERT_TRUE(lerpPerceptual(kSolid, kYellow, 191) == f[90]);
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
        const uint8_t v = f[idx].g;
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

static void test_comet_blends_yellow_tail_into_makeup(void) {
    Comet fx;
    Frame f;
    EffectContext ctx{kMakeup, zeroRandom};
    fx.begin(ctx);
    for (int i = 0; i <= 100; ++i) fx.step(f, ctx);
    TEST_ASSERT_TRUE(kYellow == f[100]);
    // Both head and tail are yellow; the tail blends back towards white.
    TEST_ASSERT_TRUE(f[90].r > 0 && f[90].r == f[90].g && f[90].b == 0);
    TEST_ASSERT_TRUE(f[90].w > 0 && f[90].w < 255);
    TEST_ASSERT_TRUE(f[90].r > f[80].r && f[90].w < f[80].w);
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

static void test_registry_names_roundtrip(void) {
    const EffectId ids[] = {EffectId::Dark, EffectId::Rainbow, EffectId::Wave, EffectId::Comet};
    const char* expectedNames[] = {"dark", "rainbow", "wave", "comet"};
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
    TEST_ASSERT_TRUE(EffectId::None == effectIdFromName("breathe"));
    TEST_ASSERT_TRUE(EffectId::None == effectIdFromName("flame"));
    TEST_ASSERT_TRUE(EffectId::None == effectIdFromName("embers"));

    TEST_ASSERT_EQUAL_STRING("solid", baseModeName(BaseMode::Solid));
    TEST_ASSERT_EQUAL_STRING("makeup", baseModeName(BaseMode::Makeup));
}

static uint32_t s_ticket = 0;
static uint32_t ticketRandom(uint32_t bound) {
    TEST_ASSERT_EQUAL_UINT32(415, bound);
    TEST_ASSERT_TRUE(s_ticket < bound);
    return s_ticket;
}

static void test_random_effect_covers_all(void) {
    unsigned dark = 0, rainbow = 0, wave = 0, comet = 0;
    for (s_ticket = 0; s_ticket < 415; ++s_ticket) {
        const EffectId id = randomEffect(ticketRandom);
        const EffectId expected = s_ticket < 100 ? EffectId::Dark :
                                  s_ticket < 215 ? EffectId::Rainbow :
                                  s_ticket < 315 ? EffectId::Wave : EffectId::Comet;
        TEST_ASSERT_TRUE(expected == id);
        switch (id) {
            case EffectId::Dark: ++dark; break;
            case EffectId::Rainbow: ++rainbow; break;
            case EffectId::Wave: ++wave; break;
            case EffectId::Comet: ++comet; break;
            default: TEST_FAIL_MESSAGE("removed or unknown effect selected"); break;
        }
    }
    TEST_ASSERT_EQUAL_UINT32(100, dark);
    TEST_ASSERT_EQUAL_UINT32(115, rainbow);
    TEST_ASSERT_EQUAL_UINT32(100, wave);
    TEST_ASSERT_EQUAL_UINT32(100, comet);
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
    RUN_TEST(test_rainbow_snake_shuffles_its_six_colours);
    RUN_TEST(test_rainbow_snake_order_changes_every_start);
    RUN_TEST(test_rainbow_snake_is_smooth_and_saturated);
    RUN_TEST(test_wave_finishes_after_103_steps);
    RUN_TEST(test_wave_fades_back_to_base);
    RUN_TEST(test_wave_meeting_point_not_darker_than_single_wave);
    RUN_TEST(test_wave_golden_frames);
    RUN_TEST(test_fade_alpha_ramps_in_and_out);
    RUN_TEST(test_comet_finishes_after_one_lap_plus_tail);
    RUN_TEST(test_comet_head_is_yellow_with_a_tail_even_to_the_eye);
    RUN_TEST(test_comet_tail_falls_off_monotonically_and_wraps);
    RUN_TEST(test_comet_blends_yellow_tail_into_makeup);
    RUN_TEST(test_registry_names_roundtrip);
    RUN_TEST(test_random_effect_covers_all);
    return UNITY_END();
}

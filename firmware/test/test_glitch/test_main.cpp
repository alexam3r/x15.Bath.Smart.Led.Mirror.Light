// GlitchOverlay — "neon failure" glitch: a random core of 3..6 adjacent ring
// pixels flickers between off, dim and full in the base colour for
// 300..600 ms, with a soft edge on each side that fades from the core's level
// back to the plain base — 4..6 pixels, in even steps of perceived brightness
// (CIE 1931, v1.3.0; 2..3 pixels linear in PWM before). Runs on the `native`
// PlatformIO environment.
#include <unity.h>

#include "ColorMath.h"
#include "Config.h"
#include "Frame.h"
#include "effects/Glitch.h"

void setUp(void) {}
void tearDown(void) {}

static const Rgbw kSolid{255, 140, 50, 0};
static const Rgbw kMakeup{0, 0, 0, 255};

// Edge pixel k (1 = next to the core) of an `edge`-pixel edge around a core at
// PWM `level`: even perceived steps from the core's brightness up to full.
static Rgbw edgePixel(Rgbw base, uint8_t level, uint8_t k, uint8_t edge) {
    const uint8_t core = lightness8(level);
    return scale(base, cie8(static_cast<uint8_t>(core + (255 - core) * k / (edge + 1))));
}

// Minimum everywhere: core starts at 0, 3 pixels, 4-pixel edges, 300 ms, level 0.
static uint32_t zeroRandom(uint32_t /*bound*/) { return 0; }

// Maximum everywhere, core starting near the ring end (wraps), level 0.
static uint32_t lateRandom(uint32_t bound) {
    if (bound == cfg::TOTAL_LEDS) return 165;
    if (bound == kGlitchLevelCount) return 0;
    return bound - 1;
}

// Level index cycles 0,1,2,... on every level pick; everything else 0.
static uint32_t g_levelPick = 0;
static uint32_t cyclingLevels(uint32_t bound) {
    if (bound == kGlitchLevelCount) return (g_levelPick++) % kGlitchLevelCount;
    return 0;
}

// Every level pick is the "full" entry (255).
static uint32_t fullFlash(uint32_t bound) {
    if (bound == kGlitchLevelCount) {
        for (uint8_t n = 0; n < kGlitchLevelCount; ++n) {
            if (kGlitchLevels[n] == 255) return n;
        }
    }
    return 0;
}

static uint16_t ring(int i) { return static_cast<uint16_t>((i + cfg::TOTAL_LEDS) % cfg::TOTAL_LEDS); }

static void test_min_glitch_is_3_pixel_core_with_4_pixel_edges_for_300ms(void) {
    GlitchOverlay g;
    g.start(zeroRandom, 1000);
    TEST_ASSERT_TRUE(g.active());
    TEST_ASSERT_EQUAL_UINT16(0, g.first());
    TEST_ASSERT_EQUAL_UINT8(cfg::GLITCH_LEN_MIN, g.length());
    TEST_ASSERT_EQUAL_UINT8(cfg::GLITCH_EDGE_MIN, g.edge());
    TEST_ASSERT_EQUAL_UINT32(cfg::GLITCH_DURATION_MIN_MS, g.durationMs());

    Frame f;
    TEST_ASSERT_TRUE(g.step(f, kSolid, zeroRandom, 1000));
    const Rgbw off = scale(kSolid, 0);
    TEST_ASSERT_TRUE(off == f[0]);
    TEST_ASSERT_TRUE(off == f[2]);
    TEST_ASSERT_EQUAL_UINT8(4, cfg::GLITCH_EDGE_MIN);
    // Right edge 3..6 and left edge 167..164 (wraps below 0), from the core outwards.
    for (uint8_t k = 1; k <= 4; ++k) {
        TEST_ASSERT_TRUE(edgePixel(kSolid, 0, k, 4) == f[ring(2 + k)]);
        TEST_ASSERT_TRUE(edgePixel(kSolid, 0, k, 4) == f[ring(0 - k)]);
    }
    for (uint16_t i = 7; i <= 163; ++i) {
        TEST_ASSERT_TRUE_MESSAGE(kSolid == f[i], "pixel outside core + edges differs from base");
    }
}

static void test_max_glitch_is_6_pixel_core_with_6_pixel_edges_for_600ms_and_wraps(void) {
    GlitchOverlay g;
    g.start(lateRandom, 0);
    TEST_ASSERT_EQUAL_UINT16(165, g.first());
    TEST_ASSERT_EQUAL_UINT8(cfg::GLITCH_LEN_MAX, g.length());
    TEST_ASSERT_EQUAL_UINT8(cfg::GLITCH_EDGE_MAX, g.edge());
    TEST_ASSERT_EQUAL_UINT32(cfg::GLITCH_DURATION_MAX_MS, g.durationMs());

    Frame f;
    g.step(f, kSolid, lateRandom, 0);
    const uint16_t core[] = {165, 166, 167, 0, 1, 2};  // wraps past the ring end
    for (uint16_t idx : core) TEST_ASSERT_TRUE(scale(kSolid, 0) == f[idx]);
    TEST_ASSERT_EQUAL_UINT8(6, cfg::GLITCH_EDGE_MAX);
    for (uint8_t k = 1; k <= 6; ++k) {
        TEST_ASSERT_TRUE(edgePixel(kSolid, 0, k, 6) == f[ring(2 + k)]);    // right: 3..8
        TEST_ASSERT_TRUE(edgePixel(kSolid, 0, k, 6) == f[ring(165 - k)]);  // left: 164..159
    }
    for (uint16_t i = 9; i <= 158; ++i) TEST_ASSERT_TRUE(kSolid == f[i]);
}

// Core and edges follow each flicker together: the edge always fades from the
// current core level to the plain base.
static void test_core_and_edges_flicker_together(void) {
    g_levelPick = 0;
    GlitchOverlay g;
    g.start(cyclingLevels, 0);  // core 0..2, edges 4
    Frame f;
    for (uint8_t n = 0; n < kGlitchLevelCount; ++n) {
        g.step(f, kSolid, cyclingLevels, n * cfg::GLITCH_STEP_MS);
        const uint8_t level = kGlitchLevels[n];
        for (uint16_t i = 0; i < 3; ++i) TEST_ASSERT_TRUE(scale(kSolid, level) == f[i]);  // core as before
        for (uint8_t k = 1; k <= 4; ++k) {
            TEST_ASSERT_TRUE(edgePixel(kSolid, level, k, 4) == f[ring(2 + k)]);
            TEST_ASSERT_TRUE(edgePixel(kSolid, level, k, 4) == f[ring(0 - k)]);
        }
        TEST_ASSERT_TRUE(kSolid == f[7]);
        TEST_ASSERT_TRUE(kSolid == f[163]);
    }
    // The table must actually contain "off", something dim and "full".
    bool hasOff = false, hasDim = false, hasFull = false;
    for (uint8_t n = 0; n < kGlitchLevelCount; ++n) {
        hasOff |= (kGlitchLevels[n] == 0);
        hasDim |= (kGlitchLevels[n] > 0 && kGlitchLevels[n] < 128);
        hasFull |= (kGlitchLevels[n] == 255);
    }
    TEST_ASSERT_TRUE(hasOff && hasDim && hasFull);
}

// The edge must look like an even ramp: equal perceived steps, not the old
// linear-PWM edge that looked almost full right after the dark core.
static void test_edge_is_even_to_the_eye(void) {
    GlitchOverlay g;
    g.start(zeroRandom, 0);  // core at level 0, 4-pixel edges
    Frame f;
    g.step(f, kMakeup, zeroRandom, 0);  // white channel only: w is the multiplier itself
    int prevLightness = lightness8(f[2].w);
    int prevStep = -1;
    for (uint8_t k = 1; k <= 4; ++k) {
        const int l = lightness8(f[2 + k].w);
        const int step = l - prevLightness;
        TEST_ASSERT_TRUE_MESSAGE(step > 0, "the edge must brighten outwards");
        if (prevStep >= 0) TEST_ASSERT_INT_WITHIN_MESSAGE(3, prevStep, step, "uneven perceived steps");
        prevStep = step;
        prevLightness = l;
    }
    TEST_ASSERT_TRUE_MESSAGE(f[3].w < 32, "the first edge pixel is barely lit: a real gradient, not a jump");
}

static void test_full_flash_merges_edges_into_base(void) {
    GlitchOverlay g;
    g.start(fullFlash, 0);
    Frame f;
    g.step(f, kSolid, fullFlash, 0);
    for (uint16_t i = 0; i < Frame::kSize; ++i) TEST_ASSERT_TRUE(kSolid == f[i]);
}

static void test_glitch_ends_after_its_duration_with_plain_base(void) {
    GlitchOverlay g;
    g.start(zeroRandom, 1000);  // 300 ms
    Frame f;
    for (uint32_t t = 1000; t < 1300; t += cfg::GLITCH_STEP_MS) {
        TEST_ASSERT_TRUE_MESSAGE(g.step(f, kSolid, zeroRandom, t), "glitch ended early");
    }
    TEST_ASSERT_FALSE(g.step(f, kSolid, zeroRandom, 1300));
    TEST_ASSERT_FALSE(g.active());
    for (uint16_t i = 0; i < Frame::kSize; ++i) TEST_ASSERT_TRUE(kSolid == f[i]);
}

static void test_glitch_uses_makeup_white_channel(void) {
    GlitchOverlay g;
    g.start(zeroRandom, 0);
    Frame f;
    g.step(f, kMakeup, zeroRandom, 0);
    TEST_ASSERT_TRUE(scale(kMakeup, 0) == f[0]);
    TEST_ASSERT_TRUE(edgePixel(kMakeup, 0, 1, 4) == f[3]);
    TEST_ASSERT_TRUE(kMakeup == f[7]);
}

static void test_cancel_deactivates(void) {
    GlitchOverlay g;
    g.start(zeroRandom, 0);
    g.cancel();
    TEST_ASSERT_FALSE(g.active());
}

static void test_glitch_survives_millis_wraparound(void) {
    GlitchOverlay g;
    const uint32_t t0 = 0xFFFFFF00u;
    g.start(zeroRandom, t0);
    Frame f;
    TEST_ASSERT_TRUE(g.step(f, kSolid, zeroRandom, t0 + 299));  // wraps past 0
    TEST_ASSERT_FALSE(g.step(f, kSolid, zeroRandom, t0 + 300));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_min_glitch_is_3_pixel_core_with_4_pixel_edges_for_300ms);
    RUN_TEST(test_max_glitch_is_6_pixel_core_with_6_pixel_edges_for_600ms_and_wraps);
    RUN_TEST(test_core_and_edges_flicker_together);
    RUN_TEST(test_edge_is_even_to_the_eye);
    RUN_TEST(test_full_flash_merges_edges_into_base);
    RUN_TEST(test_glitch_ends_after_its_duration_with_plain_base);
    RUN_TEST(test_glitch_uses_makeup_white_channel);
    RUN_TEST(test_cancel_deactivates);
    RUN_TEST(test_glitch_survives_millis_wraparound);
    return UNITY_END();
}

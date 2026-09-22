// GlitchOverlay — "neon failure" glitch (v1.1.0): a random run of 3..6
// adjacent ring pixels flickers between off, dim and full in the base colour
// for 300..600 ms. Runs on the `native` PlatformIO environment.
#include <unity.h>

#include "ColorMath.h"
#include "Config.h"
#include "Frame.h"
#include "effects/Glitch.h"

void setUp(void) {}
void tearDown(void) {}

static const Rgbw kSolid{255, 140, 50, 0};
static const Rgbw kMakeup{0, 0, 0, 255};
static const Rgbw kBlack{0, 0, 0, 0};

static uint32_t zeroRandom(uint32_t /*bound*/) { return 0; }

// Largest segment and duration, first pixel near the ring end (wraps), level 0.
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

static bool inSegment(const GlitchOverlay& g, uint16_t i) {
    for (uint8_t k = 0; k < g.length(); ++k) {
        if ((g.first() + k) % cfg::TOTAL_LEDS == i) return true;
    }
    return false;
}

static void test_min_glitch_is_3_pixels_for_300ms(void) {
    GlitchOverlay g;
    g.start(zeroRandom, 1000);
    TEST_ASSERT_TRUE(g.active());
    TEST_ASSERT_EQUAL_UINT16(0, g.first());
    TEST_ASSERT_EQUAL_UINT8(cfg::GLITCH_LEN_MIN, g.length());
    TEST_ASSERT_EQUAL_UINT32(cfg::GLITCH_DURATION_MIN_MS, g.durationMs());

    Frame f;
    TEST_ASSERT_TRUE(g.step(f, kSolid, zeroRandom, 1000));
    for (uint16_t i = 0; i < Frame::kSize; ++i) {
        const Rgbw expected = (i < 3) ? scale(kSolid, kGlitchLevels[0]) : kSolid;
        TEST_ASSERT_TRUE_MESSAGE(expected == f[i], "only the 3-pixel segment may differ from base");
    }
}

static void test_max_glitch_is_6_pixels_for_600ms_and_wraps(void) {
    GlitchOverlay g;
    g.start(lateRandom, 0);
    TEST_ASSERT_EQUAL_UINT16(165, g.first());
    TEST_ASSERT_EQUAL_UINT8(cfg::GLITCH_LEN_MAX, g.length());
    TEST_ASSERT_EQUAL_UINT32(cfg::GLITCH_DURATION_MAX_MS, g.durationMs());

    Frame f;
    g.step(f, kSolid, lateRandom, 0);
    const uint16_t segment[] = {165, 166, 167, 0, 1, 2};  // wraps past the ring end
    for (uint16_t idx : segment) {
        TEST_ASSERT_TRUE(scale(kSolid, kGlitchLevels[0]) == f[idx]);
    }
    for (uint16_t i = 3; i < 165; ++i) TEST_ASSERT_TRUE(kSolid == f[i]);
}

static void test_segment_flickers_through_neon_levels_as_one(void) {
    g_levelPick = 0;
    GlitchOverlay g;
    g.start(cyclingLevels, 0);
    Frame f;
    for (uint8_t n = 0; n < kGlitchLevelCount; ++n) {
        g.step(f, kSolid, cyclingLevels, n * cfg::GLITCH_STEP_MS);
        const Rgbw lit = scale(kSolid, kGlitchLevels[n]);
        for (uint16_t i = 0; i < Frame::kSize; ++i) {
            TEST_ASSERT_TRUE((inSegment(g, i) ? lit : kSolid) == f[i]);
        }
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
    TEST_ASSERT_TRUE(scale(kMakeup, kGlitchLevels[0]) == f[0]);
    TEST_ASSERT_TRUE(kMakeup == f[3]);
}

static void test_cancel_deactivates(void) {
    GlitchOverlay g;
    g.start(zeroRandom, 0);
    g.cancel();
    TEST_ASSERT_FALSE(g.active());
    (void)kBlack;
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
    RUN_TEST(test_min_glitch_is_3_pixels_for_300ms);
    RUN_TEST(test_max_glitch_is_6_pixels_for_600ms_and_wraps);
    RUN_TEST(test_segment_flickers_through_neon_levels_as_one);
    RUN_TEST(test_glitch_ends_after_its_duration_with_plain_base);
    RUN_TEST(test_glitch_uses_makeup_white_channel);
    RUN_TEST(test_cancel_deactivates);
    RUN_TEST(test_glitch_survives_millis_wraparound);
    return UNITY_END();
}

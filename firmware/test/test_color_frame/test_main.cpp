// Task 1 tests: Config, Log, Types, ColorMath, Frame (168-pixel virtual ring).
// Runs on the `native` PlatformIO environment (host, no Arduino/FreeRTOS).
#include <unity.h>

#include "ColorMath.h"
#include "Config.h"
#include "Frame.h"
#include "Log.h"
#include "Types.h"

void setUp(void) {}
void tearDown(void) {}

// --- Config.h ---------------------------------------------------------

static void test_config_derived_constants(void) {
    TEST_ASSERT_EQUAL_UINT16(168, cfg::TOTAL_LEDS);
    TEST_ASSERT_EQUAL_UINT16(95, cfg::SLIDE_MAX_RADIUS);
}

// --- Types.h ------------------------------------------------------------

static void test_state_snapshot_equality(void) {
    StateSnapshot a;
    StateSnapshot b;
    TEST_ASSERT_TRUE(a == b);

    b.brightness = 100;
    TEST_ASSERT_FALSE(a == b);
}

// --- ColorMath.h ----------------------------------------------------------

static void test_scale8_bounds(void) {
    // scale8(x, 255) == x (identity at full scale)
    TEST_ASSERT_EQUAL_UINT8(0, scale8(0, 255));
    TEST_ASSERT_EQUAL_UINT8(1, scale8(1, 255));
    TEST_ASSERT_EQUAL_UINT8(127, scale8(127, 255));
    TEST_ASSERT_EQUAL_UINT8(200, scale8(200, 255));
    TEST_ASSERT_EQUAL_UINT8(255, scale8(255, 255));

    // scale8(x, 0) == 0 (fully dark)
    TEST_ASSERT_EQUAL_UINT8(0, scale8(0, 0));
    TEST_ASSERT_EQUAL_UINT8(0, scale8(1, 0));
    TEST_ASSERT_EQUAL_UINT8(0, scale8(200, 0));
    TEST_ASSERT_EQUAL_UINT8(0, scale8(255, 0));
}

static void test_rgbw_scale_all_channels(void) {
    // scale() applies scale8 independently to each of r, g, b, w.
    Rgbw c{200, 100, 50, 10};

    Rgbw full = scale(c, 255);
    TEST_ASSERT_TRUE(c == full);

    Rgbw zero = scale(c, 0);
    Rgbw expectZero{0, 0, 0, 0};
    TEST_ASSERT_TRUE(expectZero == zero);

    Rgbw half = scale(c, 127);
    Rgbw expectHalf{
        scale8(200, 127), scale8(100, 127), scale8(50, 127), scale8(10, 127)};
    TEST_ASSERT_TRUE(expectHalf == half);
}

static void test_pack_color_grbw_layout(void) {
    // packColor = (w<<24) | (r<<16) | (g<<8) | b — Adafruit NEO_GRBW layout.
    Rgbw c{0x11, 0x22, 0x33, 0x44};
    TEST_ASSERT_EQUAL_UINT32(0x44112233u, packColor(c));

    Rgbw zero{0, 0, 0, 0};
    TEST_ASSERT_EQUAL_UINT32(0u, packColor(zero));

    Rgbw white{0, 0, 0, 255};
    TEST_ASSERT_EQUAL_UINT32(0xFF000000u, packColor(white));
}

static void test_hsv_primary_hues(void) {
    // Reference values independently computed from Adafruit_NeoPixel::
    // ColorHSV(hue, 255, 255) (see Adafruit_NeoPixel.cpp), not by calling
    // hsv() itself.
    struct Case { uint16_t hue; uint8_t r, g, b; };
    const Case cases[] = {
        {0,     255, 0,   0},    // pure red
        {21845, 0,   255, 0},    // pure green
        {43690, 0,   0,   255},  // pure blue
        {10000, 255, 233, 0},    // arbitrary
        {30000, 0,   255, 190},  // arbitrary
        {50000, 147, 0,   255},  // arbitrary
    };

    for (const auto& tc : cases) {
        Rgbw got = hsv(tc.hue);
        Rgbw expected{tc.r, tc.g, tc.b, 0};
        TEST_ASSERT_TRUE_MESSAGE(expected == got, "hsv() mismatch for given hue");
    }
}

// --- Frame.h / ring mapping -------------------------------------------------

static void test_map_virtual_table(void) {
    // Table from ARCHITECTURE.md section 6.
    PhysicalPixel p0 = mapVirtual(0);
    TEST_ASSERT_EQUAL_UINT8(STRIP_RIGHT, p0.strip);
    TEST_ASSERT_EQUAL_UINT8(0, p0.index);

    PhysicalPixel p101 = mapVirtual(101);
    TEST_ASSERT_EQUAL_UINT8(STRIP_RIGHT, p101.strip);
    TEST_ASSERT_EQUAL_UINT8(101, p101.index);

    PhysicalPixel p102 = mapVirtual(102);
    TEST_ASSERT_EQUAL_UINT8(STRIP_LEFT, p102.strip);
    TEST_ASSERT_EQUAL_UINT8(65, p102.index);

    PhysicalPixel p167 = mapVirtual(167);
    TEST_ASSERT_EQUAL_UINT8(STRIP_LEFT, p167.strip);
    TEST_ASSERT_EQUAL_UINT8(0, p167.index);
}

static void test_map_virtual_is_bijection(void) {
    // All 168 virtual indices must map to 168 distinct physical pixels,
    // with each strip's index range fully covered exactly once.
    bool seenRight[cfg::LEDS_RIGHT_CNT] = {false};
    bool seenLeft[cfg::LEDS_LEFT_CNT]   = {false};
    uint16_t uniqueCount = 0;

    for (uint16_t v = 0; v < cfg::TOTAL_LEDS; ++v) {
        PhysicalPixel p = mapVirtual(v);
        if (p.strip == STRIP_RIGHT) {
            TEST_ASSERT_TRUE(p.index < cfg::LEDS_RIGHT_CNT);
            TEST_ASSERT_FALSE_MESSAGE(seenRight[p.index], "duplicate right-strip pixel");
            seenRight[p.index] = true;
            ++uniqueCount;
        } else {
            TEST_ASSERT_EQUAL_UINT8(STRIP_LEFT, p.strip);
            TEST_ASSERT_TRUE(p.index < cfg::LEDS_LEFT_CNT);
            TEST_ASSERT_FALSE_MESSAGE(seenLeft[p.index], "duplicate left-strip pixel");
            seenLeft[p.index] = true;
            ++uniqueCount;
        }
    }

    TEST_ASSERT_EQUAL_UINT16(168, uniqueCount);
    for (uint16_t i = 0; i < cfg::LEDS_RIGHT_CNT; ++i) TEST_ASSERT_TRUE(seenRight[i]);
    for (uint16_t i = 0; i < cfg::LEDS_LEFT_CNT; ++i) TEST_ASSERT_TRUE(seenLeft[i]);
}

static void test_ring_dist_wraps(void) {
    TEST_ASSERT_EQUAL_UINT16(1, ringDist(0, 167));
    TEST_ASSERT_EQUAL_UINT16(84, ringDist(0, 84));
    TEST_ASSERT_EQUAL_UINT16(0, ringDist(9, 9));
    TEST_ASSERT_EQUAL_UINT16(1, ringDist(167, 0));  // symmetric
}

static void test_frame_scale(void) {
    Frame f;
    Rgbw c{200, 100, 50, 10};
    f.fill(c);

    f.scale(128);

    Rgbw expected = scale(c, 128);
    for (uint16_t i = 0; i < Frame::kSize; ++i) {
        TEST_ASSERT_TRUE_MESSAGE(expected == f[i], "Frame::scale mismatch");
    }
}

static void test_frame_clear(void) {
    Frame f;
    f.fill(Rgbw{1, 2, 3, 4});
    f.clear();

    Rgbw black{0, 0, 0, 0};
    for (uint16_t i = 0; i < Frame::kSize; ++i) {
        TEST_ASSERT_TRUE(black == f[i]);
    }
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_config_derived_constants);
    RUN_TEST(test_state_snapshot_equality);
    RUN_TEST(test_scale8_bounds);
    RUN_TEST(test_rgbw_scale_all_channels);
    RUN_TEST(test_pack_color_grbw_layout);
    RUN_TEST(test_hsv_primary_hues);
    RUN_TEST(test_map_virtual_table);
    RUN_TEST(test_map_virtual_is_bijection);
    RUN_TEST(test_ring_dist_wraps);
    RUN_TEST(test_frame_scale);
    RUN_TEST(test_frame_clear);
    return UNITY_END();
}

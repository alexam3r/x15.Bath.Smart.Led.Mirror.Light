// Ramp (linear uint8_t interpolation over time, overflow-safe) and the lerp
// helpers in ColorMath (v1.2.0). Runs on the `native` PlatformIO environment.
#include <unity.h>

#include "ColorMath.h"
#include "Ramp.h"

void setUp(void) {}
void tearDown(void) {}

static void test_lerp8_endpoints_and_midpoint(void) {
    TEST_ASSERT_EQUAL_UINT8(10, lerp8(10, 200, 0));
    TEST_ASSERT_EQUAL_UINT8(200, lerp8(10, 200, 255));
    TEST_ASSERT_EQUAL_UINT8(105, lerp8(10, 200, 128));  // 10 + 190*128/255 = 105
    TEST_ASSERT_EQUAL_UINT8(100, lerp8(200, 0, 128));   // downwards: 200 - 200*128/255 = 100
}

static void test_lerp_rgbw_per_channel(void) {
    const Rgbw a{255, 140, 50, 0};
    const Rgbw b{0, 0, 255, 255};
    TEST_ASSERT_TRUE(a == lerp(a, b, 0));
    TEST_ASSERT_TRUE(b == lerp(a, b, 255));
    const Rgbw h = lerp(a, b, 128);
    TEST_ASSERT_EQUAL_UINT8(lerp8(255, 0, 128), h.r);
    TEST_ASSERT_EQUAL_UINT8(lerp8(140, 0, 128), h.g);
    TEST_ASSERT_EQUAL_UINT8(lerp8(50, 255, 128), h.b);
    TEST_ASSERT_EQUAL_UINT8(lerp8(0, 255, 128), h.w);
}

static void test_ramp_interpolates_linearly(void) {
    Ramp r;
    r.start(0, 200, 1000, 400);
    TEST_ASSERT_TRUE(r.active);
    TEST_ASSERT_EQUAL_UINT8(0, r.value(1000));
    TEST_ASSERT_EQUAL_UINT8(100, r.value(1200));
    TEST_ASSERT_EQUAL_UINT8(150, r.value(1300));
    TEST_ASSERT_TRUE(r.running(1300));
}

static void test_ramp_retires_at_end_and_holds_target(void) {
    Ramp r;
    r.start(200, 50, 0, 100);
    TEST_ASSERT_EQUAL_UINT8(50, r.value(100));
    TEST_ASSERT_FALSE(r.active);
    TEST_ASSERT_EQUAL_UINT8(50, r.value(5000));
    r.set(77);
    TEST_ASSERT_EQUAL_UINT8(77, r.value(6000));
    TEST_ASSERT_FALSE(r.running(6000));
}

static void test_ramp_without_change_is_inactive(void) {
    Ramp r;
    r.start(90, 90, 0, 500);
    TEST_ASSERT_FALSE(r.active);
    TEST_ASSERT_EQUAL_UINT8(90, r.value(10));
    r.start(10, 200, 0, 0);  // zero duration: jump straight to the target
    TEST_ASSERT_FALSE(r.active);
    TEST_ASSERT_EQUAL_UINT8(200, r.value(0));
}

static void test_ramp_survives_millis_wraparound(void) {
    Ramp r;
    const uint32_t t0 = 0xFFFFFF00u;
    r.start(0, 100, t0, 512);
    TEST_ASSERT_EQUAL_UINT8(50, r.value(t0 + 256));  // crosses 0
    TEST_ASSERT_TRUE(r.active);
    TEST_ASSERT_EQUAL_UINT8(100, r.value(t0 + 512));
    TEST_ASSERT_FALSE(r.active);
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_lerp8_endpoints_and_midpoint);
    RUN_TEST(test_lerp_rgbw_per_channel);
    RUN_TEST(test_ramp_interpolates_linearly);
    RUN_TEST(test_ramp_retires_at_end_and_holds_target);
    RUN_TEST(test_ramp_without_change_is_inactive);
    RUN_TEST(test_ramp_survives_millis_wraparound);
    return UNITY_END();
}

// CIE 1931 lightness (v1.3.0): gradients are computed in perceived
// brightness and turned into PWM duty with cie8(), so a fade looks even to
// the eye instead of rushing through the dark end.
#include <unity.h>

#include "ColorMath.h"

void setUp(void) {}
void tearDown(void) {}

// Y = L/903.3 for L <= 8, else ((L + 16) / 116)^3, with L = lightness in %.
static void test_cie8_matches_the_cie_1931_curve(void) {
    TEST_ASSERT_EQUAL_UINT8(0, cie8(0));
    TEST_ASSERT_EQUAL_UINT8(255, cie8(255));
    TEST_ASSERT_EQUAL_UINT8(47, cie8(128));   // half as bright to the eye = 18.6 % duty
    TEST_ASSERT_EQUAL_UINT8(16, cie8(77));    // 30 %
    TEST_ASSERT_EQUAL_UINT8(145, cie8(204));  // 80 %
    TEST_ASSERT_EQUAL_UINT8(1, cie8(10));     // linear segment near black
}

static void test_cie8_is_monotonic(void) {
    for (int p = 1; p < 256; ++p) {
        TEST_ASSERT_TRUE_MESSAGE(cie8(static_cast<uint8_t>(p)) >= cie8(static_cast<uint8_t>(p - 1)),
                                 "cie8 went down");
    }
}

// lightness8 is the inverse: the smallest lightness whose duty reaches pwm.
static void test_lightness8_inverts_cie8(void) {
    TEST_ASSERT_EQUAL_UINT8(0, lightness8(0));
    TEST_ASSERT_EQUAL_UINT8(255, lightness8(255));
    TEST_ASSERT_EQUAL_UINT8(127, lightness8(47));  // 127 and 128 both give 47
    for (int x = 0; x < 256; ++x) {
        const uint8_t l = lightness8(static_cast<uint8_t>(x));
        TEST_ASSERT_TRUE(cie8(l) >= x);
        TEST_ASSERT_TRUE_MESSAGE(l == 0 || cie8(static_cast<uint8_t>(l - 1)) < x, "not the smallest lightness");
        if (x > 0) TEST_ASSERT_TRUE(l >= lightness8(static_cast<uint8_t>(x - 1)));
    }
}

// A perceptual fade must start and end exactly on its colours — the top of
// the curve skips duty values, so a plain round trip would jump.
static void test_perceptual_lerp_is_exact_at_both_ends(void) {
    for (int a = 0; a < 256; a += 3) {
        for (int b = 0; b < 256; b += 7) {
            TEST_ASSERT_EQUAL_UINT8(a, lerp8Perceptual(static_cast<uint8_t>(a), static_cast<uint8_t>(b), 0));
            TEST_ASSERT_EQUAL_UINT8(b, lerp8Perceptual(static_cast<uint8_t>(a), static_cast<uint8_t>(b), 255));
        }
    }
    const Rgbw from{255, 140, 50, 0};
    const Rgbw to{0, 0, 255, 253};
    TEST_ASSERT_TRUE(from == lerpPerceptual(from, to, 0));
    TEST_ASSERT_TRUE(to == lerpPerceptual(from, to, 255));
}

static void test_perceptual_lerp_is_even_to_the_eye(void) {
    TEST_ASSERT_EQUAL_UINT8(47, lerp8Perceptual(0, 255, 128));   // halfway = half as bright to the eye
    TEST_ASSERT_EQUAL_UINT8(47, lerp8Perceptual(255, 0, 127));
    uint8_t prev = 0;
    for (int t = 0; t < 256; ++t) {
        const uint8_t v = lerp8Perceptual(0, 255, static_cast<uint8_t>(t));
        TEST_ASSERT_TRUE_MESSAGE(v >= prev, "the fade went backwards");
        prev = v;
    }
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_cie8_matches_the_cie_1931_curve);
    RUN_TEST(test_cie8_is_monotonic);
    RUN_TEST(test_lightness8_inverts_cie8);
    RUN_TEST(test_perceptual_lerp_is_exact_at_both_ends);
    RUN_TEST(test_perceptual_lerp_is_even_to_the_eye);
    return UNITY_END();
}

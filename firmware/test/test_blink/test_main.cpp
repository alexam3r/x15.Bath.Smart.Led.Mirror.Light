// Blink (v1.5.0): the confirmation flashes a button hold with the mirror off
// plays on the dark ring — `count` cosine flashes of `flashMs` each, `gapMs`
// apart, peaking at `peak` (perceived brightness).
#include <unity.h>

#include "Blink.h"

void setUp(void) {}
void tearDown(void) {}

static void test_one_flash_rises_to_the_peak_and_falls(void) {
    Blink b;
    b.start(1, 1000, 0, 77, 100);
    TEST_ASSERT_TRUE(b.armed());
    TEST_ASSERT_TRUE(b.active(100));
    TEST_ASSERT_EQUAL_UINT8(0, b.level(100));      // starts dark
    TEST_ASSERT_EQUAL_UINT8(77, b.level(600));     // peak halfway
    TEST_ASSERT_UINT8_WITHIN(1, 39, b.level(350));  // a quarter in: half the peak (cosine)
    TEST_ASSERT_UINT8_WITHIN(1, 39, b.level(850));
    TEST_ASSERT_TRUE(b.level(300) < b.level(500));  // rising
    TEST_ASSERT_TRUE(b.level(700) > b.level(900));  // falling
    TEST_ASSERT_TRUE(b.active(1099));
    TEST_ASSERT_FALSE(b.active(1100));             // over after one flash
    TEST_ASSERT_EQUAL_UINT8(0, b.level(1100));
    TEST_ASSERT_EQUAL_UINT32(1000, b.durationMs());
}

static void test_two_flashes_with_a_dark_gap(void) {
    Blink b;
    b.start(2, 400, 200, 77, 0);
    TEST_ASSERT_EQUAL_UINT8(77, b.level(200));  // first peak
    TEST_ASSERT_EQUAL_UINT8(0, b.level(400));
    TEST_ASSERT_EQUAL_UINT8(0, b.level(500));   // dark between the flashes
    TEST_ASSERT_TRUE(b.active(500));
    TEST_ASSERT_EQUAL_UINT8(77, b.level(800));  // second peak
    TEST_ASSERT_TRUE(b.active(999));
    TEST_ASSERT_FALSE(b.active(1000));
    TEST_ASSERT_EQUAL_UINT8(0, b.level(1200));  // no third flash
    TEST_ASSERT_EQUAL_UINT32(1000, b.durationMs());
}

static void test_cancel_and_idle(void) {
    Blink idle;
    TEST_ASSERT_FALSE(idle.armed());
    TEST_ASSERT_FALSE(idle.active(0));
    TEST_ASSERT_EQUAL_UINT8(0, idle.level(123));

    Blink b;
    b.start(1, 1000, 0, 77, 0);
    b.cancel();
    TEST_ASSERT_FALSE(b.armed());
    TEST_ASSERT_FALSE(b.active(500));
    TEST_ASSERT_EQUAL_UINT8(0, b.level(500));
}

// millis() wraps after 49.7 days: a blink started just before must still
// play out.
static void test_survives_the_millis_wraparound(void) {
    Blink b;
    const uint32_t start = 0xFFFFFFFFu - 300;
    b.start(1, 1000, 0, 77, start);
    TEST_ASSERT_EQUAL_UINT8(77, b.level(start + 500));  // after the wrap
    TEST_ASSERT_TRUE(b.active(start + 900));
    TEST_ASSERT_FALSE(b.active(start + 1000));
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_one_flash_rises_to_the_peak_and_falls);
    RUN_TEST(test_two_flashes_with_a_dark_gap);
    RUN_TEST(test_cancel_and_idle);
    RUN_TEST(test_survives_the_millis_wraparound);
    return UNITY_END();
}

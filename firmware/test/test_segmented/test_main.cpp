#include <unity.h>
#include <initializer_list>

#include "Frame.h"
#include "effects/SegmentedAnimation.h"
#include "effects/SlideAnimation.h"

void setUp(void) {}
void tearDown(void) {}

static const Rgbw kWhite{0, 0, 0, 255};
static const Rgbw kRgb{90, 180, 240, 0};
static const Rgbw kBlack{0, 0, 0, 0};
static uint32_t zeroRandom(uint32_t bound) { TEST_ASSERT_TRUE(bound > 0); return 0; }
static uint32_t maxRandom(uint32_t bound) { TEST_ASSERT_TRUE(bound > 0); return bound - 1; }
static uint32_t seed = 1;
static uint32_t seededRandom(uint32_t bound) {
    TEST_ASSERT_TRUE(bound > 0);
    seed = seed * 1664525u + 1013904223u;
    return (seed >> 8) % bound;
}

static void assertFrame(const Frame& frame, Rgbw want) {
    for (uint16_t i = 0; i < Frame::kSize; ++i) TEST_ASSERT_TRUE(want == frame[i]);
}

static void test_segments_match_slider_duration_and_endpoints(void) {
    for (auto rnd : {zeroRandom, maxRandom, seededRandom}) {
        SegmentedAnimation animation;
        SlideAnimation slide;
        Frame frame, other;
        animation.startOn(rnd);
        slide.startOn(9);
        for (int call = 1; call <= 102; ++call) {
            const bool running = animation.step(frame, kWhite);
            TEST_ASSERT_EQUAL(slide.step(other, kWhite), running);
            if (call == 1) assertFrame(frame, kBlack);
            if (call == 102) assertFrame(frame, kWhite);
        }
        animation.startOff(rnd);
        slide.startOff();
        for (int call = 1; call <= 103; ++call) {
            TEST_ASSERT_EQUAL(slide.step(other, kWhite), animation.step(frame, kWhite));
            if (call == 1) assertFrame(frame, kWhite);
            if (call == 103) assertFrame(frame, kBlack);
        }
    }
}

// At progress 1 only the first shuffled core is active: its brightest run
// must contain 8..12 LEDs even for partitions near the ring's end.
static void test_segments_have_legal_cores_random_positions_and_full_coverage(void) {
    uint32_t firstSignature = 0;
    int changed = 0;
    for (uint32_t run = 1; run <= 200; ++run) {
        seed = run;
        SegmentedAnimation animation;
        Frame frame;
        animation.startOn(seededRandom);
        animation.step(frame, kWhite);
        animation.step(frame, kWhite);
        uint8_t brightest = 0;
        for (uint16_t i = 0; i < Frame::kSize; ++i) if (frame[i].w > brightest) brightest = frame[i].w;
        TEST_ASSERT_TRUE(brightest > 0);
        int count = 0;
        uint32_t signature = 0;
        for (uint16_t i = 0; i < Frame::kSize; ++i) {
            if (frame[i].w == brightest) ++count;
            signature = signature * 31 + frame[i].w;
        }
        TEST_ASSERT_TRUE(count >= 8 && count <= 12);
        if (run == 1) firstSignature = signature;
        else if (signature != firstSignature) ++changed;
        for (int call = 3; call <= 102; ++call) animation.step(frame, kWhite);
        assertFrame(frame, kWhite);
    }
    TEST_ASSERT_TRUE_MESSAGE(changed > 180, "segments always begin in the same place/order");
}

static void test_segments_have_perceptual_edges_without_additive_overlap(void) {
    SegmentedAnimation animation;
    Frame frame;
    animation.startOn(zeroRandom);  // origin 0, first core 0..7, next starts at progress 4
    for (int call = 1; call <= 5; ++call) animation.step(frame, kWhite);
    const uint8_t core = cie8(102);  // progress 4 of the ten-step perceived fade
    for (int i = 0; i < 8; ++i) TEST_ASSERT_EQUAL_UINT8(core, frame[i].w);
    TEST_ASSERT_TRUE(frame[167].w > frame[166].w && frame[166].w > frame[165].w);
    TEST_ASSERT_EQUAL_UINT8(0, frame[164].w);
    TEST_ASSERT_EQUAL_UINT8(scale8(core, cie8(191)), frame[167].w);
    // Subsequent overlapping halos must not cause brightness to overshoot or
    // fall backwards as more segments join the pattern.
    Frame previous = frame;
    for (int call = 6; call <= 102; ++call) {
        animation.step(frame, kWhite);
        for (uint16_t i = 0; i < Frame::kSize; ++i) TEST_ASSERT_TRUE(frame[i].w >= previous[i].w);
        previous = frame;
    }
    assertFrame(frame, kWhite);
    animation.startOff(zeroRandom);
    for (int call = 1; call <= 103; ++call) {
        animation.step(frame, kRgb);
        for (uint16_t i = 0; i < Frame::kSize; ++i) {
            TEST_ASSERT_TRUE(frame[i].r <= 90 && frame[i].g <= 180 && frame[i].b <= 240 && frame[i].w == 0);
        }
    }
    assertFrame(frame, kBlack);
}

static void test_segments_reverse_without_changing_pattern_or_flashing(void) {
    SegmentedAnimation animation;
    Frame frame;
    animation.startOn(seededRandom);
    animation.startOff();  // no rendered step yet
    TEST_ASSERT_FALSE(animation.step(frame, kWhite));
    assertFrame(frame, kBlack);

    animation.startOn(zeroRandom);
    for (int i = 0; i < 45; ++i) animation.step(frame, kWhite);
    animation.startOff();
    for (int i = 0; i < 10; ++i) animation.step(frame, kWhite);
    Frame before = frame;
    animation.reverseToOn();
    TEST_ASSERT_TRUE(animation.turningOn());
    // The cursor is one frame behind the last render, as for the slider.
    animation.step(frame, kWhite);
    animation.step(frame, kWhite);
    for (uint16_t i = 0; i < Frame::kSize; ++i) TEST_ASSERT_TRUE(before[i] == frame[i]);
    int calls = 0;
    while (animation.step(frame, kWhite)) TEST_ASSERT_TRUE(++calls < 103);
    assertFrame(frame, kWhite);
}

static void test_fresh_slider_off_does_not_depend_on_previous_animation(void) {
    SlideAnimation slide;
    Frame frame;
    slide.startOff(51);
    TEST_ASSERT_TRUE(slide.step(frame, kWhite));
    assertFrame(frame, kWhite);
    int calls = 1;
    while (slide.step(frame, kWhite)) ++calls;
    TEST_ASSERT_EQUAL_INT(103, calls + 1);
    assertFrame(frame, kBlack);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_segments_match_slider_duration_and_endpoints);
    RUN_TEST(test_segments_have_legal_cores_random_positions_and_full_coverage);
    RUN_TEST(test_segments_have_perceptual_edges_without_additive_overlap);
    RUN_TEST(test_segments_reverse_without_changing_pattern_or_flashing);
    RUN_TEST(test_fresh_slider_off_does_not_depend_on_previous_animation);
    return UNITY_END();
}

// Task 2 tests: Button — raw pressed level -> Click/Hold events
// (ARCHITECTURE.md 4.3 / 8.1). Runs on the `native` PlatformIO environment
// (host, no Arduino/FreeRTOS dependency).
#include <unity.h>

#include <vector>

#include "Button.h"
#include "Config.h"

void setUp(void) {}
void tearDown(void) {}

namespace {

// Calls update(pressed, t) for t = from, from+step, ..., to (inclusive;
// (to - from) must be a multiple of step), mirroring the real loop() which
// polls the button level every few ms. Non-None events are appended to
// *out in the order they occur. Returns the last t used.
uint32_t poll(Button& btn, bool pressed, uint32_t from, uint32_t to, uint32_t step,
              std::vector<ButtonEvent>* out) {
    uint32_t t = from;
    for (;;) {
        ButtonEvent ev = btn.update(pressed, t);
        if (out != nullptr && ev.type != ButtonEventType::None) {
            out->push_back(ev);
        }
        if (t == to) break;
        t += step;
    }
    return t;
}

void assertType(ButtonEventType expected, ButtonEventType actual) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected), static_cast<uint8_t>(actual));
}

}  // namespace

// --- Clicks -----------------------------------------------------------------

static void test_single_click_after_400ms(void) {
    Button btn;
    std::vector<ButtonEvent> events;

    // Press edge at t=0, short press (~80ms), release edge at t=80.
    poll(btn, true, 0, 75, 5, &events);
    ButtonEvent releaseEv = btn.update(false, 80);
    assertType(ButtonEventType::None, releaseEv.type);

    // No Click yet at release+400 (strict '>' threshold).
    poll(btn, false, 85, 480, 5, &events);
    TEST_ASSERT_EQUAL_INT(0, (int)events.size());

    // Click(1) appears once release+400 is exceeded (release+405).
    poll(btn, false, 485, 485, 5, &events);
    TEST_ASSERT_EQUAL_INT(1, (int)events.size());
    assertType(ButtonEventType::Click, events[0].type);
    TEST_ASSERT_EQUAL_UINT8(1, events[0].clicks);
}

static void test_double_click(void) {
    Button btn;
    std::vector<ButtonEvent> events;

    // Click #1: press 0-75, release at 80 (clickCount=1, lastRelease=80).
    poll(btn, true, 0, 75, 5, &events);
    btn.update(false, 80);

    // Click #2 within the 400ms gap: press at 200, release at 280.
    poll(btn, false, 85, 195, 5, &events);
    btn.update(true, 200);
    poll(btn, true, 205, 275, 5, &events);
    btn.update(false, 280);  // now-lastRelease = 200 <= 400 -> clickCount=2

    TEST_ASSERT_EQUAL_INT(0, (int)events.size());  // nothing emitted yet

    // Wait past the gap (release+400=680) for the combined Click(2).
    poll(btn, false, 285, 685, 5, &events);

    TEST_ASSERT_EQUAL_INT(1, (int)events.size());
    assertType(ButtonEventType::Click, events[0].type);
    TEST_ASSERT_EQUAL_UINT8(2, events[0].clicks);
}

static void test_triple_click(void) {
    Button btn;
    std::vector<ButtonEvent> events;

    // Click #1
    poll(btn, true, 0, 75, 5, &events);
    btn.update(false, 80);  // clickCount=1, lastRelease=80

    // Click #2 within gap
    poll(btn, false, 85, 195, 5, &events);
    btn.update(true, 200);
    poll(btn, true, 205, 275, 5, &events);
    btn.update(false, 280);  // clickCount=2, lastRelease=280

    // Click #3 within gap
    poll(btn, false, 285, 395, 5, &events);
    btn.update(true, 400);
    poll(btn, true, 405, 475, 5, &events);
    btn.update(false, 480);  // clickCount=3, lastRelease=480

    TEST_ASSERT_EQUAL_INT(0, (int)events.size());

    // Wait past the gap (release+400=880) for the combined Click(3).
    poll(btn, false, 485, 885, 5, &events);

    TEST_ASSERT_EQUAL_INT(1, (int)events.size());
    assertType(ButtonEventType::Click, events[0].type);
    TEST_ASSERT_EQUAL_UINT8(3, events[0].clicks);
}

static void test_clicks_separated_by_gap_are_separate(void) {
    Button btn;
    std::vector<ButtonEvent> events;

    // Click #1: press 0-75, release at 80, resolves at release+400=480.
    poll(btn, true, 0, 75, 5, &events);
    btn.update(false, 80);
    poll(btn, false, 85, 485, 5, &events);
    TEST_ASSERT_EQUAL_INT(1, (int)events.size());
    assertType(ButtonEventType::Click, events[0].type);
    TEST_ASSERT_EQUAL_UINT8(1, events[0].clicks);

    // Click #2, separated from click #1's release by a 450ms gap
    // (well past the 400ms window, so it must NOT merge into click #1).
    btn.update(true, 530);
    poll(btn, true, 535, 605, 5, &events);
    btn.update(false, 610);  // now-lastRelease = 530 > 400 -> clickCount resets to 1
    poll(btn, false, 615, 1015, 5, &events);

    TEST_ASSERT_EQUAL_INT(2, (int)events.size());
    assertType(ButtonEventType::Click, events[1].type);
    TEST_ASSERT_EQUAL_UINT8(1, events[1].clicks);
}

// --- Hold -------------------------------------------------------------------

static void test_hold_start_at_500ms(void) {
    Button btn;
    btn.update(true, 0);  // press edge

    ButtonEvent at500 = btn.update(true, 500);
    assertType(ButtonEventType::None, at500.type);

    ButtonEvent at501 = btn.update(true, 501);
    assertType(ButtonEventType::HoldStart, at501.type);
}

static void test_hold_ticks_every_30ms(void) {
    Button btn;
    std::vector<ButtonEvent> events;

    poll(btn, true, 0, 500, 5, &events);
    TEST_ASSERT_EQUAL_INT(0, (int)events.size());

    ButtonEvent holdStart = btn.update(true, 505);
    assertType(ButtonEventType::HoldStart, holdStart.type);

    events.clear();
    poll(btn, true, 510, 1510, 5, &events);  // ~1s of holding after HoldStart

    for (size_t i = 0; i < events.size(); ++i) {
        assertType(ButtonEventType::HoldTick, events[i].type);
    }
    TEST_ASSERT_TRUE(events.size() >= 28);
    TEST_ASSERT_TRUE(events.size() <= 32);
}

static void test_hold_release_does_not_click(void) {
    Button btn;
    std::vector<ButtonEvent> events;

    // Hold for 800ms (crosses HoldStart and several HoldTicks).
    poll(btn, true, 0, 800, 5, &events);
    for (size_t i = 0; i < events.size(); ++i) {
        ButtonEventType t = events[i].type;
        TEST_ASSERT_TRUE(t == ButtonEventType::HoldStart || t == ButtonEventType::HoldTick);
    }
    events.clear();

    ButtonEvent releaseEv = btn.update(false, 805);
    assertType(ButtonEventType::HoldEnd, releaseEv.type);

    // Rule #5: a hold never becomes a click, no matter how long we wait.
    poll(btn, false, 810, 1810, 5, &events);
    TEST_ASSERT_EQUAL_INT(0, (int)events.size());
}

static void test_click_after_hold_counts_from_one(void) {
    Button btn;
    std::vector<ButtonEvent> events;

    // Hold, then release -> HoldEnd, clickCount reset to 0.
    poll(btn, true, 0, 550, 5, &events);
    ButtonEvent releaseEv = btn.update(false, 555);
    assertType(ButtonEventType::HoldEnd, releaseEv.type);
    events.clear();

    // A short click shortly after: must count from one, not continue at two.
    btn.update(true, 600);
    ButtonEvent releaseEv2 = btn.update(false, 650);
    assertType(ButtonEventType::None, releaseEv2.type);

    poll(btn, false, 655, 1055, 5, &events);  // past release2+400=1050

    TEST_ASSERT_EQUAL_INT(1, (int)events.size());
    assertType(ButtonEventType::Click, events[0].type);
    TEST_ASSERT_EQUAL_UINT8(1, events[0].clicks);
}

// --- Wraparound ---------------------------------------------------------

static void test_works_across_millis_wraparound(void) {
    const uint32_t base = 0xFFFFFF00u;  // 256 ms before uint32_t wraps to 0
    std::vector<ButtonEvent> events;

    // Click scenario, resolving after the wraparound.
    Button clickBtn;
    poll(clickBtn, true, base + 0, base + 75, 5, &events);
    clickBtn.update(false, base + 80);
    poll(clickBtn, false, base + 85, base + 480, 5, &events);
    TEST_ASSERT_EQUAL_INT(0, (int)events.size());
    poll(clickBtn, false, base + 485, base + 485, 5, &events);  // crosses the wrap
    TEST_ASSERT_EQUAL_INT(1, (int)events.size());
    assertType(ButtonEventType::Click, events[0].type);
    TEST_ASSERT_EQUAL_UINT8(1, events[0].clicks);
    events.clear();

    // Hold scenario, HoldStart/HoldTick/HoldEnd all resolving after the wrap.
    Button holdBtn;
    poll(holdBtn, true, base + 0, base + 500, 5, &events);
    TEST_ASSERT_EQUAL_INT(0, (int)events.size());
    ButtonEvent holdStart = holdBtn.update(true, base + 505);  // crosses the wrap
    assertType(ButtonEventType::HoldStart, holdStart.type);

    events.clear();
    poll(holdBtn, true, base + 510, base + 1510, 5, &events);
    for (size_t i = 0; i < events.size(); ++i) {
        assertType(ButtonEventType::HoldTick, events[i].type);
    }
    TEST_ASSERT_TRUE(events.size() >= 28);
    TEST_ASSERT_TRUE(events.size() <= 32);

    ButtonEvent releaseEv = holdBtn.update(false, base + 1515);
    assertType(ButtonEventType::HoldEnd, releaseEv.type);
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_single_click_after_400ms);
    RUN_TEST(test_double_click);
    RUN_TEST(test_triple_click);
    RUN_TEST(test_clicks_separated_by_gap_are_separate);
    RUN_TEST(test_hold_start_at_500ms);
    RUN_TEST(test_hold_ticks_every_30ms);
    RUN_TEST(test_hold_release_does_not_click);
    RUN_TEST(test_click_after_hold_counts_from_one);
    RUN_TEST(test_works_across_millis_wraparound);
    return UNITY_END();
}

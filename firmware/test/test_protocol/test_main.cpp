// Task 6 tests: Topics (topic strings + routing, Ruling R4) and Protocol
// (payload <-> Command / StateSnapshot, ARCHITECTURE.md 5.2/5.3/8.1). Runs
// on the `native` PlatformIO environment (host, no Arduino/FreeRTOS
// dependency). A PlatformIO test filter runs only this suite:
// `pio test -e native -f test_protocol`. Grown test-first in slices, each
// function/rule added only after its test failed for the expected reason.
#include <unity.h>

#include <cstring>

#include "Config.h"
#include "Protocol.h"
#include "Topics.h"
#include "Types.h"
#include "effects/EffectRegistry.h"

void setUp(void) {}
void tearDown(void) {}

static const char* kBase = "home/flat8/bath/mirror";

// --- Topics::init (Ruling R4) ----------------------------------------------

static void test_topics_built_from_base(void) {
    Topics t;
    t.init(kBase);
    TEST_ASSERT_EQUAL_STRING("home/flat8/bath/mirror/set", t.set);
    TEST_ASSERT_EQUAL_STRING("home/flat8/bath/mirror/+/set", t.setWildcard);
    TEST_ASSERT_EQUAL_STRING("home/flat8/bath/mirror/state", t.state);
    TEST_ASSERT_EQUAL_STRING("home/flat8/bath/mirror/motion/set", t.automationSet);
    TEST_ASSERT_EQUAL_STRING("home/flat8/bath/mirror/motion/state", t.automationState);
    TEST_ASSERT_EQUAL_STRING("home/flat8/bath/mirror/makeup/set", t.makeupSet);
    TEST_ASSERT_EQUAL_STRING("home/flat8/bath/mirror/makeup/state", t.makeupState);
    TEST_ASSERT_EQUAL_STRING("home/flat8/bath/mirror/effect/set", t.effectSet);
    TEST_ASSERT_EQUAL_STRING("home/flat8/bath/mirror/motion_disable/set", t.nightModeSet);
    TEST_ASSERT_EQUAL_STRING("home/flat8/bath/mirror/motion_disable/state", t.nightModeState);
    TEST_ASSERT_EQUAL_STRING("home/flat8/bath/mirror/pir/state", t.pirState);
    TEST_ASSERT_EQUAL_STRING("home/flat8/bath/mirror/availability", t.availability);
}

// --- routeTopic (Ruling R4) -------------------------------------------------

static void test_route_all_topics(void) {
    TEST_ASSERT_TRUE(Route::Light == routeTopic("home/flat8/bath/mirror/set", kBase));
    TEST_ASSERT_TRUE(Route::Automation == routeTopic("home/flat8/bath/mirror/motion/set", kBase));
    TEST_ASSERT_TRUE(Route::Makeup == routeTopic("home/flat8/bath/mirror/makeup/set", kBase));
    TEST_ASSERT_TRUE(Route::Effect == routeTopic("home/flat8/bath/mirror/effect/set", kBase));
    TEST_ASSERT_TRUE(Route::NightMode == routeTopic("home/flat8/bath/mirror/motion_disable/set", kBase));
}

static void test_route_rejects_foreign_base(void) {
    // Foreign base entirely.
    TEST_ASSERT_TRUE(Route::Unknown == routeTopic("some/other/base/set", kBase));
    // Unknown sub-topic.
    TEST_ASSERT_TRUE(Route::Unknown == routeTopic("home/flat8/bath/mirror/foo/set", kBase));
    // Outgoing (state) topic, not a command.
    TEST_ASSERT_TRUE(Route::Unknown == routeTopic("home/flat8/bath/mirror/motion/state", kBase));
    // Prefix trick: base with an extra suffix character before the slash.
    TEST_ASSERT_TRUE(Route::Unknown == routeTopic("home/flat8/bath/mirrorx/set", kBase));
    // Prefix trick: nested extra segment before /set.
    TEST_ASSERT_TRUE(Route::Unknown == routeTopic("home/flat8/bath/mirror/motion/extra/set", kBase));
    // Bare base, no suffix at all.
    TEST_ASSERT_TRUE(Route::Unknown == routeTopic("home/flat8/bath/mirror", kBase));
}

// --- parseSwitch (section 5.1 switch-payload) -------------------------------

static bool switchOf(const char* payload) {
    bool out = true;   // poisoned; a false return must leave it untouched
    bool ok = parseSwitch(reinterpret_cast<const uint8_t*>(payload), std::strlen(payload), out);
    TEST_ASSERT_TRUE_MESSAGE(ok, payload);
    return out;
}

static void assertSwitchRejected(const char* payload) {
    bool out = true;  // poison with true...
    bool ok1 = parseSwitch(reinterpret_cast<const uint8_t*>(payload), std::strlen(payload), out);
    TEST_ASSERT_FALSE_MESSAGE(ok1, payload);
    TEST_ASSERT_TRUE_MESSAGE(out, payload);  // ...still true: untouched

    out = false;  // ...and with false, to prove it wasn't set true either
    bool ok2 = parseSwitch(reinterpret_cast<const uint8_t*>(payload), std::strlen(payload), out);
    TEST_ASSERT_FALSE_MESSAGE(ok2, payload);
    TEST_ASSERT_FALSE_MESSAGE(out, payload);  // ...still false: untouched
}

static void test_switch_payload_variants(void) {
    TEST_ASSERT_TRUE(switchOf("on"));
    TEST_ASSERT_TRUE(switchOf("On"));
    TEST_ASSERT_TRUE(switchOf("1"));
    TEST_ASSERT_TRUE(switchOf("true"));
    TEST_ASSERT_TRUE(switchOf("TRUE"));

    TEST_ASSERT_FALSE(switchOf("OFF"));
    TEST_ASSERT_FALSE(switchOf("off"));
    TEST_ASSERT_FALSE(switchOf("0"));
    TEST_ASSERT_FALSE(switchOf("false"));

    assertSwitchRejected("yes");
    assertSwitchRejected("2");
    assertSwitchRejected("");
    assertSwitchRejected("ONN");
}

// --- parseLight (section 5.2) -----------------------------------------------

static bool lightOf(const char* json, LightCommand& out) {
    return parseLight(reinterpret_cast<const uint8_t*>(json), std::strlen(json), out);
}

// §5.2 example: {"state": "ON", "brightness": 128, "color": {"r": 255, "g": 0, "b": 0}, "effect": "wave"}
static void test_light_full_command(void) {
    LightCommand out;
    bool ok = lightOf(
        "{\"state\": \"ON\", \"brightness\": 128, "
        "\"color\": {\"r\": 255, \"g\": 0, \"b\": 0}, \"effect\": \"wave\"}",
        out);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT8(1, out.state);
    TEST_ASSERT_EQUAL_INT16(128, out.brightness);
    TEST_ASSERT_EQUAL_INT16(255, out.r);
    TEST_ASSERT_EQUAL_INT16(0, out.g);
    TEST_ASSERT_EQUAL_INT16(0, out.b);
    TEST_ASSERT_TRUE(EffectRequest::Temporary == out.effect);
    TEST_ASSERT_TRUE(EffectId::Wave == out.effectId);
}

static void test_light_partial_color_keeps_components(void) {
    LightCommand out;
    bool ok = lightOf("{\"color\": {\"r\": 10}}", out);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT16(10, out.r);
    TEST_ASSERT_EQUAL_INT16(-1, out.g);  // absent -> sentinel, Mirror keeps current
    TEST_ASSERT_EQUAL_INT16(-1, out.b);

    // No color key at all -> all three stay at the sentinel.
    LightCommand out2;
    TEST_ASSERT_TRUE(lightOf("{\"state\": \"ON\"}", out2));
    TEST_ASSERT_EQUAL_INT16(-1, out2.r);
    TEST_ASSERT_EQUAL_INT16(-1, out2.g);
    TEST_ASSERT_EQUAL_INT16(-1, out2.b);
}

static void test_light_brightness_zero_is_off(void) {
    LightCommand out;
    bool ok = lightOf("{\"brightness\": 0}", out);
    TEST_ASSERT_TRUE(ok);
    // parseLight only clamps/parses; the OFF decision is Mirror's (task 5).
    TEST_ASSERT_EQUAL_INT16(0, out.brightness);
    TEST_ASSERT_EQUAL_INT8(-1, out.state);  // untouched: no "state" key
}

static void test_light_clamping_and_sentinels(void) {
    LightCommand out;
    TEST_ASSERT_TRUE(lightOf("{\"brightness\": 300}", out));
    TEST_ASSERT_EQUAL_INT16(255, out.brightness);

    LightCommand out2;
    TEST_ASSERT_TRUE(lightOf("{\"brightness\": -5}", out2));
    TEST_ASSERT_EQUAL_INT16(0, out2.brightness);

    LightCommand out3;
    TEST_ASSERT_TRUE(lightOf("{\"color\": {\"r\": 999, \"g\": -5, \"b\": 260}}", out3));
    TEST_ASSERT_EQUAL_INT16(255, out3.r);
    TEST_ASSERT_EQUAL_INT16(0, out3.g);
    TEST_ASSERT_EQUAL_INT16(255, out3.b);

    // Non-integer brightness ignored (stays -1).
    LightCommand out4;
    TEST_ASSERT_TRUE(lightOf("{\"brightness\": 12.5}", out4));
    TEST_ASSERT_EQUAL_INT16(-1, out4.brightness);

    // state neither "ON" nor "OFF" -> ignored.
    LightCommand out5;
    TEST_ASSERT_TRUE(lightOf("{\"state\": \"MAYBE\"}", out5));
    TEST_ASSERT_EQUAL_INT8(-1, out5.state);

    // Case-insensitive state.
    LightCommand out6;
    TEST_ASSERT_TRUE(lightOf("{\"state\": \"off\"}", out6));
    TEST_ASSERT_EQUAL_INT8(0, out6.state);

    // Ignored keys: color.w, color_temp, transition, unknown top-level key.
    LightCommand out7;
    TEST_ASSERT_TRUE(lightOf(
        "{\"color\": {\"r\": 1, \"w\": 200}, \"color_temp\": 300, "
        "\"transition\": 1, \"unknown_field\": 42}",
        out7));
    TEST_ASSERT_EQUAL_INT16(1, out7.r);
}

// Regression for the LightCommand clamp contract: Mirror (Task 5) casts
// brightness/r/g/b from int16_t to uint8_t WITHOUT clamping, so parseLight
// must never let a non-integer JSON value slip through as if it were a
// clamped byte -- it must be ignored (stay at the -1 sentinel) instead.
// `is<int>()` is what rejects these; `as<int>()` alone would happily
// truncate/convert them, silently accepting out-of-contract values.
static void test_light_non_integer_brightness_and_color_ignored(void) {
    LightCommand str;
    TEST_ASSERT_TRUE(lightOf("{\"brightness\": \"128\"}", str));
    TEST_ASSERT_EQUAL_INT16(-1, str.brightness);

    LightCommand exp;
    TEST_ASSERT_TRUE(lightOf("{\"brightness\": 1e9}", exp));
    TEST_ASSERT_EQUAL_INT16(-1, exp.brightness);

    LightCommand overflow;
    TEST_ASSERT_TRUE(lightOf("{\"brightness\": 5000000000}", overflow));
    TEST_ASSERT_EQUAL_INT16(-1, overflow.brightness);

    LightCommand colorStr;
    TEST_ASSERT_TRUE(lightOf("{\"color\": {\"r\": \"200\"}}", colorStr));
    TEST_ASSERT_EQUAL_INT16(-1, colorStr.r);

    LightCommand colorExp;
    TEST_ASSERT_TRUE(lightOf("{\"color\": {\"g\": 1e9}}", colorExp));
    TEST_ASSERT_EQUAL_INT16(-1, colorExp.g);
}

// Base modes and "random" map to their own EffectRequest; every other name
// is looked up in the effect registry (no per-effect table in Protocol).
static void test_light_effect_names(void) {
    LightCommand solid;
    TEST_ASSERT_TRUE(lightOf("{\"effect\": \"solid\"}", solid));
    TEST_ASSERT_TRUE(EffectRequest::Solid == solid.effect);
    TEST_ASSERT_TRUE(EffectId::None == solid.effectId);

    LightCommand makeup;
    TEST_ASSERT_TRUE(lightOf("{\"effect\": \"makeup\"}", makeup));
    TEST_ASSERT_TRUE(EffectRequest::Makeup == makeup.effect);
    TEST_ASSERT_TRUE(EffectId::None == makeup.effectId);

    LightCommand random;
    TEST_ASSERT_TRUE(lightOf("{\"effect\": \"random\"}", random));
    TEST_ASSERT_TRUE(EffectRequest::Random == random.effect);
    TEST_ASSERT_TRUE(EffectId::None == random.effectId);

    LightCommand dark;
    TEST_ASSERT_TRUE(lightOf("{\"effect\": \"dark\"}", dark));
    TEST_ASSERT_TRUE(EffectRequest::Temporary == dark.effect);
    TEST_ASSERT_TRUE(EffectId::Dark == dark.effectId);

    LightCommand rainbow;
    TEST_ASSERT_TRUE(lightOf("{\"effect\": \"rainbow\"}", rainbow));
    TEST_ASSERT_TRUE(EffectRequest::Temporary == rainbow.effect);
    TEST_ASSERT_TRUE(EffectId::Rainbow == rainbow.effectId);

    LightCommand wave;
    TEST_ASSERT_TRUE(lightOf("{\"effect\": \"wave\"}", wave));
    TEST_ASSERT_TRUE(EffectRequest::Temporary == wave.effect);
    TEST_ASSERT_TRUE(EffectId::Wave == wave.effectId);
}

static void test_light_unknown_effect_ignored(void) {
    LightCommand out;
    TEST_ASSERT_TRUE(lightOf("{\"effect\": \"bogus\"}", out));
    TEST_ASSERT_TRUE(EffectRequest::None == out.effect);
    TEST_ASSERT_TRUE(EffectId::None == out.effectId);

    LightCommand wrongCase;  // registry names are case-sensitive
    TEST_ASSERT_TRUE(lightOf("{\"effect\": \"Wave\"}", wrongCase));
    TEST_ASSERT_TRUE(EffectRequest::None == wrongCase.effect);
    TEST_ASSERT_TRUE(EffectId::None == wrongCase.effectId);
}

static void test_light_invalid_json_rejected(void) {
    LightCommand out;
    TEST_ASSERT_FALSE(lightOf("not json{{{", out));
    TEST_ASSERT_FALSE(lightOf("", out));
    TEST_ASSERT_FALSE(lightOf("[1,2,3]", out));   // valid JSON, not an object
    TEST_ASSERT_FALSE(lightOf("\"just a string\"", out));
}

// --- toCommand ---------------------------------------------------------

static void test_to_command_light(void) {
    Command out;
    const char* json = "{\"state\": \"ON\", \"brightness\": 200}";
    bool ok = toCommand(Route::Light, reinterpret_cast<const uint8_t*>(json), std::strlen(json), out);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(CommandType::Light == out.type);
    TEST_ASSERT_EQUAL_INT8(1, out.light.state);
    TEST_ASSERT_EQUAL_INT16(200, out.light.brightness);
}

static void test_to_command_automation_makeup_nightmode(void) {
    Command a;
    bool okA = toCommand(Route::Automation, reinterpret_cast<const uint8_t*>("ON"), 2, a);
    TEST_ASSERT_TRUE(okA);
    TEST_ASSERT_TRUE(CommandType::Automation == a.type);
    TEST_ASSERT_TRUE(a.flag);

    Command m;
    bool okM = toCommand(Route::Makeup, reinterpret_cast<const uint8_t*>("OFF"), 3, m);
    TEST_ASSERT_TRUE(okM);
    TEST_ASSERT_TRUE(CommandType::Makeup == m.type);
    TEST_ASSERT_FALSE(m.flag);

    Command n;
    bool okN = toCommand(Route::NightMode, reinterpret_cast<const uint8_t*>("1"), 1, n);
    TEST_ASSERT_TRUE(okN);
    TEST_ASSERT_TRUE(CommandType::NightMode == n.type);
    TEST_ASSERT_TRUE(n.flag);
}

static void test_to_command_switch_invalid_rejected(void) {
    Command out;
    bool ok = toCommand(Route::Automation, reinterpret_cast<const uint8_t*>("bogus"), 5, out);
    TEST_ASSERT_FALSE(ok);
}

static void test_effect_topic_any_payload_is_random(void) {
    Command empty;
    TEST_ASSERT_TRUE(toCommand(Route::Effect, reinterpret_cast<const uint8_t*>(""), 0, empty));
    TEST_ASSERT_TRUE(CommandType::RandomEffect == empty.type);

    Command nonEmpty;
    const char* payload = "anything at all";
    TEST_ASSERT_TRUE(toCommand(Route::Effect, reinterpret_cast<const uint8_t*>(payload),
                                std::strlen(payload), nonEmpty));
    TEST_ASSERT_TRUE(CommandType::RandomEffect == nonEmpty.type);
}

static void test_to_command_unknown_route_rejected(void) {
    Command out;
    TEST_ASSERT_FALSE(toCommand(Route::Unknown, reinterpret_cast<const uint8_t*>("ON"), 2, out));
}

// --- buildStateJson (section 5.3) -------------------------------------------

// §5.3 exact key order and values, for the default StateSnapshot (on=false,
// brightness=255, r=255/g=140/b=50, base=Solid, effect=None, automation=true,
// nightMode=false).
static void test_state_json_matches_5_3(void) {
    StateSnapshot s;  // default
    char buf[cfg::STATE_JSON_CAP];
    size_t n = buildStateJson(s, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_INT((int)std::strlen(buf), (int)n);
    TEST_ASSERT_EQUAL_STRING(
        "{\"state\":\"OFF\",\"brightness\":255,\"color_mode\":\"rgb\","
        "\"color\":{\"r\":255,\"g\":140,\"b\":50},\"effect\":\"solid\","
        "\"automation\":\"ON\",\"night_mode\":\"OFF\",\"fw\":\"1.0.0\","
        "\"brightness_pct\":100,\"moveDetection\":\"ON\",\"makeup\":\"OFF\"}",
        buf);
}

// Running effect takes priority over the base mode name; automation off,
// night mode on, makeup base -> exercises every "else" branch of 5.3 too.
static void test_state_json_running_effect_and_makeup(void) {
    StateSnapshot s;
    s.on = true;
    s.brightness = 128;
    s.r = 10;
    s.g = 20;
    s.b = 30;
    s.base = BaseMode::Makeup;
    s.effect = EffectId::Rainbow;
    s.automation = false;
    s.nightMode = true;
    char buf[cfg::STATE_JSON_CAP];
    size_t n = buildStateJson(s, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING(
        "{\"state\":\"ON\",\"brightness\":128,\"color_mode\":\"rgb\","
        "\"color\":{\"r\":10,\"g\":20,\"b\":30},\"effect\":\"rainbow\","
        "\"automation\":\"OFF\",\"night_mode\":\"ON\",\"fw\":\"1.0.0\","
        "\"brightness_pct\":50,\"moveDetection\":\"OFF\",\"makeup\":\"ON\"}",
        buf);
}

static void test_state_json_fits_384_bytes(void) {
    // Worst case: longest effect name ("rainbow"), makeup base, all fields
    // at their maximum printed width.
    StateSnapshot s;
    s.on = true;
    s.brightness = 255;
    s.r = 255;
    s.g = 255;
    s.b = 255;
    s.base = BaseMode::Makeup;
    s.effect = EffectId::Rainbow;
    s.automation = true;
    s.nightMode = true;
    char buf[cfg::STATE_JSON_CAP];
    size_t n = buildStateJson(s, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(n < cfg::STATE_JSON_CAP);
}

static void test_state_json_zero_when_buffer_too_small(void) {
    StateSnapshot s;
    char tiny[8];
    size_t n = buildStateJson(s, tiny, sizeof(tiny));
    TEST_ASSERT_EQUAL_INT(0, (int)n);
}

// Ruling R16: Network republishes the retained `state` only when a field
// that actually appears in its JSON changed. `pir` is not in the state JSON
// (it has its own pir/state sidecar), so a PIR-only change must not count;
// every other snapshot field must.
static void test_state_json_differs_ignores_pir_only(void) {
    StateSnapshot a;
    StateSnapshot b = a;
    TEST_ASSERT_FALSE(stateJsonDiffers(a, b));

    b.pir = !a.pir;
    TEST_ASSERT_FALSE_MESSAGE(stateJsonDiffers(a, b), "a PIR-only change counted as a state change");

    StateSnapshot c;
    c = a; c.on = !a.on;                    TEST_ASSERT_TRUE(stateJsonDiffers(a, c));
    c = a; c.brightness = 17;               TEST_ASSERT_TRUE(stateJsonDiffers(a, c));
    c = a; c.r = 1;                         TEST_ASSERT_TRUE(stateJsonDiffers(a, c));
    c = a; c.g = 2;                         TEST_ASSERT_TRUE(stateJsonDiffers(a, c));
    c = a; c.b = 3;                         TEST_ASSERT_TRUE(stateJsonDiffers(a, c));
    c = a; c.base = BaseMode::Makeup;       TEST_ASSERT_TRUE(stateJsonDiffers(a, c));
    c = a; c.effect = EffectId::Wave;       TEST_ASSERT_TRUE(stateJsonDiffers(a, c));
    c = a; c.automation = !a.automation;    TEST_ASSERT_TRUE(stateJsonDiffers(a, c));
    c = a; c.nightMode = !a.nightMode;      TEST_ASSERT_TRUE(stateJsonDiffers(a, c));

    // A real change plus a PIR change still counts.
    c = a; c.brightness = 17; c.pir = !a.pir;
    TEST_ASSERT_TRUE(stateJsonDiffers(a, c));
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_topics_built_from_base);
    RUN_TEST(test_route_all_topics);
    RUN_TEST(test_route_rejects_foreign_base);
    RUN_TEST(test_switch_payload_variants);
    RUN_TEST(test_light_full_command);
    RUN_TEST(test_light_partial_color_keeps_components);
    RUN_TEST(test_light_brightness_zero_is_off);
    RUN_TEST(test_light_clamping_and_sentinels);
    RUN_TEST(test_light_non_integer_brightness_and_color_ignored);
    RUN_TEST(test_light_effect_names);
    RUN_TEST(test_light_unknown_effect_ignored);
    RUN_TEST(test_light_invalid_json_rejected);
    RUN_TEST(test_to_command_light);
    RUN_TEST(test_to_command_automation_makeup_nightmode);
    RUN_TEST(test_to_command_switch_invalid_rejected);
    RUN_TEST(test_effect_topic_any_payload_is_random);
    RUN_TEST(test_to_command_unknown_route_rejected);
    RUN_TEST(test_state_json_matches_5_3);
    RUN_TEST(test_state_json_running_effect_and_makeup);
    RUN_TEST(test_state_json_fits_384_bytes);
    RUN_TEST(test_state_json_zero_when_buffer_too_small);
    RUN_TEST(test_state_json_differs_ignores_pir_only);
    return UNITY_END();
}

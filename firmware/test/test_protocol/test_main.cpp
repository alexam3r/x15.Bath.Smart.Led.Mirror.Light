// Task 6 tests: Topics (topic strings + routing, Ruling R4) and Protocol
// (payload <-> Command / StateSnapshot, ARCHITECTURE.md 5.2/5.3/8.1). Runs
// on the `native` PlatformIO environment (host, no Arduino/FreeRTOS
// dependency). A PlatformIO test filter runs only this suite:
// `pio test -e native -f test_protocol`. Grown test-first in slices, each
// function/rule added only after its test failed for the expected reason.
#include <unity.h>

#include <ArduinoJson.h>

#include <cstdlib>
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
    TEST_ASSERT_EQUAL_STRING("home/flat8/bath/mirror/glitch/set", t.glitchSet);
    TEST_ASSERT_EQUAL_STRING("home/flat8/bath/mirror/glitch/state", t.glitchState);
}

// --- routeTopic (Ruling R4) -------------------------------------------------

static void test_route_all_topics(void) {
    TEST_ASSERT_TRUE(Route::Light == routeTopic("home/flat8/bath/mirror/set", kBase));
    TEST_ASSERT_TRUE(Route::Automation == routeTopic("home/flat8/bath/mirror/motion/set", kBase));
    TEST_ASSERT_TRUE(Route::Makeup == routeTopic("home/flat8/bath/mirror/makeup/set", kBase));
    TEST_ASSERT_TRUE(Route::Effect == routeTopic("home/flat8/bath/mirror/effect/set", kBase));
    TEST_ASSERT_TRUE(Route::NightMode == routeTopic("home/flat8/bath/mirror/motion_disable/set", kBase));
    TEST_ASSERT_TRUE(Route::Glitch == routeTopic("home/flat8/bath/mirror/glitch/set", kBase));
    TEST_ASSERT_TRUE(Route::Unknown == routeTopic("home/flat8/bath/mirror/glitch/state", kBase));
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

    LightCommand flame;
    TEST_ASSERT_TRUE(lightOf("{\"effect\": \"flame\"}", flame));
    TEST_ASSERT_TRUE(EffectRequest::Temporary == flame.effect);
    TEST_ASSERT_TRUE(EffectId::Flame == flame.effectId);

    // breathe was replaced by flame in v1.4.0: its name is unknown now and ignored.
    LightCommand breathe;
    TEST_ASSERT_TRUE(lightOf("{\"effect\": \"breathe\"}", breathe));
    TEST_ASSERT_TRUE(EffectRequest::None == breathe.effect);

    LightCommand embers;
    TEST_ASSERT_TRUE(lightOf("{\"effect\": \"embers\"}", embers));
    TEST_ASSERT_TRUE(EffectRequest::Temporary == embers.effect);
    TEST_ASSERT_TRUE(EffectId::Embers == embers.effectId);

    // candle was removed in v1.3.0: its name is unknown now and ignored.
    LightCommand candle;
    TEST_ASSERT_TRUE(lightOf("{\"effect\": \"candle\"}", candle));
    TEST_ASSERT_TRUE(EffectRequest::None == candle.effect);

    LightCommand comet;
    TEST_ASSERT_TRUE(lightOf("{\"effect\": \"comet\"}", comet));
    TEST_ASSERT_TRUE(EffectRequest::Temporary == comet.effect);
    TEST_ASSERT_TRUE(EffectId::Comet == comet.effectId);

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

    Command g;
    bool okG = toCommand(Route::Glitch, reinterpret_cast<const uint8_t*>("OFF"), 3, g);
    TEST_ASSERT_TRUE(okG);
    TEST_ASSERT_TRUE(CommandType::Glitch == g.type);
    TEST_ASSERT_FALSE(g.flag);
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
        "\"automation\":\"ON\",\"night_mode\":\"OFF\",\"glitch\":\"ON\",\"fw\":\"1.5.0\","
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
    s.glitch = false;
    char buf[cfg::STATE_JSON_CAP];
    size_t n = buildStateJson(s, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING(
        "{\"state\":\"ON\",\"brightness\":128,\"color_mode\":\"rgb\","
        "\"color\":{\"r\":10,\"g\":20,\"b\":30},\"effect\":\"rainbow\","
        "\"automation\":\"OFF\",\"night_mode\":\"ON\",\"glitch\":\"OFF\",\"fw\":\"1.5.0\","
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
    c = a; c.glitch = !a.glitch;            TEST_ASSERT_TRUE(stateJsonDiffers(a, c));

    // A real change plus a PIR change still counts.
    c = a; c.brightness = 17; c.pir = !a.pir;
    TEST_ASSERT_TRUE(stateJsonDiffers(a, c));
}

// --- Diagnostics topic (v1.2.0) ---------------------------------------------

static void test_diag_topic_built_from_base(void) {
    Topics t;
    t.init(kBase);
    TEST_ASSERT_EQUAL_STRING("home/flat8/bath/mirror/diag", t.diag);
    // Outgoing only: an incoming message on it must not route anywhere.
    TEST_ASSERT_TRUE(Route::Unknown == routeTopic("home/flat8/bath/mirror/diag", kBase));
}

static void test_reset_reason_names(void) {
    TEST_ASSERT_EQUAL_STRING("POWERON", resetReasonName(1));
    TEST_ASSERT_EQUAL_STRING("EXT", resetReasonName(2));
    TEST_ASSERT_EQUAL_STRING("SW", resetReasonName(3));
    TEST_ASSERT_EQUAL_STRING("PANIC", resetReasonName(4));
    TEST_ASSERT_EQUAL_STRING("INT_WDT", resetReasonName(5));
    TEST_ASSERT_EQUAL_STRING("TASK_WDT", resetReasonName(6));
    TEST_ASSERT_EQUAL_STRING("WDT", resetReasonName(7));
    TEST_ASSERT_EQUAL_STRING("DEEPSLEEP", resetReasonName(8));
    TEST_ASSERT_EQUAL_STRING("BROWNOUT", resetReasonName(9));
    TEST_ASSERT_EQUAL_STRING("SDIO", resetReasonName(10));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", resetReasonName(0));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", resetReasonName(99));
}

static void test_diag_json(void) {
    DiagInfo d;
    d.uptimeS = 3723;
    d.rssi = -61;
    d.resetReason = 1;
    d.freeHeap = 231000;
    d.minFreeHeap = 198000;
    d.maxAllocHeap = 110592;
    d.lastEffect = EffectId::Embers;
    char buf[cfg::DIAG_JSON_CAP];
    const size_t n = buildDiagJson(d, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING(
        "{\"uptime_s\":3723,\"rssi\":-61,\"reset_reason\":\"POWERON\","
        "\"free_heap\":231000,\"min_free_heap\":198000,\"max_alloc_heap\":110592,"
        "\"last_effect\":\"embers\",\"fw\":\"1.5.0\"}",
        buf);
    TEST_ASSERT_EQUAL_UINT32(n, strlen(buf));
    // Nothing started since boot.
    d.lastEffect = EffectId::None;
    buildDiagJson(d, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"last_effect\":\"none\""));
}

// last_effect lives in diag, not in state: a change of it alone must not
// republish the JSON Light state (like pir, Ruling R16).
static void test_state_json_differs_ignores_last_effect(void) {
    StateSnapshot a, b;
    b.lastEffect = EffectId::Comet;
    TEST_ASSERT_FALSE(stateJsonDiffers(a, b));
    TEST_ASSERT_FALSE(a == b);  // but the snapshot itself did change: Core 0 must see it
}

static void test_diag_json_worst_case_fits_the_cap(void) {
    DiagInfo d;
    d.uptimeS = 4294967295u;
    d.rssi = -128;
    d.resetReason = 6;  // TASK_WDT, the longest name
    d.freeHeap = d.minFreeHeap = d.maxAllocHeap = 4294967295u;
    d.lastEffect = EffectId::Rainbow;  // the longest effect name (7 letters)
    char buf[cfg::DIAG_JSON_CAP];
    TEST_ASSERT_TRUE(buildDiagJson(d, buf, sizeof(buf)) > 0);
}

static void test_diag_json_zero_when_buffer_too_small(void) {
    DiagInfo d;
    char small[16];
    TEST_ASSERT_EQUAL_UINT32(0, buildDiagJson(d, small, sizeof(small)));
}

// --- Out of memory (v1.2.1) ----------------------------------------------------
// ArduinoJson drops what it cannot allocate and would still serialise the
// rest — e.g. "{}" — which would go out retained as <base>/state. The
// builders must return 0 instead, like when the buffer is too small.

// Lets the first `left` allocations succeed, then fails every one.
struct FailAfter : ArduinoJson::Allocator {
    int left;
    explicit FailAfter(int n) : left(n) {}
    void* allocate(size_t n) override { return (left-- > 0) ? malloc(n) : nullptr; }
    void deallocate(void* p) override { free(p); }
    void* reallocate(void* p, size_t n) override { return (left-- > 0) ? realloc(p, n) : nullptr; }
};

static void test_state_json_zero_when_memory_runs_out(void) {
    StateSnapshot s;
    char buf[cfg::STATE_JSON_CAP];
    int needed = 0;  // allocations a full document needs
    while (true) {
        FailAfter a(needed);
        if (buildStateJson(s, buf, sizeof(buf), &a) > 0) break;
        ++needed;
        TEST_ASSERT_TRUE(needed < 50);
    }
    TEST_ASSERT_TRUE_MESSAGE(needed >= 1, "a document built with no memory at all was serialised");
    FailAfter plenty(needed);
    TEST_ASSERT_TRUE(buildStateJson(s, buf, sizeof(buf), &plenty) > 0);
    TEST_ASSERT_EQUAL_STRING_LEN("{\"state\":\"OFF\"", buf, 14);  // the real thing, not a stub
}

static void test_diag_json_zero_when_memory_runs_out(void) {
    DiagInfo d;
    char buf[cfg::DIAG_JSON_CAP];
    FailAfter none(0);
    TEST_ASSERT_EQUAL_UINT32(0, buildDiagJson(d, buf, sizeof(buf), &none));
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
    RUN_TEST(test_diag_topic_built_from_base);
    RUN_TEST(test_reset_reason_names);
    RUN_TEST(test_diag_json);
    RUN_TEST(test_state_json_differs_ignores_last_effect);
    RUN_TEST(test_diag_json_worst_case_fits_the_cap);
    RUN_TEST(test_diag_json_zero_when_buffer_too_small);
    RUN_TEST(test_state_json_zero_when_memory_runs_out);
    RUN_TEST(test_diag_json_zero_when_memory_runs_out);
    return UNITY_END();
}

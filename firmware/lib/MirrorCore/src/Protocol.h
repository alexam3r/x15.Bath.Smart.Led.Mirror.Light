// Task 6: payload <-> Command / StateSnapshot (ARCHITECTURE.md sections 5.2,
// 5.3, 8.1). Pure C++, no Arduino/FreeRTOS dependency (see global
// constraints). ArduinoJson v7 API only.
#pragma once

#include <ArduinoJson.h>

#include <cstddef>
#include <cstdint>

#include "Topics.h"
#include "Types.h"

// Case-insensitive ON/1/TRUE -> true, OFF/0/FALSE -> false; anything else
// (including empty payload) returns false and leaves `out` untouched.
bool parseSwitch(const uint8_t* payload, size_t len, bool& out);

// Parses a `<base>/set` JSON payload (section 5.2) into `out`. Every field
// present in `out` is already clamped/sentineled: brightness and r/g/b are
// clamped to 0..255, -1 means "absent". `effect`: "solid"/"makeup"/"random"
// map to their EffectRequest; any registry name becomes Temporary + effectId.
// Returns false on invalid JSON or a non-object top level; unrecognised
// keys/values (including unknown effect names) are simply ignored.
bool parseLight(const uint8_t* payload, size_t len, LightCommand& out);

// Turns a routed topic + payload into a Command. Unknown route -> false.
bool toCommand(Route route, const uint8_t* payload, size_t len, Command& out);

// Serializes a StateSnapshot as the `<base>/state` JSON (section 5.3) into
// `buf` (capacity `cap`). Returns bytes written (NUL not counted), or 0 if
// it would not fit.
// Diagnostics published on <base>/diag (v1.2.0). Gathered on Core 0 only —
// Mirror neither sees nor cares about any of it.
struct DiagInfo {
    uint32_t uptimeS     = 0;
    int8_t   rssi        = 0;
    uint8_t  resetReason = 0;  // esp_reset_reason_t value
    uint32_t freeHeap    = 0;
    uint32_t minFreeHeap = 0;
};

// "POWERON", "EXT", "SW", "PANIC", "INT_WDT", "TASK_WDT", "WDT",
// "DEEPSLEEP", "BROWNOUT", "SDIO"; "UNKNOWN" for anything else.
const char* resetReasonName(uint8_t code);

// Serialises DiagInfo; 0 if it does not fit `cap` or memory ran out (like
// buildStateJson). `alloc` is for tests; nullptr = the default heap.
size_t buildDiagJson(const DiagInfo& d, char* buf, size_t cap, ArduinoJson::Allocator* alloc = nullptr);

size_t buildStateJson(const StateSnapshot& s, char* buf, size_t cap, ArduinoJson::Allocator* alloc = nullptr);

// True if `a` and `b` would serialize to different `<base>/state` JSON, i.e.
// any snapshot field except `pir` (which only goes to `<base>/pir/state`)
// differs. Network uses it to skip republishing `state` on PIR-only changes.
bool stateJsonDiffers(const StateSnapshot& a, const StateSnapshot& b);

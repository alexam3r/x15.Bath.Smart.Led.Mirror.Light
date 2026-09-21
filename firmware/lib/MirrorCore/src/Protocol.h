// Task 6: payload <-> Command / StateSnapshot (ARCHITECTURE.md sections 5.2,
// 5.3, 8.1). Pure C++, no Arduino/FreeRTOS dependency (see global
// constraints). ArduinoJson v7 API only.
#pragma once

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
size_t buildStateJson(const StateSnapshot& s, char* buf, size_t cap);

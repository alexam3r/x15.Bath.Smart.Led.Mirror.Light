// Task 6: Topics implementation (ARCHITECTURE.md section 5.1, Ruling R4).
#include "Topics.h"

#include <cstdio>
#include <cstring>

void Topics::init(const char* base) {
    std::snprintf(set, sizeof(set), "%s/set", base);
    std::snprintf(setWildcard, sizeof(setWildcard), "%s/+/set", base);
    std::snprintf(state, sizeof(state), "%s/state", base);
    std::snprintf(automationSet, sizeof(automationSet), "%s/motion/set", base);
    std::snprintf(automationState, sizeof(automationState), "%s/motion/state", base);
    std::snprintf(makeupSet, sizeof(makeupSet), "%s/makeup/set", base);
    std::snprintf(makeupState, sizeof(makeupState), "%s/makeup/state", base);
    std::snprintf(effectSet, sizeof(effectSet), "%s/effect/set", base);
    std::snprintf(nightModeSet, sizeof(nightModeSet), "%s/motion_disable/set", base);
    std::snprintf(nightModeState, sizeof(nightModeState), "%s/motion_disable/state", base);
    std::snprintf(pirState, sizeof(pirState), "%s/pir/state", base);
    std::snprintf(availability, sizeof(availability), "%s/availability", base);
    std::snprintf(glitchSet, sizeof(glitchSet), "%s/glitch/set", base);
    std::snprintf(glitchState, sizeof(glitchState), "%s/glitch/state", base);
}

Route routeTopic(const char* topic, const char* base) {
    size_t baseLen = std::strlen(base);
    if (std::strncmp(topic, base, baseLen) != 0) return Route::Unknown;
    if (topic[baseLen] != '/') return Route::Unknown;

    const char* rest = topic + baseLen + 1;

    // Exact `<base>/set` -> Light.
    if (std::strcmp(rest, "set") == 0) return Route::Light;

    // `<base>/<sub>/set` -> Route by sub (single path segment only).
    const char* setPtr = std::strstr(rest, "/set");
    if (setPtr == nullptr || setPtr[4] != '\0') return Route::Unknown;

    size_t subLen = static_cast<size_t>(setPtr - rest);
    if (subLen == 0) return Route::Unknown;

    if (subLen == 6 && std::strncmp(rest, "motion", subLen) == 0) return Route::Automation;
    if (subLen == 6 && std::strncmp(rest, "makeup", subLen) == 0) return Route::Makeup;
    if (subLen == 6 && std::strncmp(rest, "effect", subLen) == 0) return Route::Effect;
    if (subLen == 14 && std::strncmp(rest, "motion_disable", subLen) == 0) return Route::NightMode;
    if (subLen == 6 && std::strncmp(rest, "glitch", subLen) == 0) return Route::Glitch;

    return Route::Unknown;
}

// Task 6: MQTT topic strings built from one base, and routing of incoming
// topics to a Route (ARCHITECTURE.md section 5.1, Ruling R4). Pure C++, no
// Arduino/FreeRTOS dependency (see global constraints).
#pragma once

#include <cstdint>

enum class Route : uint8_t { Unknown, Light, Automation, Makeup, Effect, NightMode, Glitch };

struct Topics {
    char set[96];
    char setWildcard[96];
    char state[96];
    char automationSet[96];
    char automationState[96];
    char makeupSet[96];
    char makeupState[96];
    char effectSet[96];
    char nightModeSet[96];
    char nightModeState[96];
    char pirState[96];
    char availability[96];
    char glitchSet[96];
    char glitchState[96];
    char diag[96];  // outgoing only (v1.2.0): never routed back in

    void init(const char* base);  // snprintf each; base without trailing slash
};

// Classifies an incoming topic against `base`. Only exact `<base>/set` and
// `<base>/<sub>/set` (sub one of motion, makeup, effect, motion_disable, glitch) are
// recognised; anything else (foreign base, unknown sub, outgoing */state
// topics, prefix tricks) is Unknown.
Route routeTopic(const char* topic, const char* base);

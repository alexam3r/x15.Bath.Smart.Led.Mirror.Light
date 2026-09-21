// Task 2: raw (hardware-debounced) button level -> Click/Hold events
// (ARCHITECTURE.md 4.3 / 8.1, verbatim interface). No software debounce —
// the Schmitt-trigger input is already a clean logical level.
#pragma once

#include <cstdint>

enum class ButtonEventType : uint8_t { None, Click, HoldStart, HoldTick, HoldEnd };

struct ButtonEvent {
    ButtonEventType type   = ButtonEventType::None;
    uint8_t         clicks = 0;
};

class Button {
public:
    // pressed: current logical level (true = pressed). now: millis().
    // Returns at most one event per call.
    ButtonEvent update(bool pressed, uint32_t now);

private:
    bool     prevPressed_ = false;
    uint32_t pressStart_  = 0;
    bool     holding_     = false;
    uint32_t lastTick_    = 0;
    uint8_t  clickCount_  = 0;
    uint32_t lastRelease_ = 0;
};

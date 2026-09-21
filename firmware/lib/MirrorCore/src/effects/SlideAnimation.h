// Task 4 (Ruling R3): the power-on/off slide. Not an Effect (not in the
// registry) — it is the power animation, reversible mid-slide, and driven
// directly by Mirror (ARCHITECTURE.md section 7).
#pragma once

#include <cstdint>

#include "../ColorMath.h"
#include "../Config.h"

class Frame;

class SlideAnimation {
public:
    // center_ = center, radius_ = 0, turningOn_ = true.
    void startOn(uint16_t center);

    // turningOn_ = false; radius_ = SLIDE_MAX_RADIUS if it is outside
    // [0, SLIDE_MAX_RADIUS) (slide-on finished, or a slide-out already
    // ended), otherwise kept — including 0, so switching off before the
    // first slide-on step stays dark (Ruling R17; v27 jumped to max at 0).
    void startOff();

    // turningOn_ = true, radius_ kept (reversible mid slide-out).
    void reverseToOn();

    // Renders one frame for the current radius, advances radius. Returns
    // false once the just-rendered frame was the last one.
    bool step(Frame& out, Rgbw base);

    int16_t radius() const { return radius_; }
    bool turningOn() const { return turningOn_; }

    // Mirror's concern (step period), exposed for convenience.
    static constexpr uint16_t kStepMs = cfg::SLIDE_STEP_MS;

private:
    uint16_t center_    = cfg::CENTERS[0];
    int16_t  radius_    = 0;
    bool     turningOn_ = true;
};

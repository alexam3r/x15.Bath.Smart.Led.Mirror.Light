// Task 4: SlideAnimation implementation, ported from v27 processAnimation()
// MODE_ANIM_ON/MODE_ANIM_OFF branch and triggerPowerOff() (main.cpp @
// d4421dd, lines 946-1013 and 891-899). Brightness (globalScale) is not
// applied here — Mirror scales the whole frame later.
#include "SlideAnimation.h"

#include "../Frame.h"

void SlideAnimation::startOn(uint16_t center) {
    center_    = center;
    radius_    = 0;
    turningOn_ = true;
}

void SlideAnimation::startOff() {
    turningOn_ = false;
    // Ruling R17: only a finished slide-on (ON) or an already finished
    // slide-out restarts from the maximum radius. Radius 0 (slide-on has not
    // rendered a step yet) stays 0, so the slide-out stays dark and ends at
    // once — v27 jumped to the maximum here, flashing the whole ring.
    if (radius_ >= static_cast<int16_t>(cfg::SLIDE_MAX_RADIUS) || radius_ < 0) {
        radius_ = static_cast<int16_t>(cfg::SLIDE_MAX_RADIUS);
    }
}

void SlideAnimation::reverseToOn() { turningOn_ = true; }

bool SlideAnimation::step(Frame& out, Rgbw base) {
    out.clear();

    for (uint16_t i = 0; i < cfg::TOTAL_LEDS; ++i) {
        int dist = ringDist(i, center_);

        uint8_t edgeFade = 0;
        if (dist <= radius_) {
            edgeFade = (dist <= radius_ - static_cast<int16_t>(cfg::SLIDE_EDGE))
                           ? 255
                           : static_cast<uint8_t>((radius_ - dist) * 255 / cfg::SLIDE_EDGE);
        }
        if (edgeFade > 0) out[i] = scale(base, edgeFade);
    }

    radius_ += turningOn_ ? 1 : -1;

    bool finished = turningOn_ ? (radius_ >= static_cast<int16_t>(cfg::SLIDE_MAX_RADIUS))
                                : (radius_ < 0);
    if (finished && !turningOn_) out.clear();

    return !finished;
}

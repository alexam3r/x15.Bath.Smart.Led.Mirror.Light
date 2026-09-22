// Ramp — linear interpolation of a uint8_t value over a time span (v1.2.0),
// using (now - startMs) so it survives the 49.7-day millis() wraparound.
// value() retires the ramp (active = false) once the span has elapsed and
// holds `to` from then on, like Countdown::update() (Ruling R7).
#pragma once

#include <cstdint>

struct Ramp {
    uint8_t  from    = 0;
    uint8_t  to      = 0;
    uint32_t startMs = 0;
    uint32_t durMs   = 0;
    bool     active  = false;

    // A zero duration or no change leaves the ramp inactive at `t`.
    void start(uint8_t f, uint8_t t, uint32_t now, uint32_t dur) {
        from = f;
        to = t;
        startMs = now;
        durMs = dur;
        active = (dur > 0 && f != t);
        if (!active) from = t;
    }
    void set(uint8_t v) {
        from = to = v;
        active = false;
    }

    uint8_t value(uint32_t now) {
        if (!active) return to;
        const uint32_t elapsed = (uint32_t)(now - startMs);
        if (elapsed >= durMs) {
            active = false;
            return to;
        }
        return static_cast<uint8_t>(from + (static_cast<int32_t>(to) - from) * static_cast<int32_t>(elapsed) /
                                               static_cast<int32_t>(durMs));
    }
    bool running(uint32_t now) const { return active && (uint32_t)(now - startMs) < durMs; }
};

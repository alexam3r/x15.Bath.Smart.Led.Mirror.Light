// Flame (v1.4.0, replaces breathe): wide patches of the ring breathe on their
// own clocks — about 4 at a time, each a flat core of 10..12 pixels with a
// soft edge of 3..4 pixels each side (v1.4.1), the core dimming to 30 % (to
// the eye) and back over 3..5 s. Breathe dimmed the whole ring at once.
#pragma once

#include <cstdint>

#include "../Config.h"
#include "Effect.h"

class Flame : public Effect {
public:
    void begin(const EffectContext& ctx) override;
    uint16_t stepIntervalMs() const override;
    bool step(Frame& out, const EffectContext& ctx) override;

private:
    struct Patch {
        uint16_t start    = 0;  // first pixel, the patch runs clockwise
        uint8_t  core     = 0;  // flat core, pixels; the edge is cfg::flameEdge(core)
        uint8_t  duration = 0;  // steps; 0 = free slot
        uint8_t  age      = 0;
    };

    void spawn(const EffectContext& ctx);

    uint16_t step_ = 0;
    Patch    patches_[cfg::FLAME_MAX_PATCHES];
};

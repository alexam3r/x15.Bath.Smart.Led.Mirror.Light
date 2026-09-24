// Flame (v1.4.0, replaces breathe): wide patches of the ring breathe on their
// own clocks — about 4-5 at a time, each 10..15 pixels wide including a soft
// edge of FLAME_EDGE pixels each side, its flat core dimming to 60 % (to the
// eye) and back over 3..5 s. Breathe dimmed the whole ring at once.
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
        uint8_t  width    = 0;
        uint8_t  duration = 0;  // steps; 0 = free slot
        uint8_t  age      = 0;
    };

    void spawn(const EffectContext& ctx);

    uint16_t step_ = 0;
    Patch    patches_[cfg::FLAME_MAX_PATCHES];
};

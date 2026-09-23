// Embers (v1.3.0): the ring smoulders — about one spot per twelve pixels is
// a coal whose centre dims to 10..30 % (to the eye) and back over 1..2.5 s,
// with a soft edge of EMBERS_EDGE pixels each side (v1.3.1), every coal on its
// own clock. Replaces the
// v1.2.0 version, where all 168 pixels drifted a little at once and the eye
// saw only a faint ripple.
#pragma once

#include <cstdint>

#include "../Config.h"
#include "Effect.h"

class Embers : public Effect {
public:
    void begin(const EffectContext& ctx) override;
    uint16_t stepIntervalMs() const override;
    bool step(Frame& out, const EffectContext& ctx) override;

private:
    struct Coal {
        uint16_t centre   = 0;
        uint8_t  depth    = 0;  // 255 - floor, perceived
        uint8_t  duration = 0;  // steps; 0 = free slot
        uint8_t  age      = 0;
    };

    void spawn(const EffectContext& ctx);

    uint16_t step_ = 0;
    Coal     coals_[cfg::EMBERS_MAX_COALS];
};

// Task 4: the dark wave — two pulses (CW/CCW) start at a random CENTERS[]
// point and meet at the opposite side of the ring, then fade back to base
// (ARCHITECTURE.md section 4.2/7). Rule #6: where the two pulses overlap,
// darkness is the MAX of the two factors, never their sum.
#pragma once

#include <cstdint>

#include "Effect.h"

class Wave : public Effect {
public:
    void begin(const EffectContext& ctx) override;
    uint16_t stepIntervalMs() const override;
    bool step(Frame& out, const EffectContext& ctx) override;

private:
    uint16_t step_   = 0;
    uint16_t center_ = 0;
};

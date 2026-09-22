// Breathe (v1.2.0): the whole ring slowly dims to BREATHE_MIN_LEVEL and back,
// BREATHE_CYCLES times — a calm "breathing" effect over the current base.
#pragma once

#include <cstdint>

#include "Effect.h"

class Breathe : public Effect {
public:
    void begin(const EffectContext& ctx) override;
    uint16_t stepIntervalMs() const override;
    bool step(Frame& out, const EffectContext& ctx) override;

private:
    uint16_t step_ = 0;
};

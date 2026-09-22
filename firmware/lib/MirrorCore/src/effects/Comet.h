// Comet (v1.2.0): a bright white head with a fading tail flies once around
// the ring, from a random pixel and in a random direction. Pixels are lerped
// from the base towards white, so it adds light in solid and in makeup alike.
#pragma once

#include <cstdint>

#include "Effect.h"

class Comet : public Effect {
public:
    void begin(const EffectContext& ctx) override;
    uint16_t stepIntervalMs() const override;
    bool step(Frame& out, const EffectContext& ctx) override;

private:
    uint16_t step_      = 0;
    uint16_t startPos_  = 0;
    int8_t   direction_ = 1;
};

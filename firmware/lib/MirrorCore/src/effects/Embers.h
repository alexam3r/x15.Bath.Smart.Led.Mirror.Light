// Embers (v1.2.0): the ring smoulders — every pixel drifts between
// EMBERS_MIN_LEVEL and full brightness of the base colour at its own pace,
// like coals breathing under ash.
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
    uint16_t step_ = 0;
    uint8_t  level_[cfg::TOTAL_LEDS];
    uint8_t  target_[cfg::TOTAL_LEDS];
};

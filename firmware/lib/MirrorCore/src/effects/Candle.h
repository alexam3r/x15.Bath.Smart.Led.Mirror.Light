// Candle (v1.2.0): the whole ring trembles like a candle flame — mostly
// between CANDLE_MIN_LEVEL and full, with rare dips towards CANDLE_DIP_LEVEL.
#pragma once

#include <cstdint>

#include "Effect.h"

class Candle : public Effect {
public:
    void begin(const EffectContext& ctx) override;
    uint16_t stepIntervalMs() const override;
    bool step(Frame& out, const EffectContext& ctx) override;

private:
    uint16_t step_   = 0;
    uint8_t  level_  = 255;
    uint8_t  target_ = 255;
};

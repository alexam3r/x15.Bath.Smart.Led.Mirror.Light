// Task 4: the snake effects — a 60-LED trail with a 4-LED solid head and a
// 30-step fade-in/out, moving once around the 168-LED ring (ARCHITECTURE.md
// section 4.2/7). SnakeBase owns the shared step/alpha/head/dist geometry;
// DarkSnake and RainbowSnake differ only in how they color a pixel.
#pragma once

#include <cstdint>

#include "../ColorMath.h"
#include "Effect.h"

class SnakeBase : public Effect {
public:
    void begin(const EffectContext& ctx) override;
    uint16_t stepIntervalMs() const override;
    bool step(Frame& out, const EffectContext& ctx) override;

protected:
    // dist in [0, TOTAL_LEDS): directed distance from the head (see v27
    // processAnimation MODE_EFFECT_SNAKE). alpha in [0, 1]: fade-in/out.
    virtual Rgbw renderPixel(Rgbw base, uint16_t dist, float alpha) const = 0;

    uint16_t step_     = 0;
    uint16_t startPos_ = 0;
    int8_t   direction_ = 1;
};

class DarkSnake : public SnakeBase {
protected:
    Rgbw renderPixel(Rgbw base, uint16_t dist, float alpha) const override;
};

class RainbowSnake : public SnakeBase {
protected:
    Rgbw renderPixel(Rgbw base, uint16_t dist, float alpha) const override;
};

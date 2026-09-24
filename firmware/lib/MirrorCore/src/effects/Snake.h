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

// v1.4.2: every start shuffles the six key colours (red, yellow, green,
// cyan, blue, magenta; v27 always ran them in that order from the head).
// Between two keys the hue runs the short way round the wheel, and the tail
// runs back to the head colour. A RandomFn returning 0 keeps the v27 order.
class RainbowSnake : public SnakeBase {
public:
    void begin(const EffectContext& ctx) override;

protected:
    Rgbw renderPixel(Rgbw base, uint16_t dist, float alpha) const override;

private:
    static constexpr uint8_t kColours = 6;
    uint8_t order_[kColours] = {0, 1, 2, 3, 4, 5};  // key colours from the head
};

// Task 4: Snake implementation, ported from v27 processAnimation()
// MODE_EFFECT_SNAKE branch (main.cpp @ d4421dd, lines 1019-1074).
// Brightness (globalScale) is not applied here — Mirror scales the whole
// frame later.
#include "Snake.h"

#include "../Config.h"
#include "../Frame.h"

namespace {
constexpr uint16_t kTotalSteps = cfg::TOTAL_LEDS + cfg::SNAKE_SIZE;  // 228
}  // namespace

void SnakeBase::begin(const EffectContext& ctx) {
    startPos_  = static_cast<uint16_t>(ctx.random(cfg::TOTAL_LEDS));
    direction_ = (ctx.random(2) == 0) ? 1 : -1;
    step_      = 0;
}

uint16_t SnakeBase::stepIntervalMs() const { return static_cast<uint16_t>(cfg::SNAKE_STEP_MS); }

bool SnakeBase::step(Frame& out, const EffectContext& ctx) {
    float alpha = 1.0f;
    if (step_ < cfg::SNAKE_FADE_STEPS) {
        alpha = static_cast<float>(step_) / static_cast<float>(cfg::SNAKE_FADE_STEPS);
    } else if (step_ > kTotalSteps - cfg::SNAKE_FADE_STEPS) {
        alpha = static_cast<float>(kTotalSteps - step_) / static_cast<float>(cfg::SNAKE_FADE_STEPS);
    }
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;

    int head = static_cast<int>(startPos_) + static_cast<int>(step_) * direction_;
    while (head < 0) head += cfg::TOTAL_LEDS;
    while (head >= static_cast<int>(cfg::TOTAL_LEDS)) head -= cfg::TOTAL_LEDS;

    for (uint16_t i = 0; i < cfg::TOTAL_LEDS; ++i) {
        int dist = (direction_ == 1) ? (head - static_cast<int>(i)) : (static_cast<int>(i) - head);
        if (dist < 0) dist += cfg::TOTAL_LEDS;
        out[i] = renderPixel(ctx.base, static_cast<uint16_t>(dist), alpha);
    }

    ++step_;
    return step_ <= kTotalSteps;
}

Rgbw DarkSnake::renderPixel(Rgbw base, uint16_t dist, float alpha) const {
    float snakeFactor = 1.0f;
    if (dist < cfg::SNAKE_SIZE) {
        snakeFactor = (dist < cfg::SNAKE_HEAD_SOLID)
                          ? 0.0f
                          : static_cast<float>(dist - cfg::SNAKE_HEAD_SOLID) /
                                static_cast<float>(cfg::SNAKE_SIZE - cfg::SNAKE_HEAD_SOLID);
    }
    uint8_t res = static_cast<uint8_t>((1.0f - alpha * (1.0f - snakeFactor)) * 255.0f);
    return scale(base, res);
}

Rgbw RainbowSnake::renderPixel(Rgbw base, uint16_t dist, float alpha) const {
    if (dist >= cfg::SNAKE_SIZE) return base;

    uint16_t hue = static_cast<uint16_t>((static_cast<long>(dist) * 65535L) / cfg::SNAKE_SIZE);
    Rgbw c = hsv(hue);
    uint8_t a = static_cast<uint8_t>(alpha * 255.0f);

    Rgbw out;
    out.r = static_cast<uint8_t>(scale8(c.r, a) + scale8(base.r, 255 - a));
    out.g = static_cast<uint8_t>(scale8(c.g, a) + scale8(base.g, 255 - a));
    out.b = static_cast<uint8_t>(scale8(c.b, a) + scale8(base.b, 255 - a));
    out.w = scale8(base.w, 255 - a);
    return out;
}

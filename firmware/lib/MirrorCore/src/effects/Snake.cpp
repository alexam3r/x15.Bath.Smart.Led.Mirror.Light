// Task 4: Snake implementation, ported from v27 processAnimation()
// MODE_EFFECT_SNAKE branch (main.cpp @ d4421dd, lines 1019-1074).
// Brightness (globalScale) is not applied here — Mirror scales the whole
// frame later.
#include "Snake.h"

#include "../Config.h"
#include "../Frame.h"

namespace {
constexpr uint16_t kTotalSteps = cfg::TOTAL_LEDS + cfg::SNAKE_SIZE;  // 228
// The rainbow wheel in pixels: key colour c sits at c * kKeyGap, and a hue
// on the wheel is wheel * 65535 / SNAKE_SIZE, the v27 formula.
constexpr uint16_t kKeyGap = cfg::SNAKE_SIZE / 6;  // 10
static_assert(cfg::SNAKE_SIZE % 6 == 0, "six key colours, evenly spaced");
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

void RainbowSnake::begin(const EffectContext& ctx) {
    SnakeBase::begin(ctx);
    // Fisher-Yates, every order equally likely; a zero draw leaves an entry
    // in place, so a RandomFn returning 0 keeps the v27 order.
    for (uint8_t i = 0; i < kColours; ++i) order_[i] = i;
    for (uint8_t i = kColours - 1; i > 0; --i) {
        const uint8_t j = static_cast<uint8_t>(i - ctx.random(i + 1));
        const uint8_t t = order_[i];
        order_[i] = order_[j];
        order_[j] = t;
    }
}

Rgbw RainbowSnake::renderPixel(Rgbw base, uint16_t dist, float alpha) const {
    if (dist >= cfg::SNAKE_SIZE) return base;

    // From key colour order_[k] to the next one (the head colour after the
    // last), the short way round: -2..+3 wheel pixels per pixel (+3 for the
    // opposite colour). In the v27 order it is +1, i.e. wheel == dist.
    const uint8_t k = static_cast<uint8_t>(dist / kKeyGap);
    const uint8_t from = order_[k];
    const uint8_t to = order_[(k + 1) % kColours];
    int steps = (to - from + kColours) % kColours;  // 1..5
    if (steps > kColours / 2) steps -= kColours;
    int wheel = from * kKeyGap + (dist % kKeyGap) * steps;
    wheel = (wheel + cfg::SNAKE_SIZE) % cfg::SNAKE_SIZE;
    uint16_t hue = static_cast<uint16_t>((static_cast<long>(wheel) * 65535L) / cfg::SNAKE_SIZE);
    Rgbw c = hsv(hue);
    uint8_t a = static_cast<uint8_t>(alpha * 255.0f);

    Rgbw out;
    out.r = static_cast<uint8_t>(scale8(c.r, a) + scale8(base.r, 255 - a));
    out.g = static_cast<uint8_t>(scale8(c.g, a) + scale8(base.g, 255 - a));
    out.b = static_cast<uint8_t>(scale8(c.b, a) + scale8(base.b, 255 - a));
    out.w = scale8(base.w, 255 - a);
    return out;
}

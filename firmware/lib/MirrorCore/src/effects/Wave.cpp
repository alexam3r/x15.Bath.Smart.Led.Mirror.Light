// Task 4: Wave implementation, ported from v27 processAnimation()
// MODE_EFFECT_WAVE branch and getDarkFactor() (main.cpp @ d4421dd, lines
// 1076-1136 and 1169-1175). Brightness (globalScale) is not applied here —
// Mirror scales the whole frame later.
#include "Wave.h"

#include "../Config.h"
#include "../Frame.h"

namespace {

constexpr int kLimit = cfg::TOTAL_LEDS / 2;  // 84

float darkFactor(uint16_t i, uint16_t center) {
    uint16_t d = ringDist(i, center);
    if (d <= cfg::WAVE_CORE) return 1.0f;
    if (d <= cfg::WAVE_RADIUS) {
        return 1.0f - static_cast<float>(d - cfg::WAVE_CORE) /
                           static_cast<float>(cfg::WAVE_RADIUS - cfg::WAVE_CORE);
    }
    return 0.0f;
}

}  // namespace

void Wave::begin(const EffectContext& ctx) {
    center_ = cfg::CENTERS[ctx.random(cfg::CENTERS_CNT)];
    step_   = 0;
}

uint16_t Wave::stepIntervalMs() const { return static_cast<uint16_t>(cfg::WAVE_STEP_MS); }

bool Wave::step(Frame& out, const EffectContext& ctx) {
    int eff = (step_ < kLimit) ? static_cast<int>(step_) : kLimit;

    float fadeOut = 1.0f;
    if (step_ > kLimit) {
        fadeOut = 1.0f - static_cast<float>(step_ - kLimit) / static_cast<float>(cfg::WAVE_RADIUS);
        if (fadeOut < 0.0f) fadeOut = 0.0f;
    }

    int cw = static_cast<int>(center_) + eff;
    while (cw >= static_cast<int>(cfg::TOTAL_LEDS)) cw -= cfg::TOTAL_LEDS;
    int ccw = static_cast<int>(center_) - eff;
    while (ccw < 0) ccw += cfg::TOTAL_LEDS;

    for (uint16_t i = 0; i < cfg::TOTAL_LEDS; ++i) {
        float darkCw  = darkFactor(i, static_cast<uint16_t>(cw));
        float darkCcw = darkFactor(i, static_cast<uint16_t>(ccw));
        // Rule #6: MAX, never sum.
        float dark = (darkCw > darkCcw ? darkCw : darkCcw) * fadeOut;
        uint8_t bm = static_cast<uint8_t>((1.0f - dark) * 255.0f);
        out[i] = scale(ctx.base, bm);
    }

    ++step_;
    return step_ <= (kLimit + cfg::WAVE_RADIUS);
}

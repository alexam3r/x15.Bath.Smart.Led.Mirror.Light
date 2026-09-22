#include "Embers.h"

#include "../ColorMath.h"
#include "../Frame.h"

namespace {

// Weight of the effect over the plain base for this step, 0..255: ramps up
// over the first `fade` steps and back down over the last `fade`.
uint8_t fadeAlpha(uint16_t step, uint16_t total, uint16_t fade) {
    if (step < fade) return static_cast<uint8_t>(step * 255 / fade);
    if (step > total - fade) return static_cast<uint8_t>((total - step) * 255 / fade);
    return 255;
}

// Moves `level` towards `target` by at most EMBERS_SLEW.
uint8_t slew(uint8_t level, uint8_t target) {
    if (level < target) {
        const uint8_t gap = static_cast<uint8_t>(target - level);
        return static_cast<uint8_t>(level + (gap < cfg::EMBERS_SLEW ? gap : cfg::EMBERS_SLEW));
    }
    const uint8_t gap = static_cast<uint8_t>(level - target);
    return static_cast<uint8_t>(level - (gap < cfg::EMBERS_SLEW ? gap : cfg::EMBERS_SLEW));
}

}  // namespace

void Embers::begin(const EffectContext&) {
    step_ = 0;
    for (uint16_t i = 0; i < cfg::TOTAL_LEDS; ++i) level_[i] = target_[i] = 255;
}

uint16_t Embers::stepIntervalMs() const { return static_cast<uint16_t>(cfg::EMBERS_STEP_MS); }

bool Embers::step(Frame& out, const EffectContext& ctx) {
    const uint8_t alpha = fadeAlpha(step_, cfg::EMBERS_STEPS, cfg::EMBERS_FADE_STEPS);

    for (uint8_t n = 0; n < cfg::EMBERS_CHANGES_PER_STEP; ++n) {
        const uint16_t i = static_cast<uint16_t>(ctx.random(cfg::TOTAL_LEDS));
        target_[i] = static_cast<uint8_t>(cfg::EMBERS_MIN_LEVEL +
                                          ctx.random(255 - cfg::EMBERS_MIN_LEVEL + 1));
    }
    for (uint16_t i = 0; i < cfg::TOTAL_LEDS; ++i) {
        level_[i] = slew(level_[i], target_[i]);
        out[i] = scale(ctx.base, lerp8(255, level_[i], alpha));
    }
    ++step_;
    return step_ < cfg::EMBERS_STEPS;
}

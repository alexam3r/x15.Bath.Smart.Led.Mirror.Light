#include "Embers.h"

#include "../ColorMath.h"
#include "../Frame.h"

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
        level_[i] = slewTowards(level_[i], target_[i], cfg::EMBERS_SLEW);
        out[i] = scale(ctx.base, lerp8(255, level_[i], alpha));
    }
    ++step_;
    return step_ < cfg::EMBERS_STEPS;
}

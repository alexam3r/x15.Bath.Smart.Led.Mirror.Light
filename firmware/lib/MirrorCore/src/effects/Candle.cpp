#include "Candle.h"

#include "../ColorMath.h"
#include "../Config.h"
#include "../Frame.h"

void Candle::begin(const EffectContext&) {
    step_ = 0;
    level_ = target_ = 255;
}

uint16_t Candle::stepIntervalMs() const { return static_cast<uint16_t>(cfg::CANDLE_STEP_MS); }

bool Candle::step(Frame& out, const EffectContext& ctx) {
    const uint8_t alpha = fadeAlpha(step_, cfg::CANDLE_STEPS, cfg::CANDLE_FADE_STEPS);

    if (step_ % cfg::CANDLE_RETARGET_STEPS == 0) {
        if (ctx.random(cfg::CANDLE_DIP_CHANCE) == 0) {  // a draught: the flame ducks
            target_ = static_cast<uint8_t>(cfg::CANDLE_DIP_LEVEL +
                                           ctx.random(cfg::CANDLE_MIN_LEVEL - cfg::CANDLE_DIP_LEVEL));
        } else {
            target_ = static_cast<uint8_t>(cfg::CANDLE_MIN_LEVEL +
                                           ctx.random(255 - cfg::CANDLE_MIN_LEVEL + 1));
        }
    }
    level_ = slewTowards(level_, target_, cfg::CANDLE_SLEW);
    out.fill(scale(ctx.base, lerp8(255, level_, alpha)));
    ++step_;
    return step_ < cfg::CANDLE_STEPS;
}

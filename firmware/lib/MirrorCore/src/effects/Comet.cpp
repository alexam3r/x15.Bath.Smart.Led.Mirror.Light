#include "Comet.h"

#include "../ColorMath.h"
#include "../Config.h"
#include "../Frame.h"

namespace {
constexpr uint16_t kTotalSteps = cfg::TOTAL_LEDS + cfg::COMET_TAIL;
constexpr Rgbw kWhite{255, 255, 255, 255};
}  // namespace

void Comet::begin(const EffectContext& ctx) {
    startPos_  = static_cast<uint16_t>(ctx.random(cfg::TOTAL_LEDS));
    direction_ = (ctx.random(2) == 0) ? 1 : -1;
    step_      = 0;
}

uint16_t Comet::stepIntervalMs() const { return static_cast<uint16_t>(cfg::COMET_STEP_MS); }

bool Comet::step(Frame& out, const EffectContext& ctx) {
    const uint8_t alpha = fadeAlpha(step_, kTotalSteps, cfg::COMET_FADE_STEPS);

    int head = static_cast<int>(startPos_) + static_cast<int>(step_) * direction_;
    while (head < 0) head += cfg::TOTAL_LEDS;
    while (head >= static_cast<int>(cfg::TOTAL_LEDS)) head -= cfg::TOTAL_LEDS;

    for (uint16_t i = 0; i < cfg::TOTAL_LEDS; ++i) {
        // Distance behind the head along the direction of travel.
        int dist = (direction_ == 1) ? (head - static_cast<int>(i)) : (static_cast<int>(i) - head);
        if (dist < 0) dist += cfg::TOTAL_LEDS;
        if (dist >= static_cast<int>(cfg::COMET_TAIL)) {
            out[i] = ctx.base;
            continue;
        }
        const uint16_t back = static_cast<uint16_t>(cfg::COMET_TAIL - dist);  // TAIL at the head, 1 at the end
        // Linear in perceived brightness (CIE 1931, v1.3.0): the tail fades
        // evenly to the eye; the quadratic PWM curve before was a hand-made
        // approximation of the same thing.
        const uint8_t t = static_cast<uint8_t>(255UL * back / cfg::COMET_TAIL);
        out[i] = lerpPerceptual(ctx.base, kWhite, scale8(t, alpha));
    }
    ++step_;
    return step_ <= kTotalSteps;
}

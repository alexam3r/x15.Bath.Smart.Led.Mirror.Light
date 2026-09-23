#include "Breathe.h"

#include <cmath>

#include "../ColorMath.h"
#include "../Config.h"
#include "../Frame.h"

namespace {
constexpr uint16_t kTotalSteps = cfg::BREATHE_CYCLE_STEPS * cfg::BREATHE_CYCLES;
constexpr float kTwoPi = 6.28318530718f;
}  // namespace

void Breathe::begin(const EffectContext&) { step_ = 0; }

uint16_t Breathe::stepIntervalMs() const { return static_cast<uint16_t>(cfg::BREATHE_STEP_MS); }

bool Breathe::step(Frame& out, const EffectContext& ctx) {
    const float phase = kTwoPi * static_cast<float>(step_ % cfg::BREATHE_CYCLE_STEPS) /
                        static_cast<float>(cfg::BREATHE_CYCLE_STEPS);
    const float k = (1.0f + std::cos(phase)) * 0.5f;  // 1 at the cycle start, 0 halfway
    const uint8_t level =
        static_cast<uint8_t>(cfg::BREATHE_MIN_LEVEL + (255 - cfg::BREATHE_MIN_LEVEL) * k + 0.5f);
    out.fill(scale(ctx.base, cie8(level)));  // the curve is in perceived brightness (v1.3.0)
    ++step_;
    return step_ < kTotalSteps;
}

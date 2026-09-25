#include "Blink.h"

#include <cmath>

namespace {
constexpr float kTwoPi = 6.28318530718f;
}  // namespace

void Blink::start(uint8_t count, uint32_t flashMs, uint32_t gapMs, uint8_t peak, uint32_t now) {
    start_ = now;
    flashMs_ = flashMs;
    gapMs_ = gapMs;
    count_ = flashMs == 0 ? 0 : count;
    peak_ = peak;
}

uint32_t Blink::durationMs() const {
    return count_ == 0 ? 0 : count_ * flashMs_ + (count_ - 1u) * gapMs_;
}

bool Blink::active(uint32_t now) const { return count_ != 0 && (uint32_t)(now - start_) < durationMs(); }

uint8_t Blink::level(uint32_t now) const {
    if (!active(now)) return 0;
    const uint32_t into = (uint32_t)(now - start_) % (flashMs_ + gapMs_);
    if (into >= flashMs_) return 0;  // the dark gap between two flashes
    const float shape = (1.0f - std::cos(kTwoPi * into / flashMs_)) * 0.5f;  // 0 -> 1 -> 0
    return static_cast<uint8_t>(peak_ * shape + 0.5f);
}

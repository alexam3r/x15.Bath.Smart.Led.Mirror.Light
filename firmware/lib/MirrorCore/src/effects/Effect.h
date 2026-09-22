// Task 4: effect interface (ARCHITECTURE.md section 7, verbatim). A temporary
// effect draws over the current base color each step and reports when it is
// finished; Mirror (Task 5) owns the state machine that drives it.
#pragma once

#include <cstdint>

#include "../ColorMath.h"

class Frame;

// Uniform in [0, bound). On hardware: esp_random(); tests use a deterministic
// stand-in so golden frames and coverage are reproducible.
using RandomFn = uint32_t (*)(uint32_t bound);

struct EffectContext {
    Rgbw     base;    // base color without brightness: Solid -> {r,g,b,0}, Makeup -> {0,0,0,255}
    RandomFn random;
};

// Weight of an effect over the plain base for this step, 0..255: ramps up
// over the first `fade` steps and back down over the last `fade` (v1.2.0,
// shared by Embers and Candle).
inline uint8_t fadeAlpha(uint16_t step, uint16_t total, uint16_t fade) {
    if (step < fade) return static_cast<uint8_t>(step * 255 / fade);
    if (step > total - fade) return static_cast<uint8_t>((total - step) * 255 / fade);
    return 255;
}

// Moves `level` towards `target` by at most `maxStep` (v1.2.0).
inline uint8_t slewTowards(uint8_t level, uint8_t target, uint8_t maxStep) {
    if (level < target) {
        const uint8_t gap = static_cast<uint8_t>(target - level);
        return static_cast<uint8_t>(level + (gap < maxStep ? gap : maxStep));
    }
    const uint8_t gap = static_cast<uint8_t>(level - target);
    return static_cast<uint8_t>(level - (gap < maxStep ? gap : maxStep));
}

class Effect {
public:
    virtual ~Effect() = default;

    // Reset step counter and any random parameters for a fresh run.
    virtual void begin(const EffectContext& ctx) = 0;

    // Step period in ms.
    virtual uint16_t stepIntervalMs() const = 0;

    // Render one frame for the current step, advance the step. Returns
    // false once the just-rendered frame was the effect's last one.
    virtual bool step(Frame& out, const EffectContext& ctx) = 0;
};

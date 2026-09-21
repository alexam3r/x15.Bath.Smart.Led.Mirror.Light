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

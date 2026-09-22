// GlitchOverlay — "neon failure" glitch (v1.1.0). A random run of
// GLITCH_LEN_MIN..MAX adjacent ring pixels flickers between off, dim and full
// in the base colour, like a failing neon tube, for
// GLITCH_DURATION_MIN..MAX ms. Not an Effect: it never enters the registry,
// effect_list or `random`, and never changes the reported state — Mirror
// decides when it runs (see Mirror::tick).
#pragma once

#include <cstdint>

#include "ColorMath.h"
#include "Frame.h"
#include "effects/Effect.h"  // RandomFn

// Brightness levels the whole segment jumps between on each step (scale8
// factor of the base colour): mostly off or barely glowing, sometimes full.
constexpr uint8_t kGlitchLevelCount = 6;
extern const uint8_t kGlitchLevels[kGlitchLevelCount];

class GlitchOverlay {
public:
    // Picks the segment (any start on the ring, may wrap past its end),
    // its length and the duration.
    void start(RandomFn rnd, uint32_t now);
    void cancel() { active_ = false; }
    bool active() const { return active_; }

    // Renders one frame at `now`: base everywhere and the segment at a random
    // neon level. Once the duration is over it renders plain base,
    // deactivates and returns false.
    bool step(Frame& out, Rgbw base, RandomFn rnd, uint32_t now);

    uint16_t first() const { return first_; }
    uint8_t length() const { return length_; }
    uint32_t durationMs() const { return durationMs_; }

private:
    bool     active_     = false;
    uint16_t first_      = 0;
    uint8_t  length_     = 0;
    uint32_t startMs_    = 0;
    uint32_t durationMs_ = 0;
};

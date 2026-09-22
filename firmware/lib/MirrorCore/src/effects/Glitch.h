// GlitchOverlay — "neon failure" glitch (v1.1.0). A random core of
// GLITCH_LEN_MIN..MAX adjacent ring pixels flickers between off, dim and full
// in the base colour, like a failing neon tube, for
// GLITCH_DURATION_MIN..MAX ms. v1.1.1: GLITCH_EDGE_MIN..MAX pixels on each
// side fade linearly from the core's current level back to the plain base. Not an Effect: it never enters the registry,
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
    // Picks the core (any start on the ring, may wrap past its end), its
    // length, the edge width (same on both sides) and the duration.
    void start(RandomFn rnd, uint32_t now);
    void cancel() { active_ = false; }
    bool active() const { return active_; }

    // Renders one frame at `now`: base everywhere, the core at a random neon
    // level and each edge pixel k (1 = next to the core) at
    // level + (255 - level) * k / (edge + 1). Once the duration is over it
    // renders plain base, deactivates and returns false.
    bool step(Frame& out, Rgbw base, RandomFn rnd, uint32_t now);

    uint16_t first() const { return first_; }
    uint8_t length() const { return length_; }
    uint8_t edge() const { return edge_; }
    uint32_t durationMs() const { return durationMs_; }

private:
    bool     active_     = false;
    uint16_t first_      = 0;
    uint8_t  length_     = 0;
    uint8_t  edge_       = 0;
    uint32_t startMs_    = 0;
    uint32_t durationMs_ = 0;
};

#include "Glitch.h"

#include "Config.h"

const uint8_t kGlitchLevels[kGlitchLevelCount] = {0, 0, 10, 25, 60, 255};

void GlitchOverlay::start(RandomFn rnd, uint32_t now) {
    first_ = static_cast<uint16_t>(rnd(cfg::TOTAL_LEDS));
    length_ = static_cast<uint8_t>(cfg::GLITCH_LEN_MIN + rnd(cfg::GLITCH_LEN_MAX - cfg::GLITCH_LEN_MIN + 1));
    durationMs_ = cfg::GLITCH_DURATION_MIN_MS +
                  rnd(cfg::GLITCH_DURATION_MAX_MS - cfg::GLITCH_DURATION_MIN_MS + 1);
    startMs_ = now;
    active_ = true;
}

bool GlitchOverlay::step(Frame& out, Rgbw base, RandomFn rnd, uint32_t now) {
    out.fill(base);
    if (!active_ || (uint32_t)(now - startMs_) >= durationMs_) {
        active_ = false;
        return false;
    }
    const Rgbw lit = scale(base, kGlitchLevels[rnd(kGlitchLevelCount)]);
    for (uint8_t k = 0; k < length_; ++k) {
        out[(first_ + k) % cfg::TOTAL_LEDS] = lit;
    }
    return true;
}

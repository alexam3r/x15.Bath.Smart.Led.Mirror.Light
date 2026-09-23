#include "Glitch.h"

#include "Config.h"

const uint8_t kGlitchLevels[kGlitchLevelCount] = {0, 0, 10, 25, 60, 255};

void GlitchOverlay::start(RandomFn rnd, uint32_t now) {
    first_ = static_cast<uint16_t>(rnd(cfg::TOTAL_LEDS));
    length_ = static_cast<uint8_t>(cfg::GLITCH_LEN_MIN + rnd(cfg::GLITCH_LEN_MAX - cfg::GLITCH_LEN_MIN + 1));
    edge_ = static_cast<uint8_t>(cfg::GLITCH_EDGE_MIN + rnd(cfg::GLITCH_EDGE_MAX - cfg::GLITCH_EDGE_MIN + 1));
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
    const uint8_t level = kGlitchLevels[rnd(kGlitchLevelCount)];
    const Rgbw lit = scale(base, level);
    for (uint8_t k = 0; k < length_; ++k) {
        out[(first_ + k) % cfg::TOTAL_LEDS] = lit;
    }
    const uint16_t last = static_cast<uint16_t>(first_ + length_ - 1);
    // Edge: even steps of perceived brightness (CIE 1931, v1.3.0) from the
    // core's level up to full — a linear-PWM edge looked nearly full right
    // after the dark core. The core itself keeps its PWM level.
    const uint8_t core = lightness8(level);
    for (uint8_t k = 1; k <= edge_; ++k) {
        const Rgbw fade = scale(base, cie8(static_cast<uint8_t>(core + (255 - core) * k / (edge_ + 1))));
        out[(first_ + cfg::TOTAL_LEDS - k) % cfg::TOTAL_LEDS] = fade;  // left, wraps below 0
        out[(last + k) % cfg::TOTAL_LEDS] = fade;                       // right
    }
    return true;
}

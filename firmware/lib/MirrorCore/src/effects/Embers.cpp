#include "Embers.h"

#include <cmath>

#include "../ColorMath.h"
#include "../Frame.h"

namespace {
constexpr float kTwoPi = 6.28318530718f;
constexpr uint16_t kLastSpawnStep = cfg::EMBERS_STEPS - cfg::EMBERS_DIP_MAX_STEPS - 1;
}  // namespace

void Embers::begin(const EffectContext&) {
    step_ = 0;
    for (Coal& c : coals_) c = Coal{};
}

uint16_t Embers::stepIntervalMs() const { return static_cast<uint16_t>(cfg::EMBERS_STEP_MS); }

// Lights at most one new coal, at a random pixel clear of the others.
void Embers::spawn(const EffectContext& ctx) {
    if (ctx.random(1000) >= cfg::EMBERS_SPAWN_PER_MILLE) return;
    Coal* slot = nullptr;
    for (Coal& c : coals_) {
        if (c.duration == 0) {
            slot = &c;
            break;
        }
    }
    if (slot == nullptr) return;

    const uint16_t centre = static_cast<uint16_t>(ctx.random(cfg::TOTAL_LEDS));
    for (const Coal& c : coals_) {
        if (c.duration != 0 && ringDist(c.centre, centre) < cfg::EMBERS_MIN_GAP) return;  // too close
    }
    const uint8_t floor = static_cast<uint8_t>(
        cfg::EMBERS_FLOOR_MIN + ctx.random(cfg::EMBERS_FLOOR_MAX - cfg::EMBERS_FLOOR_MIN + 1));
    slot->centre = centre;
    slot->depth = static_cast<uint8_t>(255 - floor);
    slot->duration = static_cast<uint8_t>(
        cfg::EMBERS_DIP_MIN_STEPS + ctx.random(cfg::EMBERS_DIP_MAX_STEPS - cfg::EMBERS_DIP_MIN_STEPS + 1));
    slot->age = 0;
}

bool Embers::step(Frame& out, const EffectContext& ctx) {
    if (step_ <= kLastSpawnStep) spawn(ctx);

    // Perceived brightness per pixel: the deepest dip wins where coals meet.
    uint8_t level[cfg::TOTAL_LEDS];
    for (uint16_t i = 0; i < cfg::TOTAL_LEDS; ++i) level[i] = 255;
    for (Coal& c : coals_) {
        if (c.duration == 0) continue;
        const float shape = (1.0f - std::cos(kTwoPi * c.age / c.duration)) * 0.5f;  // 0 -> 1 -> 0
        const float dip = c.depth * shape;
        // Centre at full depth, the soft edge falling off in even perceived
        // steps: pixel k out of EMBERS_EDGE gets (EDGE + 1 - k) / (EDGE + 1).
        for (int k = -static_cast<int>(cfg::EMBERS_EDGE); k <= static_cast<int>(cfg::EMBERS_EDGE); ++k) {
            const int off = k < 0 ? -k : k;
            const float share = static_cast<float>(cfg::EMBERS_EDGE + 1 - off) / (cfg::EMBERS_EDGE + 1);
            const uint8_t l = static_cast<uint8_t>(255 - static_cast<int>(dip * share + 0.5f));
            const uint16_t i = static_cast<uint16_t>((c.centre + cfg::TOTAL_LEDS + k) % cfg::TOTAL_LEDS);
            if (l < level[i]) level[i] = l;
        }
        if (++c.age > c.duration) c.duration = 0;  // rendered ages 0..duration: back at base
    }
    for (uint16_t i = 0; i < cfg::TOTAL_LEDS; ++i) out[i] = scale(ctx.base, cie8(level[i]));

    ++step_;
    return step_ < cfg::EMBERS_STEPS;
}

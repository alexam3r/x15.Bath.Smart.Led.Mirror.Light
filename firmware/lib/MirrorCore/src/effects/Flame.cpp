#include "Flame.h"

#include <cmath>

#include "../ColorMath.h"
#include "../Frame.h"

namespace {
constexpr float kTwoPi = 6.28318530718f;

uint8_t widthOf(uint8_t core) { return static_cast<uint8_t>(core + 2 * cfg::flameEdge(core)); }
constexpr uint8_t kDepth = 255 - cfg::FLAME_FLOOR;
constexpr uint16_t kLastSpawnStep = cfg::FLAME_STEPS - cfg::FLAME_DIP_MAX_STEPS - 1;
}  // namespace

void Flame::begin(const EffectContext&) {
    step_ = 0;
    for (Patch& p : patches_) p = Patch{};
}

uint16_t Flame::stepIntervalMs() const { return static_cast<uint16_t>(cfg::FLAME_STEP_MS); }

// Starts at most one new patch, at a random place FLAME_MIN_GAP pixels clear
// of the others.
void Flame::spawn(const EffectContext& ctx) {
    if (ctx.random(1000) >= cfg::FLAME_SPAWN_PER_MILLE) return;
    Patch* slot = nullptr;
    for (Patch& p : patches_) {
        if (p.duration == 0) {
            slot = &p;
            break;
        }
    }
    if (slot == nullptr) return;

    const uint16_t start = static_cast<uint16_t>(ctx.random(cfg::TOTAL_LEDS));
    const uint8_t core = static_cast<uint8_t>(
        cfg::FLAME_CORE_MIN + ctx.random(cfg::FLAME_CORE_MAX - cfg::FLAME_CORE_MIN + 1));
    const uint8_t width = widthOf(core);
    // Pixels taken by the live patches, each widened by the gap on both sides.
    bool taken[cfg::TOTAL_LEDS] = {false};
    for (const Patch& p : patches_) {
        if (p.duration == 0) continue;
        for (int k = -static_cast<int>(cfg::FLAME_MIN_GAP); k < widthOf(p.core) + cfg::FLAME_MIN_GAP; ++k) {
            taken[(p.start + cfg::TOTAL_LEDS + k) % cfg::TOTAL_LEDS] = true;
        }
    }
    for (uint8_t k = 0; k < width; ++k) {
        if (taken[(start + k) % cfg::TOTAL_LEDS]) return;  // too close
    }
    slot->start = start;
    slot->core = core;
    slot->duration = static_cast<uint8_t>(
        cfg::FLAME_DIP_MIN_STEPS + ctx.random(cfg::FLAME_DIP_MAX_STEPS - cfg::FLAME_DIP_MIN_STEPS + 1));
    slot->age = 0;
}

bool Flame::step(Frame& out, const EffectContext& ctx) {
    if (step_ <= kLastSpawnStep) spawn(ctx);

    // Perceived brightness per pixel; patches never overlap.
    uint8_t level[cfg::TOTAL_LEDS];
    for (uint16_t i = 0; i < cfg::TOTAL_LEDS; ++i) level[i] = 255;
    for (Patch& p : patches_) {
        if (p.duration == 0) continue;
        const float shape = (1.0f - std::cos(kTwoPi * p.age / p.duration)) * 0.5f;  // 0 -> 1 -> 0
        const float dip = kDepth * shape;
        // A flat core at full depth; the soft edge falls off in even
        // perceived steps: pixel k from the outside gets (k + 1) / (edge + 1).
        const uint8_t edge = cfg::flameEdge(p.core);
        const uint8_t width = widthOf(p.core);
        for (uint8_t j = 0; j < width; ++j) {
            const uint8_t fromOutside = j < width - 1 - j ? j : static_cast<uint8_t>(width - 1 - j);
            const float share = fromOutside < edge ? static_cast<float>(fromOutside + 1) / (edge + 1) : 1.0f;
            level[(p.start + j) % cfg::TOTAL_LEDS] = static_cast<uint8_t>(255 - static_cast<int>(dip * share + 0.5f));
        }
        if (++p.age > p.duration) p.duration = 0;  // rendered ages 0..duration: back at base
    }
    for (uint16_t i = 0; i < cfg::TOTAL_LEDS; ++i) out[i] = scale(ctx.base, cie8(level[i]));

    ++step_;
    return step_ < cfg::FLAME_STEPS;
}

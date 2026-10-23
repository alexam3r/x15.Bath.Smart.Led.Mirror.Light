// Task 4: EffectRegistry implementation (ARCHITECTURE.md section 7).
// Static instances only — no heap allocation on Core 1.
#include "EffectRegistry.h"

#include <cstring>

#include "Comet.h"
#include "Snake.h"
#include "Wave.h"

namespace {

Comet        s_comet;
DarkSnake    s_dark;
RainbowSnake s_rainbow;
Wave         s_wave;

const EffectEntry kEffects[] = {
    {EffectId::Dark,    "dark",    &s_dark,    100},
    {EffectId::Rainbow, "rainbow", &s_rainbow, 115},
    {EffectId::Wave,    "wave",    &s_wave,    100},
    {EffectId::Comet,   "comet",   &s_comet,   100},
};

}  // namespace

Effect* effectInstance(EffectId id) {
    for (const auto& e : kEffects) {
        if (e.id == id) return e.effect;
    }
    return nullptr;
}

const char* effectName(EffectId id) {
    for (const auto& e : kEffects) {
        if (e.id == id) return e.name;
    }
    return nullptr;
}

EffectId effectIdFromName(const char* name) {
    if (name == nullptr) return EffectId::None;
    for (const auto& e : kEffects) {
        if (std::strcmp(e.name, name) == 0) return e.id;
    }
    return EffectId::None;
}

EffectId randomEffect(RandomFn rnd) {
    uint32_t total = 0;
    for (const auto& e : kEffects) total += e.weight;
    uint32_t ticket = rnd(total);
    for (const auto& e : kEffects) {
        if (ticket < e.weight) return e.id;
        ticket -= e.weight;
    }
    return EffectId::None;  // RandomFn must return a value in [0, total).
}

const char* baseModeName(BaseMode m) {
    switch (m) {
        case BaseMode::Solid:  return "solid";
        case BaseMode::Makeup: return "makeup";
    }
    return nullptr;
}

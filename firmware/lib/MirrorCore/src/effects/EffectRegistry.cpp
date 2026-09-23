// Task 4: EffectRegistry implementation (ARCHITECTURE.md section 7).
// Static instances only — no heap allocation on Core 1.
#include "EffectRegistry.h"

#include <cstddef>
#include <cstring>

#include "Breathe.h"
#include "Comet.h"
#include "Embers.h"
#include "Snake.h"
#include "Wave.h"

namespace {

Breathe      s_breathe;
Comet        s_comet;
DarkSnake    s_dark;
Embers       s_embers;
RainbowSnake s_rainbow;
Wave         s_wave;

const EffectEntry kEffects[] = {
    {EffectId::Dark,    "dark",    &s_dark},
    {EffectId::Rainbow, "rainbow", &s_rainbow},
    {EffectId::Wave,    "wave",    &s_wave},
    {EffectId::Breathe, "breathe", &s_breathe},
    {EffectId::Embers,  "embers",  &s_embers},
    {EffectId::Comet,   "comet",   &s_comet},
};
constexpr size_t kEffectCount = sizeof(kEffects) / sizeof(kEffects[0]);

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
    return kEffects[rnd(static_cast<uint32_t>(kEffectCount))].id;
}

const char* baseModeName(BaseMode m) {
    switch (m) {
        case BaseMode::Solid:  return "solid";
        case BaseMode::Makeup: return "makeup";
    }
    return nullptr;
}

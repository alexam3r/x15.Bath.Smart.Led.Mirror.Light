// Task 4: the effect registry — the ONLY place that lists the temporary
// effects (ARCHITECTURE.md section 7). SlideAnimation is deliberately not
// here: it is the power animation, driven directly by Mirror.
#pragma once

#include "../Types.h"
#include "Effect.h"

struct EffectEntry {
    EffectId    id;
    const char* name;
    Effect*     effect;
};

Effect*     effectInstance(EffectId id);          // nullptr for None
const char* effectName(EffectId id);              // "dark" | "rainbow" | "wave"; nullptr for None
EffectId    effectIdFromName(const char* name);   // exact, case-sensitive; None if unknown
EffectId    randomEffect(RandomFn rnd);           // kEffects[rnd(count)].id
const char* baseModeName(BaseMode m);             // "solid" | "makeup"

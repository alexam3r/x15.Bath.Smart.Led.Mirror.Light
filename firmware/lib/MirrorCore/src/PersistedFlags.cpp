#include "PersistedFlags.h"

namespace {

constexpr uint32_t kMagic = 0x4D495252u;  // "MIRR"

uint32_t fnv1a(const PersistedFlags& p) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&p);
    uint32_t h = 2166136261u;
    for (unsigned i = 0; i < 8; ++i) {  // magic + the four flag bytes
        h ^= bytes[i];
        h *= 16777619u;
    }
    return h;
}

}  // namespace

PersistedFlags packFlags(bool automation, bool nightMode, bool glitch) {
    PersistedFlags p{};
    p.magic = kMagic;
    p.automation = automation ? 1 : 0;
    p.nightMode = nightMode ? 1 : 0;
    p.glitch = glitch ? 1 : 0;
    p.reserved = 0;
    p.check = fnv1a(p);
    return p;
}

bool unpackFlags(const PersistedFlags& p, bool& automation, bool& nightMode, bool& glitch) {
    if (p.magic != kMagic || p.check != fnv1a(p)) return false;
    if (p.automation > 1 || p.nightMode > 1 || p.glitch > 1 || p.reserved != 0) return false;
    automation = p.automation == 1;
    nightMode = p.nightMode == 1;
    glitch = p.glitch == 1;
    return true;
}

bool shouldRestoreFlags(uint8_t resetReason) {
    switch (resetReason) {  // esp_reset_reason_t (ESP-IDF 4.4)
        case 3:  // SW
        case 4:  // PANIC
        case 5:  // INT_WDT
        case 6:  // TASK_WDT
        case 7:  // WDT
        case 9:  // BROWNOUT
            return true;
        default:
            return false;
    }
}

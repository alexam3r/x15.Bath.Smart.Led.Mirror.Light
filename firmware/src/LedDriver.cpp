// Task 7: LedDriver implementation.
#include "LedDriver.h"

#include "ColorMath.h"
#include "Config.h"
#include "RmtLock.h"

void LedDriver::begin() {
    stripL_.begin();
    stripR_.begin();
    Frame dark;
    dark.clear();
    show(dark);  // dark on boot
}

bool LedDriver::show(const Frame& f) {
    for (uint16_t v = 0; v < Frame::kSize; ++v) {
        const PhysicalPixel p = mapVirtual(v);
        const uint32_t packed = packColor(f[v]);
        if (p.strip == STRIP_RIGHT) {
            stripR_.setPixelColor(p.index, packed);
        } else {
            stripL_.setPixelColor(p.index, packed);
        }
    }
    if (!rmt_lock::take(cfg::RMT_LOCK_TIMEOUT_MS)) return false;
    stripL_.show();
    stripR_.show();
    rmt_lock::give();
    return true;
}

// Task 7: LedDriver implementation.
#include "LedDriver.h"

#include "ColorMath.h"

void LedDriver::begin() {
    stripL_.begin();
    stripR_.begin();
    stripL_.show();  // dark on boot
    stripR_.show();
}

void LedDriver::show(const Frame& f) {
    for (uint16_t v = 0; v < Frame::kSize; ++v) {
        const PhysicalPixel p = mapVirtual(v);
        const uint32_t packed = packColor(f[v]);
        if (p.strip == STRIP_RIGHT) {
            stripR_.setPixelColor(p.index, packed);
        } else {
            stripL_.setPixelColor(p.index, packed);
        }
    }
    stripL_.show();
    stripR_.show();
}

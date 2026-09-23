// Task 7: StatusLed implementation.
#include "StatusLed.h"

#include "RmtLock.h"

void StatusLed::begin() {
    pixel_.begin();
    set(0, 0, 0);  // off
}

void StatusLed::set(uint8_t r, uint8_t g, uint8_t b) {
    pixel_.setPixelColor(0, pixel_.Color(r, g, b));
    // A status blink that loses the race for the RMT is simply skipped.
    if (!rmt_lock::take(cfg::RMT_LOCK_TIMEOUT_MS)) return;
    pixel_.show();
    rmt_lock::give();
}

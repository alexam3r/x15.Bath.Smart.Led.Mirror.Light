#include "NetWatchdog.h"

#include "Config.h"

bool NetWatchdog::update(uint32_t now, bool wifiUp, bool lit) {
    if (!started_) {
        started_ = true;
        lastMs_ = now;
        return false;
    }
    const uint32_t elapsed = (uint32_t)(now - lastMs_);
    lastMs_ = now;

    if (wifiUp) {
        downMs_ = 0;
        return false;
    }
    // Saturate instead of wrapping: an outage of weeks must stay "long".
    downMs_ = (downMs_ > UINT32_MAX - elapsed) ? UINT32_MAX : downMs_ + elapsed;
    return downMs_ > cfg::WATCHDOG_TIMEOUT_MS && !lit;
}

#include "NetWatchdog.h"

#include "Config.h"

namespace {
// Saturating add: an outage of weeks must stay "long", never wrap to short.
uint32_t addSat(uint32_t a, uint32_t b) { return (a > UINT32_MAX - b) ? UINT32_MAX : a + b; }
}  // namespace

NetAction NetWatchdog::update(uint32_t now, bool wifiUp, bool mqttUp, bool quiet) {
    if (!started_) {
        started_ = true;
        lastMs_ = now;
        wasQuiet_ = quiet;
        return NetAction::None;
    }
    const uint32_t elapsed = (uint32_t)(now - lastMs_);
    lastMs_ = now;

    wifiDownMs_ = wifiUp ? 0 : addSat(wifiDownMs_, elapsed);
    mqttDownMs_ = (wifiUp && !mqttUp) ? addSat(mqttDownMs_, elapsed) : 0;
    // The quiet spell counts only between two quiet observations: the time
    // before the first one is not known to have been quiet.
    quietMs_ = (quiet && wasQuiet_) ? addSat(quietMs_, elapsed) : 0;
    wasQuiet_ = quiet;

    if (quietMs_ < cfg::RESTART_QUIET_MS) return NetAction::None;
    if (wifiDownMs_ > cfg::WATCHDOG_TIMEOUT_MS) return NetAction::Restart;
    if (mqttDownMs_ > cfg::MQTT_DOWN_REJOIN_MS) {
        mqttDownMs_ = 0;  // one re-join per outage period
        return NetAction::RejoinWifi;
    }
    return NetAction::None;
}

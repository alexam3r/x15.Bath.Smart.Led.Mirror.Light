// NetWatchdog — decides when a lost network warrants restarting the ESP
// (v1.2.1). WiFi down for WATCHDOG_TIMEOUT_MS in total -> restart, as in v27,
// because a wedged WiFi stack is what a reboot can fix. Two changes from v27:
//   - the restart never happens while the mirror is lit — it waits until the
//     mirror goes dark, so nobody gets the light switched off mid-use;
//   - MQTT is not an input at all: a dead broker (HA updating for half an
//     hour) is not fixed by rebooting, the network task just keeps
//     reconnecting while the mirror works locally.
// Downtime is measured between calls, so a flapping link is counted only for
// the time it is really down. Pure logic, wraparound-safe.
#pragma once

#include <cstdint>

class NetWatchdog {
public:
    // Call on every network-task pass (also after long blocking reconnects).
    // Returns true when the device should restart now.
    bool update(uint32_t now, bool wifiUp, bool lit);

    uint32_t wifiDownMs() const { return downMs_; }

private:
    bool     started_ = false;
    uint32_t lastMs_  = 0;
    uint32_t downMs_  = 0;
};

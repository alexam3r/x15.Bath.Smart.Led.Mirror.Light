// NetWatchdog — what a lost network warrants (v1.2.1). Pure logic,
// wraparound-safe, fed once per network-task pass.
//
//   - WiFi down for WATCHDOG_TIMEOUT_MS in total -> Restart the ESP, as in
//     v27: a wedged WiFi stack is what a reboot can fix.
//   - WiFi up, MQTT down for MQTT_DOWN_REJOIN_MS -> RejoinWifi, never a
//     restart: a dead broker (HA updating for half an hour) is not fixed by
//     rebooting, but a stale DHCP lease or an IP conflict after a router
//     swap is fixed by re-joining the AP.
//   - Either action waits until the mirror has been quiet (dark, no motion)
//     for RESTART_QUIET_MS: nobody gets the light switched off mid-use, and a
//     reboot right after the mirror went dark cannot wipe the 15 s PIR
//     cooldown and relight it behind the person leaving.
// Downtime is measured between calls, so a flapping link counts only for the
// time it is really down.
#pragma once

#include <cstdint>

enum class NetAction : uint8_t { None, Restart, RejoinWifi };

class NetWatchdog {
public:
    // quiet: the mirror is dark and the PIR sees no motion.
    NetAction update(uint32_t now, bool wifiUp, bool mqttUp, bool quiet);

    uint32_t wifiDownMs() const { return wifiDownMs_; }
    uint32_t mqttDownMs() const { return mqttDownMs_; }

private:
    bool     started_    = false;
    uint32_t lastMs_     = 0;
    uint32_t wifiDownMs_ = 0;
    uint32_t mqttDownMs_ = 0;  // only while WiFi is up
    bool     wasQuiet_   = false;
    uint32_t quietMs_    = 0;
};

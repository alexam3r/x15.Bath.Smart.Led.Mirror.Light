// Task 3: PIR automation gating (ARCHITECTURE.md 4.4 / 4.5 / 8.1) —
// automation switch, night mode, 15s cooldown after manual OFF, 2s
// blackout after slide-out. No Arduino includes; no knowledge of power
// state — the caller (Mirror, Task 5) decides WHEN to call these.
#pragma once

#include <cstdint>

#include "Countdown.h"

class MotionGate {
public:
    // Main automation switch (motion/set). Turning it on clears cooldown.
    void setAutomation(bool on);
    bool automation() const { return automation_; }

    // "Deaf" PIR disable (motion_disable/set, hold, powerOn()). Turning
    // it off clears cooldown.
    void setNightMode(bool on);
    bool nightMode() const { return nightMode_; }

    // powerOff(manual=true), or a hold that clears night mode: start the
    // 15s "don't turn the light back on behind the departing user" cooldown.
    void onManualOff(uint32_t now);

    // Entered OFF after a slide-out: start the 2s blackout guarding
    // against the PIR's false trigger right as the light goes dark.
    void onOffReached(uint32_t now);

    // v27 triggerPowerOn(): clears nightMode, cooldown and blackout.
    void onPowerOn();

    bool canAutoOn(uint32_t now) const;
    bool automationActive() const;  // automation && !nightMode

    // Ruling R7: retire expired cooldown/blackout countdowns. Called
    // every loop tick by the owner.
    void tick(uint32_t now);

private:
    bool automation_ = true;
    bool nightMode_  = false;
    Countdown cooldown_;
    Countdown blackout_;
};

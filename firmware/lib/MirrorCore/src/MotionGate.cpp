// Task 3: MotionGate implementation (ARCHITECTURE.md 4.4 / 4.5 / 8.1).
#include "MotionGate.h"

#include "Config.h"

void MotionGate::setAutomation(bool on) {
    automation_ = on;
    if (on) {
        cooldown_.cancel();
    }
}

void MotionGate::setNightMode(bool on) {
    // Ruling R18: only a real night mode -> off transition means "PIR active
    // now". A redundant OFF must not cancel a running cooldown (e.g. the one a
    // night-mode button hold starts).
    if (!on && nightMode_) {
        cooldown_.cancel();
    }
    nightMode_ = on;
}

void MotionGate::onManualOff(uint32_t now) { cooldown_.start(now, cfg::PIR_COOLDOWN_MS); }

void MotionGate::onOffReached(uint32_t now) { blackout_.start(now, cfg::PIR_BLACKOUT_MS); }

void MotionGate::onPowerOn() {
    nightMode_ = false;
    cooldown_.cancel();
    blackout_.cancel();
}

bool MotionGate::canAutoOn(uint32_t now) const {
    return automation_ && !nightMode_ && !cooldown_.running(now) && !blackout_.running(now);
}

bool MotionGate::automationActive() const { return automation_ && !nightMode_; }

void MotionGate::tick(uint32_t now) {
    cooldown_.update(now);
    blackout_.update(now);
}

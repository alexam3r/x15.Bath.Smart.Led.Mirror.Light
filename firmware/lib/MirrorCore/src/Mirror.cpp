// Task 5: Mirror implementation. Grown test-first in slices — see
// firmware/test/test_mirror/test_main.cpp and task-5-report.md for the
// TDD history. This file starts as a stub (slice 0) and each slice below
// fills in one more piece of tick()/onButton()/onPir()/apply().
#include "Mirror.h"

#include "Log.h"
#include "effects/EffectRegistry.h"

Mirror::Mirror(RandomFn rnd) : rnd_(rnd) {}

void Mirror::markActivity(uint32_t now) { lastActivity_ = lastIdle_ = now; }

uint32_t Mirror::rollAutoEffectDelay() {
    return cfg::AUTO_EFFECT_MIN_MS + rnd_(cfg::AUTO_EFFECT_MAX_MS - cfg::AUTO_EFFECT_MIN_MS);
}

Rgbw Mirror::baseColor() const {
    return base_ == BaseMode::Makeup ? Rgbw{0, 0, 0, 255} : Rgbw{r_, g_, b_, 0};
}

EffectContext Mirror::ctx() const { return EffectContext{baseColor(), rnd_}; }

void Mirror::powerOn(uint32_t now) {
    if (power_ == PowerState::Off) {
        slide_.startOn(cfg::CENTERS[rnd_(cfg::CENTERS_CNT)]);
    } else if (power_ == PowerState::SlideOff) {
        slide_.reverseToOn();
    } else {
        return;  // SlideOn/On: no-op (table 4.1)
    }
    power_ = PowerState::SlideOn;
    lastStepMs_ = now;
    gate_.onPowerOn();
    markActivity(now);
    MLOG("[%lu] POWER ON -> SLIDE_ON\n", (unsigned long)now);
}

void Mirror::powerOff(bool manual, uint32_t now) {
    if (power_ != PowerState::SlideOn && power_ != PowerState::On) {
        return;  // Off/SlideOff: no-op (table 4.1)
    }
    effect_ = EffectId::None;
    pending_ = EffectId::None;
    slide_.startOff();
    power_ = PowerState::SlideOff;
    lastStepMs_ = now;
    if (manual) gate_.onManualOff(now);
    MLOG("[%lu] POWER OFF (manual=%d) -> SLIDE_OFF\n", (unsigned long)now, (int)manual);
}

void Mirror::startEffect(EffectRequest /*req*/, uint32_t /*now*/) {}

void Mirror::applyDefaults() {
    r_ = cfg::DEFAULT_R;
    g_ = cfg::DEFAULT_G;
    b_ = cfg::DEFAULT_B;
    base_ = BaseMode::Solid;
    brightness_ = cfg::DEFAULT_BRIGHTNESS;
    effect_ = EffectId::None;
    pending_ = EffectId::None;
}

void Mirror::begin(uint32_t now) {
    markActivity(now);
    nextAutoEffectMs_ = rollAutoEffectDelay();
    frame_.clear();
    frameDirty_ = true;
}

void Mirror::apply(const Command& /*cmd*/, uint32_t /*now*/) {}

void Mirror::onButton(const ButtonEvent& ev, uint32_t now) {
    markActivity(now);
    if (ev.type == ButtonEventType::Click && ev.clicks == 1) {
        if (power_ == PowerState::Off || power_ == PowerState::SlideOff) {
            powerOn(now);
        } else {
            powerOff(true, now);
        }
    }
}

void Mirror::onPir(bool /*level*/, uint32_t /*now*/) {}

void Mirror::tick(uint32_t now) {
    gate_.tick(now);

    if ((power_ == PowerState::SlideOn || power_ == PowerState::SlideOff) &&
        (uint32_t)(now - lastStepMs_) >= cfg::SLIDE_STEP_MS) {
        lastStepMs_ = now;
        bool wasTurningOn = slide_.turningOn();
        bool running = slide_.step(frame_, baseColor());
        frame_.scale(brightness_);
        frameDirty_ = true;

        if (!running) {
            if (wasTurningOn) {
                power_ = PowerState::On;
                staticDirty_ = true;
                MLOG("[%lu] SLIDE_ON -> ON\n", (unsigned long)now);
                if (pending_ != EffectId::None) {
                    EffectId id = pending_;
                    pending_ = EffectId::None;
                    effect_ = id;
                    effectInstance(id)->begin(ctx());
                    lastStepMs_ = now;
                    lastIdle_ = now;
                }
            } else {
                power_ = PowerState::Off;
                applyDefaults();
                gate_.onOffReached(now);
                MLOG("[%lu] SLIDE_OFF -> OFF (defaults applied)\n", (unsigned long)now);
            }
        }
    }
}

bool Mirror::takeFrameDirty() { return false; }

const Frame& Mirror::frame() const { return frame_; }

StateSnapshot Mirror::snapshot() const { return StateSnapshot{}; }

PowerState Mirror::power() const { return power_; }

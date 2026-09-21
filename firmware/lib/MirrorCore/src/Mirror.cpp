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

void Mirror::startEffect(EffectRequest req, uint32_t now) {
    EffectId id;
    switch (req) {
        case EffectRequest::Dark:    id = EffectId::Dark;    break;
        case EffectRequest::Rainbow: id = EffectId::Rainbow; break;
        case EffectRequest::Wave:    id = EffectId::Wave;    break;
        case EffectRequest::Random:
            id = randomEffect(rnd_);
            nextAutoEffectMs_ = rollAutoEffectDelay();  // re-roll even if ignored below (v27)
            MLOG("[%lu] RANDOM EFFECT rolled id=%d, next auto in %lu ms\n",
                 (unsigned long)now, (int)id, (unsigned long)nextAutoEffectMs_);
            break;
        case EffectRequest::None:
        case EffectRequest::Solid:
        case EffectRequest::Makeup:
        default:
            return;  // not a temporary-effect id; nothing to do here
    }

    if (power_ == PowerState::Off || power_ == PowerState::SlideOff) {
        return;  // ignore (table 4.1)
    }
    if (power_ == PowerState::SlideOn) {
        pending_ = id;
        return;
    }

    // On: replaces whatever is currently running.
    effect_ = id;
    effectInstance(id)->begin(ctx());
    lastStepMs_ = now;
    lastIdle_ = now;
    MLOG("[%lu] START EFFECT id=%d\n", (unsigned long)now, (int)id);
}

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

void Mirror::apply(const Command& cmd, uint32_t now) {
    switch (cmd.type) {
        case CommandType::Light: {
            const LightCommand& lc = cmd.light;

            if (lc.brightness >= 1) {
                brightness_ = static_cast<uint8_t>(lc.brightness);
                staticDirty_ = true;
            }
            if (lc.r >= 0 || lc.g >= 0 || lc.b >= 0) {
                if (lc.r >= 0) r_ = static_cast<uint8_t>(lc.r);
                if (lc.g >= 0) g_ = static_cast<uint8_t>(lc.g);
                if (lc.b >= 0) b_ = static_cast<uint8_t>(lc.b);
                base_ = BaseMode::Solid;
                staticDirty_ = true;
            }
            if (lc.effect == EffectRequest::Solid) {
                base_ = BaseMode::Solid;
                staticDirty_ = true;
            } else if (lc.effect == EffectRequest::Makeup) {
                base_ = BaseMode::Makeup;
                staticDirty_ = true;
            }

            if (lc.brightness == 0 || lc.state == 0) {
                powerOff(true, now);
            } else if (lc.state == 1) {
                powerOn(now);
            }

            if (lc.effect == EffectRequest::Dark || lc.effect == EffectRequest::Rainbow ||
                lc.effect == EffectRequest::Wave || lc.effect == EffectRequest::Random) {
                startEffect(lc.effect, now);
            }
            break;
        }
        case CommandType::Automation:
            gate_.setAutomation(cmd.flag);
            if (cmd.flag) markActivity(now);  // R9: enabling automation never auto-offs instantly
            break;
        case CommandType::Makeup:
            if (cmd.flag) {
                base_ = BaseMode::Makeup;
                if (power_ == PowerState::Off || power_ == PowerState::SlideOff) powerOn(now);
            } else {
                base_ = BaseMode::Solid;
            }
            staticDirty_ = true;
            break;
        case CommandType::NightMode:
            if (cmd.flag) {
                gate_.setNightMode(true);
                MLOG("[%lu] NIGHT MODE ON\n", (unsigned long)now);
                if (power_ == PowerState::SlideOn || power_ == PowerState::On) powerOff(true, now);
            } else {
                gate_.setNightMode(false);
                MLOG("[%lu] NIGHT MODE OFF\n", (unsigned long)now);
            }
            break;
        case CommandType::RandomEffect:
            startEffect(EffectRequest::Random, now);
            break;
    }
}

void Mirror::onButton(const ButtonEvent& ev, uint32_t now) {
    markActivity(now);
    switch (ev.type) {
        case ButtonEventType::Click:
            if (ev.clicks == 1) {
                if (power_ == PowerState::Off || power_ == PowerState::SlideOff) {
                    powerOn(now);
                } else {
                    powerOff(true, now);
                }
            } else if (ev.clicks == 2) {
                if (power_ == PowerState::Off || power_ == PowerState::SlideOff) {
                    base_ = BaseMode::Makeup;
                    powerOn(now);
                } else {
                    base_ = (base_ == BaseMode::Solid) ? BaseMode::Makeup : BaseMode::Solid;
                    staticDirty_ = true;
                }
            } else if (ev.clicks == 3) {
                startEffect(EffectRequest::Random, now);
            }
            // clicks == 0 or >= 4: ignore.
            break;
        case ButtonEventType::HoldStart:
            if (gate_.nightMode()) {
                gate_.setNightMode(false);
                holdLocked_ = true;  // bug A fix: this hold must not power on or dim
            } else if (power_ == PowerState::Off || power_ == PowerState::SlideOff) {
                powerOn(now);
            }
            break;
        case ButtonEventType::HoldTick:
            if (!holdLocked_ && power_ == PowerState::On) {
                effect_ = EffectId::None;
                int v = static_cast<int>(brightness_) + dimDir_;
                if (v >= cfg::DIM_MAX) {
                    v = cfg::DIM_MAX;
                    dimDir_ = static_cast<int16_t>(-cfg::DIM_STEP);
                }
                if (v <= cfg::DIM_MIN) {
                    v = cfg::DIM_MIN;
                    dimDir_ = static_cast<int16_t>(cfg::DIM_STEP);
                }
                brightness_ = static_cast<uint8_t>(v);
                staticDirty_ = true;
            }
            break;
        case ButtonEventType::HoldEnd:
            holdLocked_ = false;
            break;
        case ButtonEventType::None:
        default:
            break;
    }
}

void Mirror::onPir(bool level, uint32_t now) {
    pir_ = level;
    if (level) {
        if (power_ == PowerState::Off && gate_.canAutoOn(now)) {
            powerOn(now);
            MLOG("[%lu] PIR triggered POWER ON\n", (unsigned long)now);
        }
        if (power_ == PowerState::SlideOn || power_ == PowerState::On) {
            markActivity(now);  // R9: regardless of automation
        }
    }
}

void Mirror::tick(uint32_t now) {
    gate_.tick(now);

    if (gate_.automationActive() && power_ == PowerState::On) {
        if ((uint32_t)(now - lastActivity_) > cfg::AUTO_OFF_MS) {
            MLOG("[%lu] AUTO-OFF (idle %lu ms)\n", (unsigned long)now,
                 (unsigned long)(now - lastActivity_));
            powerOff(false, now);
        } else if (effect_ == EffectId::None && base_ == BaseMode::Solid &&
                   (uint32_t)(now - lastIdle_) > nextAutoEffectMs_) {
            startEffect(EffectRequest::Random, now);
        }
    }

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

    if (power_ == PowerState::On && effect_ != EffectId::None) {
        Effect* fx = effectInstance(effect_);
        if (fx != nullptr && (uint32_t)(now - lastStepMs_) >= static_cast<uint32_t>(fx->stepIntervalMs())) {
            lastStepMs_ = now;
            bool running = fx->step(frame_, ctx());
            frame_.scale(brightness_);
            frameDirty_ = true;
            if (!running) {
                effect_ = EffectId::None;
                lastIdle_ = now;
                staticDirty_ = true;
            }
        }
    }

    if (power_ == PowerState::On && effect_ == EffectId::None && staticDirty_) {
        frame_.fill(baseColor());
        staticDirty_ = false;
        frame_.scale(brightness_);
        frameDirty_ = true;
    }
}

bool Mirror::takeFrameDirty() {
    bool dirty = frameDirty_;
    frameDirty_ = false;
    return dirty;
}

const Frame& Mirror::frame() const { return frame_; }

StateSnapshot Mirror::snapshot() const {
    StateSnapshot s;
    s.on = (power_ == PowerState::SlideOn || power_ == PowerState::On);
    s.brightness = brightness_;
    s.r = r_;
    s.g = g_;
    s.b = b_;
    s.base = base_;
    s.effect = effect_;  // running only; pending_ is not reported
    s.automation = gate_.automation();
    s.nightMode = gate_.nightMode();
    s.pir = pir_;
    return s;
}

PowerState Mirror::power() const { return power_; }

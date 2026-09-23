// Mirror — the single-owner state machine (ARCHITECTURE.md section 4
// state machine, 8.1 interface). Behaviour is specified by the tests in
// firmware/test/test_mirror/test_main.cpp.
#include "Mirror.h"

#include "Log.h"
#include "effects/EffectRegistry.h"

Mirror::Mirror(RandomFn rnd) : rnd_(rnd) {}

void Mirror::markActivity(uint32_t now) {
    lastActivity_ = lastIdle_ = now;
    if (warning_) endWarning(now);
}

bool Mirror::lit() const { return power_ == PowerState::SlideOn || power_ == PowerState::On; }

// How long the mirror may stay idle before it switches itself off: makeup
// gets AUTO_OFF_MAKEUP_MS, everything else AUTO_OFF_MS (v1.2.0).
uint32_t Mirror::autoOffLimit() const {
    return base_ == BaseMode::Makeup ? cfg::AUTO_OFF_MAKEUP_MS : cfg::AUTO_OFF_MS;
}

// Leaves the warning and ramps the render back to full brightness.
void Mirror::endWarning(uint32_t now) {
    warning_ = false;
    warn_.start(warn_.value(now), 255, now, cfg::WARN_FADE_OUT_MS);
    lastWarnStepMs_ = now;
}

uint32_t Mirror::rollAutoEffectDelay() {
    return cfg::AUTO_EFFECT_MIN_MS + rnd_(cfg::AUTO_EFFECT_MAX_MS - cfg::AUTO_EFFECT_MIN_MS);
}

uint32_t Mirror::rollGlitchDelay() {
    return cfg::GLITCH_INTERVAL_MIN_MS + rnd_(cfg::GLITCH_INTERVAL_MAX_MS - cfg::GLITCH_INTERVAL_MIN_MS);
}

// The glitch only runs over a quiet, solid, lit mirror with automation on:
// never in makeup, during an effect/slide/pending effect, while the button
// is held, or during a colour transition or warning fade (v1.2.1 — those
// redraw every 20 ms, so a glitch started between two steps would be cut
// off by the next one: a 15..20 ms blip).
bool Mirror::glitchAllowed() const {
    return glitchEnabled_ && power_ == PowerState::On && effect_ == EffectId::None &&
           pending_ == EffectId::None && base_ == BaseMode::Solid && gate_.automationActive() &&
           !holding_ && !trans_.active && !warn_.active;
}

// The colour the targets ask for; what is on screen is shownColor_.
Rgbw Mirror::targetColor() const {
    return base_ == BaseMode::Makeup ? Rgbw{0, 0, 0, 255} : Rgbw{r_, g_, b_, 0};
}

Rgbw Mirror::baseColor() const { return shownColor_; }

// Called after any change of the targets. smooth=true fades from the shown
// values over TRANSITION_MS while lit; otherwise (or while OFF/SLIDE_OFF)
// the targets apply at once.
void Mirror::retarget(uint32_t now, bool smooth) {
    const Rgbw target = targetColor();
    if (smooth && lit() && (!(target == shownColor_) || brightness_ != shownBrightness_)) {
        fromColor_ = shownColor_;
        fromBrightness_ = shownBrightness_;
        trans_.start(0, 255, now, cfg::TRANSITION_MS);
        lastTransStepMs_ = now;
    } else {
        shownColor_ = target;
        shownBrightness_ = brightness_;
        trans_.set(255);
    }
    staticDirty_ = true;
}

// Every render ends here: brightness applied to the whole frame, frame flagged.
void Mirror::finishFrame() {
    frame_.scale(scale8(shownBrightness_, warnLevel_));
    frameDirty_ = true;
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
    retarget(now, false);  // a base/colour set together with power-on shows at once, no fade
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

void Mirror::startRandomEffect(uint32_t now) {
    const EffectId id = randomEffect(rnd_);
    nextAutoEffectMs_ = rollAutoEffectDelay();  // re-roll even if ignored below (v27)
    MLOG("[%lu] RANDOM EFFECT rolled id=%d, next auto in %lu ms\n",
         (unsigned long)now, (int)id, (unsigned long)nextAutoEffectMs_);
    startEffect(id, now);
}

void Mirror::startEffect(EffectId id, uint32_t now) {
    Effect* fx = effectInstance(id);
    if (fx == nullptr) return;  // None / not in the registry: nothing to start

    if (power_ == PowerState::Off || power_ == PowerState::SlideOff) {
        return;  // ignore (table 4.1)
    }
    if (power_ == PowerState::SlideOn) {
        pending_ = id;
        return;
    }

    // On: replaces whatever is currently running.
    effect_ = id;
    fx->begin(ctx());
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
    shownColor_ = targetColor();
    shownBrightness_ = brightness_;
    trans_.set(255);
    warning_ = false;
    warn_.set(255);
    warnLevel_ = 255;
}

void Mirror::begin(uint32_t now) {
    markActivity(now);
    nextAutoEffectMs_ = rollAutoEffectDelay();
    nextGlitchMs_ = rollGlitchDelay();
    frame_.clear();
    frameDirty_ = true;
}

void Mirror::apply(const Command& cmd, uint32_t now) {
    switch (cmd.type) {
        case CommandType::Light: {
            const LightCommand& lc = cmd.light;
            bool changed = false;

            if (lc.brightness >= 1) {
                brightness_ = static_cast<uint8_t>(lc.brightness);
                changed = true;
            }
            if (lc.r >= 0 || lc.g >= 0 || lc.b >= 0) {
                if (lc.r >= 0) r_ = static_cast<uint8_t>(lc.r);
                if (lc.g >= 0) g_ = static_cast<uint8_t>(lc.g);
                if (lc.b >= 0) b_ = static_cast<uint8_t>(lc.b);
                base_ = BaseMode::Solid;
                changed = true;
            }
            if (lc.effect == EffectRequest::Solid || lc.effect == EffectRequest::Makeup) {
                base_ = (lc.effect == EffectRequest::Solid) ? BaseMode::Solid : BaseMode::Makeup;
                // Picking a base mode in the HA effect list ends a running
                // or pending temporary effect (v1.2.1): otherwise a 20 s
                // candle kept going and the list jumped back to "candle".
                effect_ = EffectId::None;
                pending_ = EffectId::None;
                changed = true;
            }
            if (changed) retarget(now, true);  // a bare ON must not cut a running fade

            if (lc.brightness == 0 || lc.state == 0) {
                powerOff(true, now);
            } else if (lc.state == 1) {
                powerOn(now);
            }

            if (lc.effect == EffectRequest::Temporary) {
                startEffect(lc.effectId, now);
            } else if (lc.effect == EffectRequest::Random) {
                startRandomEffect(now);
            }
            // A command to a lit mirror is activity (v1.2.1): whoever sent it
            // is there, even out of the PIR's sight. This also restarts the
            // timer on a mode change, whose two modes have different limits.
            if (lit()) markActivity(now);
            break;
        }
        case CommandType::Automation:
            gate_.setAutomation(cmd.flag);
            if (cmd.flag) markActivity(now);  // R9: enabling automation never auto-offs instantly
            break;
        case CommandType::Makeup: {
            if (cmd.flag) {
                base_ = BaseMode::Makeup;
                if (power_ == PowerState::Off || power_ == PowerState::SlideOff) powerOn(now);
            } else {
                base_ = BaseMode::Solid;
            }
            retarget(now, true);  // after a power-on from OFF this only marks the frame
            if (lit()) markActivity(now);  // a command to a lit mirror is activity
            break;
        }
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
            startRandomEffect(now);
            if (lit()) markActivity(now);  // the HA button; automatic effects do not count
            break;
        case CommandType::Glitch:
            glitchEnabled_ = cmd.flag;
            MLOG("[%lu] GLITCH %s\n", (unsigned long)now, cmd.flag ? "ON" : "OFF");
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
                    retarget(now, true);
                }
            } else if (ev.clicks == 3) {
                startRandomEffect(now);
            }
            // clicks == 0 or >= 4: ignore.
            break;
        case ButtonEventType::HoldStart:
            holding_ = true;
            if (gate_.nightMode()) {
                gate_.setNightMode(false);
                gate_.onManualOff(now);  // Ruling R14: 15 s PIR quiet — PIR must not relight it either
                holdLocked_ = true;      // bug A fix: this hold must not power on or dim
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
                retarget(now, false);  // dimming is already stepped: no fade
            }
            break;
        case ButtonEventType::HoldEnd:
            holdLocked_ = false;
            holding_ = false;
            break;
        case ButtonEventType::None:
        default:
            break;
    }
}

void Mirror::onPir(bool level, uint32_t now) {
    if (level && !pir_) {
        MLOG("[%lu] PIR rising edge\n", (unsigned long)now);  // edge only: no per-loop spam
        pirHighSince_ = now;
    }
    pir_ = level;  // the raw level is always reported (pir/state)
    if (!level) {
        pirStuck_ = false;
        return;
    }
    // HIGH without a break for over an hour is a stuck sensor, not a person:
    // stop letting it hold the light on (or switch it back on) until it goes
    // LOW. Latched, so the difference wrapping after 49.7 days changes nothing.
    if (!pirStuck_ && (uint32_t)(now - pirHighSince_) > cfg::PIR_STUCK_MS) {
        pirStuck_ = true;
        MLOG("[%lu] PIR stuck HIGH for %lu min: ignored until LOW\n", (unsigned long)now,
             (unsigned long)(cfg::PIR_STUCK_MS / 60000));
    }
    if (pirStuck_) return;

    if (power_ == PowerState::Off && gate_.canAutoOn(now)) {
        powerOn(now);
        MLOG("[%lu] PIR triggered POWER ON\n", (unsigned long)now);
    }
    if (lit()) markActivity(now);  // R9: regardless of automation
}

void Mirror::tick(uint32_t now) {
    gate_.tick(now);

    if (trans_.active && (uint32_t)(now - lastTransStepMs_) >= cfg::TRANSITION_STEP_MS) {
        lastTransStepMs_ = now;
        const uint8_t t = trans_.value(now);  // retires itself at the end (t == 255)
        shownColor_ = lerp(fromColor_, targetColor(), t);
        shownBrightness_ = lerp8(fromBrightness_, brightness_, t);
        staticDirty_ = true;
    }

    if (gate_.automationActive() && power_ == PowerState::On) {
        const uint32_t idle = (uint32_t)(now - lastActivity_);
        const uint32_t limit = autoOffLimit();
        if (idle > limit) {
            MLOG("[%lu] AUTO-OFF (idle %lu ms)\n", (unsigned long)now, (unsigned long)idle);
            powerOff(false, now);
        } else {
            if (!warning_ && idle > limit - cfg::AUTO_OFF_WARN_MS) {
                warning_ = true;
                warn_.start(warnLevel_, cfg::WARN_DIM_LEVEL, now, cfg::WARN_FADE_IN_MS);
                lastWarnStepMs_ = now;
                MLOG("[%lu] AUTO-OFF WARNING (dim to 50%%)\n", (unsigned long)now);
            }
            if (effect_ == EffectId::None && base_ == BaseMode::Solid &&
                (uint32_t)(now - lastIdle_) > nextAutoEffectMs_) {
                startRandomEffect(now);
            }
        }
    } else if (warning_ && (power_ == PowerState::On || power_ == PowerState::SlideOn)) {
        endWarning(now);  // automation switched off mid-warning: back to full brightness
    }

    if (warn_.active && (uint32_t)(now - lastWarnStepMs_) >= cfg::TRANSITION_STEP_MS) {
        lastWarnStepMs_ = now;
        warnLevel_ = warn_.value(now);  // retires itself at the end, exactly on 128 / 255
        staticDirty_ = true;
    }

    if ((power_ == PowerState::SlideOn || power_ == PowerState::SlideOff) &&
        (uint32_t)(now - lastStepMs_) >= cfg::SLIDE_STEP_MS) {
        lastStepMs_ = now;
        bool wasTurningOn = slide_.turningOn();
        bool running = slide_.step(frame_, baseColor());
        finishFrame();

        if (!running) {
            if (wasTurningOn) {
                power_ = PowerState::On;
                staticDirty_ = true;
                lastGlitch_ = now;  // first glitch one interval after the mirror is lit
                nextGlitchMs_ = rollGlitchDelay();
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
            finishFrame();
            if (!running) {
                effect_ = EffectId::None;
                lastIdle_ = now;
                staticDirty_ = true;
            }
        }
    }

    tickGlitch(now);

    if (power_ == PowerState::On && effect_ == EffectId::None && staticDirty_) {
        frame_.fill(baseColor());
        staticDirty_ = false;
        finishFrame();
    }
}

// Glitch overlay (v1.1.0): due every GLITCH_INTERVAL_MIN..MAX ms while ON.
// A due moment when the glitch is not allowed is skipped, not deferred.
// Any change that needs a re-render (staticDirty_) or a lost precondition
// cancels a running glitch; the static fill below then restores the base.
void Mirror::tickGlitch(uint32_t now) {
    if (glitch_.active()) {
        if (!glitchAllowed() || staticDirty_) {
            glitch_.cancel();
            staticDirty_ = true;
            return;
        }
        if ((uint32_t)(now - lastGlitchStepMs_) >= cfg::GLITCH_STEP_MS) {
            lastGlitchStepMs_ = now;
            glitch_.step(frame_, baseColor(), rnd_, now);  // last step renders plain base
            finishFrame();
        }
        return;
    }

    if (power_ != PowerState::On || (uint32_t)(now - lastGlitch_) < nextGlitchMs_) return;
    lastGlitch_ = now;
    nextGlitchMs_ = rollGlitchDelay();
    if (!glitchAllowed() || staticDirty_) return;

    glitch_.start(rnd_, now);
    lastGlitchStepMs_ = now;
    glitch_.step(frame_, baseColor(), rnd_, now);
    finishFrame();
    MLOG("[%lu] GLITCH at %u x%u for %lu ms\n", (unsigned long)now, (unsigned)glitch_.first(),
         (unsigned)glitch_.length(), (unsigned long)glitch_.durationMs());
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
    s.glitch = glitchEnabled_;
    return s;
}

PowerState Mirror::power() const { return power_; }

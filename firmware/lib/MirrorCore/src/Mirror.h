// Task 5: Mirror — the single-owner state machine (ARCHITECTURE.md section 4
// state machine, 8.1 interface, verbatim). Runs only on Core 1; fed by
// commands (MQTT via cmdQueue), button events and the PIR level. No
// Arduino/FreeRTOS dependency; no heap allocation.
#pragma once

#include <cstdint>

#include "Button.h"
#include "Config.h"
#include "Frame.h"
#include "MotionGate.h"
#include "Ramp.h"
#include "Types.h"
#include "effects/Effect.h"
#include "effects/Glitch.h"
#include "effects/SlideAnimation.h"

class Mirror {
public:
    explicit Mirror(RandomFn rnd);

    void begin(uint32_t now);
    void apply(const Command& cmd, uint32_t now);
    void onButton(const ButtonEvent& ev, uint32_t now);
    void onPir(bool level, uint32_t now);
    void tick(uint32_t now);

    bool takeFrameDirty();               // true if the frame changed since the last call (flag resets)
    const Frame& frame() const;          // final frame with brightness already applied
    StateSnapshot snapshot() const;
    PowerState power() const;

private:
    // --- internal operations ---
    void markActivity(uint32_t now);
    uint32_t autoOffLimit() const;
    void endWarning(uint32_t now);
    uint32_t rollAutoEffectDelay();
    uint32_t rollGlitchDelay();
    bool glitchAllowed() const;
    bool lit() const;  // SlideOn or On
    void tickGlitch(uint32_t now);
    void powerOn(uint32_t now);
    void powerOff(bool manual, uint32_t now);
    void startEffect(EffectId id, uint32_t now);
    void startRandomEffect(uint32_t now);
    void applyDefaults();
    Rgbw targetColor() const;
    Rgbw baseColor() const;
    void retarget(uint32_t now, bool smooth);
    void finishFrame();
    EffectContext ctx() const;

    RandomFn rnd_;

    PowerState power_  = PowerState::Off;
    BaseMode   base_   = BaseMode::Solid;
    EffectId   effect_  = EffectId::None;
    EffectId   pending_ = EffectId::None;

    uint8_t r_ = cfg::DEFAULT_R;
    uint8_t g_ = cfg::DEFAULT_G;
    uint8_t b_ = cfg::DEFAULT_B;
    uint8_t brightness_ = cfg::DEFAULT_BRIGHTNESS;
    int16_t dimDir_ = static_cast<int16_t>(cfg::DIM_STEP);  // v27 initial +5
    bool    holdLocked_ = false;
    bool    holding_    = false;  // between HoldStart and HoldEnd

    // Shown (rendered) base colour/brightness: follow the targets r_/g_/b_/
    // base_/brightness_ through a TRANSITION_MS ramp (v1.2.0).
    Rgbw     shownColor_{cfg::DEFAULT_R, cfg::DEFAULT_G, cfg::DEFAULT_B, 0};
    uint8_t  shownBrightness_ = cfg::DEFAULT_BRIGHTNESS;
    Rgbw     fromColor_{cfg::DEFAULT_R, cfg::DEFAULT_G, cfg::DEFAULT_B, 0};
    uint8_t  fromBrightness_  = cfg::DEFAULT_BRIGHTNESS;
    Ramp     trans_;
    uint32_t lastTransStepMs_ = 0;

    // Pre-auto-off warning (v1.2.0): inside the last AUTO_OFF_WARN_MS before
    // auto-off the whole render is scaled by warnLevel_ (255 = no warning).
    bool     warning_        = false;
    Ramp     warn_;
    uint8_t  warnLevel_      = 255;
    uint32_t lastWarnStepMs_ = 0;

    uint32_t lastActivity_     = 0;
    uint32_t lastIdle_         = 0;
    uint32_t nextAutoEffectMs_ = 0;
    uint32_t lastStepMs_       = 0;

    SlideAnimation slide_;
    MotionGate     gate_;
    GlitchOverlay  glitch_;
    bool           glitchEnabled_    = true;
    uint32_t       lastGlitch_       = 0;
    uint32_t       nextGlitchMs_     = 0;
    uint32_t       lastGlitchStepMs_ = 0;
    bool           pir_ = false;
    uint32_t       pirHighSince_ = 0;      // last PIR rising edge
    bool           pirStuck_     = false;  // HIGH for over PIR_STUCK_MS: ignored until LOW

    Frame frame_;
    bool  frameDirty_  = false;
    bool  staticDirty_ = false;
};

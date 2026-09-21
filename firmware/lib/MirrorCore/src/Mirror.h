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
#include "Types.h"
#include "effects/Effect.h"
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
    uint32_t rollAutoEffectDelay();
    void powerOn(uint32_t now);
    void powerOff(bool manual, uint32_t now);
    void startEffect(EffectId id, uint32_t now);
    void startRandomEffect(uint32_t now);
    void applyDefaults();
    Rgbw baseColor() const;
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

    uint32_t lastActivity_     = 0;
    uint32_t lastIdle_         = 0;
    uint32_t nextAutoEffectMs_ = 0;
    uint32_t lastStepMs_       = 0;

    SlideAnimation slide_;
    MotionGate     gate_;
    bool           pir_ = false;

    Frame frame_;
    bool  frameDirty_  = false;
    bool  staticDirty_ = false;
};

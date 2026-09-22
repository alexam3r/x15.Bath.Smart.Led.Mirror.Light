// Task 1: shared value types (ARCHITECTURE.md 8.1). Trivially copyable —
// Command and StateSnapshot cross FreeRTOS queues (cmdQueue/snapQueue) by
// memcpy, so their layout must stay POD-like.
#pragma once

#include <cstdint>
#include <type_traits>

enum class PowerState : uint8_t { Off, SlideOn, On, SlideOff };
enum class BaseMode   : uint8_t { Solid, Makeup };
enum class EffectId   : uint8_t { None = 0, Dark, Rainbow, Wave, Breathe, Embers, Candle };  // temporary effects (see kEffects[])
// What the JSON Light `effect` field asks for. Every temporary effect is
// `Temporary` + LightCommand::effectId, so adding an effect never touches
// this enum.
enum class EffectRequest : uint8_t { None, Solid, Makeup, Random, Temporary };
enum class CommandType   : uint8_t { Light, Automation, Makeup, NightMode, RandomEffect, Glitch };

// Precondition for consumers (Mirror): every present value is already
// clamped to 0..255 by Protocol (parseLight), and -1 means "field absent" —
// Mirror casts to uint8_t without re-checking.
struct LightCommand {
    int8_t        state      = -1;  // -1 none, 0 OFF, 1 ON
    int16_t       brightness = -1;  // -1 none, else 0..255
    int16_t       r = -1, g = -1, b = -1;
    EffectRequest effect     = EffectRequest::None;
    EffectId      effectId   = EffectId::None;  // used only when effect == Temporary
};
static_assert(std::is_trivially_copyable<LightCommand>::value,
              "LightCommand crosses cmdQueue by memcpy");

struct Command {
    CommandType  type = CommandType::Light;
    LightCommand light;         // for Light
    bool         flag = false;  // for Automation / Makeup / NightMode / Glitch
};
static_assert(std::is_trivially_copyable<Command>::value,
              "Command crosses cmdQueue by memcpy");

struct StateSnapshot {
    bool     on = false;  // SLIDE_ON / ON
    uint8_t  brightness = 255;
    uint8_t  r = 255, g = 140, b = 50;
    BaseMode base = BaseMode::Solid;
    EffectId effect = EffectId::None;
    bool     automation = true;
    bool     nightMode = false;
    bool     pir = false;
    bool     glitch = true;  // glitch overlay enabled (v1.1.0)
};
static_assert(std::is_trivially_copyable<StateSnapshot>::value,
              "StateSnapshot crosses snapQueue by memcpy");

inline bool operator==(const StateSnapshot& a, const StateSnapshot& b) {
    return a.on == b.on &&
           a.brightness == b.brightness &&
           a.r == b.r && a.g == b.g && a.b == b.b &&
           a.base == b.base &&
           a.effect == b.effect &&
           a.automation == b.automation &&
           a.nightMode == b.nightMode &&
           a.pir == b.pir &&
           a.glitch == b.glitch;
}

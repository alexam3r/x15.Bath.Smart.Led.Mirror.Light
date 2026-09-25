// Soak test for Mirror + Button (ARCHITECTURE.md section 4).
//
// Mirror and the real Button class are driven in the main.cpp loop order
// (commands, button, PIR, tick, frame, snapshot) by pseudo-random command,
// button and PIR streams, with irregular loop intervals and a millis() clock
// that crosses the 2^32 ms wraparound. After every loop iteration the state
// is checked against invariants and against a reference model of the timers
// that the harness keeps in 64-bit time, which never wraps: the auto-off
// moment, the pre-auto-off warning, the auto-effect deadline, the PIR
// cooldown/blackout and the stuck button/PIR limits must agree with it to
// the loop. Fixed long scenarios (a button or PIR stuck for weeks, a button
// stuck across a reboot, weeks on or off across the wrap, colour and warning
// fades vs the glitch, commands ending the warning) run through the same
// checker.
//
// Deterministic: fixed seeds and clocks. SOAK_DAYS=<days> lengthens the long
// random runs for a local soak. On a violation the run stops; the failure
// message names the run and the invariant, and `pio test -v` also shows the
// state and the tail of the event log (t = simulated seconds since the run
// started, ms = the millis() value Mirror saw).
#include <unity.h>

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>

// The invariants read Mirror's internals (timers, ramps, glitch). Every
// standard header the firmware headers use is already included above, so
// the redefinition only opens up the firmware classes.
#define private public
#include "Button.h"
#include "Config.h"
#include "Mirror.h"
#include "Types.h"
#include "effects/EffectRegistry.h"
#undef private

void setUp(void) {}
void tearDown(void) {}

namespace {

constexpr uint64_t kMinute = 60ULL * 1000;
constexpr uint64_t kHour = 60 * kMinute;
constexpr uint64_t kDay = 24 * kHour;

// ----------------------------------------------------------------- random

struct XorShift {
    uint64_t s = 1;

    void seed(uint64_t v) {
        s = v * 0x9E3779B97F4A7C15ULL + 0x632BE59BD9B4E019ULL;
        if (s == 0) s = 1;
    }
    uint64_t next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return s;
    }
    uint32_t below(uint32_t n) { return n ? static_cast<uint32_t>(next() % n) : 0; }
    uint32_t range(uint32_t lo, uint32_t hi) { return lo + below(hi - lo + 1); }
    bool chance(uint32_t perMillion) { return below(1000000) < perMillion; }
};

XorShift g_mirrorRng;  // behind Mirror's RandomFn: effects, glitch, delays
uint32_t mirrorRandom(uint32_t bound) { return g_mirrorRng.below(bound); }

// ----------------------------------------------------------------- event log

constexpr int kLogLines = 48;
constexpr int kLogWidth = 176;
char g_log[kLogLines][kLogWidth];
int g_logNext = 0;
int g_logCount = 0;

void logReset() {
    g_logNext = 0;
    g_logCount = 0;
}

void logDump() {
    const int first = (g_logNext - g_logCount + kLogLines) % kLogLines;
    for (int i = 0; i < g_logCount; ++i) printf("  %s\n", g_log[(first + i) % kLogLines]);
}

// The first violation, for the Unity failure message; the state and the
// event log tail go to stdout, which `pio test -v` shows.
char g_violation[320] = "";
#define RIG_OK(expr) TEST_ASSERT_TRUE_MESSAGE((expr), g_violation)

// ----------------------------------------------------------------- helpers

const char* powerName(PowerState p) {
    switch (p) {
        case PowerState::Off: return "OFF";
        case PowerState::SlideOn: return "SLIDE_ON";
        case PowerState::On: return "ON";
        case PowerState::SlideOff: return "SLIDE_OFF";
    }
    return "?";
}

const char* fxName(EffectId id) {
    const char* n = effectName(id);
    return n ? n : "none";
}

const char* buttonName(ButtonEventType t) {
    switch (t) {
        case ButtonEventType::None: return "none";
        case ButtonEventType::Click: return "click";
        case ButtonEventType::HoldStart: return "hold-start";
        case ButtonEventType::HoldTick: return "hold-tick";
        case ButtonEventType::HoldEnd: return "hold-end";
    }
    return "?";
}

bool isLit(PowerState p) { return p == PowerState::SlideOn || p == PowerState::On; }

uint32_t autoOffLimit(BaseMode b) { return b == BaseMode::Makeup ? cfg::AUTO_OFF_MAKEUP_MS : cfg::AUTO_OFF_MS; }

// The base colour the snapshot asks for (section 4.2).
Rgbw baseColour(const StateSnapshot& s) {
    return s.base == BaseMode::Makeup ? Rgbw{0, 0, 0, 255} : Rgbw{s.r, s.g, s.b, 0};
}

bool isDark(const Frame& f) {
    for (uint16_t i = 0; i < Frame::kSize; ++i) {
        const Rgbw& px = f[i];
        if (px.r || px.g || px.b || px.w) return false;
    }
    return true;
}

// v1.5.0: the night-mode confirmation on the dark ring — the default warm
// colour over the whole ring, no brighter than the SIGNAL_LEVEL peak.
bool isConfirmationFrame(const Frame& f) {
    const Rgbw peak = scale(Rgbw{cfg::DEFAULT_R, cfg::DEFAULT_G, cfg::DEFAULT_B, 0}, cie8(cfg::SIGNAL_LEVEL));
    const Rgbw& px = f[0];
    if (px.r > peak.r || px.g > peak.g || px.b > peak.b || px.w != 0) return false;
    for (uint16_t i = 1; i < Frame::kSize; ++i) {
        if (!(f[i] == px)) return false;
    }
    return true;
}

// Longest run of each temporary effect in steps (section 4.2 table).
uint32_t effectMaxSteps(EffectId id) {
    switch (id) {
        case EffectId::Dark:
        case EffectId::Rainbow: return cfg::TOTAL_LEDS + cfg::SNAKE_SIZE + 1;
        case EffectId::Wave: return cfg::TOTAL_LEDS / 2 + cfg::WAVE_RADIUS + 1;
        case EffectId::Flame: return cfg::FLAME_STEPS;
        case EffectId::Embers: return cfg::EMBERS_STEPS;
        case EffectId::Comet: return cfg::TOTAL_LEDS + cfg::COMET_TAIL + 1;
        case EffectId::None: break;
    }
    return 0;
}

void describe(const Command& c, char* out, size_t n) {
    switch (c.type) {
        case CommandType::Light: {
            const LightCommand& l = c.light;
            static const char* const kReq[] = {"-", "solid", "makeup", "random", "temporary"};
            snprintf(out, n, "cmd set state=%d bri=%d rgb=%d,%d,%d effect=%s%s%s", l.state, l.brightness, l.r, l.g,
                     l.b, kReq[static_cast<int>(l.effect)], l.effect == EffectRequest::Temporary ? ":" : "",
                     l.effect == EffectRequest::Temporary ? fxName(l.effectId) : "");
            break;
        }
        case CommandType::Automation: snprintf(out, n, "cmd motion/set %s", c.flag ? "ON" : "OFF"); break;
        case CommandType::Makeup: snprintf(out, n, "cmd makeup/set %s", c.flag ? "ON" : "OFF"); break;
        case CommandType::NightMode: snprintf(out, n, "cmd motion_disable/set %s", c.flag ? "ON" : "OFF"); break;
        case CommandType::RandomEffect: snprintf(out, n, "cmd effect/set"); break;
        case CommandType::Glitch: snprintf(out, n, "cmd glitch/set %s", c.flag ? "ON" : "OFF"); break;
    }
}

Command lightCmd(int state, int brightness = -1) {
    Command c;
    c.type = CommandType::Light;
    c.light.state = static_cast<int8_t>(state);
    c.light.brightness = static_cast<int16_t>(brightness);
    return c;
}

Command colourCmd(int r, int g, int b) {
    Command c;
    c.type = CommandType::Light;
    c.light.r = static_cast<int16_t>(r);
    c.light.g = static_cast<int16_t>(g);
    c.light.b = static_cast<int16_t>(b);
    return c;
}

Command flagCmd(CommandType type, bool flag) {
    Command c;
    c.type = type;
    c.flag = flag;
    return c;
}

// How long a condition has held without a break, and the longest loop
// interval seen meanwhile: a loop stall legitimately stretches anything that
// advances once per tick. A new `key` (e.g. a ramp's start time) restarts it.
struct Span {
    bool     on = false;
    uint64_t since = 0;
    uint32_t maxDt = 0;
    uint32_t key = 0;

    uint64_t update(bool cond, uint32_t k, uint64_t t, uint32_t dt) {
        if (!cond) {
            on = false;
            return 0;
        }
        if (!on || k != key) {
            on = true;
            since = t;
            maxDt = dt;
            key = k;
            return 0;
        }
        if (dt > maxDt) maxDt = dt;
        return t - since;
    }
};

// ----------------------------------------------------------------- the rig

struct Stats {
    uint64_t loops = 0;
    uint64_t wraps = 0;
    uint64_t powerOns = 0;
    uint64_t autoOffs = 0;
    uint64_t autoEffects = 0;
    uint64_t glitches = 0;
    uint64_t warnings = 0;
    uint64_t holdTicks = 0;
    uint64_t stuckButtons = 0;  // presses that outlasted HOLD_STUCK_MS
    uint64_t stuckPirs = 0;     // PIR HIGH phases that outlasted PIR_STUCK_MS
    uint64_t nightToggles = 0;  // holds with the mirror off (v1.5.0)
    uint64_t lastHoldTickT = 0;
    uint64_t lastAutoOffT = 0;
};

// Mirror + Button + the reference model + the invariant checker. Inputs are
// the `pressed` / `pir` levels and the commands passed to loop().
class Rig {
public:
    Rig(const char* label, uint32_t startMs, uint64_t mirrorSeed) : label_(label), m_(mirrorRandom), now_(startMs) {
        g_mirrorRng.seed(mirrorSeed);
        logReset();
        m_.begin(now_);  // begin() counts as activity: t = 0
        if (m_.takeFrameDirty()) shown_ = m_.frame();
    }

    // One main.cpp loop iteration, dt ms after the previous one.
    bool loop(uint32_t dt, const Command* cmds = nullptr, int ncmd = 0) {
        if (failed_) return false;
        const uint32_t prev = now_;
        now_ += dt;
        t_ += dt;
        dt_ = dt;
        ++stats.loops;
        if (now_ < prev) {
            ++stats.wraps;
            note("millis() wrapped");
        }
        bool input = false;
        for (int i = 0; i < ncmd && !failed_; ++i) {
            onCommand(cmds[i]);
            input = true;
        }
        if (!failed_ && onButtonLevel()) input = true;
        if (!failed_) onPirLevel();
        if (!failed_) onTick(input);
        return !failed_;
    }

    bool send(const Command& c, uint32_t dt = 5) { return loop(dt, &c, 1); }

    // Loops every dt ms for `ms` of simulated time with the current inputs.
    bool run(uint64_t ms, uint32_t dt) {
        const uint64_t end = t_ + ms;
        while (t_ < end) {
            if (!loop(dt)) return false;
        }
        return true;
    }

    void note(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
        char* line = g_log[g_logNext];
        const int prefix = snprintf(line, kLogWidth, "t=%.3f ms=%lu ", t_ / 1000.0, static_cast<unsigned long>(now_));
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(line + prefix, kLogWidth - static_cast<size_t>(prefix), fmt, ap);  // truncated if too long
        va_end(ap);
        g_logNext = (g_logNext + 1) % kLogLines;
        if (g_logCount < kLogLines) ++g_logCount;
    }

    bool ok() const { return !failed_; }
    Mirror& mirror() { return m_; }
    uint64_t t() const { return t_; }

    bool pressed = false;
    bool pir = false;
    Stats stats;

private:
    void fail(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
        if (failed_) return;
        failed_ = true;
        char msg[256];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(msg, sizeof msg, fmt, ap);
        va_end(ap);
        snprintf(g_violation, sizeof g_violation, "[%s, t=%.3f s] %s", label_, t_ / 1000.0, msg);
        const StateSnapshot s = m_.snapshot();
        printf("\n=== SOAK VIOLATION in \"%s\" at t=%.3f s (%.2f days), millis()=%lu, loop %llu\n", label_,
               t_ / 1000.0, t_ / static_cast<double>(kDay), static_cast<unsigned long>(now_),
               static_cast<unsigned long long>(stats.loops));
        printf("%s\n", msg);
        printf("state: power=%s base=%s effect=%s pending=%s bri=%u rgb=%u,%u,%u automation=%d night=%d "
               "glitchFlag=%d pir=%d pressed=%d warning=%d warnLevel=%u trans=%d warnRamp=%d glitch=%d\n",
               powerName(m_.power()), baseModeName(s.base), fxName(s.effect), fxName(m_.pending_), s.brightness, s.r,
               s.g, s.b, s.automation, s.nightMode, s.glitch, pir, pressed, m_.warning_, m_.warnLevel_,
               m_.trans_.active, m_.warn_.active, m_.glitch_.active());
        printf("reference: idle %.3f s since activity, %.3f s since lastIdle, cooldown=%d blackout=%d\n",
               (t_ - lastActivity_) / 1000.0, (t_ - lastIdle_) / 1000.0, cooldownRunning(), blackoutRunning());
        printf("--- last %d events:\n", g_logCount);
        logDump();
        fflush(stdout);
    }

    // markActivity() in the reference: resets the auto-off and auto-effect timers.
    void activity() { lastActivity_ = lastIdle_ = t_; }

    bool cooldownRunning() const { return cooldownOn_ && t_ - cooldownStart_ < cfg::PIR_COOLDOWN_MS; }
    bool blackoutRunning() const { return blackoutOn_ && t_ - blackoutStart_ < cfg::PIR_BLACKOUT_MS; }

    // Power changes made by an input (command, button, PIR) — table 4.1.
    void inputChangedPower(PowerState p0, PowerState p1) {
        if (p0 == p1) return;
        powerTouched_ = true;
        if (!isLit(p0) && isLit(p1)) {
            // powerOn(): activity; clears night mode, cooldown and blackout.
            ++stats.powerOns;
            activity();
            cooldownOn_ = false;
            blackoutOn_ = false;
        } else if (isLit(p0) && p1 == PowerState::SlideOff) {
            // Every input power-off is manual: 15 s PIR cooldown.
            cooldownOn_ = true;
            cooldownStart_ = t_;
        } else {
            fail("input changed power %s -> %s (table 4.1 allows only powerOn / powerOff)", powerName(p0),
                 powerName(p1));
        }
    }

    void onCommand(const Command& c) {
        const PowerState p0 = m_.power();
        const StateSnapshot s0 = m_.snapshot();
        m_.apply(c, now_);
        const PowerState p1 = m_.power();
        char text[112];
        describe(c, text, sizeof text);
        note("%s  [%s -> %s]", text, powerName(p0), powerName(p1));
        inputChangedPower(p0, p1);
        switch (c.type) {
            case CommandType::Light:
            case CommandType::Makeup:
            case CommandType::RandomEffect:
                // v1.2.1: a command to a lit mirror is activity — whoever sent
                // it is there, even out of the PIR's sight.
                if (isLit(p1)) activity();
                break;
            case CommandType::Automation:
                if (c.flag) activity();                             // Ruling R9
                if (c.flag && !s0.automation) cooldownOn_ = false;  // only a real off -> on clears the cooldown
                break;
            case CommandType::NightMode:
                if (!c.flag && s0.nightMode) cooldownOn_ = false;  // Ruling R18
                break;
            case CommandType::Glitch: break;
        }
        // v1.2.1: picking solid/makeup in the effect list ends a running or
        // pending temporary effect.
        if (c.type == CommandType::Light &&
            (c.light.effect == EffectRequest::Solid || c.light.effect == EffectRequest::Makeup) &&
            (m_.effect_ != EffectId::None || m_.pending_ != EffectId::None)) {
            fail("solid/makeup picked but effect=%s pending=%s kept going", fxName(m_.effect_), fxName(m_.pending_));
        }
    }

    // Returns true when the button produced an event.
    bool onButtonLevel() {
        // v1.2.1: a button already pressed at the first sample (boot) is not
        // a press until it has been seen released — no events till then.
        if (firstSample_) {
            firstSample_ = false;
            heldAtBoot_ = pressed;
        }
        const bool ignored = heldAtBoot_;
        if (heldAtBoot_ && !pressed) heldAtBoot_ = false;
        if (pressed && !prevPressed_) {
            pressStart_ = t_;
            stuckNoted_ = false;
        }
        prevPressed_ = pressed;
        if (pressed && !stuckNoted_ && t_ - pressStart_ > cfg::HOLD_STUCK_MS) {
            stuckNoted_ = true;
            ++stats.stuckButtons;
            note("button pressed for over HOLD_STUCK_MS: stuck");
        }
        const ButtonEvent ev = btn_.update(pressed, now_);
        if (ev.type == ButtonEventType::None) return false;
        if (ignored) {
            fail("button %s from a press held since boot", buttonName(ev.type));
            return true;
        }

        if (ev.type == ButtonEventType::HoldTick) {
            // v1.2.1: a press longer than HOLD_STUCK_MS is a stuck button. It
            // must stop dimming (and keeping the mirror on) until released —
            // also after the press outlasts the 49.7-day millis() period.
            if (t_ - pressStart_ > cfg::HOLD_STUCK_MS) {
                fail("HoldTick %.3f s into a press (stuck-button limit %lu ms)", (t_ - pressStart_) / 1000.0,
                     static_cast<unsigned long>(cfg::HOLD_STUCK_MS));
                return true;
            }
            ++stats.holdTicks;
            ++ticksThisHold_;
            stats.lastHoldTickT = t_;
        }
        const PowerState p0 = m_.power();
        const bool night0 = m_.snapshot().nightMode;
        m_.onButton(ev, now_);
        const PowerState p1 = m_.power();
        if (ev.type == ButtonEventType::HoldEnd) {
            note("button hold-end after %lu hold ticks  [%s] bri=%u", static_cast<unsigned long>(ticksThisHold_),
                 powerName(p1), m_.brightness_);
        } else if (ev.type != ButtonEventType::HoldTick) {
            note("button %s x%u  [%s -> %s]", buttonName(ev.type), ev.clicks, powerName(p0), powerName(p1));
        }
        activity();  // every button event (section 4.3)
        inputChangedPower(p0, p1);
        const StateSnapshot s1 = m_.snapshot();
        if (ev.type == ButtonEventType::Click && !isLit(p0) && isLit(p1) && !s1.automation) {
            fail("a click switched the light on but left automation off (v1.5.0)");
            return true;
        }
        if (ev.type == ButtonEventType::HoldStart) {
            holdActive_ = true;
            ticksThisHold_ = 0;
            if (!isLit(p0)) {
                // v1.5.0: a hold with the mirror off or sliding out toggles
                // night mode and never touches power; the ring confirms it.
                if (p1 != p0 || s1.nightMode == night0) {
                    fail("hold from %s: power -> %s, night mode %d -> %d (expected a toggle, same power)",
                         powerName(p0), powerName(p1), night0, s1.nightMode);
                    return true;
                }
                if (s1.automation == s1.nightMode) {  // v1.5.1: automation follows, for HA
                    fail("hold from %s: night mode %d but automation %d", powerName(p0), s1.nightMode,
                         s1.automation);
                    return true;
                }
                ++stats.nightToggles;
                if (p0 == PowerState::Off) {
                    blinkOn_ = true;
                    blinkStart_ = t_;
                } else {
                    blinkPending_ = true;
                }
                if (night0) {  // Ruling R14: the hold that clears night mode starts the cooldown
                    cooldownOn_ = true;
                    cooldownStart_ = t_;
                }
            }
        } else if (ev.type == ButtonEventType::HoldEnd) {
            holdActive_ = false;
        }
        return true;
    }

    void onPirLevel() {
        if (pir && !pirPrev_) {
            pirHighSince_ = t_;
            pirStuckNoted_ = false;
        }
        if (pir != pirPrev_) note("PIR %s", pir ? "HIGH" : "LOW");
        pirPrev_ = pir;
        // v1.2.1: HIGH without a break for over PIR_STUCK_MS is a stuck
        // sensor — no activity, no auto-on — until it goes LOW.
        const bool stuck = pir && t_ - pirHighSince_ > cfg::PIR_STUCK_MS;
        if (stuck && !pirStuckNoted_) {
            pirStuckNoted_ = true;
            ++stats.stuckPirs;
            note("PIR HIGH for over PIR_STUCK_MS: stuck");
        }
        const PowerState p0 = m_.power();
        const StateSnapshot s0 = m_.snapshot();
        // Auto-on rule (section 4.4), evaluated by the reference model.
        const bool autoOn = pir && !stuck && p0 == PowerState::Off && s0.automation && !s0.nightMode &&
                            !cooldownRunning() && !blackoutRunning();
        m_.onPir(pir, now_);
        const PowerState p1 = m_.power();
        const bool switchedOn = p0 == PowerState::Off && p1 == PowerState::SlideOn;
        if (switchedOn != autoOn || (!switchedOn && p1 != p0)) {
            fail("PIR %s: power %s -> %s, reference auto-on=%d (stuck=%d automation=%d night=%d cooldown=%d "
                 "blackout=%d)",
                 pir ? "HIGH" : "LOW", powerName(p0), powerName(p1), autoOn, stuck, s0.automation, s0.nightMode,
                 cooldownRunning(), blackoutRunning());
            return;
        }
        if (switchedOn) {
            note("PIR switched the mirror on");
            inputChangedPower(p0, p1);
        }
        if (pir && !stuck && isLit(p1)) activity();  // Ruling R9: regardless of automation
    }

    void onTick(bool input) {
        const PowerState p0 = m_.power();
        const StateSnapshot s0 = m_.snapshot();
        const EffectId fx0 = m_.effect_;
        const EffectId pending0 = m_.pending_;
        const uint32_t nextFx0 = m_.nextAutoEffectMs_;
        const bool warning0 = m_.warning_;
        const uint64_t sinceIdle0 = t_ - lastIdle_;

        m_.tick(now_);
        if (m_.takeFrameDirty()) shown_ = m_.frame();

        const PowerState p = m_.power();
        const StateSnapshot s = m_.snapshot();
        const bool automationActive = s.automation && !s.nightMode;
        const uint64_t idle = t_ - lastActivity_;
        const uint32_t limit = autoOffLimit(s0.base);

        // --- Power changes made by tick() itself (table 4.1).
        if (p != p0) {
            if (p0 == PowerState::SlideOn && p == PowerState::On) {
                note("slide-on finished");
            } else if (p0 == PowerState::On && p == PowerState::SlideOff) {
                // Auto-off: only with automation active and only once the idle
                // time by the reference clock exceeds the base mode's limit.
                if (!automationActive || idle <= limit)
                    return fail("auto-off too early: idle %.3f s, limit %lu ms, automation active=%d", idle / 1000.0,
                                static_cast<unsigned long>(limit), automationActive);
                ++stats.autoOffs;
                stats.lastAutoOffT = t_;
                note("AUTO-OFF after %.3f s idle (base %s)", idle / 1000.0, baseModeName(s0.base));
            } else if (p0 == PowerState::SlideOff && p == PowerState::Off) {
                // applyDefaults() on reaching OFF resets targets, shown values
                // and the warning; the blackout starts.
                const Rgbw def{cfg::DEFAULT_R, cfg::DEFAULT_G, cfg::DEFAULT_B, 0};
                if (s.r != cfg::DEFAULT_R || s.g != cfg::DEFAULT_G || s.b != cfg::DEFAULT_B ||
                    s.brightness != cfg::DEFAULT_BRIGHTNESS || s.base != BaseMode::Solid ||
                    s.effect != EffectId::None || m_.pending_ != EffectId::None || !(m_.shownColor_ == def) ||
                    m_.shownBrightness_ != cfg::DEFAULT_BRIGHTNESS || m_.warning_ || m_.warnLevel_ != 255)
                    return fail("OFF reached without applyDefaults(): rgb=%u,%u,%u bri=%u base=%s shown bri=%u",
                                s.r, s.g, s.b, s.brightness, baseModeName(s.base), m_.shownBrightness_);
                blackoutOn_ = true;
                blackoutStart_ = t_;
                if (blinkPending_) {
                    blinkPending_ = false;
                    blinkOn_ = true;
                    blinkStart_ = t_;
                }
                note("OFF reached");
            } else {
                return fail("tick() changed power %s -> %s", powerName(p0), powerName(p));
            }
        } else if (s.r != s0.r || s.g != s0.g || s.b != s0.b || s.brightness != s0.brightness ||
                   s.base != s0.base) {
            // Targets change only through inputs; tick() touches them only
            // through applyDefaults() on reaching OFF.
            return fail("tick() changed the targets: rgb %u,%u,%u -> %u,%u,%u bri %u -> %u", s0.r, s0.g, s0.b, s.r,
                        s.g, s.b, s0.brightness, s.brightness);
        }
        if (s.automation != s0.automation || s.nightMode != s0.nightMode || s.glitch != s0.glitch)
            return fail("tick() changed a flag (automation / night mode / glitch)");

        // --- Effects started or ended by tick().
        if (m_.effect_ != fx0) {
            lastIdle_ = t_;
            if (fx0 == EffectId::None) {
                if (p0 == PowerState::SlideOn) {
                    if (m_.effect_ != pending0)
                        return fail("slide-on finished with effect %s, pending was %s", fxName(m_.effect_),
                                    fxName(pending0));
                    note("pending effect %s started", fxName(m_.effect_));
                } else {
                    // Auto effect: automation active, solid base, and the time
                    // since lastIdle past its randomised delay.
                    if (!automationActive || s.base != BaseMode::Solid || sinceIdle0 <= nextFx0)
                        return fail("auto effect %s too early: %.3f s since lastIdle, delay %lu ms, base %s",
                                    fxName(m_.effect_), sinceIdle0 / 1000.0, static_cast<unsigned long>(nextFx0),
                                    baseModeName(s.base));
                    ++stats.autoEffects;
                    note("auto effect %s started", fxName(m_.effect_));
                }
            } else if (m_.effect_ != EffectId::None) {
                return fail("tick() switched effect %s -> %s", fxName(fx0), fxName(m_.effect_));
            } else {
                note("effect %s ended", fxName(fx0));
            }
        }
        if (m_.warning_ != warning0) {
            if (m_.warning_) ++stats.warnings;
            note("warning %s", m_.warning_ ? "started (dim to 50%)" : "ended");
        }

        // --- The snapshot reports what the mirror does; pir is the raw
        // level even while the sensor is ignored as stuck.
        if (s.on != isLit(p)) return fail("snapshot.on=%d while power=%s", s.on, powerName(p));
        if (s.pir != pir) return fail("snapshot.pir=%d but the PIR level is %d", s.pir, pir);
        if (s.effect != m_.effect_) return fail("snapshot.effect differs from the running effect");

        // --- Every power-on clears night mode; night mode ON powers off.
        if (isLit(p) && s.nightMode) return fail("lit with night mode on");

        // --- OFF means dark, except for the night-mode confirmation (v1.5.0):
        // only right after a hold, only the warm flash, and any power-on
        // ends it.
        if (isLit(p)) blinkOn_ = blinkPending_ = false;
        if (p == PowerState::Off && !isDark(shown_)) {
            const uint64_t blinkMs = cfg::SIGNAL_ON_FLASH_MS > 2 * cfg::SIGNAL_OFF_FLASH_MS + cfg::SIGNAL_OFF_GAP_MS
                                         ? cfg::SIGNAL_ON_FLASH_MS
                                         : 2 * cfg::SIGNAL_OFF_FLASH_MS + cfg::SIGNAL_OFF_GAP_MS;
            if (!blinkOn_ || t_ - blinkStart_ > blinkMs + cfg::SIGNAL_STEP_MS)
                return fail("OFF but the displayed frame is not dark (no confirmation due)");
            if (!isConfirmationFrame(shown_)) return fail("OFF: the frame is not the night-mode confirmation");
        }

        // --- Auto-off is never late: ON with automation active is never
        // idle past the limit (and never early: see the transition above).
        if (p == PowerState::On && automationActive && idle > limit)
            return fail("auto-off late: idle %.3f s > limit %lu ms", idle / 1000.0, static_cast<unsigned long>(limit));

        // --- The pre-auto-off warning is on exactly inside the last
        // AUTO_OFF_WARN_MS before auto-off; any activity ends it; without
        // automation (or while sliding on) there is none; OFF resets it.
        if (p == PowerState::On && automationActive) {
            const bool due = idle > limit - cfg::AUTO_OFF_WARN_MS;
            if (m_.warning_ != due)
                return fail("warning=%d but %.3f s idle (warning from %lu ms idle)", m_.warning_, idle / 1000.0,
                            static_cast<unsigned long>(limit - cfg::AUTO_OFF_WARN_MS));
        } else if (isLit(p) && m_.warning_) {
            return fail("warning while %s with automation active=%d", powerName(p), automationActive);
        }
        if (p == PowerState::Off && (m_.warning_ || m_.warnLevel_ != 255 || m_.warn_.active))
            return fail("OFF with warning state left over (level %u)", m_.warnLevel_);

        // --- The warning fade settles on 50 % while warning, 100 % otherwise.
        if (isLit(p) && !m_.warn_.active && m_.warnLevel_ != (m_.warning_ ? cfg::WARN_DIM_LEVEL : 255))
            return fail("warn level %u settled with warning=%d", m_.warnLevel_, m_.warning_);

        // --- The auto effect is never late: a quiet solid ON mirror with
        // automation starts one within its randomised 4..5 min delay.
        if (p == PowerState::On && automationActive && s.base == BaseMode::Solid && m_.effect_ == EffectId::None) {
            if (m_.nextAutoEffectMs_ < cfg::AUTO_EFFECT_MIN_MS || m_.nextAutoEffectMs_ >= cfg::AUTO_EFFECT_MAX_MS)
                return fail("auto-effect delay %lu ms out of range", static_cast<unsigned long>(m_.nextAutoEffectMs_));
            if (t_ - lastIdle_ > m_.nextAutoEffectMs_)
                return fail("auto effect late: %.3f s since lastIdle, delay %lu ms", (t_ - lastIdle_) / 1000.0,
                            static_cast<unsigned long>(m_.nextAutoEffectMs_));
        }

        // --- A running effect only while ON, a pending one only while SLIDE_ON.
        if (m_.effect_ != EffectId::None && p != PowerState::On)
            return fail("effect %s while %s", fxName(m_.effect_), powerName(p));
        if (m_.pending_ != EffectId::None && p != PowerState::SlideOn)
            return fail("pending effect %s while %s", fxName(m_.pending_), powerName(p));

        // --- Outside a transition the shown colour/brightness are the targets.
        const Rgbw target = baseColour(s);
        if (!m_.trans_.active && (!(m_.shownColor_ == target) || m_.shownBrightness_ != s.brightness))
            return fail("no transition but shown %u,%u,%u,%u @%u != target %u,%u,%u,%u @%u", m_.shownColor_.r,
                        m_.shownColor_.g, m_.shownColor_.b, m_.shownColor_.w, m_.shownBrightness_, target.r, target.g,
                        target.b, target.w, s.brightness);

        // --- A quiet ON frame is the plain base at brightness x warn level
        // (the warn level is perceived brightness, CIE 1931, v1.3.0).
        if (p == PowerState::On && m_.effect_ == EffectId::None && !m_.trans_.active && !m_.warn_.active &&
            !m_.glitch_.active()) {
            const uint8_t level = m_.warning_ ? cfg::WARN_DIM_LEVEL : 255;
            const Rgbw want = scale(target, scale8(s.brightness, cie8(level)));
            for (uint16_t i = 0; i < Frame::kSize; ++i) {
                if (!(shown_[i] == want))
                    return fail("static frame px %u = %u,%u,%u,%u, expected %u,%u,%u,%u", i, shown_[i].r, shown_[i].g,
                                shown_[i].b, shown_[i].w, want.r, want.g, want.b, want.w);
            }
        }

        // --- The glitch runs only over a quiet, solid, lit mirror with
        // automation, never while the button is held and (v1.2.1) never
        // during a colour transition or the warning fade (section 4.6).
        const bool glitchMayRun = s.glitch && p == PowerState::On && m_.effect_ == EffectId::None &&
                                  m_.pending_ == EffectId::None && s.base == BaseMode::Solid && automationActive &&
                                  !holdActive_ && !m_.trans_.active && !m_.warn_.active;
        const bool glitchOn = m_.glitch_.active();
        if (glitchOn && !glitchMayRun)
            return fail("glitch running while not allowed (trans=%d warnRamp=%d holding=%d effect=%s base=%s)",
                        m_.trans_.active, m_.warn_.active, holdActive_, fxName(m_.effect_), baseModeName(s.base));
        if (glitchOn && (!glitch_.on || m_.glitch_.startMs_ != glitch_.key)) {
            ++stats.glitches;
            note("glitch at px %u x%u for %lu ms", m_.glitch_.first(), m_.glitch_.length(),
                 static_cast<unsigned long>(m_.glitch_.durationMs()));
        }
        if (glitch_.update(glitchOn, m_.glitch_.startMs_, t_, dt_) >
            cfg::GLITCH_DURATION_MAX_MS + cfg::GLITCH_STEP_MS + 2ULL * glitch_.maxDt)
            return fail("glitch running for %.3f s", (t_ - glitch_.since) / 1000.0);
        // Glitch liveness: allowed without a break for two maximum intervals
        // (plus slack for a due moment skipped on a re-render) yet none started.
        if (glitchIdle_.update(glitchMayRun && !glitchOn, 0, t_, dt_) >
            2ULL * cfg::GLITCH_INTERVAL_MAX_MS + 10000 + 2ULL * glitchIdle_.maxDt)
            return fail("glitch allowed for %.3f s but none started", (t_ - glitchIdle_.since) / 1000.0);

        // --- Ramps end on time: colour transition and warning fade.
        if (trans_.update(m_.trans_.active, m_.trans_.startMs, t_, dt_) >
            cfg::TRANSITION_MS + cfg::TRANSITION_STEP_MS + 2ULL * trans_.maxDt)
            return fail("colour transition running for %.3f s", (t_ - trans_.since) / 1000.0);
        if (warnRamp_.update(m_.warn_.active, m_.warn_.startMs, t_, dt_) >
            cfg::WARN_FADE_IN_MS + cfg::TRANSITION_STEP_MS + 2ULL * warnRamp_.maxDt)
            return fail("warning fade running for %.3f s", (t_ - warnRamp_.since) / 1000.0);

        // --- A slide takes at most SLIDE_MAX_RADIUS + 1 steps.
        if (p != slidePower_ || powerTouched_) {
            slidePower_ = p;
            slideSteps_ = 0;
            slideLastStep_ = now_;
        } else if (p == PowerState::SlideOn || p == PowerState::SlideOff) {
            if (static_cast<uint32_t>(now_ - slideLastStep_) >= cfg::SLIDE_STEP_MS) {
                ++slideSteps_;
                slideLastStep_ = now_;
            }
            if (slideSteps_ > cfg::SLIDE_MAX_RADIUS + 3u)
                return fail("%s lasted %lu steps", powerName(p), static_cast<unsigned long>(slideSteps_));
        }
        powerTouched_ = false;

        // --- A temporary effect ends within its step count (any input may
        // restart it, so inputs restart the count).
        if (m_.effect_ != fxTracked_ || input) {
            fxTracked_ = m_.effect_;
            fxSteps_ = 0;
            fxLastStep_ = now_;
        } else if (m_.effect_ != EffectId::None) {
            if (static_cast<uint32_t>(now_ - fxLastStep_) >= effectInstance(m_.effect_)->stepIntervalMs()) {
                ++fxSteps_;
                fxLastStep_ = now_;
            }
            if (fxSteps_ > effectMaxSteps(m_.effect_) + 3)
                return fail("effect %s ran %lu steps", fxName(m_.effect_), static_cast<unsigned long>(fxSteps_));
        }

        // --- PIR cooldown / blackout agree with the reference (wrap-safe
        // Countdown) and are retired once expired (Ruling R7).
        const Countdown& cd = m_.gate_.cooldown_;
        const Countdown& bo = m_.gate_.blackout_;
        if (cd.running(now_) != cooldownRunning() || bo.running(now_) != blackoutRunning())
            return fail("countdowns: cooldown %d (reference %d), blackout %d (reference %d)", cd.running(now_),
                        cooldownRunning(), bo.running(now_), blackoutRunning());
        if ((cd.active && static_cast<uint32_t>(now_ - cd.startMs) >= cd.durationMs) ||
            (bo.active && static_cast<uint32_t>(now_ - bo.startMs) >= bo.durationMs))
            return fail("expired countdown not retired by tick()");

        // --- Mirror's hold flag follows HoldStart/HoldEnd, and a released
        // button always ends the hold.
        if (m_.holding_ != holdActive_) return fail("Mirror holding=%d, events say %d", m_.holding_, holdActive_);
        if (!pressed && holdActive_) return fail("button released but the hold did not end");
    }

    const char* label_;
    Mirror   m_;
    Button   btn_;
    Frame    shown_;  // what the strips show: the last frame taken when dirty
    uint32_t now_;
    uint64_t t_ = 0;
    uint32_t dt_ = 0;
    bool     failed_ = false;

    // Reference model, in 64-bit time since the run started.
    uint64_t lastActivity_ = 0;
    uint64_t lastIdle_ = 0;
    bool     cooldownOn_ = false;
    uint64_t cooldownStart_ = 0;
    bool     blackoutOn_ = false;
    uint64_t blackoutStart_ = 0;
    bool     pirPrev_ = false;
    uint64_t pirHighSince_ = 0;
    bool     pirStuckNoted_ = false;
    bool     firstSample_ = true;
    bool     heldAtBoot_ = false;
    bool     prevPressed_ = false;
    uint64_t pressStart_ = 0;
    bool     stuckNoted_ = false;
    bool     holdActive_ = false;
    uint32_t ticksThisHold_ = 0;
    // v1.5.0 night-mode confirmation: may light the dark ring from
    // blinkStart_ for one flash sequence (plus one frame step to go dark);
    // a hold during the slide-out leaves it pending until OFF.
    bool     blinkOn_ = false;
    bool     blinkPending_ = false;
    uint64_t blinkStart_ = 0;

    // Duration trackers.
    Span       glitch_, glitchIdle_, trans_, warnRamp_;
    PowerState slidePower_ = PowerState::Off;
    bool       powerTouched_ = false;
    uint32_t   slideSteps_ = 0;
    uint32_t   slideLastStep_ = 0;
    EffectId   fxTracked_ = EffectId::None;
    uint32_t   fxSteps_ = 0;
    uint32_t   fxLastStep_ = 0;
};

// ----------------------------------------------------------------- random soak

struct Profile {
    const char* name;
    uint32_t cmdPpmActive;      // chance per loop of a command burst while someone is there
    uint32_t cmdPpmQuiet;       // ... of a remote command while the room is empty
    uint32_t btnPpm;            // chance per loop of a new button gesture while someone is there
    uint32_t pirStuckPerMille;  // chance that a new PIR phase is a stuck-HIGH one
    uint32_t quietMaxMin;       // longest empty-room phase
    bool     stuckButton;       // allow presses of minutes to hours
};

const Profile kBusy{"busy", 3000, 60, 3000, 60, 90, true};
const Profile kCalm{"calm", 400, 10, 500, 15, 600, false};

// PIR: empty room, someone moving (HIGH/LOW with gaps), a chattering sensor,
// or a sensor stuck HIGH for over PIR_STUCK_MS.
struct PirModel {
    enum Phase : uint8_t { Quiet, Presence, Chatter, Stuck };
    Phase    phase = Quiet;
    uint64_t until = 0;
    uint64_t toggleAt = 0;
    bool     level = false;

    void update(uint64_t t, XorShift& rng, const Profile& p, Rig& rig) {
        if (t >= until) {
            const uint32_t k = rng.below(1000);
            static const char* const kNames[] = {"quiet", "presence", "chatter", "stuck HIGH"};
            if (k < p.pirStuckPerMille) {
                phase = Stuck;
                until = t + rng.range(61, 240) * kMinute;
            } else if (k < p.pirStuckPerMille + 40) {
                phase = Chatter;
                until = t + rng.range(10000, 20 * 60000);
            } else if (k < 520) {
                phase = Presence;
                until = t + rng.range(30000, 40 * 60000);
            } else {
                phase = Quiet;
                until = t + rng.range(60000, p.quietMaxMin * 60000);
            }
            rig.note("PIR phase: %s for %.1f min", kNames[phase], (until - t) / 60000.0);
        }
        switch (phase) {
            case Quiet: level = false; break;
            case Stuck: level = true; break;
            case Chatter:
                if (t >= toggleAt) {
                    level = !level;
                    toggleAt = t + rng.range(30, 120);
                }
                break;
            case Presence:
                if (t >= toggleAt) {
                    level = !level;
                    toggleAt = t + (level ? rng.range(2000, 60000) : rng.range(500, 90000));
                }
                break;
        }
    }
    bool someoneThere() const { return phase == Presence || phase == Chatter; }
};

// A scripted timeline of button levels: clicks, holds, chatter, stuck presses.
class ButtonScript {
public:
    bool idle() const { return i_ >= n_; }

    bool levelAt(uint64_t t) {
        while (i_ < n_ && seg_[i_].until <= t) ++i_;
        if (idle()) n_ = i_ = 0;
        return idle() ? false : seg_[i_].level;
    }

    void gesture(uint64_t t, XorShift& rng, bool allowStuck, Rig& rig) {
        start_ = t;
        const uint32_t k = rng.below(100);
        if (k < 35) {
            clicks(1, rng);
            rig.note("script: 1 click");
        } else if (k < 50) {
            clicks(2, rng);
            rig.note("script: 2 clicks");
        } else if (k < 60) {
            clicks(3, rng);
            rig.note("script: 3 clicks");
        } else if (k < 65) {
            const int n = static_cast<int>(rng.range(4, 9));
            clicks(n, rng);
            rig.note("script: %d clicks", n);
        } else if (k < 90) {
            const uint32_t d = rng.range(600, 12000);
            add(true, d);
            add(false, 700);
            rig.note("script: hold %lu ms", static_cast<unsigned long>(d));
        } else if (k < 97 || !allowStuck) {
            const uint32_t n = rng.range(5, 60);
            for (uint32_t i = 0; i < n; ++i) {
                add(true, rng.range(1, 40));
                add(false, rng.range(1, 60));
            }
            add(false, 700);
            rig.note("script: contact chatter x%lu", static_cast<unsigned long>(n));
        } else {
            const uint64_t d = rng.range(61, 3 * 3600) * 1000ULL;
            add(true, d);
            add(false, 700);
            rig.note("script: button stuck for %.1f min", d / 60000.0);
        }
    }

private:
    struct Seg {
        bool     level;
        uint64_t until;
    };

    void add(bool level, uint64_t dur) {
        const uint64_t from = n_ ? seg_[n_ - 1].until : start_;
        seg_[n_++] = Seg{level, from + dur};
    }
    void clicks(int n, XorShift& rng) {
        for (int i = 0; i < n; ++i) {
            add(true, rng.range(40, 250));
            add(false, rng.range(60, 300));
        }
        add(false, 700);
    }

    Seg      seg_[128];
    int      n_ = 0;
    int      i_ = 0;
    uint64_t start_ = 0;
};

Command randomCommand(XorShift& rng) {
    Command c;
    const uint32_t k = rng.below(100);
    if (k < 45) {
        c.type = CommandType::Light;
        LightCommand& l = c.light;
        const uint32_t s = rng.below(10);
        if (s < 4) {
            l.state = 1;
        } else if (s < 7) {
            l.state = 0;
        }
        if (rng.chance(400000)) l.brightness = static_cast<int16_t>(rng.below(256));
        if (rng.chance(300000)) {
            l.r = static_cast<int16_t>(rng.below(256));
            l.g = static_cast<int16_t>(rng.below(256));
            l.b = static_cast<int16_t>(rng.below(256));
        } else if (rng.chance(100000)) {
            l.r = static_cast<int16_t>(rng.below(256));
        }
        const uint32_t e = rng.below(12);
        if (e == 0) {
            l.effect = EffectRequest::Solid;
        } else if (e == 1) {
            l.effect = EffectRequest::Makeup;
        } else if (e == 2) {
            l.effect = EffectRequest::Random;
        } else if (e <= 4) {
            l.effect = EffectRequest::Temporary;
            l.effectId = static_cast<EffectId>(rng.range(1, 7));
        }
    } else if (k < 58) {
        c = flagCmd(CommandType::Automation, rng.chance(750000));
    } else if (k < 70) {
        c = flagCmd(CommandType::Makeup, rng.chance(500000));
    } else if (k < 78) {
        c = flagCmd(CommandType::NightMode, rng.chance(400000));
    } else if (k < 90) {
        c.type = CommandType::RandomEffect;
    } else {
        c = flagCmd(CommandType::Glitch, rng.chance(750000));
    }
    return c;
}

// A loop interval: mostly the 5 ms loop with jitter, sometimes a slow
// iteration or a stall of seconds.
uint32_t loopInterval(XorShift& rng) {
    const uint32_t r = rng.below(1000);
    if (r < 700) return rng.range(1, 20);
    if (r < 960) return rng.range(20, 500);
    if (r < 998) return rng.range(500, 3000);
    return rng.range(3000, 8000);
}

Stats g_totals;

void addTotals(const Stats& s) {
    g_totals.loops += s.loops;
    g_totals.wraps += s.wraps;
    g_totals.powerOns += s.powerOns;
    g_totals.autoOffs += s.autoOffs;
    g_totals.autoEffects += s.autoEffects;
    g_totals.glitches += s.glitches;
    g_totals.warnings += s.warnings;
    g_totals.holdTicks += s.holdTicks;
    g_totals.stuckButtons += s.stuckButtons;
    g_totals.stuckPirs += s.stuckPirs;
    g_totals.nightToggles += s.nightToggles;
}

// One random run of `durationMs` starting at millis() == startMs. With
// switchOnFirst the mirror is switched on by a command in the first loop.
bool runSoak(const Profile& p, uint64_t seed, uint32_t startMs, uint64_t durationMs, bool switchOnFirst) {
    char label[96];
    snprintf(label, sizeof label, "%s seed %llu, %.2f days from millis()=%lu", p.name,
             static_cast<unsigned long long>(seed), durationMs / static_cast<double>(kDay),
             static_cast<unsigned long>(startMs));
    XorShift rng;
    rng.seed(seed);
    Rig rig(label, startMs, seed + 0x5EED);
    PirModel pirModel;
    ButtonScript button;

    bool ok = true;
    if (switchOnFirst) {
        const Command on = lightCmd(1);
        ok = rig.send(on);
    }
    Command cmds[6];
    while (ok && rig.t() < durationMs) {
        uint32_t dt = loopInterval(rng);
        // Long empty stretches with the mirror dark: coarser loops keep the
        // run fast without skipping anything that happens.
        if (rig.mirror().power() == PowerState::Off && pirModel.phase == PirModel::Quiet && button.idle() &&
            rng.below(10) < 8)
            dt = rng.range(200, 2000);
        const uint64_t t = rig.t() + dt;

        pirModel.update(t, rng, p, rig);
        rig.pir = pirModel.level;

        const bool there = pirModel.someoneThere();
        if (there && button.idle() && rng.chance(p.btnPpm)) button.gesture(t, rng, p.stuckButton, rig);
        rig.pressed = button.levelAt(t);

        int ncmd = 0;
        if (rng.chance(there ? p.cmdPpmActive : p.cmdPpmQuiet)) {
            ncmd = rng.chance(50000) ? static_cast<int>(rng.range(2, 6)) : 1;  // occasional burst
            for (int i = 0; i < ncmd; ++i) cmds[i] = randomCommand(rng);
        }
        ok = rig.loop(dt, cmds, ncmd);
    }
    addTotals(rig.stats);
    return ok;
}

double soakDays(double dflt) {
    const char* v = getenv("SOAK_DAYS");
    const double d = v ? atof(v) : 0.0;
    return d > 0.0 ? d : dflt;
}

// Long runs: the wrap lands at a pseudo-random point inside each run.
void longRuns(const Profile& p, uint64_t firstSeed, int seeds, double days) {
    const uint64_t dur = static_cast<uint64_t>(days * kDay);
    uint64_t span = dur * 9 / 10;
    if (span > 0xF0000000ULL) span = 0xF0000000ULL;
    g_totals = Stats{};
    for (int i = 0; i < seeds; ++i) {
        const uint64_t seed = firstSeed + static_cast<uint64_t>(i);
        XorShift pick;
        pick.seed(seed * 7919);
        const uint32_t before = static_cast<uint32_t>(kMinute + pick.next() % span);  // ms before the wrap
        RIG_OK(runSoak(p, seed, 0u - before, dur, false));
    }
    // The runs must have exercised what the invariants guard.
    TEST_ASSERT_TRUE(g_totals.wraps >= static_cast<uint64_t>(seeds));
    TEST_ASSERT_TRUE(g_totals.autoOffs > 0);
    TEST_ASSERT_TRUE(g_totals.autoEffects > 0);
    TEST_ASSERT_TRUE(g_totals.glitches > 0);
    TEST_ASSERT_TRUE(g_totals.warnings > 0);
    TEST_ASSERT_TRUE(g_totals.holdTicks > 0);
}

void test_random_soak_busy(void) {
    longRuns(kBusy, 1, 3, soakDays(2.0));
    TEST_ASSERT_TRUE_MESSAGE(g_totals.stuckPirs > 0, "no stuck PIR phase exercised");
    TEST_ASSERT_TRUE_MESSAGE(g_totals.nightToggles > 0, "no night-mode hold with the mirror off exercised");
    TEST_ASSERT_TRUE_MESSAGE(g_totals.stuckButtons > 0, "no stuck button exercised");
}

void test_random_soak_calm(void) { longRuns(kCalm, 101, 2, soakDays(3.0)); }

// Many short busy runs that switch the mirror on at most 5 minutes before
// the wrap, so every timer is started just before 2^32 and read after it.
void test_random_soak_across_the_wrap(void) {
    g_totals = Stats{};
    for (uint64_t seed = 1001; seed <= 1040; ++seed) {
        XorShift pick;
        pick.seed(seed);
        const uint32_t before = static_cast<uint32_t>(pick.next() % (5 * kMinute)) + 1;
        RIG_OK(runSoak(kBusy, seed, 0u - before, 25 * kMinute, true));
    }
    TEST_ASSERT_EQUAL_UINT32(40, static_cast<uint32_t>(g_totals.wraps));
    TEST_ASSERT_TRUE(g_totals.autoOffs > 0);
}

// ----------------------------------------------------------------- fixed scenarios

// Loop interval for the weeks-long idle stretches: coarse enough to keep the
// suite fast, fine enough for several loops inside every 60 s window.
constexpr uint32_t kIdleStep = 5000;

// Button stuck pressed for 51 days, from 30 s before the wrap: the hold
// switches the mirror on and dims for HOLD_STUCK_MS, then brightness freezes
// and the mirror auto-offs AUTO_OFF_MS after the last tick. It stays dark
// (no ticks come back when the press itself outlasts the 49.7-day period),
// and after release the button works again.
void test_button_stuck_for_weeks(void) {
    Rig r("button stuck for 51 days", 0u - 30000u, 11);
    Mirror& m = r.mirror();
    RIG_OK(r.loop(5));  // released at boot: the next sample is a real press
    // Switched on with a click, then the button sticks: dimming until
    // HOLD_STUCK_MS, then frozen (v1.5.0: a hold from OFF only toggles night
    // mode, so the light comes on by a click first).
    r.pressed = true;
    RIG_OK(r.run(100, 5));
    r.pressed = false;
    RIG_OK(r.run(cfg::CLICK_GAP_MS + 4000, 5));
    TEST_ASSERT_TRUE_MESSAGE(m.power() == PowerState::On, "the click did not switch the mirror on");
    const uint64_t pressAt = r.t() + 5;
    r.pressed = true;
    RIG_OK(r.run(cfg::HOLD_STUCK_MS + 1000, 5));
    TEST_ASSERT_TRUE_MESSAGE(m.power() == PowerState::On, "the stuck hold switched the mirror off");
    TEST_ASSERT_TRUE(r.stats.holdTicks > 1000);
    const uint64_t lastTick = r.stats.lastHoldTickT;
    TEST_ASSERT_TRUE(lastTick <= pressAt + cfg::HOLD_STUCK_MS);
    const uint8_t frozen = m.brightness_;
    while (r.ok() && m.power() == PowerState::On) {
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(frozen, m.brightness_, "brightness changed after HOLD_STUCK_MS");
        r.loop(20);
    }
    RIG_OK(r.ok());
    TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(r.stats.autoOffs));
    TEST_ASSERT_TRUE(r.stats.lastAutoOffT > lastTick + cfg::AUTO_OFF_MS);
    TEST_ASSERT_TRUE(r.stats.lastAutoOffT <= lastTick + cfg::AUTO_OFF_MS + 20);

    RIG_OK(r.run(51 * kDay, kIdleStep));
    TEST_ASSERT_TRUE(m.power() == PowerState::Off);
    TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(r.stats.powerOns));
    TEST_ASSERT_TRUE(r.stats.wraps >= 2);

    r.pressed = false;
    RIG_OK(r.run(1000, 5));
    r.pressed = true;  // one click
    RIG_OK(r.run(100, 5));
    r.pressed = false;
    RIG_OK(r.run(cfg::CLICK_GAP_MS + 100, 5));
    TEST_ASSERT_TRUE_MESSAGE(isLit(m.power()), "the button did not work again after release");
}

// Rebooted with the button stuck and night mode restored from RTC memory:
// a press already there at boot is no press — no hold, no light, night mode
// kept — for days, until it is released; then the button works as usual.
void test_button_stuck_across_a_reboot(void) {
    Rig r("button stuck across a reboot", 0u - 60000u, 18);
    Mirror& m = r.mirror();
    r.pressed = true;
    RIG_OK(r.send(flagCmd(CommandType::NightMode, true)));
    RIG_OK(r.run(3 * kDay, kIdleStep));
    TEST_ASSERT_TRUE(m.power() == PowerState::Off);
    TEST_ASSERT_TRUE_MESSAGE(m.snapshot().nightMode, "a press held since boot cleared night mode");
    TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(r.stats.powerOns));

    r.pressed = false;
    RIG_OK(r.run(1000, 5));
    r.pressed = true;  // one click
    RIG_OK(r.run(100, 5));
    r.pressed = false;
    RIG_OK(r.run(cfg::CLICK_GAP_MS + 100, 5));
    TEST_ASSERT_TRUE_MESSAGE(isLit(m.power()), "the button did not work after release");
    TEST_ASSERT_FALSE(m.snapshot().nightMode);
}

// PIR stuck HIGH for 51 days, from 10 min before the wrap: it switches the
// mirror on and keeps it on for PIR_STUCK_MS, then counts for nothing: the
// mirror auto-offs AUTO_OFF_MS later and is not relit while the level stays
// HIGH (also once the HIGH outlasts the 49.7-day period). snapshot.pir keeps
// reporting HIGH. The first LOW re-arms the sensor.
void test_pir_stuck_high_for_weeks(void) {
    Rig r("PIR stuck HIGH for 51 days", 0u - 600000u, 12);
    Mirror& m = r.mirror();
    r.pir = true;
    RIG_OK(r.loop(5));
    TEST_ASSERT_TRUE_MESSAGE(isLit(m.power()), "PIR did not switch the mirror on");
    RIG_OK(r.run(2 * kHour, 10));
    TEST_ASSERT_TRUE(m.power() == PowerState::Off);
    TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(r.stats.autoOffs));
    TEST_ASSERT_TRUE(r.stats.lastAutoOffT > cfg::PIR_STUCK_MS + cfg::AUTO_OFF_MS);
    TEST_ASSERT_TRUE(r.stats.lastAutoOffT <= cfg::PIR_STUCK_MS + cfg::AUTO_OFF_MS + 30);

    RIG_OK(r.run(51 * kDay, kIdleStep));
    TEST_ASSERT_TRUE(m.power() == PowerState::Off);
    TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(r.stats.powerOns));
    TEST_ASSERT_TRUE(m.snapshot().pir);

    r.pir = false;
    RIG_OK(r.loop(1000));
    r.pir = true;
    RIG_OK(r.loop(5));
    TEST_ASSERT_TRUE_MESSAGE(isLit(m.power()), "PIR not re-armed by a LOW");
}

// ON for 60 days with automation off (crossing the wrap): it stays on; after
// motion/set ON the auto-off comes AUTO_OFF_MS later, not at once or never.
void test_on_for_60_days_with_automation_off(void) {
    Rig r("ON for 60 days, automation off", 0u - 5000u, 13);
    Mirror& m = r.mirror();
    RIG_OK(r.send(flagCmd(CommandType::Automation, false)));
    RIG_OK(r.send(lightCmd(1)));
    RIG_OK(r.run(10000, 5));
    TEST_ASSERT_TRUE(m.power() == PowerState::On);
    RIG_OK(r.run(60 * kDay, kIdleStep));
    TEST_ASSERT_TRUE(m.power() == PowerState::On);
    TEST_ASSERT_TRUE(r.stats.wraps >= 2);

    RIG_OK(r.send(flagCmd(CommandType::Automation, true)));
    const uint64_t enabledAt = r.t();
    RIG_OK(r.run(cfg::AUTO_OFF_MS - 1000, 20));
    TEST_ASSERT_TRUE_MESSAGE(m.power() == PowerState::On, "auto-off too early after 60 days");
    RIG_OK(r.run(2000, 20));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, static_cast<uint32_t>(r.stats.autoOffs), "no auto-off after 60 days");
    TEST_ASSERT_TRUE(r.stats.lastAutoOffT > enabledAt + cfg::AUTO_OFF_MS);
}

// OFF for 60 days across the wrap, bracketed by manual offs: a cooldown
// that straddles 2^32 is honoured for exactly PIR_COOLDOWN_MS, stale
// countdowns do not block the PIR after 60 days, and a fresh manual off
// holds the PIR off for PIR_COOLDOWN_MS again.
void test_off_for_60_days_across_the_wrap_then_pir(void) {
    Rig r("OFF for 60 days across the wrap", 0u - 10000u, 14);
    Mirror& m = r.mirror();
    RIG_OK(r.send(lightCmd(1)));
    RIG_OK(r.run(5000 - 5, 5));
    TEST_ASSERT_TRUE(m.power() == PowerState::On);
    RIG_OK(r.send(lightCmd(0)));  // manual off 5 s before the wrap
    r.pir = true;
    RIG_OK(r.run(cfg::PIR_COOLDOWN_MS - 100, 5));
    TEST_ASSERT_TRUE_MESSAGE(m.power() == PowerState::Off, "cooldown across the wrap not honoured");
    RIG_OK(r.run(200, 5));
    TEST_ASSERT_TRUE_MESSAGE(isLit(m.power()), "PIR not re-armed after the cooldown");

    r.pir = false;
    RIG_OK(r.send(lightCmd(0)));
    RIG_OK(r.run(5000, 5));
    TEST_ASSERT_TRUE(m.power() == PowerState::Off);
    RIG_OK(r.run(60 * kDay, kIdleStep));
    TEST_ASSERT_TRUE(m.power() == PowerState::Off);
    TEST_ASSERT_TRUE(r.stats.wraps >= 2);
    r.pir = true;
    RIG_OK(r.loop(5));
    TEST_ASSERT_TRUE_MESSAGE(isLit(m.power()), "PIR did not wake the mirror after 60 days off");

    RIG_OK(r.run(5000, 5));
    RIG_OK(r.send(lightCmd(0)));
    RIG_OK(r.run(cfg::PIR_COOLDOWN_MS - 100, 5));
    TEST_ASSERT_TRUE_MESSAGE(m.power() == PowerState::Off, "cooldown not honoured after 60 days");
    RIG_OK(r.run(200, 5));
    TEST_ASSERT_TRUE_MESSAGE(isLit(m.power()), "PIR never re-armed");
}

// Colour commands every 400 ms keep a 500 ms transition running for 20 min:
// no glitch may start inside a transition (v1.2.1), and the commands alone
// keep the mirror on (a command to a lit mirror is activity). Once they
// stop, glitches come back.
void test_no_glitch_during_colour_fades(void) {
    Rig r("colour fades vs glitch", 0u - 300000u, 15);
    Mirror& m = r.mirror();
    RIG_OK(r.send(lightCmd(1)));
    RIG_OK(r.run(5000, 5));
    TEST_ASSERT_TRUE(m.power() == PowerState::On);
    for (uint64_t i = 0; r.t() < 20 * kMinute; ++i) {
        const Command c = (i % 2) ? colourCmd(255, 60, 20) : colourCmd(40, 90, 255);
        RIG_OK(r.send(c));
        RIG_OK(r.run(400 - 5, 5));
    }
    TEST_ASSERT_TRUE(m.power() == PowerState::On);
    TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(r.stats.autoOffs));
    TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(r.stats.glitches));
    RIG_OK(r.run(3 * kMinute, 5));
    TEST_ASSERT_TRUE_MESSAGE(r.stats.glitches > 0, "no glitch after the fades stopped");
}

// Many warning fades (2 s in, 1 s out after a PIR pulse): no glitch may start
// inside one (v1.2.1). The quiet part of each cycle runs in coarse steps;
// the fades run at the 5 ms loop, where 3 of 4 loops do not redraw the fade
// and a glitch could slip in. Enough glitch due moments must fall inside a
// fade for the scenario to mean anything.
void test_no_glitch_during_warning_fades(void) {
    Rig r("warning fades vs glitch", 0u - 3 * 3600000u, 17);
    Mirror& m = r.mirror();
    RIG_OK(r.send(lightCmd(1)));
    uint32_t duesInFade = 0;
    uint32_t lastGlitch = m.lastGlitch_;
    for (int cycle = 0; cycle < 150; ++cycle) {
        const uint64_t warnAt = r.t() + cfg::AUTO_OFF_MS - cfg::AUTO_OFF_WARN_MS;
        while (r.t() + 3000 < warnAt) RIG_OK(r.loop(200));
        for (int i = 0; i < 1500; ++i) {  // 3 s before the warning .. 1.5 s after the PIR pulse
            if (i == 1100) r.pir = true;     // activity 2.5 s into the fade-in
            if (i == 1101) r.pir = false;
            RIG_OK(r.loop(5));
            if (m.lastGlitch_ != lastGlitch && m.warn_.active) ++duesInFade;
            lastGlitch = m.lastGlitch_;
        }
        TEST_ASSERT_TRUE(m.power() == PowerState::On);
    }
    TEST_ASSERT_TRUE(r.stats.warnings >= 150);
    TEST_ASSERT_TRUE_MESSAGE(duesInFade >= 3, "too few glitch due moments inside a warning fade");
}

// A command during the pre-auto-off warning ends it and restarts the timer:
// a brightness change in solid (15 min), effect/set in makeup (45 min).
void test_command_ends_the_warning(void) {
    Rig r("commands end the warning", 0u - 20 * 60000u, 16);
    Mirror& m = r.mirror();
    RIG_OK(r.send(lightCmd(1)));
    RIG_OK(r.run(cfg::AUTO_OFF_MS - cfg::AUTO_OFF_WARN_MS + 1000, 20));
    TEST_ASSERT_TRUE_MESSAGE(m.warning_, "no warning before auto-off");
    RIG_OK(r.send(lightCmd(-1, 128)));
    TEST_ASSERT_FALSE_MESSAGE(m.warning_, "a brightness command did not end the warning");
    const uint64_t solidCmdAt = r.t();
    RIG_OK(r.run(cfg::AUTO_OFF_MS - 1000, 20));
    TEST_ASSERT_TRUE(m.power() == PowerState::On);
    RIG_OK(r.run(2000, 20));
    TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(r.stats.autoOffs));
    TEST_ASSERT_TRUE(r.stats.lastAutoOffT > solidCmdAt + cfg::AUTO_OFF_MS);

    RIG_OK(r.run(5000, 20));
    RIG_OK(r.send(flagCmd(CommandType::Makeup, true)));  // switches on in makeup
    RIG_OK(r.run(cfg::AUTO_OFF_MAKEUP_MS - cfg::AUTO_OFF_WARN_MS + 1000, 20));
    TEST_ASSERT_TRUE_MESSAGE(m.warning_, "no warning before the makeup auto-off");
    Command fx;
    fx.type = CommandType::RandomEffect;
    RIG_OK(r.send(fx));
    TEST_ASSERT_FALSE_MESSAGE(m.warning_, "effect/set did not end the warning");
    const uint64_t makeupCmdAt = r.t();
    RIG_OK(r.run(cfg::AUTO_OFF_MAKEUP_MS - 1000, 20));
    TEST_ASSERT_TRUE(m.power() == PowerState::On);
    RIG_OK(r.run(2000, 20));
    TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(r.stats.autoOffs));
    TEST_ASSERT_TRUE(r.stats.lastAutoOffT > makeupCmdAt + cfg::AUTO_OFF_MAKEUP_MS);
}

}  // namespace

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_random_soak_busy);
    RUN_TEST(test_random_soak_calm);
    RUN_TEST(test_random_soak_across_the_wrap);
    RUN_TEST(test_button_stuck_for_weeks);
    RUN_TEST(test_button_stuck_across_a_reboot);
    RUN_TEST(test_pir_stuck_high_for_weeks);
    RUN_TEST(test_on_for_60_days_with_automation_off);
    RUN_TEST(test_off_for_60_days_across_the_wrap_then_pir);
    RUN_TEST(test_no_glitch_during_colour_fades);
    RUN_TEST(test_no_glitch_during_warning_fades);
    RUN_TEST(test_command_ends_the_warning);
    return UNITY_END();
}

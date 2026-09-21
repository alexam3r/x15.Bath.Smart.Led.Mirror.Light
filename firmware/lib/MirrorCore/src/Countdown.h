// Task 3: overflow-safe countdown timer (ARCHITECTURE.md 8.1, Ruling R7).
// Used by MotionGate for its cooldown (15s after manual OFF) and blackout
// (2s after slide-out) timers. All comparisons go through
// (uint32_t)(now - startMs), which is correct across millis() wraparound
// as long as the elapsed time since start() is itself under ~49.7 days.
#pragma once

#include <cstdint>

struct Countdown {
    uint32_t startMs    = 0;
    uint32_t durationMs = 0;
    bool     active     = false;

    void start(uint32_t now, uint32_t dur) {
        startMs    = now;
        durationMs = dur;
        active     = true;
    }

    void cancel() { active = false; }

    bool running(uint32_t now) const { return active && (uint32_t)(now - startMs) < durationMs; }

    // Ruling R7: retire an expired countdown. Without this, a Countdown
    // left `active` forever would have running() incorrectly report true
    // again ~49.7 days later, once (uint32_t)(now - startMs) wraps back
    // around into [0, durationMs). The owner calls update() every loop
    // tick so expiry is observed well within the current cycle.
    void update(uint32_t now) {
        if (active && (uint32_t)(now - startMs) >= durationMs) {
            active = false;
        }
    }
};

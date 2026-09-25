// Blink (v1.5.0): the confirmation a button hold with the mirror off plays on
// the dark ring — `count` cosine flashes of `flashMs` each, `gapMs` apart,
// peaking at `peak` (perceived brightness, CIE 1931). Timing only: level()
// is the perceived level to show now; 0 between the flashes and once done.
// All times go through now - start, so the millis() wrap is harmless.
#pragma once

#include <cstdint>

class Blink {
public:
    void start(uint8_t count, uint32_t flashMs, uint32_t gapMs, uint8_t peak, uint32_t now);
    void cancel() { count_ = 0; }
    bool armed() const { return count_ != 0; }  // started and not cancelled
    bool active(uint32_t now) const;            // armed and the last flash not over yet
    uint8_t level(uint32_t now) const;          // 0..peak
    uint32_t durationMs() const;

private:
    uint32_t start_   = 0;
    uint32_t flashMs_ = 0;
    uint32_t gapMs_   = 0;
    uint8_t  count_   = 0;
    uint8_t  peak_    = 0;
};

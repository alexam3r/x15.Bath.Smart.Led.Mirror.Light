// Reversible power transition: shuffled 8..12-pixel cores with soft edges.
// Not a temporary Effect; Mirror drives it at the slider's frame interval.
#pragma once

#include "../Config.h"
#include "Effect.h"

class Frame;

class SegmentedAnimation {
public:
    void startOn(RandomFn rnd);
    void startOff(RandomFn rnd);  // fresh full-to-dark pattern
    void startOff();              // reverse an unfinished power-on
    void reverseToOn();
    bool step(Frame& out, Rgbw base);
    bool turningOn() const { return turningOn_; }

private:
    static constexpr uint8_t kMinLength = 8;
    static constexpr uint8_t kMaxLength = 12;
    static constexpr uint8_t kEdge = 3;
    static constexpr uint8_t kFadeSteps = 10;
    static constexpr uint8_t kMaxSegments = cfg::TOTAL_LEDS / kMinLength;

    struct Segment {
        uint16_t first;
        uint8_t length;
    };
    void makePattern(RandomFn rnd);

    Segment segments_[kMaxSegments]{};  // stored in activation order
    uint8_t count_ = 0;
    int16_t progress_ = 0;              // same next-frame cursor as the slider
    bool turningOn_ = true;
};

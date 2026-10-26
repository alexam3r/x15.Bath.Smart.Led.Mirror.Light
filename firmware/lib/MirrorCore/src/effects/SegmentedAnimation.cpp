#include "SegmentedAnimation.h"

#include "../Frame.h"

void SegmentedAnimation::makePattern(RandomFn rnd) {
    uint16_t first = static_cast<uint16_t>(rnd(cfg::TOTAL_LEDS));
    uint16_t remaining = cfg::TOTAL_LEDS;
    count_ = 0;
    while (remaining != 0) {
        uint8_t choices[kMaxLength - kMinLength + 1];
        uint8_t n = 0;
        for (uint8_t length = kMinLength; length <= kMaxLength; ++length) {
            if (length > remaining) continue;
            const uint16_t rest = remaining - length;
            // A remainder must itself admit a partition into 8..12 cores.
            const uint16_t fewest = (rest + kMaxLength - 1) / kMaxLength;
            if (fewest * kMinLength <= rest) choices[n++] = length;
        }
        const uint8_t length = choices[rnd(n)];
        segments_[count_++] = Segment{first, length};
        first = (first + length) % cfg::TOTAL_LEDS;
        remaining -= length;
    }
    // Forward Fisher-Yates; zero RNG gives identity order for simple fixtures.
    for (uint8_t i = 0; i + 1 < count_; ++i) {
        const uint8_t j = i + static_cast<uint8_t>(rnd(count_ - i));
        const Segment tmp = segments_[i];
        segments_[i] = segments_[j];
        segments_[j] = tmp;
    }
}

void SegmentedAnimation::startOn(RandomFn rnd) {
    makePattern(rnd);
    progress_ = 0;
    turningOn_ = true;
}

void SegmentedAnimation::startOff(RandomFn rnd) {
    makePattern(rnd);
    progress_ = cfg::SLIDE_MAX_RADIUS;
    turningOn_ = false;
}

void SegmentedAnimation::startOff() {
    turningOn_ = false;
    if (progress_ >= static_cast<int16_t>(cfg::SLIDE_MAX_RADIUS) || progress_ < 0)
        progress_ = cfg::SLIDE_MAX_RADIUS;
}

void SegmentedAnimation::reverseToOn() { turningOn_ = true; }

bool SegmentedAnimation::step(Frame& out, Rgbw base) {
    out.clear();
    for (uint8_t rank = 0; rank < count_; ++rank) {
        const Segment& segment = segments_[rank];
        const int start = rank * (cfg::SLIDE_MAX_RADIUS - 1 - kFadeSteps) / (count_ - 1);
        int lightness = (progress_ - start) * 255 / kFadeSteps;
        if (lightness <= 0) continue;
        if (lightness > 255) lightness = 255;
        const uint8_t level = cie8(static_cast<uint8_t>(lightness));
        for (int offset = -kEdge; offset < segment.length + kEdge; ++offset) {
            int distance = 0;
            if (offset < 0) distance = -offset;
            else if (offset >= segment.length) distance = offset - segment.length + 1;
            const uint8_t edge = cie8(static_cast<uint8_t>((kEdge + 1 - distance) * 255 / (kEdge + 1)));
            const Rgbw pixel = scale(base, scale8(level, edge));
            const uint16_t index = (segment.first + offset + cfg::TOTAL_LEDS) % cfg::TOTAL_LEDS;
            Rgbw& dest = out[index];
            // Same base for all cores: max blends halos without over-bright seams.
            if (pixel.r > dest.r) dest.r = pixel.r;
            if (pixel.g > dest.g) dest.g = pixel.g;
            if (pixel.b > dest.b) dest.b = pixel.b;
            if (pixel.w > dest.w) dest.w = pixel.w;
        }
    }
    progress_ += turningOn_ ? 1 : -1;
    const bool finished = turningOn_ ? progress_ >= static_cast<int16_t>(cfg::SLIDE_MAX_RADIUS) : progress_ < 0;
    if (finished && !turningOn_) out.clear();
    return !finished;
}

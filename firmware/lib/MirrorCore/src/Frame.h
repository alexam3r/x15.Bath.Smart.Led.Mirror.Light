// Task 1: the 168-pixel virtual ring frame and its mapping onto the two
// physical strips. Effects draw only into Frame; only mapVirtual() knows
// about physical strip layout (ARCHITECTURE.md section 6).
#pragma once

#include <cstdint>

#include "ColorMath.h"
#include "Config.h"

class Frame {
public:
    static constexpr uint16_t kSize = cfg::TOTAL_LEDS;

    Rgbw& operator[](uint16_t i) { return px[i]; }
    const Rgbw& operator[](uint16_t i) const { return px[i]; }

    void fill(Rgbw c);
    void clear();
    void scale(uint8_t k);  // scale8 every pixel by k

private:
    Rgbw px[cfg::TOTAL_LEDS];
};

// Shortest distance between two virtual ring indices, wrapping at 168.
uint16_t ringDist(uint16_t a, uint16_t b);

// strip: 0 = right (GPIO 5), 1 = left (GPIO 4).
struct PhysicalPixel {
    uint8_t strip;
    uint8_t index;
};

constexpr uint8_t STRIP_RIGHT = 0;
constexpr uint8_t STRIP_LEFT  = 1;

// v in [0, TOTAL_LEDS): virtual ring index -> physical strip + index.
PhysicalPixel mapVirtual(uint16_t v);

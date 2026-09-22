// Task 1: color math shared by every effect — v27-compatible scale8/
// packColor semantics and a bit-exact port of Adafruit_NeoPixel::ColorHSV
// (saturation=255, value=255 only; see ColorMath.cpp).
#pragma once

#include <cstdint>

struct Rgbw {
    uint8_t r, g, b, w;
};

inline bool operator==(const Rgbw& a, const Rgbw& b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.w == b.w;
}

// (x * (1 + k)) >> 8 — scale8(x, 255) == x, scale8(x, 0) == 0.
uint8_t scale8(uint8_t x, uint8_t k);

// scale8 applied independently to each of r, g, b, w.
Rgbw scale(Rgbw c, uint8_t k);

// a + (b - a) * t / 255: lerp8(a, b, 0) == a, lerp8(a, b, 255) == b.
uint8_t lerp8(uint8_t a, uint8_t b, uint8_t t);

// lerp8 applied independently to each of r, g, b, w.
Rgbw lerp(Rgbw a, Rgbw b, uint8_t t);

// Adafruit_NeoPixel::ColorHSV(hue, 255, 255), split into Rgbw (w = 0).
Rgbw hsv(uint16_t hue);

// Adafruit NEO_GRBW packed layout: (w<<24) | (r<<16) | (g<<8) | b.
uint32_t packColor(Rgbw c);

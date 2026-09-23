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

// --- CIE 1931 lightness (v1.3.0) ---------------------------------------------
// The eye sees brightness roughly as the cube root of light output, so a fade
// that is linear in PWM duty rushes through the dark end and crawls near the
// top. Gradients are therefore computed in perceived brightness (0..255) and
// turned into duty with cie8(): Y = L/903.3 for L <= 8 %, else
// ((L + 16) / 116)^3. Half as bright to the eye (128) is 18.6 % duty (47).

// Perceived brightness -> PWM duty. cie8(0) == 0, cie8(255) == 255.
uint8_t cie8(uint8_t lightness);

// PWM duty -> the smallest perceived brightness whose cie8() reaches it.
uint8_t lightness8(uint8_t pwm);

// lerp8 in perceived brightness: even to the eye, exactly `a` at t = 0 and
// exactly `b` at t = 255 (the top of the curve skips duty values, so a plain
// round trip through lightness8/cie8 would jump at the ends).
uint8_t lerp8Perceptual(uint8_t a, uint8_t b, uint8_t t);

// lerp8Perceptual applied independently to each of r, g, b, w.
Rgbw lerpPerceptual(Rgbw a, Rgbw b, uint8_t t);

// Adafruit_NeoPixel::ColorHSV(hue, 255, 255), split into Rgbw (w = 0).
Rgbw hsv(uint16_t hue);

// Adafruit NEO_GRBW packed layout: (w<<24) | (r<<16) | (g<<8) | b.
uint32_t packColor(Rgbw c);

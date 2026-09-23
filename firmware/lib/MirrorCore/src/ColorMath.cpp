#include "ColorMath.h"

uint8_t scale8(uint8_t x, uint8_t k) {
    return ((uint16_t)x * (1 + k)) >> 8;
}

Rgbw scale(Rgbw c, uint8_t k) {
    return Rgbw{scale8(c.r, k), scale8(c.g, k), scale8(c.b, k), scale8(c.w, k)};
}

uint8_t lerp8(uint8_t a, uint8_t b, uint8_t t) {
    return static_cast<uint8_t>(a + (static_cast<int32_t>(b) - a) * t / 255);
}

Rgbw lerp(Rgbw a, Rgbw b, uint8_t t) {
    return Rgbw{lerp8(a.r, b.r, t), lerp8(a.g, b.g, t), lerp8(a.b, b.b, t), lerp8(a.w, b.w, t)};
}

namespace {

// Both CIE 1931 tables, computed at compile time (512 bytes of flash).
struct CieTables {
    uint8_t toPwm[256];
    uint8_t toLightness[256];

    constexpr CieTables() : toPwm{}, toLightness{} {
        for (int p = 0; p < 256; ++p) {
            const double l = p * 100.0 / 255.0;  // lightness, %
            const double f = (l + 16.0) / 116.0;
            const double y = (l <= 8.0) ? l / 903.3 : f * f * f;
            toPwm[p] = static_cast<uint8_t>(y * 255.0 + 0.5);
        }
        int p = 0;
        for (int x = 0; x < 256; ++x) {
            while (toPwm[p] < x) ++p;  // toPwm[255] == 255 stops it
            toLightness[x] = static_cast<uint8_t>(p);
        }
    }
};

constexpr CieTables kCie{};

}  // namespace

uint8_t cie8(uint8_t lightness) { return kCie.toPwm[lightness]; }

uint8_t lightness8(uint8_t pwm) { return kCie.toLightness[pwm]; }

uint8_t lerp8Perceptual(uint8_t a, uint8_t b, uint8_t t) {
    if (t == 0) return a;
    if (t == 255) return b;
    return cie8(lerp8(lightness8(a), lightness8(b), t));
}

Rgbw lerpPerceptual(Rgbw a, Rgbw b, uint8_t t) {
    return Rgbw{lerp8Perceptual(a.r, b.r, t), lerp8Perceptual(a.g, b.g, t), lerp8Perceptual(a.b, b.b, t),
                lerp8Perceptual(a.w, b.w, t)};
}

Rgbw hsv(uint16_t hue) {
    // Bit-exact port of Adafruit_NeoPixel::ColorHSV(hue, sat, val) with
    // sat = 255, val = 255 hardcoded (the only path MirrorCore needs).
    // Source: firmware/.pio/libdeps/esp32-s3-zero/Adafruit NeoPixel/
    // Adafruit_NeoPixel.cpp.

    // Remap 0-65535 to 0-1529, red centered on the rollover.
    uint16_t h = (uint16_t)(((uint32_t)hue * 1530UL + 32768UL) / 65536UL);

    uint8_t r, g, b;
    if (h < 510) {  // Red to Green-1
        b = 0;
        if (h < 255) {
            r = 255;
            g = (uint8_t)h;
        } else {
            r = (uint8_t)(510 - h);
            g = 255;
        }
    } else if (h < 1020) {  // Green to Blue-1
        r = 0;
        if (h < 765) {
            g = 255;
            b = (uint8_t)(h - 510);
        } else {
            g = (uint8_t)(1020 - h);
            b = 255;
        }
    } else if (h < 1530) {  // Blue to Red-1
        g = 0;
        if (h < 1275) {
            r = (uint8_t)(h - 1020);
            b = 255;
        } else {
            r = 255;
            b = (uint8_t)(1530 - h);
        }
    } else {  // Last 0.5 red
        r = 255;
        g = 0;
        b = 0;
    }

    // Apply saturation/value exactly as Adafruit does, with sat = val = 255.
    const uint32_t v1 = 1 + 255;    // 256
    const uint16_t s1 = 1 + 255;    // 256
    const uint8_t  s2 = 255 - 255;  // 0

    uint32_t rr = ((((uint32_t)r * s1) >> 8) + s2) * v1;
    uint32_t gg = ((((uint32_t)g * s1) >> 8) + s2) * v1;
    uint32_t bb = ((((uint32_t)b * s1) >> 8) + s2) * v1;

    Rgbw out;
    out.r = (uint8_t)((rr & 0xff00u) >> 8);
    out.g = (uint8_t)((gg & 0xff00u) >> 8);
    out.b = (uint8_t)(bb >> 8);
    out.w = 0;
    return out;
}

uint32_t packColor(Rgbw c) {
    return ((uint32_t)c.w << 24) | ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | c.b;
}

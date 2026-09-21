#include "Frame.h"

void Frame::fill(Rgbw c) {
    for (uint16_t i = 0; i < kSize; ++i) px[i] = c;
}

void Frame::clear() {
    fill(Rgbw{0, 0, 0, 0});
}

void Frame::scale(uint8_t k) {
    for (uint16_t i = 0; i < kSize; ++i) px[i] = ::scale(px[i], k);
}

uint16_t ringDist(uint16_t a, uint16_t b) {
    uint16_t diff = (a > b) ? (uint16_t)(a - b) : (uint16_t)(b - a);
    uint16_t wrap = (uint16_t)(cfg::TOTAL_LEDS - diff);
    return (diff < wrap) ? diff : wrap;
}

PhysicalPixel mapVirtual(uint16_t v) {
    if (v < cfg::LEDS_RIGHT_CNT) {
        return PhysicalPixel{STRIP_RIGHT, (uint8_t)v};
    }
    return PhysicalPixel{
        STRIP_LEFT,
        (uint8_t)((cfg::LEDS_LEFT_CNT - 1) - (v - cfg::LEDS_RIGHT_CNT))};
}

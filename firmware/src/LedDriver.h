// Task 7: the two physical SK6812 RGBW strips (ARCHITECTURE.md 3.1, 8.1) —
// Core 1 only (owned by main.cpp). Frame -> mapVirtual() -> strip pixels;
// no other file knows about the two-strip physical layout.
#pragma once

#include <Adafruit_NeoPixel.h>

#include "Config.h"
#include "Frame.h"

class LedDriver {
public:
    void begin();
    // Draws the frame on both strips under the RMT lock. False if the lock
    // could not be taken in time — nothing was drawn, the caller retries.
    bool show(const Frame& f);

private:
    Adafruit_NeoPixel stripL_{cfg::LEDS_LEFT_CNT, cfg::PIN_LED_LEFT, NEO_GRBW + NEO_KHZ800};
    Adafruit_NeoPixel stripR_{cfg::LEDS_RIGHT_CNT, cfg::PIN_LED_RIGHT, NEO_GRBW + NEO_KHZ800};
};

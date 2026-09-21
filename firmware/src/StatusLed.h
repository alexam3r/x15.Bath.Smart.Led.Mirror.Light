// Task 7: single WS2812 status LED (GPIO 21, ARCHITECTURE.md 3.1) — Core 0
// only (owned by Network.cpp).
#pragma once

#include <Adafruit_NeoPixel.h>
#include <cstdint>

#include "Config.h"

class StatusLed {
public:
    void begin();
    void set(uint8_t r, uint8_t g, uint8_t b);

private:
    Adafruit_NeoPixel pixel_{1, cfg::PIN_STATUS_LED, NEO_GRB + NEO_KHZ800};
};

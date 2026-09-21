// Task 7: StatusLed implementation.
#include "StatusLed.h"

void StatusLed::begin() {
    pixel_.begin();
    pixel_.show();  // off
}

void StatusLed::set(uint8_t r, uint8_t g, uint8_t b) {
    pixel_.setPixelColor(0, pixel_.Color(r, g, b));
    pixel_.show();
}

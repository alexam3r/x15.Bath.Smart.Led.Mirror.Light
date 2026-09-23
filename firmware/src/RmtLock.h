// RmtLock — one mutex around every Adafruit_NeoPixel::show(), on both cores
// (v1.2.1, supersedes the "accepted risk" of Ruling R12).
//
// NeoPixel 1.15.5 on ESP-IDF 4.4 configures, installs and uninstalls the RMT
// driver on every show(). When the strips (Core 1) and the status LED
// (Core 0) overlap, the last uninstall turns the RMT module off and resets
// it under the other core's half-configured channel: the frame comes out
// garbled, and if the TX-done interrupt is lost the show() waits forever.
// Serialising the shows removes the race. This lock guards the peripheral
// only — state still crosses cores through the two queues, never a mutex.
#pragma once

#include <cstdint>

namespace rmt_lock {

// Creates the mutex. Call once in setup(), before the first show() on
// either core. Returns false if FreeRTOS could not create it.
bool init();

// Takes the lock, waiting at most `timeoutMs`. False on timeout — the caller
// skips this show() and tries again later.
bool take(uint32_t timeoutMs);

void give();

}  // namespace rmt_lock

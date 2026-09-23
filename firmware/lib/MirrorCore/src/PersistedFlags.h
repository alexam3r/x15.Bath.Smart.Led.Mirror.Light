// PersistedFlags — the three switches that survive a software restart
// (v1.2.1): automation, night mode (motion_disable) and glitch. main.cpp keeps
// the record in RTC memory (RTC_NOINIT_ATTR), which is kept across
// ESP.restart(), panics and watchdog resets but not initialised at power-on.
// Colour, brightness and mode are deliberately NOT kept: they reset to the
// defaults at every OFF (owner's decision).
//
// Pure logic, so the validation can be tested on the host: a record is used
// only if its magic and checksum match and every flag byte is 0 or 1.
#pragma once

#include <cstdint>

struct PersistedFlags {
    uint32_t magic;
    uint8_t  automation;
    uint8_t  nightMode;
    uint8_t  glitch;
    uint8_t  reserved;  // always 0
    uint32_t check;     // FNV-1a over the 8 bytes above
};
static_assert(sizeof(PersistedFlags) == 12, "RTC record layout must not change silently");

PersistedFlags packFlags(bool automation, bool nightMode, bool glitch);

// True and the three outputs set if `p` is a valid record; false and the
// outputs untouched otherwise (power-on garbage, a record from another
// firmware layout, corruption).
bool unpackFlags(const PersistedFlags& p, bool& automation, bool& nightMode, bool& glitch);

// esp_reset_reason_t -> whether the kept flags apply: after software
// restarts and faults (SW, PANIC, INT_WDT, TASK_WDT, WDT, BROWNOUT) yes;
// after power-on, the reset button and anything unknown no — a power cycle
// must bring the defaults back even if RTC memory happened to survive it.
bool shouldRestoreFlags(uint8_t resetReason);

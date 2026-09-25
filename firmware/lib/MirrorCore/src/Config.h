// Task 1: compile-time configuration for the Smart Mirror firmware.
// Pure C++, no Arduino/FreeRTOS dependency (see global constraints).
#pragma once

#include <cstddef>
#include <cstdint>

namespace cfg {

// --- Pins -----------------------------------------------------------------
constexpr uint8_t PIN_LED_LEFT   = 4;
constexpr uint8_t PIN_LED_RIGHT  = 5;
constexpr uint8_t PIN_PIR        = 6;
constexpr uint8_t PIN_BUTTON     = 7;
constexpr uint8_t PIN_STATUS_LED = 21;
constexpr uint8_t BTN_PRESSED    = 1;  // digital level HIGH

// --- LEDs / virtual ring ----------------------------------------------------
constexpr uint16_t LEDS_LEFT_CNT  = 66;
constexpr uint16_t LEDS_RIGHT_CNT = 102;
constexpr uint16_t TOTAL_LEDS     = LEDS_LEFT_CNT + LEDS_RIGHT_CNT;  // 168

// --- Snake effect -----------------------------------------------------------
constexpr uint16_t SNAKE_SIZE       = 60;
constexpr uint16_t SNAKE_HEAD_SOLID = 4;
constexpr uint32_t SNAKE_STEP_MS    = 40;
constexpr uint16_t SNAKE_FADE_STEPS = 30;

// --- Wave effect --------------------------------------------------------
constexpr uint16_t WAVE_RADIUS  = 18;
constexpr uint16_t WAVE_CORE    = 3;
constexpr uint32_t WAVE_STEP_MS = 45;

// --- Embers effect (v1.3.0, deeper with soft edges in v1.3.1) ---------------
// Sparse smouldering coals: every step a new coal lights with probability
// EMBERS_SPAWN_PER_MILLE (about one spot per twelve pixels at any moment).
// A coal dims its centre to a random floor of EMBERS_FLOOR_MIN..MAX perceived
// brightness and back along a cosine over EMBERS_DIP_MIN..MAX_STEPS, with a
// soft edge of EMBERS_EDGE pixels each side whose depth falls off in even
// perceived steps (2/3, 1/3 for an edge of 2). Coals start at least
// EMBERS_MIN_GAP pixels apart; no new ones in the last EMBERS_DIP_MAX_STEPS,
// so the effect dies down on its own. ~20 s.
constexpr uint32_t EMBERS_STEP_MS         = 30;
constexpr uint16_t EMBERS_STEPS           = 667;  // ~20 s
constexpr uint8_t  EMBERS_MAX_COALS       = 24;
constexpr uint16_t EMBERS_SPAWN_PER_MILLE = 250;
constexpr uint8_t  EMBERS_FLOOR_MIN       = 26;   // 10 % to the eye (v1.3.0: 30 %)
constexpr uint8_t  EMBERS_FLOOR_MAX       = 77;   // 30 % to the eye (v1.3.0: 50 %)
constexpr uint8_t  EMBERS_DIP_MIN_STEPS   = 33;   // ~1 s
constexpr uint8_t  EMBERS_DIP_MAX_STEPS   = 83;   // ~2.5 s
constexpr uint8_t  EMBERS_EDGE            = 2;    // soft edge, pixels each side
constexpr uint8_t  EMBERS_MIN_GAP         = 2 * EMBERS_EDGE + 1;

// --- Flame effect (v1.4.0, replaces breathe; wider and deeper in v1.4.1) -----
// Patches breathe on their own clocks: every step a new patch appears with
// probability FLAME_SPAWN_PER_MILLE at a random place at least FLAME_MIN_GAP
// full-colour pixels clear of the others (about 4 at a time). A patch is a
// flat core of FLAME_CORE_MIN..MAX pixels with a soft edge of flameEdge(core)
// pixels each side (even perceived steps), 16..20 pixels in all; the core
// dims to FLAME_FLOOR and back along a cosine over FLAME_DIP_MIN..MAX_STEPS.
// No new patches in the last FLAME_DIP_MAX_STEPS, so the effect dies down on
// its own. ~20 s. v1.4.0: 10..15 pixels with the edge, 60 % to the eye.
constexpr uint32_t FLAME_STEP_MS         = 30;
constexpr uint16_t FLAME_STEPS           = 667;  // ~20 s
constexpr uint8_t  FLAME_MAX_PATCHES     = 8;
constexpr uint16_t FLAME_SPAWN_PER_MILLE = 150;  // most tries land on a patch: ~4 at a time
constexpr uint8_t  FLAME_FLOOR           = 77;   // 30 % to the eye (v1.4.0: 153, 60 %)
constexpr uint8_t  FLAME_DIP_MIN_STEPS   = 100;  // ~3 s
constexpr uint8_t  FLAME_DIP_MAX_STEPS   = 167;  // ~5 s
constexpr uint8_t  FLAME_CORE_MIN        = 10;   // flat core, pixels
constexpr uint8_t  FLAME_CORE_MAX        = 12;
constexpr uint8_t  FLAME_MIN_GAP         = 2;    // full-colour pixels between patches
// The soft edge each side grows with the core, a third of it rounded:
// 10 -> 3, 11 -> 4, 12 -> 4 pixels.
constexpr uint8_t flameEdge(uint8_t core) { return static_cast<uint8_t>((core + 1) / 3); }
constexpr uint8_t  FLAME_WIDTH_MAX       = FLAME_CORE_MAX + 2 * flameEdge(FLAME_CORE_MAX);  // 20

// --- Comet effect (v1.2.0) --------------------------------------------------
// A white head with a COMET_TAIL-pixel tail (fading evenly to the eye, v1.3.0) travels once around the
// ring (TOTAL_LEDS + COMET_TAIL steps), fading in and out over COMET_FADE_STEPS.
constexpr uint32_t COMET_STEP_MS    = 30;
constexpr uint16_t COMET_TAIL       = 40;
constexpr uint16_t COMET_FADE_STEPS = 20;

// --- Slide effect -------------------------------------------------------
// v1.0.1: ~20% slower and ~20% longer soft edge than v27 (28 ms, 9 LEDs):
// 97 steps * 33 ms = ~3.2 s instead of 95 * 28 ms = ~2.7 s. v1.3.2: the edge
// 1.5x longer and even to the eye (CIE 1931): 102 steps * 33 ms = ~3.4 s.
constexpr uint32_t SLIDE_STEP_MS    = 33;
constexpr uint16_t SLIDE_EDGE       = 16;  // 15 lit edge pixels, even to the eye (v1.3.2; v1.0.1: 11)
constexpr uint16_t SLIDE_MAX_RADIUS = TOTAL_LEDS / 2 + SLIDE_EDGE + 2;  // 102

// --- Night-mode confirmation (v1.5.0) ---------------------------------------
// A button hold with the mirror off toggles night mode, and the dark ring
// confirms it in the warm default colour: one soft flash when night mode goes
// on, two quicker ones when it comes off. Each flash is a cosine bump up to
// SIGNAL_LEVEL (perceived brightness).
constexpr uint8_t  SIGNAL_LEVEL        = 77;    // 30 % to the eye
constexpr uint32_t SIGNAL_ON_FLASH_MS  = 1000;  // night mode on: one flash
constexpr uint32_t SIGNAL_OFF_FLASH_MS = 400;   // night mode off: two flashes...
constexpr uint32_t SIGNAL_OFF_GAP_MS   = 200;   // ...this far apart
constexpr uint32_t SIGNAL_STEP_MS      = 20;

// --- Glitch overlay ("neon failure", v1.1.0) -----------------------------
// A random core of GLITCH_LEN_MIN..MAX adjacent pixels flickers in the base
// colour for GLITCH_DURATION_MIN..MAX ms, once every GLITCH_INTERVAL_MIN..MAX
// (random) while the mirror is ON, solid and idle (see Mirror::tick). A soft
// edge of GLITCH_EDGE_MIN..MAX pixels each side fades back to the base in
// even perceived steps (v1.1.1: 2..3 pixels; v1.3.0: 4..6, CIE 1931).
constexpr uint32_t GLITCH_INTERVAL_MIN_MS = 45UL * 1000;
constexpr uint32_t GLITCH_INTERVAL_MAX_MS = 90UL * 1000;
constexpr uint32_t GLITCH_DURATION_MIN_MS = 300;
constexpr uint32_t GLITCH_DURATION_MAX_MS = 600;
constexpr uint8_t  GLITCH_LEN_MIN         = 3;
constexpr uint8_t  GLITCH_LEN_MAX         = 6;
constexpr uint8_t  GLITCH_EDGE_MIN        = 4;
constexpr uint8_t  GLITCH_EDGE_MAX        = 6;
constexpr uint32_t GLITCH_STEP_MS         = 30;

// --- Radial effect start points ------------------------------------------
constexpr uint16_t CENTERS[4] = {9, 51, 93, 134};
constexpr uint8_t  CENTERS_CNT = 4;

// --- Defaults -------------------------------------------------------------
constexpr uint8_t DEFAULT_R          = 255;
constexpr uint8_t DEFAULT_G          = 140;
constexpr uint8_t DEFAULT_B          = 50;
constexpr uint8_t DEFAULT_BRIGHTNESS = 255;

// --- Timers (ms) ------------------------------------------------------------
constexpr uint32_t AUTO_OFF_MS        = 15UL * 60 * 1000;  // 15 min
constexpr uint32_t AUTO_OFF_MAKEUP_MS = 45UL * 60 * 1000;  // 45 min in makeup (v1.2.0)
constexpr uint32_t AUTO_EFFECT_MIN_MS = 4UL * 60 * 1000;   // 4 min
constexpr uint32_t AUTO_EFFECT_MAX_MS = 5UL * 60 * 1000;   // 5 min
constexpr uint32_t PIR_COOLDOWN_MS    = 15UL * 1000;       // 15 s
constexpr uint32_t PIR_BLACKOUT_MS    = 2UL * 1000;        // 2 s
// A PIR HIGH without a break for this long is stuck: it stops counting as
// activity and stops switching the mirror on until it goes LOW (v1.2.1).
constexpr uint32_t PIR_STUCK_MS       = 60UL * 60 * 1000;  // 60 min

// --- Pre-auto-off warning (v1.2.0) ------------------------------------------
// AUTO_OFF_WARN_MS before auto-off the rendered brightness ramps down to
// WARN_DIM_LEVEL over WARN_FADE_IN_MS; any activity ramps it back over
// WARN_FADE_OUT_MS and restarts the auto-off timer.
constexpr uint32_t AUTO_OFF_WARN_MS = 60UL * 1000;  // 1 min
constexpr uint8_t  WARN_DIM_LEVEL   = 128;          // 50 % to the eye (CIE 1931, v1.3.0) = ~18 % PWM
constexpr uint32_t WARN_FADE_IN_MS  = 2000;
constexpr uint32_t WARN_FADE_OUT_MS = 1000;

// --- Transitions (v1.2.0) ---------------------------------------------------
// Colour/brightness/mode changes from commands and the double click fade
// linearly; button dimming is applied at once (it is already stepped every
// DIM_PERIOD_MS).
constexpr uint32_t TRANSITION_MS      = 500;
constexpr uint32_t TRANSITION_STEP_MS = 20;

// --- Button -----------------------------------------------------------------
constexpr uint32_t CLICK_GAP_MS  = 400;
constexpr uint32_t HOLD_MS       = 500;
constexpr uint32_t DIM_PERIOD_MS = 30;
constexpr uint8_t  DIM_STEP      = 5;
constexpr uint8_t  DIM_MIN       = 5;
constexpr uint8_t  DIM_MAX       = 255;
// A hold longer than this is a stuck button (moisture on the button or its
// Schmitt trigger): it stops ticking until released (v1.2.1).
constexpr uint32_t HOLD_STUCK_MS = 60UL * 1000;

// --- Network ------------------------------------------------------------
// NetWatchdog.h: WiFi down this long -> restart; WiFi up but MQTT down this
// long -> re-join WiFi. Both only after the mirror has been quiet (dark, no
// motion) for RESTART_QUIET_MS — longer than slide-out + PIR cooldown +
// blackout, so a reboot never relights the mirror behind someone leaving.
constexpr uint32_t WATCHDOG_TIMEOUT_MS        = 5UL * 60 * 1000;   // 5 min
constexpr uint32_t MQTT_DOWN_REJOIN_MS        = 30UL * 60 * 1000;  // 30 min
constexpr uint32_t RESTART_QUIET_MS           = 30UL * 1000;       // 30 s
static_assert(RESTART_QUIET_MS > (SLIDE_MAX_RADIUS + 2) * SLIDE_STEP_MS + PIR_COOLDOWN_MS + PIR_BLACKOUT_MS,
              "a reboot must not come before the slide-out, the PIR cooldown and the blackout are over");
constexpr uint32_t TELEMETRY_PERIOD_MS        = 10UL * 1000;      // 10 s
constexpr uint32_t PUBLISH_MIN_INTERVAL_MS    = 250;
constexpr uint16_t MQTT_BUFFER_SIZE           = 512;
constexpr size_t   STATE_JSON_CAP             = 384;
// Diagnostics (v1.2.0): retained <base>/diag, on connect and once a minute.
constexpr uint32_t DIAG_PERIOD_MS             = 60UL * 1000;     // 1 min
constexpr size_t   DIAG_JSON_CAP              = 192;
constexpr uint8_t  WIFI_CONNECT_ATTEMPTS      = 20;
constexpr uint32_t WIFI_ATTEMPT_DELAY_MS      = 500;
// Associated with the AP but no DHCP lease yet: keep waiting this long
// before tearing the attempt down (router reboots can be slow to lease).
constexpr uint32_t WIFI_DHCP_WAIT_MAX_MS      = 60UL * 1000;
constexpr uint32_t MQTT_RETRY_DELAY_MS        = 5000;
constexpr uint32_t NETWORK_TASK_STACK         = 10000;

// PubSubClient waits for CONNACK and for the rest of a packet in a busy loop
// that never lets the Core 0 idle task run. The ESP-IDF task watchdog of the
// Arduino core (CONFIG_ESP_TASK_WDT_TIMEOUT_S = 5, panic) watches that idle
// task, so every such wait must end well before 5 s — the library default of
// 15 s turned a slow broker into a TASK_WDT reboot.
constexpr uint32_t TASK_WDT_TIMEOUT_S         = 5;   // the Arduino core's sdkconfig value
constexpr uint16_t MQTT_SOCKET_TIMEOUT_S      = 3;
constexpr uint16_t MQTT_KEEPALIVE_S           = 15;
static_assert(MQTT_SOCKET_TIMEOUT_S < TASK_WDT_TIMEOUT_S,
              "an MQTT wait must never outlast the task watchdog");

// --- Runtime ------------------------------------------------------------
constexpr uint8_t  CMD_QUEUE_LEN      = 8;
constexpr uint32_t LOOP_IDLE_DELAY_MS = 5;
// Longest wait for the RMT lock (RmtLock.h): a strip frame holds it ~7 ms,
// the status LED well under 1 ms. On timeout the show is skipped and retried.
constexpr uint32_t RMT_LOCK_TIMEOUT_MS = 50;
// The strips failing to get the RMT lock for this long means the holder hung
// inside show() (e.g. the status LED on Core 0 waiting forever for a lost
// TX-done interrupt). No watchdog sees that — loop() keeps running — so
// main.cpp restarts. Normal holds last well under 10 ms.
constexpr uint32_t SHOW_STALL_RESTART_MS = 2000;
// The current frame is re-sent at least this often even when it has not
// changed: SK6812s keep whatever they last latched, so a frame garbled by
// noise on the data line would otherwise stay until the next change (all
// night on a dark mirror). Re-sending an identical frame is invisible.
constexpr uint32_t FRAME_REFRESH_MS = 2000;
constexpr uint32_t SERIAL_WAIT_MS     = 3000;  // debug build only (main.cpp setup)

// --- Firmware version -----------------------------------------------------
constexpr char FW_VERSION[] = "1.5.0";

}  // namespace cfg

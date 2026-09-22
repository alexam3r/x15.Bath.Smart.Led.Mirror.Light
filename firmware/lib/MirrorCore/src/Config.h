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

// --- Breathe effect (v1.2.0) ------------------------------------------------
// The whole ring breathes: cosine brightness 100 % -> BREATHE_MIN_LEVEL ->
// 100 %, BREATHE_CYCLES cycles of BREATHE_CYCLE_STEPS steps each.
constexpr uint32_t BREATHE_STEP_MS     = 20;
constexpr uint16_t BREATHE_CYCLE_STEPS = 200;  // 4 s per breath
constexpr uint8_t  BREATHE_CYCLES      = 3;
constexpr uint8_t  BREATHE_MIN_LEVEL   = 153;  // 60 %

// --- Embers effect (v1.2.0) -------------------------------------------------
// Each pixel smoulders at its own level in [EMBERS_MIN_LEVEL, 255]: every step
// EMBERS_CHANGES_PER_STEP random pixels get a new target and every pixel moves
// towards its own by at most EMBERS_SLEW. ~20 s with a 2 s fade in and out.
constexpr uint32_t EMBERS_STEP_MS          = 60;
constexpr uint16_t EMBERS_STEPS            = 333;  // ~20 s
constexpr uint16_t EMBERS_FADE_STEPS       = 33;   // ~2 s
constexpr uint8_t  EMBERS_CHANGES_PER_STEP = 13;
constexpr uint8_t  EMBERS_MIN_LEVEL        = 102;  // 40 %
constexpr uint8_t  EMBERS_SLEW             = 6;

// --- Slide effect -------------------------------------------------------
// v1.0.1: ~20% slower and ~20% longer soft edge than v27 (28 ms, 9 LEDs):
// 97 steps * 33 ms = ~3.2 s instead of 95 * 28 ms = ~2.7 s.
constexpr uint32_t SLIDE_STEP_MS    = 33;
constexpr uint16_t SLIDE_EDGE       = 11;
constexpr uint16_t SLIDE_MAX_RADIUS = TOTAL_LEDS / 2 + SLIDE_EDGE + 2;  // 97

// --- Glitch overlay ("neon failure", v1.1.0) -----------------------------
// A random core of GLITCH_LEN_MIN..MAX adjacent pixels flickers in the base
// colour for GLITCH_DURATION_MIN..MAX ms, once every GLITCH_INTERVAL_MIN..MAX
// (random) while the mirror is ON, solid and idle (see Mirror::tick). v1.1.1:
// a soft edge of GLITCH_EDGE_MIN..MAX pixels each side fades back to the base.
constexpr uint32_t GLITCH_INTERVAL_MIN_MS = 45UL * 1000;
constexpr uint32_t GLITCH_INTERVAL_MAX_MS = 90UL * 1000;
constexpr uint32_t GLITCH_DURATION_MIN_MS = 300;
constexpr uint32_t GLITCH_DURATION_MAX_MS = 600;
constexpr uint8_t  GLITCH_LEN_MIN         = 3;
constexpr uint8_t  GLITCH_LEN_MAX         = 6;
constexpr uint8_t  GLITCH_EDGE_MIN        = 2;
constexpr uint8_t  GLITCH_EDGE_MAX        = 3;
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

// --- Pre-auto-off warning (v1.2.0) ------------------------------------------
// AUTO_OFF_WARN_MS before auto-off the rendered brightness ramps down to
// WARN_DIM_LEVEL over WARN_FADE_IN_MS; any activity ramps it back over
// WARN_FADE_OUT_MS and restarts the auto-off timer.
constexpr uint32_t AUTO_OFF_WARN_MS = 60UL * 1000;  // 1 min
constexpr uint8_t  WARN_DIM_LEVEL   = 128;          // 50 %
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

// --- Network ------------------------------------------------------------
constexpr uint32_t WATCHDOG_TIMEOUT_MS        = 5UL * 60 * 1000;  // 5 min
constexpr uint32_t WATCHDOG_CHECK_INTERVAL_MS = 30UL * 1000;      // 30 s
constexpr uint32_t TELEMETRY_PERIOD_MS        = 10UL * 1000;      // 10 s
constexpr uint32_t PUBLISH_MIN_INTERVAL_MS    = 250;
constexpr uint16_t MQTT_BUFFER_SIZE           = 512;
constexpr size_t   STATE_JSON_CAP             = 384;
constexpr uint8_t  WIFI_CONNECT_ATTEMPTS      = 20;
constexpr uint32_t WIFI_ATTEMPT_DELAY_MS      = 500;
constexpr uint32_t MQTT_RETRY_DELAY_MS        = 5000;
constexpr uint32_t NETWORK_TASK_STACK         = 10000;

// --- Runtime ------------------------------------------------------------
constexpr uint8_t  CMD_QUEUE_LEN      = 8;
constexpr uint32_t LOOP_IDLE_DELAY_MS = 5;
constexpr uint32_t SERIAL_WAIT_MS     = 3000;

// --- Firmware version -----------------------------------------------------
constexpr char FW_VERSION[] = "1.2.0";

}  // namespace cfg

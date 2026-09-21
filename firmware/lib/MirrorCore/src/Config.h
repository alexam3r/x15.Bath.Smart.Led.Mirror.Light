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

// --- Slide effect -------------------------------------------------------
constexpr uint32_t SLIDE_STEP_MS    = 28;
constexpr uint16_t SLIDE_EDGE       = 9;
constexpr uint16_t SLIDE_MAX_RADIUS = TOTAL_LEDS / 2 + SLIDE_EDGE + 2;  // 95

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
constexpr uint32_t AUTO_EFFECT_MIN_MS = 4UL * 60 * 1000;   // 4 min
constexpr uint32_t AUTO_EFFECT_MAX_MS = 5UL * 60 * 1000;   // 5 min
constexpr uint32_t PIR_COOLDOWN_MS    = 15UL * 1000;       // 15 s
constexpr uint32_t PIR_BLACKOUT_MS    = 2UL * 1000;        // 2 s

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
constexpr char FW_VERSION[] = "1.0.0";

}  // namespace cfg

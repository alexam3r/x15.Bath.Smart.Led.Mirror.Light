// Smart Mirror firmware, v1.0.0. Core 1: setup()/loop() below — button, PIR,
// Mirror state machine, LedDriver (ARCHITECTURE.md 3.1, 3.3). Core 0:
// Network.cpp (WiFi/MQTT/watchdog). The two sides talk only through
// cmdQueue and snapQueue; neither core touches the other's objects.
#include <Arduino.h>
#include <esp_system.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "Button.h"
#include "Config.h"
#include "LedDriver.h"
#include "Mirror.h"
#include "Network.h"
#include "RmtLock.h"
#include "Types.h"

static uint32_t espRandom(uint32_t bound) { return bound ? esp_random() % bound : 0; }

static Mirror    mirror(espRandom);
static Button    button;
static LedDriver leds;

static QueueHandle_t cmdQueue  = nullptr;
static QueueHandle_t snapQueue = nullptr;

static StateSnapshot lastSnapshot;
static bool          redrawPending = false;  // last show() lost the RMT lock: draw again

void setup() {
    // First thing: SK6812s keep their last frame across an ESP reset (network
    // watchdog, panic, brownout), and their data lines float until begin().
    // Clearing them here keeps a reboot from leaving the old frame — or noise
    // picked up by the floating lines — on the ring for seconds. The RMT lock
    // (shared with the status LED on Core 0) must exist before the first show().
    if (!rmt_lock::init()) esp_restart();
    leds.begin();

    Serial.begin(115200);
#if DEBUG_LOG_ENABLED
    // Wait for a USB host only when there is someone to read the logs; in
    // the bathroom there is no USB host and the wait would just add 3 s.
    const uint32_t t0 = millis();
    while (!Serial && millis() - t0 < cfg::SERIAL_WAIT_MS) {}
#endif

    Serial.printf("\n\n=== Smart Mirror v%s ===\n", cfg::FW_VERSION);

    pinMode(cfg::PIN_BUTTON, INPUT_PULLDOWN);
    pinMode(cfg::PIN_PIR, INPUT_PULLDOWN);

    cmdQueue  = xQueueCreate(cfg::CMD_QUEUE_LEN, sizeof(Command));
    snapQueue = xQueueCreate(1, sizeof(StateSnapshot));

    mirror.begin(millis());

    lastSnapshot = mirror.snapshot();
    xQueueOverwrite(snapQueue, &lastSnapshot);

    network::start(cmdQueue, snapQueue);
}

void loop() {
    const uint32_t now = millis();

    Command cmd;
    while (xQueueReceive(cmdQueue, &cmd, 0) == pdTRUE) mirror.apply(cmd, now);  // 1. commands from network

    ButtonEvent ev = button.update(digitalRead(cfg::PIN_BUTTON) == cfg::BTN_PRESSED, now);  // 2. button
    if (ev.type != ButtonEventType::None) mirror.onButton(ev, now);

    mirror.onPir(digitalRead(cfg::PIN_PIR) == HIGH, now);  // 3. PIR
    mirror.tick(now);                                      // 4. timers + animation step

    // 5. output: only when the frame changed, or when the last attempt lost
    // the RMT lock to the status LED and drew nothing.
    if (mirror.takeFrameDirty() || redrawPending) redrawPending = !leds.show(mirror.frame());

    StateSnapshot s = mirror.snapshot();  // 6. snapshot for the network
    if (!(s == lastSnapshot)) {
        xQueueOverwrite(snapQueue, &s);
        lastSnapshot = s;
    }

    vTaskDelay(pdMS_TO_TICKS(cfg::LOOP_IDLE_DELAY_MS));  // 5 ms
}

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
#include "Types.h"

static uint32_t espRandom(uint32_t bound) { return bound ? esp_random() % bound : 0; }

static Mirror    mirror(espRandom);
static Button    button;
static LedDriver leds;

static QueueHandle_t cmdQueue  = nullptr;
static QueueHandle_t snapQueue = nullptr;

static StateSnapshot lastSnapshot;

void setup() {
    Serial.begin(115200);
    const uint32_t t0 = millis();
    while (!Serial && millis() - t0 < cfg::SERIAL_WAIT_MS) {}

    Serial.printf("\n\n=== Smart Mirror v%s ===\n", cfg::FW_VERSION);

    pinMode(cfg::PIN_BUTTON, INPUT_PULLDOWN);
    pinMode(cfg::PIN_PIR, INPUT_PULLDOWN);

    leds.begin();  // clears both strips

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

    if (mirror.takeFrameDirty()) leds.show(mirror.frame());  // 5. output (only if frame changed)

    StateSnapshot s = mirror.snapshot();  // 6. snapshot for the network
    if (!(s == lastSnapshot)) {
        xQueueOverwrite(snapQueue, &s);
        lastSnapshot = s;
    }

    vTaskDelay(pdMS_TO_TICKS(cfg::LOOP_IDLE_DELAY_MS));  // 5 ms
}

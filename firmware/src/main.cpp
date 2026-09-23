// Smart Mirror firmware, v1.0.0. Core 1: setup()/loop() below — button, PIR,
// Mirror state machine, LedDriver (ARCHITECTURE.md 3.1, 3.3). Core 0:
// Network.cpp (WiFi/MQTT/watchdog). The two sides talk only through
// cmdQueue and snapQueue; neither core touches the other's objects.
#include <Arduino.h>
#include <esp_attr.h>
#include <esp_system.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "Button.h"
#include "Config.h"
#include "LedDriver.h"
#include "Log.h"
#include "Mirror.h"
#include "Network.h"
#include "PersistedFlags.h"
#include "RmtLock.h"
#include "Types.h"

static uint32_t espRandom(uint32_t bound) { return bound ? esp_random() % bound : 0; }

// Automation, night mode and glitch kept across software restarts (network
// watchdog, panic, task watchdog, brownout) — RTC memory is not cleared by
// those, and is not initialised at power-on (PersistedFlags.h validates it).
RTC_NOINIT_ATTR static PersistedFlags rtcFlags;

static Mirror    mirror(espRandom);
static Button    button;
static LedDriver leds;

static QueueHandle_t cmdQueue  = nullptr;
static QueueHandle_t snapQueue = nullptr;

static StateSnapshot lastSnapshot;
static bool          redrawPending = false;  // last show() lost the RMT lock: draw again
static uint32_t      lastShowMs    = 0;      // last frame actually sent to the strips
static uint32_t      showFailingSince = 0;   // first show() that lost the RMT lock in a row

static void applyFlag(CommandType type, bool flag) {
    Command c;
    c.type = type;
    c.flag = flag;
    mirror.apply(c, millis());
}

// After a software restart, put back the switches HA/Node-RED had set: a
// watchdog reboot at night must not turn motion_disable off and let the PIR
// light the mirror. Only the non-default values need applying.
static void restoreFlags() {
    bool automation = true, nightMode = false, glitch = true;
    if (!shouldRestoreFlags(static_cast<uint8_t>(esp_reset_reason()))) return;
    if (!unpackFlags(rtcFlags, automation, nightMode, glitch)) return;
    if (!automation) applyFlag(CommandType::Automation, false);
    if (nightMode) applyFlag(CommandType::NightMode, true);
    if (!glitch) applyFlag(CommandType::Glitch, false);
    MLOG("restored after restart: automation=%d night=%d glitch=%d\n", (int)automation, (int)nightMode, (int)glitch);
}

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
    // Out of memory at boot: a restart is the only sane answer — running on
    // would crash on a null queue or leave the mirror offline for good.
    if (cmdQueue == nullptr || snapQueue == nullptr) esp_restart();

    mirror.begin(millis());
    restoreFlags();

    lastSnapshot = mirror.snapshot();
    rtcFlags = packFlags(lastSnapshot.automation, lastSnapshot.nightMode, lastSnapshot.glitch);
    xQueueOverwrite(snapQueue, &lastSnapshot);

    if (!network::start(cmdQueue, snapQueue)) esp_restart();

    // Core 1 has no watchdog by default (the Arduino core leaves the loop
    // task unsubscribed and sdkconfig only watches the Core 0 idle task). A
    // hung show() or loop() would freeze the mirror for good while Core 0
    // kept reporting it healthy. With this the Arduino core feeds the task
    // watchdog before every loop() pass (every 5..12 ms); a pass that takes
    // longer than CONFIG_ESP_TASK_WDT_TIMEOUT_S (5 s) panics and reboots.
    enableLoopWDT();
}

void loop() {
    const uint32_t now = millis();

    Command cmd;
    while (xQueueReceive(cmdQueue, &cmd, 0) == pdTRUE) mirror.apply(cmd, now);  // 1. commands from network

    ButtonEvent ev = button.update(digitalRead(cfg::PIN_BUTTON) == cfg::BTN_PRESSED, now);  // 2. button
    if (ev.type != ButtonEventType::None) mirror.onButton(ev, now);

    mirror.onPir(digitalRead(cfg::PIN_PIR) == HIGH, now);  // 3. PIR
    mirror.tick(now);                                      // 4. timers + animation step

    // 5. output: when the frame changed, when the last attempt lost the RMT
    // lock and drew nothing, and every FRAME_REFRESH_MS regardless, so a
    // frame corrupted on the wire heals itself.
    const bool refreshDue = (uint32_t)(now - lastShowMs) >= cfg::FRAME_REFRESH_MS;
    if (mirror.takeFrameDirty() || redrawPending || refreshDue) {
        if (leds.show(mirror.frame())) {
            redrawPending = false;
            lastShowMs = now;
        } else {
            if (!redrawPending) showFailingSince = now;
            redrawPending = true;
            // The lock has been held for seconds: its holder hung in show().
            // loop() still feeds the task watchdog, so restart ourselves.
            if ((uint32_t)(now - showFailingSince) > cfg::SHOW_STALL_RESTART_MS) {
                MLOG("[%lu] RMT lock held for %lu ms -> restart\n", (unsigned long)now,
                     (unsigned long)(now - showFailingSince));
                esp_restart();
            }
        }
    }

    StateSnapshot s = mirror.snapshot();  // 6. snapshot for the network
    if (!(s == lastSnapshot)) {
        xQueueOverwrite(snapQueue, &s);
        lastSnapshot = s;
        rtcFlags = packFlags(s.automation, s.nightMode, s.glitch);  // kept for a software restart
    }

    vTaskDelay(pdMS_TO_TICKS(cfg::LOOP_IDLE_DELAY_MS));  // 5 ms
}

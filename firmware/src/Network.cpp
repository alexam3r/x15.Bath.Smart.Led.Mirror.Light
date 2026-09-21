// Task 7: NetworkTask implementation (ARCHITECTURE.md 3.4). Runs pinned to
// Core 0. Never touches Mirror, LedDriver or the frame — only Command /
// StateSnapshot cross cmdQueue / snapQueue (rule #2, enforced here by the
// include list below).
#include "Network.h"

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <cstdio>

#include "Config.h"
#include "Types.h"
#include "Log.h"
#include "Topics.h"
#include "Protocol.h"
#include "StatusLed.h"
#include "secrets.h"

namespace network {

namespace {

Topics       topics;
WiFiClient   espClient;
PubSubClient mqtt(espClient);
StatusLed    statusLed;

QueueHandle_t cmdQueueHandle  = nullptr;
QueueHandle_t snapQueueHandle = nullptr;

StateSnapshot latest;
bool          pending          = false;
uint32_t      lastPublish      = 0;
uint32_t      lastStatePublish = 0;

// Last sidecar values actually published, for publishChanged()'s diffing.
bool lastAutomation = false;
bool lastMakeup     = false;
bool lastNightMode  = false;
bool lastPir        = false;

// Publishes only `<base>/state` (retain), with the v27 dim-green 50 ms
// blink around it. Logs and skips if the snapshot does not fit STATE_JSON_CAP.
void publishState(const StateSnapshot& s) {
    char buf[cfg::STATE_JSON_CAP];
    const size_t n = buildStateJson(s, buf, sizeof(buf));
    if (n == 0) {
        MLOG("state json did not fit\n");
        return;
    }
    statusLed.set(0, 10, 0);
    mqtt.publish(topics.state, buf, true);
    vTaskDelay(pdMS_TO_TICKS(50));
    statusLed.set(0, 0, 0);
}

// force=true (publishAll): all four sidecars, unconditionally (bug C fix).
// force=false (publishChanged): only sidecars whose value changed.
void publishSidecars(const StateSnapshot& s, bool force) {
    const bool makeup = (s.base == BaseMode::Makeup);

    if (force || s.automation != lastAutomation) {
        mqtt.publish(topics.automationState, s.automation ? "ON" : "OFF", true);
        lastAutomation = s.automation;
    }
    if (force || makeup != lastMakeup) {
        mqtt.publish(topics.makeupState, makeup ? "ON" : "OFF", true);
        lastMakeup = makeup;
    }
    if (force || s.nightMode != lastNightMode) {
        mqtt.publish(topics.nightModeState, s.nightMode ? "ON" : "OFF", false);  // retain=false
        lastNightMode = s.nightMode;
    }
    if (force || s.pir != lastPir) {
        mqtt.publish(topics.pirState, s.pir ? "ON" : "OFF", true);
        lastPir = s.pir;
    }
}

void publishAll(const StateSnapshot& s) {
    publishState(s);
    publishSidecars(s, true);
}

void publishChanged(const StateSnapshot& s) {
    publishState(s);
    publishSidecars(s, false);
}

void onMessage(char* topic, uint8_t* payload, unsigned int len) {
    const Route r = routeTopic(topic, MQTT_BASE);
    Command cmd;
    if (toCommand(r, payload, len, cmd)) {
        if (xQueueSend(cmdQueueHandle, &cmd, 0) != pdTRUE) {
            MLOG("cmd queue full, dropped\n");
        }
    } else {
        MLOG("ignored %s\n", topic);
    }
}

void task(void*) {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname("SmartMirror");
    topics.init(MQTT_BASE);
    mqtt.setServer(MQTT_SERVER, MQTT_PORT);
    mqtt.setCallback(onMessage);
    mqtt.setBufferSize(cfg::MQTT_BUFFER_SIZE);
    statusLed.begin();
    statusLed.set(0, 0, 0);

    // main.cpp's setup() already pushed the initial snapshot before
    // creating this task, so the mailbox is guaranteed non-empty here.
    xQueueReceive(snapQueueHandle, &latest, portMAX_DELAY);

    uint32_t lastWatchdogCheck = millis();
    uint32_t wifiDownAccumMs   = 0;
    uint32_t mqttDownAccumMs   = 0;

    for (;;) {
        const uint32_t now = millis();

        // 1. Watchdog — cumulative WiFi/MQTT downtime, checked every
        //    WATCHDOG_CHECK_INTERVAL_MS; restart after WATCHDOG_TIMEOUT_MS
        //    (v27, ported unchanged). Evaluated before any `continue` below
        //    so a stuck WiFi reconnect loop still gets watchdogged.
        if ((uint32_t)(now - lastWatchdogCheck) >= cfg::WATCHDOG_CHECK_INTERVAL_MS) {
            const bool wifiUp  = (WiFi.status() == WL_CONNECTED);
            const bool mqttUp  = mqtt.connected();
            const uint32_t elapsed = (uint32_t)(now - lastWatchdogCheck);
            lastWatchdogCheck = now;

            if (wifiUp && mqttUp) {
                wifiDownAccumMs = 0;
                mqttDownAccumMs = 0;
            } else {
                if (!wifiUp) wifiDownAccumMs += elapsed;
                if (!mqttUp) mqttDownAccumMs += elapsed;
                MLOG("[%lu] WDG tick: wifi_up=%d mqtt_up=%d wifi_down=%lu mqtt_down=%lu\n",
                     (unsigned long)now, (int)wifiUp, (int)mqttUp,
                     (unsigned long)wifiDownAccumMs, (unsigned long)mqttDownAccumMs);
                if (wifiDownAccumMs > cfg::WATCHDOG_TIMEOUT_MS ||
                    mqttDownAccumMs > cfg::WATCHDOG_TIMEOUT_MS) {
                    MLOG("[%lu] WDG TIMEOUT -> ESP.restart()\n", (unsigned long)now);
                    vTaskDelay(pdMS_TO_TICKS(100));  // let Serial flush
                    ESP.restart();
                }
            }
        }

        // 2. WiFi down: yellow, reconnect, WiFi.setSleep(false) once up.
        if (WiFi.status() != WL_CONNECTED) {
            statusLed.set(100, 100, 0);

            WiFi.disconnect();
            WiFi.begin(WIFI_SSID, WIFI_PASS);

            uint8_t attempts = 0;
            while (WiFi.status() != WL_CONNECTED && attempts < cfg::WIFI_CONNECT_ATTEMPTS) {
                vTaskDelay(pdMS_TO_TICKS(cfg::WIFI_ATTEMPT_DELAY_MS));
                attempts++;
            }

            if (WiFi.status() == WL_CONNECTED) {
                WiFi.setSleep(false);  // rule #1 — required right after connect
                MLOG("WiFi connected\n");
            }
            continue;
        }

        // 3. MQTT down: blue, connect with LWT, subscribe + publish on success.
        if (!mqtt.connected()) {
            statusLed.set(0, 0, 100);

            uint8_t mac[6];
            WiFi.macAddress(mac);
            char clientId[32];
            std::snprintf(clientId, sizeof(clientId), "ESP32S3-Mirror-%02X%02X%02X",
                          mac[3], mac[4], mac[5]);

            if (mqtt.connect(clientId, MQTT_USER, MQTT_PASS,
                              topics.availability, 0, true, "offline")) {
                MLOG("MQTT connected\n");
                mqtt.subscribe(topics.set);
                mqtt.subscribe(topics.setWildcard);
                mqtt.publish(topics.availability, "online", true);

                statusLed.set(0, 100, 0);
                vTaskDelay(pdMS_TO_TICKS(1000));
                statusLed.set(0, 0, 0);

                publishAll(latest);
                pending          = false;
                lastPublish      = now;
                lastStatePublish = now;
            } else {
                vTaskDelay(pdMS_TO_TICKS(cfg::MQTT_RETRY_DELAY_MS));
            }
        } else {
            // 4/5. Connected: pump MQTT, drain the snapshot mailbox, publish
            // on change (rate-limited) and on the periodic heartbeat.
            mqtt.loop();

            StateSnapshot incoming;
            if (xQueueReceive(snapQueueHandle, &incoming, 0) == pdTRUE) {
                latest  = incoming;
                pending = true;
            }

            if (pending && (uint32_t)(now - lastPublish) >= cfg::PUBLISH_MIN_INTERVAL_MS) {
                publishChanged(latest);
                pending     = false;
                lastPublish = now;
            }

            if ((uint32_t)(now - lastStatePublish) >= cfg::TELEMETRY_PERIOD_MS) {
                publishState(latest);  // heartbeat: state only
                lastStatePublish = now;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

}  // namespace

void start(QueueHandle_t cmdQueue, QueueHandle_t snapQueue) {
    cmdQueueHandle  = cmdQueue;
    snapQueueHandle = snapQueue;

    TaskHandle_t handle = nullptr;
    xTaskCreatePinnedToCore(task, "NetworkTask", cfg::NETWORK_TASK_STACK, nullptr, 1, &handle, 0);
}

}  // namespace network

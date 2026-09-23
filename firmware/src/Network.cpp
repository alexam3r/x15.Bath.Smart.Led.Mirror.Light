// Task 7: NetworkTask implementation (ARCHITECTURE.md 3.4). Runs pinned to
// Core 0. Never touches Mirror, LedDriver or the frame — only Command /
// StateSnapshot cross cmdQueue / snapQueue (rule #2, enforced here by the
// include list below).
#include "Network.h"

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <esp_system.h>
#include <lwip/netdb.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <cstdio>

#include "Config.h"
#include "Types.h"
#include "Log.h"
#include "NetWatchdog.h"
#include "Topics.h"
#include "Protocol.h"
#include "StatusLed.h"
#include "secrets.h"

// Config.h keeps MQTT waits below the task watchdog; make sure its idea of
// the watchdog timeout matches the sdkconfig this core was built with.
static_assert(cfg::TASK_WDT_TIMEOUT_S == CONFIG_ESP_TASK_WDT_TIMEOUT_S,
              "cfg::TASK_WDT_TIMEOUT_S is out of date with the Arduino core's sdkconfig");

namespace network {

namespace {

Topics       topics;
WiFiClient   espClient;
PubSubClient mqtt(espClient);
StatusLed    statusLed;

QueueHandle_t cmdQueueHandle  = nullptr;
QueueHandle_t snapQueueHandle = nullptr;

NetWatchdog   netWatchdog;
StateSnapshot latest;
StateSnapshot lastStatePublished;  // what `<base>/state` last carried (publishChanged()'s diffing)
bool          pending          = false;
uint32_t      lastPublish      = 0;
uint32_t      lastStatePublish = 0;
bool          stackReported    = false;  // stack high-water mark logged once (debug build)

// Last sidecar values actually published, for publishChanged()'s diffing.
bool lastAutomation = false;
bool lastMakeup     = false;
bool lastNightMode  = false;
bool lastPir        = false;
bool lastGlitch     = false;

// Diagnostics (v1.2.0): uptime is counted here in whole seconds so it
// survives the 49.7-day millis() wraparound.
uint32_t uptimeS        = 0;
uint32_t lastUptimeTick = 0;
uint32_t lastDiagPublish = 0;

// Resolves MQTT_SERVER (a hostname or an IP literal) with lwIP's
// getaddrinfo(), which runs the lookup in the TCP/IP thread and waits for the
// resolver's own answer. Arduino-ESP32 2.0.17's WiFi.hostByName() — what
// PubSubClient uses when given a hostname — calls dns_gethostbyname() without
// the lwIP core lock and gives up after its own timeout; a late DNS answer
// then writes into a stack frame that no longer exists. Once per connect
// attempt only, so the small allocation inside getaddrinfo() is harmless.
bool resolveBroker(IPAddress& out) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(MQTT_SERVER, nullptr, &hints, &res) != 0 || res == nullptr) return false;
    out = IPAddress(reinterpret_cast<const sockaddr_in*>(res->ai_addr)->sin_addr.s_addr);
    freeaddrinfo(res);
    return true;
}

// Publishes `<base>/diag` (retain): uptime, WiFi level, last reset reason,
// free heap. Diagnostics live outside `<base>/state` on purpose — uptime
// changes every second and would churn the JSON Light entity in HA.
void publishDiag() {
    DiagInfo d;
    d.uptimeS     = uptimeS;
    d.rssi        = static_cast<int8_t>(WiFi.RSSI());
    d.resetReason = static_cast<uint8_t>(esp_reset_reason());
    d.freeHeap    = ESP.getFreeHeap();
    d.minFreeHeap = ESP.getMinFreeHeap();
    char buf[cfg::DIAG_JSON_CAP];
    if (buildDiagJson(d, buf, sizeof(buf)) == 0) {
        MLOG("diag json did not fit\n");
        return;
    }
    mqtt.publish(topics.diag, buf, true);
}

// Publishes only `<base>/state` (retain). blink=true — only the 10 s
// heartbeat (Ruling R16) — wraps it in the v27 dim-green 50 ms status-LED
// blink; event-driven publishes leave the status LED alone, so its
// NeoPixel show() calls (the R12 race with the strips, ARCHITECTURE.md 3.5)
// don't scale with how often the snapshot changes. Logs and skips if the
// snapshot does not fit STATE_JSON_CAP.
void publishState(const StateSnapshot& s, bool blink) {
    char buf[cfg::STATE_JSON_CAP];
    const size_t n = buildStateJson(s, buf, sizeof(buf));
    if (n == 0) {
        MLOG("state json did not fit\n");
        return;
    }
    if (blink) statusLed.set(0, 10, 0);
    if (mqtt.publish(topics.state, buf, true)) lastStatePublished = s;
    if (blink) {
        vTaskDelay(pdMS_TO_TICKS(50));
        statusLed.set(0, 0, 0);
    }
}

// force=true (publishAll): all sidecars, unconditionally (bug C fix).
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
    if (force || s.glitch != lastGlitch) {
        mqtt.publish(topics.glitchState, s.glitch ? "ON" : "OFF", true);
        lastGlitch = s.glitch;
    }
}

void publishAll(const StateSnapshot& s) {
    publishState(s, false);
    publishSidecars(s, true);
}

// `state` only if a field of its JSON changed — a PIR-only change goes out
// on pir/state alone (Ruling R16).
void publishChanged(const StateSnapshot& s) {
    if (stateJsonDiffers(lastStatePublished, s)) publishState(s, false);
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
    // setHostname() before mode(): on Arduino-ESP32 2.0.17, setHostname()
    // only caches the string (WiFiGeneric.cpp:901-904); the cached name is
    // pushed to the netif inside mode() (WiFiGeneric.cpp:1265), which is a
    // no-op if the mode isn't actually changing (early return at 1252-1254).
    // Calling it after mode(WIFI_STA) would leave the auto-generated
    // "esp32s3-XXXXXX" name on the netif — "SmartMirror" would be cached
    // but never applied.
    WiFi.setHostname("SmartMirror");
    WiFi.mode(WIFI_STA);
    topics.init(MQTT_BASE);
    mqtt.setCallback(onMessage);
    mqtt.setBufferSize(cfg::MQTT_BUFFER_SIZE);
    mqtt.setSocketTimeout(cfg::MQTT_SOCKET_TIMEOUT_S);  // < task watchdog, see Config.h
    mqtt.setKeepAlive(cfg::MQTT_KEEPALIVE_S);
    statusLed.begin();
    statusLed.set(0, 0, 0);

    // main.cpp's setup() already pushed the initial snapshot before
    // creating this task, so the mailbox is guaranteed non-empty here.
    xQueueReceive(snapQueueHandle, &latest, portMAX_DELAY);

    lastUptimeTick = millis();

    for (;;) {
        const uint32_t now = millis();

        // Uptime in whole seconds, counted by difference (wraparound-safe).
        while ((uint32_t)(now - lastUptimeTick) >= 1000) {
            lastUptimeTick += 1000;
            ++uptimeS;
        }

        // Newest snapshot from Core 1, in every state — the watchdog below
        // needs to know whether the mirror is lit even while offline.
        StateSnapshot incoming;
        if (xQueueReceive(snapQueueHandle, &incoming, 0) == pdTRUE) {
            latest  = incoming;
            pending = true;
        }

        // 1. Watchdog (NetWatchdog.h, v1.2.1): WiFi down for 5 min -> restart,
        //    but only once the mirror is dark; an MQTT-only outage never
        //    restarts. Evaluated before any `continue` below so a stuck WiFi
        //    reconnect loop still gets watchdogged.
        if (netWatchdog.update(now, WiFi.status() == WL_CONNECTED, latest.on)) {
            MLOG("[%lu] WDG: WiFi down %lu ms, mirror dark -> ESP.restart()\n",
                 (unsigned long)now, (unsigned long)netWatchdog.wifiDownMs());
            vTaskDelay(pdMS_TO_TICKS(100));  // let Serial flush
            ESP.restart();
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

            IPAddress brokerIp;
            if (!resolveBroker(brokerIp)) {
                MLOG("MQTT: cannot resolve the broker\n");
                vTaskDelay(pdMS_TO_TICKS(cfg::MQTT_RETRY_DELAY_MS));
                continue;
            }
            mqtt.setServer(brokerIp, MQTT_PORT);  // by IP: PubSubClient never calls hostByName()

            if (mqtt.connect(clientId, MQTT_USER, MQTT_PASS,
                              topics.availability, 0, true, "offline")) {
                MLOG("MQTT connected\n");
                mqtt.subscribe(topics.set);
                mqtt.subscribe(topics.setWildcard);

                // Mailbox depth is 1 (xQueueOverwrite on Core 1), so a
                // single non-blocking receive gets the newest snapshot if
                // Core 1 pushed one while mqtt.connect() was blocking.
                // Without this, publishAll() below would publish a stale
                // snapshot as retained state, and the real one would only
                // follow ~1 loop later — a visible false transition for
                // subscribers (e.g. pir/state ON->OFF on reconnect).
                xQueueReceive(snapQueueHandle, &latest, 0);

                // Real state first, "online" last (v1.2.1): when an entity
                // becomes available HA shows its cached state until the next
                // message, so announcing "online" first showed e.g. a stale
                // ON for a second before the real OFF — a false edge for
                // automations and the logbook.
                publishAll(latest);
                publishDiag();
                mqtt.publish(topics.availability, "online", true);

                statusLed.set(0, 100, 0);
                vTaskDelay(pdMS_TO_TICKS(1000));
                statusLed.set(0, 0, 0);

                const uint32_t connectedAt = millis();  // not `now` — mqtt.connect() and the 1 s flash both block
                pending          = false;
                lastPublish      = connectedAt;
                lastStatePublish = connectedAt;
                lastDiagPublish  = connectedAt;

                if (!stackReported) {
                    // Debug build only (MLOG compiles to nothing otherwise):
                    // minimum free stack so far, once WiFi connect, MQTT
                    // connect and publishAll() have all run. ESP-IDF's
                    // StackType_t is uint8_t, so the value is in bytes.
                    MLOG("[%lu] NetworkTask stack high-water mark: %lu of %lu bytes free\n",
                         (unsigned long)connectedAt,
                         (unsigned long)uxTaskGetStackHighWaterMark(nullptr),
                         (unsigned long)cfg::NETWORK_TASK_STACK);
                    stackReported = true;
                }
            } else {
                vTaskDelay(pdMS_TO_TICKS(cfg::MQTT_RETRY_DELAY_MS));
            }
        } else {
            // 4/5. Connected: pump MQTT, drain the snapshot mailbox, publish
            // on change (rate-limited) and on the periodic heartbeat.
            mqtt.loop();

            if (pending && (uint32_t)(now - lastPublish) >= cfg::PUBLISH_MIN_INTERVAL_MS) {
                publishChanged(latest);
                pending     = false;
                lastPublish = now;
            }

            if ((uint32_t)(now - lastStatePublish) >= cfg::TELEMETRY_PERIOD_MS) {
                publishState(latest, true);  // heartbeat: state only, with the status-LED blink
                lastStatePublish = now;
            }

            if ((uint32_t)(now - lastDiagPublish) >= cfg::DIAG_PERIOD_MS) {
                publishDiag();
                lastDiagPublish = now;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

}  // namespace

bool start(QueueHandle_t cmdQueue, QueueHandle_t snapQueue) {
    cmdQueueHandle  = cmdQueue;
    snapQueueHandle = snapQueue;

    TaskHandle_t handle = nullptr;
    return xTaskCreatePinnedToCore(task, "NetworkTask", cfg::NETWORK_TASK_STACK, nullptr, 1, &handle, 0) ==
           pdPASS;
}

}  // namespace network

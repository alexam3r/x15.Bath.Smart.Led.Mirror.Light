/**
 * ПРОЕКТ: Умное зеркало (v27 — simplified)
 * МК: ESP32-S3 Zero (4MB Flash)
 * Ленты: SK6812 (RGBW) на PIN 4 и 5. Status LED на PIN 21.
 *
 * АРХИТЕКТУРА:
 *  1. DUAL CORE: Core 0 (Network + MQTT + Status LED), Core 1 (Animation + Button + PIR).
 *  2. HARDWARE DEBOUNCE: Schmitt trigger SN74LVC2G14. BTN_PRESSED = HIGH.
 *  3. THREADSAFE: FreeRTOS Mutex (stateMutex).
 *  4. CONSTANTS: таймеры и параметры эффектов — #define, не runtime-config.
 *
 * v27 упрощения (по сравнению с v26):
 *  - Удалён NVS / runtime-конфиг. Все таймауты и параметры эффектов — #define.
 *  - Удалены select (mode), switch (makeup), scene, number (auto_off_min, pir_cooldown),
 *    template sensors. В HA только light + switch (motion) + binary_sensor (PIR).
 *  - Добавлена кнопка "Эффект": тройной клик hardware-кнопки или MQTT effect/set:GO.
 *  - applyDefaults(): при любом выключении (кнопкой, mqtt) сбрасывает цвет на
 *    тёплый (255,140,50) и яркость на 100%.
 *  - PIR_RESUME_DELAY_MS захардкожен на 30 секунд.
 *  - AUTO_OFF_MS захардкожен на 15 минут, сбрасывается при движении.
 *  - Случайный эффект через 4-5 минут простоя (рандом в этом диапазоне после
 *    каждого эффекта).
 *  - JSON Light упрощён до {state, brightness, color (r,g,b)}; color_temp / w /
 *    effect убраны. JSON state телеметрии: тот же набор + brightness_pct.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>

#include "secrets.h"  // WIFI_*, MQTT_*, TOPIC_*, MQTT_BASE, HA_DISCOVERY_PREFIX

// ==========================================
// 1. PINS & CONSTANTS
// ==========================================

#define PIN_LED_LEFT    4
#define PIN_LED_RIGHT   5
#define PIN_PIR         6
#define PIN_BUTTON      7
#define STATUS_LED_PIN  21

// Schmitt trigger inverts — pressed = HIGH
#define BTN_PRESSED     HIGH

#define LEDS_LEFT_CNT   66
#define LEDS_RIGHT_CNT  102
#define TOTAL_LEDS      (LEDS_LEFT_CNT + LEDS_RIGHT_CNT) // 168

#define LED_TYPE        (NEO_GRBW + NEO_KHZ800)

// --- Эффекты (параметры захардкожены — пользователь не настраивает) ---
#define SNAKE_SIZE       60
#define SNAKE_HEAD_SOLID 4
#define SNAKE_DELAY      40
#define SNAKE_FADE_STEPS 30

#define WAVE_RADIUS      18
#define WAVE_CORE        3
#define WAVE_DELAY       45

#define SLIDE_SPEED      28
#define SLIDE_EDGE       9

// --- Таймеры (захардкожены — пользователь не настраивает) ---
#define AUTO_OFF_MS         (15UL * 60UL * 1000UL)   // 15 минут до автовыключения
#define AUTO_EFFECT_MIN_MS  (4UL  * 60UL * 1000UL)   // 4 минуты (нижняя граница)
#define AUTO_EFFECT_MAX_MS  (5UL  * 60UL * 1000UL)   // 5 минут (верхняя граница)
#define PIR_RESUME_DELAY_MS (30UL * 1000UL)          // 30 секунд кулдауна PIR после OFF

#define LOOP_IDLE_DELAY_MS 5

// 4 центра для радиальных эффектов
const int CENTERS[] = {9, 51, 93, 134};

// Дефолтный цвет (тёплый оранжевый) — сюда сбрасываются настройки при выключении
#define DEFAULT_R 255
#define DEFAULT_G 140
#define DEFAULT_B 50

// ==========================================
// 2. FREERTOS OBJECTS
// ==========================================

Adafruit_NeoPixel stripL(LEDS_LEFT_CNT, PIN_LED_LEFT, LED_TYPE);
Adafruit_NeoPixel stripR(LEDS_RIGHT_CNT, PIN_LED_RIGHT, LED_TYPE);
Adafruit_NeoPixel statusLed(1, STATUS_LED_PIN, NEO_GRB + NEO_KHZ800);

WiFiClient espClient;
PubSubClient mqtt(espClient);

SemaphoreHandle_t stateMutex;
TaskHandle_t networkTask;

// ==========================================
// 3. STATE
// ==========================================

enum MirrorMode {
    MODE_OFF,
    MODE_ON,
    MODE_ANIM_ON,
    MODE_ANIM_OFF,
    MODE_EFFECT_SNAKE,
    MODE_EFFECT_WAVE
};

struct AppState {
    MirrorMode currentMode = MODE_OFF;

    uint8_t r = DEFAULT_R, g = DEFAULT_G, b = DEFAULT_B;

    // Режим работы. false = solid (RGB), true = makeup (белый канал).
    // Это НЕ эффект. Эффекты простоя (snake/wave) запускаются только в solid;
    // ручные эффекты (тройной клик / button) работают в обоих режимах.
    bool isMakeup = false;

    float valBrightness    = 0.0f;
    int   targetBrightness = 255;

    bool motionDetection = true;
    bool forceUpdate     = false;

    unsigned long lastAnimTimer = 0;

    int  snakeStartPos = 0, snakeStep = 0, snakeDirection = 1;
    bool isRainbowSnake = false;

    int waveCenter = 0, waveStep = 0;

    int slideCenter = 9, slideRadius = 0, slideMaxRadius = 0;

    // Таймер для отложенной реактивации PIR после ручного выключения.
    // 0 = нет активного кулдауна. После OFF выставляется в millis() + PIR_RESUME_DELAY_MS.
    unsigned long pirCooldownUntil = 0;
};

AppState state;

// volatile: эти поля пишутся из обоих ядер.
volatile unsigned long lastActivityTime    = 0;
volatile unsigned long lastIdleEffectTime  = 0;
volatile unsigned long nextAutoEffectMs    = AUTO_EFFECT_MIN_MS;     // пересчитывается после каждого эффекта
volatile bool           needTelemetryUpdate = false;

// --- Логика Кнопки (только Core 1) ---
int           btnState      = !BTN_PRESSED;
unsigned long btnPressTime  = 0;
bool          isHolding     = false;
int           clickCount    = 0;
unsigned long lastClickTime = 0;
int           dimDirection  = 5;
unsigned long lastDimRender = 0;

// ==========================================
// 4. PROTOTYPES & HELPERS
// ==========================================

void   setStatusLED(uint8_t r, uint8_t g, uint8_t b);
bool   processAnimation();                // true, если буферы лент были модифицированы
void   setVirtualPixel(int vIndex, uint8_t r, uint8_t g, uint8_t b, uint8_t w);
void   setGlobalColor();                  // только заполняет буферы, БЕЗ show()
static inline void showStrips();          // show() обеих лент, вызывать БЕЗ мьютекса
void   triggerPowerOn();
void   triggerPowerOff();
void   startSpecificEffect(int effectId);
void   startRandomEffect();
void   resetActivityTimer();
void   mqttCallback(char* topic, byte* payload, unsigned int length);
void   NetworkTaskCode(void * pvParameters);
void   publishDiscovery();

static inline float   getDarkFactor(int i, int center);
static inline uint8_t scale8(uint8_t x, uint8_t scale);
static inline uint32_t packColor(uint8_t r, uint8_t g, uint8_t b, uint8_t w);

static inline uint8_t scale8(uint8_t x, uint8_t scale) {
    return ((uint16_t)x * (1 + scale)) >> 8;
}

static inline uint32_t packColor(uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    return ((uint32_t)w << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

// Сбросить цвет/яркость/режим на дефолт. Вызывается при любом выключении.
// При следующем включении зеркало зажжётся в solid-режиме, тёплым оранжевым на 100%.
static void applyDefaults() {
    state.r               = DEFAULT_R;
    state.g               = DEFAULT_G;
    state.b               = DEFAULT_B;
    state.isMakeup        = false;
    state.targetBrightness = 255;
    state.valBrightness    = 255;
}

// ==========================================
// 5. SETUP
// ==========================================

void setup() {
    Serial.begin(115200);
    long startTime = millis();
    while (!Serial && millis() - startTime < 3000) {}
    Serial.println("\n\n!!! System Start: Smart Mirror v27 (simplified) !!!");

    randomSeed(esp_random());

    pinMode(PIN_BUTTON, INPUT_PULLDOWN);
    pinMode(PIN_PIR,    INPUT_PULLDOWN);

    stripL.begin(); stripR.begin();
    stripL.show();  stripR.show();   // гасим ленты при старте

    statusLed.begin();
    setStatusLED(0, 0, 0);

    stateMutex = xSemaphoreCreateMutex();
    resetActivityTimer();

    // Первый автоэффект придёт через 4-5 минут после старта
    nextAutoEffectMs = AUTO_EFFECT_MIN_MS + (unsigned long)random(0, (long)(AUTO_EFFECT_MAX_MS - AUTO_EFFECT_MIN_MS));

    // Сеть на Ядре 0
    xTaskCreatePinnedToCore(NetworkTaskCode, "NetworkTask", 10000, NULL, 1, &networkTask, 0);
}

// ==========================================
// 6. LOOP (CORE 1)
// ==========================================

void loop() {
    unsigned long now = millis();
    bool needsShow = false;  // true -> после отпускания мьютекса показать кадр

    // --- КНОПКА (аппаратный триггер Шмитта, без программного debounce) ---
    int reading = digitalRead(PIN_BUTTON);

    if (reading != btnState) {
        btnState = reading;
        if (btnState == BTN_PRESSED) {
            btnPressTime = now;
            isHolding = false;
            resetActivityTimer();
        }
        if (btnState == !BTN_PRESSED) {
            if (isHolding) {
                isHolding = false;
                clickCount = 0;
                needTelemetryUpdate = true;
            } else {
                if (now - lastClickTime > 400) clickCount = 1;
                else clickCount++;
                lastClickTime = now;
            }
            resetActivityTimer();
        }
    }

    // Удержание (диммирование)
    if (btnState == BTN_PRESSED && (now - btnPressTime > 500)) {
        isHolding = true;
        clickCount = 0;
        resetActivityTimer();

        if (now - lastDimRender > 30) {
            lastDimRender = now;

            xSemaphoreTake(stateMutex, portMAX_DELAY);
            state.targetBrightness += dimDirection;
            if (state.targetBrightness >= 255) { state.targetBrightness = 255; dimDirection = -5; }
            if (state.targetBrightness <= 5)   { state.targetBrightness = 5;   dimDirection = 5; }

            state.valBrightness = state.targetBrightness;
            if (state.currentMode != MODE_ON) state.currentMode = MODE_ON;

            setGlobalColor();   // заполняет буферы
            needsShow = true;   // отрисуем после мьютекса
            xSemaphoreGive(stateMutex);
        }
    }

    // Клики
    if (clickCount > 0 && btnState == !BTN_PRESSED && (now - lastClickTime > 400) && !isHolding) {
        resetActivityTimer();

        xSemaphoreTake(stateMutex, portMAX_DELAY);
        if (clickCount == 1) {
            if (state.currentMode == MODE_OFF || state.currentMode == MODE_ANIM_OFF) {
                triggerPowerOn();
                state.motionDetection = true;
                state.pirCooldownUntil = 0;
            } else {
                triggerPowerOff();
                state.motionDetection = false;
                state.pirCooldownUntil = now + PIR_RESUME_DELAY_MS;
            }
        }
        else if (clickCount == 2) {
            // Двойной клик — toggle режима Макияж (только если свет включён).
            // Если выключен — режим уже сброшен в OFF через applyDefaults,
            // поэтому здесь только включаем и сразу ставим makeup.
            if (state.currentMode == MODE_OFF || state.currentMode == MODE_ANIM_OFF) {
                state.isMakeup = true;
                triggerPowerOn();
                state.motionDetection = true;
                state.pirCooldownUntil = 0;
            } else {
                state.isMakeup = !state.isMakeup;
                state.forceUpdate = true;
            }
        }
        else if (clickCount == 3) {
            // Тройной клик — случайный эффект (работает в обоих режимах)
            startRandomEffect();
        }
        xSemaphoreGive(stateMutex);

        clickCount = 0;
        needTelemetryUpdate = true;
    }

    // --- АВТОМАТИКА + АНИМАЦИЯ (один блок под мьютексом) ---
    xSemaphoreTake(stateMutex, portMAX_DELAY);

    if (state.forceUpdate) {
        state.forceUpdate = false;
        setGlobalColor();
        needsShow = true;
    }

    // Реактивация PIR после истечения кулдауна (ручное выключение кнопкой)
    if (!state.motionDetection && state.pirCooldownUntil > 0 && now >= state.pirCooldownUntil) {
        state.motionDetection = true;
        state.pirCooldownUntil = 0;
        needTelemetryUpdate = true;
    }

    if (state.motionDetection) {
        if (digitalRead(PIN_PIR) == HIGH) {
            if (state.currentMode == MODE_OFF || state.currentMode == MODE_ANIM_OFF) {
                triggerPowerOn();
            }
            resetActivityTimer();  // сброс таймера автовыключения при движении
        }
        if ((state.currentMode == MODE_ON) && (now - lastActivityTime > AUTO_OFF_MS)) {
            triggerPowerOff();
        }
        if (state.currentMode == MODE_ON) {
            // Авто-эффект по таймеру запускается ТОЛЬКО в solid-режиме.
            // В makeup-режиме автоэффект не запускается, но ручной (тройной клик /
            // button.mirror_effect_button) — работает.
            if (!state.isMakeup) {
                unsigned long nextMs = nextAutoEffectMs;
                if (now - lastIdleEffectTime > nextMs) {
                    startRandomEffect();
                }
            }
        }
    }

    if (!isHolding) {
        if (processAnimation()) needsShow = true;
    }

    xSemaphoreGive(stateMutex);

    // --- ОТРИСОВКА ВНЕ МЬЮТЕКСА ---
    if (needsShow) showStrips();

    vTaskDelay(LOOP_IDLE_DELAY_MS / portTICK_PERIOD_MS);
}

// ==========================================
// 7. NETWORK TASK (CORE 0)
// ==========================================

void setStatusLED(uint8_t r, uint8_t g, uint8_t b) {
    statusLed.setPixelColor(0, statusLed.Color(r, g, b));
    statusLed.show();
}

// Публикация HA MQTT Discovery для light-сущности зеркала.
// После этого HA создаст light.mirror_bath_flat8 автоматически.
void publishDiscovery() {
    JsonDocument d;
    d["name"]             = "Зеркало ванная";
    d["unique_id"]        = "mirror_bath_flat8";
    d["stat_t"]           = TOPIC_STATE;
    d["cmd_t"]            = TOPIC_SET;
    d["schema"]           = "json";
    d["brightness"]       = true;
    d["brightness_scale"] = 255;
    JsonArray modes = d["supported_color_modes"].to<JsonArray>();
    modes.add("rgb");  // RGB без W — Yandex Smart Home требует чистый RGB для color_setting.rgb

    JsonObject dev = d["device"].to<JsonObject>();
    dev["identifiers"]  = "bath_mirror_esp32s3";
    dev["name"]         = "Контроллер Зеркала Ванной";
    dev["model"]        = "ESP32-S3-Zero";
    dev["manufacturer"] = "DIY-Vega";

    char buf[1024];
    serializeJson(d, buf);
    String topic = String(HA_DISCOVERY_PREFIX) + "/light/mirror_bath_flat8/config";
    mqtt.publish(topic.c_str(), buf, true);
}

void NetworkTaskCode(void * pvParameters) {
    Serial.print("Network Task running on Core ");
    Serial.println(xPortGetCoreID());

    WiFi.mode(WIFI_STA);
    WiFi.setHostname("SmartMirror");

    mqtt.setServer(MQTT_SERVER, MQTT_PORT);
    mqtt.setCallback(mqttCallback);

    bool wifiWasConnected = false;
    unsigned long lastPeriodicTelemetry = 0;
    bool lastPublishedMotion = true;  // синхронно с AppState.motionDetection = true по умолчанию

    for (;;) {
        unsigned long now = millis();

        // 1. WiFi
        if (WiFi.status() != WL_CONNECTED) {
            if (wifiWasConnected) {
                Serial.println("WiFi Lost.");
                wifiWasConnected = false;
            }
            setStatusLED(100, 100, 0);

            WiFi.disconnect();
            WiFi.begin(WIFI_SSID, WIFI_PASS);

            int attempts = 0;
            while (WiFi.status() != WL_CONNECTED && attempts < 20) {
                vTaskDelay(500 / portTICK_PERIOD_MS);
                attempts++;
            }

            if (WiFi.status() == WL_CONNECTED) {
                Serial.println("WiFi connected!");
                WiFi.setSleep(false);
                wifiWasConnected = true;
            }
            continue;
        }

        // 2. MQTT
        if (!mqtt.connected()) {
            setStatusLED(0, 0, 100);

            String clientId = "ESP32S3-Mirror-" + String(random(0xffff), HEX);
            if (mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
                Serial.println("MQTT connected");
                mqtt.subscribe(TOPIC_SET);        // legacy JSON
                mqtt.subscribe(TOPIC_BASE_SET);   // wildcard: motion, effect

                publishDiscovery();

                setStatusLED(0, 100, 0);
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                setStatusLED(0, 0, 0);

                needTelemetryUpdate = true;
            } else {
                vTaskDelay(5000 / portTICK_PERIOD_MS);
            }
        } else {
            mqtt.loop();

            // 3. Телеметрия (раз в 10с или по событию)
            if (needTelemetryUpdate || (now - lastPeriodicTelemetry > 10000)) {
                needTelemetryUpdate = false;
                lastPeriodicTelemetry = now;

                setStatusLED(0, 10, 0);

                JsonDocument doc;
                xSemaphoreTake(stateMutex, portMAX_DELAY);

                bool isOn = !(state.currentMode == MODE_OFF || state.currentMode == MODE_ANIM_OFF);
                doc["state"]          = isOn ? "ON" : "OFF";
                doc["brightness"]     = state.targetBrightness;             // 0..255
                doc["brightness_pct"] = map(state.targetBrightness, 0, 255, 0, 100);  // 0..100 для УДЯ
                // color_mode всегда rgb — Яндекс получает color_setting.rgb.
                // Режим Макияж не отдаём через color (чтобы Яндекс не пытался
                // сменить RGB и заодно случайно включить Макияж).
                doc["color_mode"]     = "rgb";

                JsonObject color = doc["color"].to<JsonObject>();
                color["r"] = state.r;
                color["g"] = state.g;
                color["b"] = state.b;

                doc["moveDetection"] = state.motionDetection ? "ON" : "OFF";
                doc["makeup"]        = state.isMakeup         ? "ON" : "OFF";

                bool motionNow = state.motionDetection;
                bool makeupNow = state.isMakeup;
                xSemaphoreGive(stateMutex);

                char buffer[256];
                serializeJson(doc, buffer);
                mqtt.publish(TOPIC_STATE, buffer, true);

                // Sidecar: motion state для binary_sensor (только при изменении)
                if (motionNow != lastPublishedMotion) {
                    mqtt.publish(TOPIC_MOTION_STATE, motionNow ? "ON" : "OFF", true);
                    lastPublishedMotion = motionNow;
                }

                // Sidecar: makeup state для switch (retain, всегда актуально)
                mqtt.publish(TOPIC_MAKEUP_STATE, makeupNow ? "ON" : "OFF", true);

                vTaskDelay(50 / portTICK_PERIOD_MS);
                setStatusLED(0, 0, 0);
            }
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

// ==========================================
// 8. COMMAND HANDLERS
// ==========================================

static void handleJsonCommand(byte* payload, unsigned int length) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, length);
    if (err) {
        Serial.print("JSON parse failed: ");
        Serial.println(err.c_str());
        return;
    }

    xSemaphoreTake(stateMutex, portMAX_DELAY);
    bool varsChanged = false;

    if (!doc["state"].isNull()) {
        const char* s = doc["state"] | "";
        if (strcmp(s, "ON") == 0 && (state.currentMode == MODE_OFF || state.currentMode == MODE_ANIM_OFF)) {
            triggerPowerOn();
            state.motionDetection = true;
            state.pirCooldownUntil = 0;
        }
        else if (strcmp(s, "OFF") == 0 && (state.currentMode != MODE_OFF && state.currentMode != MODE_ANIM_OFF)) {
            triggerPowerOff();
            state.motionDetection = false;
            state.pirCooldownUntil = millis() + PIR_RESUME_DELAY_MS;
        }
    }

    if (!doc["brightness"].isNull()) {
        int newTarget = constrain(doc["brightness"].as<int>(), 0, 255);
        state.targetBrightness = newTarget;
        state.valBrightness    = (float)newTarget;
        if (state.currentMode == MODE_ON) varsChanged = true;
    }

    if (!doc["color"].isNull()) {
        state.r = doc["color"]["r"] | state.r;
        state.g = doc["color"]["g"] | state.g;
        state.b = doc["color"]["b"] | state.b;
        // color.w НЕ обрабатываем. Макияж переключается ТОЛЬКО через switch
        // (mirror_makeup_switch) или 2-й клик hardware-кнопки. Раньше Яндекс
        // присылал color.w вместе с RGB, и зеркало уходило в Макияж — теперь это
        // развязано: Яндекс шлёт чистый RGB, Макияж — отдельная команда.
        if (state.currentMode == MODE_ON) varsChanged = true;
    }

    if (varsChanged) state.forceUpdate = true;

    xSemaphoreGive(stateMutex);
    needTelemetryUpdate = true;
}

// Обработка разделённых команд: motion/set, effect/set (через wildcard).
// payload: "ON"/"OFF" для motion, для effect достаточно любого непустого значения.
static void handleSimpleCommand(const char* sub, byte* payload, unsigned int length) {
    char buf[16] = {0};
    if (length >= sizeof(buf)) length = sizeof(buf) - 1;
    memcpy(buf, payload, length);
    buf[length] = 0;

    xSemaphoreTake(stateMutex, portMAX_DELAY);

    if (strcmp(sub, "motion") == 0) {
        bool wantOn = (strcasecmp(buf, "ON") == 0);
        if (state.motionDetection != wantOn) {
            state.motionDetection = wantOn;
            if (!wantOn) {
                state.pirCooldownUntil = millis() + PIR_RESUME_DELAY_MS;
            } else {
                state.pirCooldownUntil = 0;
            }
        }
    }
    else if (strcmp(sub, "makeup") == 0) {
        bool wantOn = (strcasecmp(buf, "ON") == 0);
        if (state.isMakeup != wantOn) {
            state.isMakeup = wantOn;
            // Если свет выключен, а пользователь просит Макияж — включаем.
            if (wantOn && (state.currentMode == MODE_OFF || state.currentMode == MODE_ANIM_OFF)) {
                triggerPowerOn();
                state.motionDetection = true;
                state.pirCooldownUntil = 0;
            } else {
                state.forceUpdate = true;
            }
        }
    }
    else if (strcmp(sub, "effect") == 0) {
        // Кнопка "Эффект" в HA → запустить случайный эффект
        startRandomEffect();
    }

    xSemaphoreGive(stateMutex);
    needTelemetryUpdate = true;
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    // 1. Legacy JSON
    if (strcmp(topic, TOPIC_SET) == 0) {
        handleJsonCommand(payload, length);
        return;
    }

    // 2. Разделённые топики .../<sub>/set
    size_t baseLen = strlen(MQTT_BASE);
    if (strncmp(topic, MQTT_BASE, baseLen) != 0) return;
    if (topic[baseLen] != '/') return;

    const char* rest = topic + baseLen + 1;
    const char* setPtr = strstr(rest, "/set");
    if (!setPtr || setPtr[4] != 0) return;

    size_t subLen = setPtr - rest;
    if (subLen == 0 || subLen >= 16) return;
    char sub[16] = {0};
    memcpy(sub, rest, subLen);
    sub[subLen] = 0;

    handleSimpleCommand(sub, payload, length);
}

// ==========================================
// 9. EFFECT LOGIC
// ==========================================

void resetActivityTimer() {
    lastActivityTime   = millis();
    lastIdleEffectTime = millis();
}

void triggerPowerOn() {
    state.currentMode       = MODE_ANIM_ON;
    state.slideCenter       = CENTERS[random(0, 4)];
    state.slideRadius       = 0;
    state.slideMaxRadius    = (TOTAL_LEDS / 2) + SLIDE_EDGE + 2;
    state.lastAnimTimer     = millis();

    resetActivityTimer();
}

void triggerPowerOff() {
    if (state.currentMode == MODE_OFF) return;
    state.currentMode = MODE_ANIM_OFF;
    if (state.slideRadius < state.slideMaxRadius && state.slideRadius > 0) {
        // Продолжаем с текущего радиуса
    } else {
        state.slideRadius = state.slideMaxRadius;
    }
    state.lastAnimTimer = millis();

    // ВАЖНО: при любом выключении сбрасываем цвет/яркость на дефолт.
    // При следующем включении зеркало зажжётся тёплым оранжевым на 100%.
    applyDefaults();
}

void startSpecificEffect(int effectId) {
    if (state.currentMode == MODE_OFF || state.currentMode == MODE_ANIM_OFF) return;

    if (effectId == 2) {
        state.currentMode = MODE_EFFECT_WAVE;
        state.waveCenter = CENTERS[random(0, 4)];
        state.waveStep   = 0;
    } else {
        state.currentMode = MODE_EFFECT_SNAKE;
        state.snakeStartPos   = random(TOTAL_LEDS);
        state.snakeDirection  = (random(0, 2) == 0) ? 1 : -1;
        state.snakeStep       = 0;
        state.isRainbowSnake  = (effectId == 1);
    }
    state.lastAnimTimer = millis();
    lastIdleEffectTime  = millis();
}

void startRandomEffect() {
    startSpecificEffect(random(0, 3));
    // Пересчитать случайный интервал до следующего автоэффекта (4-5 минут)
    nextAutoEffectMs = AUTO_EFFECT_MIN_MS + (unsigned long)random(0, (long)(AUTO_EFFECT_MAX_MS - AUTO_EFFECT_MIN_MS));
}

// ==========================================
// 10. ANIMATION (RENDER) — вызывается под мьютексом
// ==========================================

bool processAnimation() {
    unsigned long now = millis();
    bool rendered = false;

    // Базовый цвет и масштаб яркости.
    // В makeup-режиме базовый цвет — белый канал, RGB = 0.
    // Эффект (snake/wave) работает поверх: пиксели волны затемняют базу.
    uint8_t br = state.r, bg = state.g, bb = state.b, bw = 0;
    if (state.isMakeup) { br = 0; bg = 0; bb = 0; bw = 255; }
    uint8_t globalScale = (uint8_t)state.valBrightness;

    // --- SLIDE ON / SLIDE OFF ---
    if (state.currentMode == MODE_ANIM_ON || state.currentMode == MODE_ANIM_OFF) {
        if (now - state.lastAnimTimer < SLIDE_SPEED) return false;

        state.lastAnimTimer = now;
        int stepDir = (state.currentMode == MODE_ANIM_ON) ? +1 : -1;

        stripL.clear();
        stripR.clear();

        for (int i = 0; i < TOTAL_LEDS; i++) {
            int dist = abs(i - state.slideCenter);
            if (dist > TOTAL_LEDS / 2) dist = TOTAL_LEDS - dist;

            uint8_t edgeFade = 0;
            if (dist <= state.slideRadius) {
                if (dist <= (state.slideRadius - SLIDE_EDGE)) {
                    edgeFade = 255;
                } else {
                    edgeFade = (uint8_t)((state.slideRadius - dist) * 255 / SLIDE_EDGE);
                }
            }
            if (edgeFade > 0) {
                uint8_t s = scale8(edgeFade, globalScale);
                setVirtualPixel(i, scale8(br, s), scale8(bg, s), scale8(bb, s), scale8(bw, s));
            }
        }
        rendered = true;

        state.slideRadius += stepDir;

        bool finished = (stepDir > 0)
            ? (state.slideRadius >= state.slideMaxRadius)
            : (state.slideRadius < 0);

        if (finished) {
            if (stepDir > 0) {
                state.currentMode = MODE_ON;
                setGlobalColor();
                rendered = true;
            } else {
                state.currentMode = MODE_OFF;
                stripL.clear();
                stripR.clear();
                rendered = true;
            }
        }
    }

    // --- SNAKE ---
    else if (state.currentMode == MODE_EFFECT_SNAKE) {
        if (now - state.lastAnimTimer < SNAKE_DELAY) return false;

        state.lastAnimTimer = now;

        float effectAlpha = 1.0f;
        int totalSteps = TOTAL_LEDS + SNAKE_SIZE;
        if (state.snakeStep < SNAKE_FADE_STEPS)
            effectAlpha = (float)state.snakeStep / (float)SNAKE_FADE_STEPS;
        else if (state.snakeStep > (totalSteps - SNAKE_FADE_STEPS))
            effectAlpha = (float)(totalSteps - state.snakeStep) / (float)SNAKE_FADE_STEPS;
        if (effectAlpha < 0) effectAlpha = 0;
        if (effectAlpha > 1) effectAlpha = 1;

        int currentHead = state.snakeStartPos + (state.snakeStep * state.snakeDirection);
        while (currentHead < 0)            currentHead += TOTAL_LEDS;
        while (currentHead >= TOTAL_LEDS)  currentHead -= TOTAL_LEDS;

        for (int i = 0; i < TOTAL_LEDS; i++) {
            int dist = (state.snakeDirection == 1) ? (currentHead - i) : (i - currentHead);
            if (dist < 0) dist += TOTAL_LEDS;

            uint8_t fr, fg, fb, fw;

            if (state.isRainbowSnake) {
                if (dist >= 0 && dist < SNAKE_SIZE) {
                    uint16_t hue = map(dist, 0, SNAKE_SIZE, 0, 65535);
                    uint32_t rgbColor = stripL.ColorHSV(hue, 255, 255);
                    uint8_t rr = (uint8_t)(rgbColor >> 16);
                    uint8_t rg = (uint8_t)(rgbColor >> 8);
                    uint8_t rb = (uint8_t)rgbColor;
                    uint8_t alpha8 = (uint8_t)(effectAlpha * 255);
                    fr = scale8(rr, alpha8) + scale8(br, 255 - alpha8);
                    fg = scale8(rg, alpha8) + scale8(bg, 255 - alpha8);
                    fb = scale8(rb, alpha8) + scale8(bb, 255 - alpha8);
                    fw = scale8(bw, 255 - alpha8);
                } else {
                    fr = br; fg = bg; fb = bb; fw = bw;
                }
            } else {
                float snakeFactor = 1.0f;
                if (dist >= 0 && dist < SNAKE_SIZE) {
                    if (dist < SNAKE_HEAD_SOLID) snakeFactor = 0.0f;
                    else snakeFactor = (float)(dist - SNAKE_HEAD_SOLID) / (float)(SNAKE_SIZE - SNAKE_HEAD_SOLID);
                }
                uint8_t res = (uint8_t)((1.0f - effectAlpha * (1.0f - snakeFactor)) * 255);
                fr = scale8(br, res);
                fg = scale8(bg, res);
                fb = scale8(bb, res);
                fw = scale8(bw, res);
            }

            uint8_t s = globalScale;
            setVirtualPixel(i, scale8(fr, s), scale8(fg, s), scale8(fb, s), scale8(fw, s));
        }
        rendered = true;

        state.snakeStep++;
        if (state.snakeStep > totalSteps) {
            state.currentMode = MODE_ON;
            setGlobalColor();
            rendered = true;
            lastIdleEffectTime = millis();
        }
    }

    // --- WAVE (DARK PULSE) ---
    else if (state.currentMode == MODE_EFFECT_WAVE) {
        if (now - state.lastAnimTimer < WAVE_DELAY) return false;

        state.lastAnimTimer = now;

        int limit = TOTAL_LEDS / 2;
        int effectiveStep = (state.waveStep < limit) ? state.waveStep : limit;

        float fadeOut = 1.0f;
        if (state.waveStep > limit) {
            int fadeSteps = state.waveStep - limit;
            fadeOut = 1.0f - ((float)fadeSteps / (float)WAVE_RADIUS);
            if (fadeOut < 0) fadeOut = 0;
        }

        int c = state.waveCenter;
        int centerCW  = c + effectiveStep;
        int centerCCW = c - effectiveStep;
        while (centerCW  >= TOTAL_LEDS) centerCW  -= TOTAL_LEDS;
        while (centerCCW <  0)          centerCCW += TOTAL_LEDS;

        for (int i = 0; i < TOTAL_LEDS; i++) {
            float darkCW  = getDarkFactor(i, centerCW);
            float darkCCW = getDarkFactor(i, centerCCW);
            float totalDark = (darkCW > darkCCW ? darkCW : darkCCW) * fadeOut;
            uint8_t bm = (uint8_t)((1.0f - totalDark) * 255);
            uint8_t fr = scale8(br, bm);
            uint8_t fg = scale8(bg, bm);
            uint8_t fb = scale8(bb, bm);
            uint8_t fw = scale8(bw, bm);
            uint8_t s  = globalScale;
            setVirtualPixel(i, scale8(fr, s), scale8(fg, s), scale8(fb, s), scale8(fw, s));
        }
        rendered = true;

        state.waveStep++;
        if (state.waveStep > (limit + WAVE_RADIUS)) {
            state.currentMode = MODE_ON;
            setGlobalColor();
            rendered = true;
            lastIdleEffectTime = millis();
        }
    }

    return rendered;
}

// ==========================================
// 11. PIXEL HELPERS
// ==========================================

void setVirtualPixel(int vIndex, uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    if (vIndex < 0 || vIndex >= TOTAL_LEDS) return;

    uint32_t color = packColor(r, g, b, w);
    if (vIndex < LEDS_RIGHT_CNT) {
        stripR.setPixelColor(vIndex, color);
    } else {
        int physicalIndex = (LEDS_LEFT_CNT - 1) - (vIndex - LEDS_RIGHT_CNT);
        stripL.setPixelColor(physicalIndex, color);
    }
}

void setGlobalColor() {
    uint8_t k = (uint8_t)state.valBrightness;
    // В makeup-режиме RGB = 0, горит только белый канал (W).
    uint32_t color = state.isMakeup
        ? packColor(0, 0, 0, k)
        : packColor(scale8(state.r, k), scale8(state.g, k), scale8(state.b, k), 0);
    stripL.fill(color);
    stripR.fill(color);
}

static inline void showStrips() {
    stripL.show();
    stripR.show();
}

static inline float getDarkFactor(int i, int center) {
    int dist = abs(i - center);
    if (dist > TOTAL_LEDS / 2) dist = TOTAL_LEDS - dist;
    if (dist <= WAVE_CORE) return 1.0f;
    if (dist <= WAVE_RADIUS) return 1.0f - ((float)(dist - WAVE_CORE) / (WAVE_RADIUS - WAVE_CORE));
    return 0.0f;
}

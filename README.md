# Проект "Умное зеркало" (интеграция HA, Yandex Алиса)

[![MCU: ESP32-S3](https://img.shields.io/badge/MCU-ESP32--S3-blueviolet)](https://www.espressif.com/en/products/socs/esp32-s3)
[![LEDs: SK6812 RGBW](https://img.shields.io/badge/LEDs-SK6812%20RGBW-ff69b4)](#аппаратная-часть)
[![MQTT: JSON Light](https://img.shields.io/badge/MQTT-JSON%20Light-orange)](#mqtt-интерфейс)
[![Home Assistant](https://img.shields.io/badge/Home%20Assistant-integrated-41bdf5)](https://www.home-assistant.io/)
[![Yandex Smart Home](https://img.shields.io/badge/Yandex-Alice-yellow)](#интеграция-с-алисой)
[![License: MIT](https://img.shields.io/badge/License-MIT-green)](#лицензия)

Умное зеркало в ванной на **ESP32-S3 Zero** с адресной лентой **SK6812-RGBW** (168 светодиодов), управляемое через **Home Assistant** и **Алису** (Yandex Smart Home).

Прошивка оптимизирована для low-latency анимации на двух ядрах, с автодетекцией движения (PIR), аппаратной кнопкой и runtime-настраиваемыми параметрами эффектов через MQTT без перекомпиляции.

---

## Возможности

- **2 режима работы**: solid (RGB, тёплый оранжевый по умолчанию) и Макияж (белый канал W).
- **3 эффекта** (запускаются на фоне режима): тёмная змейка, радужная змейка, волна с затемнением.
- **Аппаратная кнопка** (триггер Шмитта SN74LVC2G14): 1 клик — вкл/выкл, 2 клика — toggle Макияж, 3 клика — случайный эффект, удержание — плавное диммирование.
- **Сброс настроек при выключении** — цвет возвращается к тёплому (255, 140, 50), яркость к 100%, режим — solid.
- **PIR с фиксированным кулдауном 30 секунд** — после ручного выключения автоматика «спит», чтобы свет не зажёгся в спину выходящему человеку.
- **Таймер автовыключения 15 минут** — если в ванной никого нет, свет гаснет. Любое движение сбрасывает таймер в ноль.
- **Автоэффект через 4-5 минут простоя** (рандом в этом диапазоне после каждого запуска) — только в solid-режиме. В Макияже автоэффекты не запускаются; ручные (3 клик / кнопка HA) работают в обоих режимах.
- **HA MQTT Discovery** — зеркало само регистрируется в Home Assistant, YAML-конфиг минимальный.
- **Разделённые MQTT-топики** — `set` (JSON Light), `motion/set`, `makeup/set`, `effect/set`.
- **Yandex Smart Home** — голосовое управление через Алису: «включи зеркало», «поставь режим макияж», «поставь таймер 45 минут», «выключи автоматику».

---

## Аппаратная часть

| Компонент | Кол-во / модель |
|---|---|
| MCU | ESP32-S3 Zero (4 MB Flash, USB-CDC) |
| LED-лента | SK6812 RGBW, 60 LED/m |
| Лента слева | 66 LED (PIN 4) |
| Лента справа | 102 LED (PIN 5) |
| Status LED | 1 × WS2812B на PIN 21 |
| PIR-датчик | HC-SR501 / AM312 на PIN 6 |
| Кнопка | Тактовая на PIN 7 (через SN74LVC2G14) |

### Схема подключения

```
                         ┌─────────────────┐
                         │   ESP32-S3 Zero │
                         │                 │
   SK6812 (left, 66) ────┤ GPIO 4   GPIO 5 ├──── SK6812 (right, 102)
                         │                 │
   PIR (HC-SR501) ───────┤ GPIO 6   GPIO 7 ├──── Кнопка → SN74LVC2G14 → GND
                         │                 │
   Status LED (WS2812B) ─┤ GPIO 21  USB-CDC├──── Serial / питание
                         │                 │
                         │    3V3   GND    │
                         └─────────────────┘
```

### Питание

- Ленты 5 В, ток до ~3.5 А при 168 LED на полной яркости белого.
- Рекомендуется отдельный источник 5 В × 5 А, общая земля с ESP32.
- Питание лент через MOSFET или силовой ключ с управлением от GPIO, если хотите гарантированно снимать питание в OFF.

---

## Софт

### Требования

- [PlatformIO](https://platformio.org/) (CLI или IDE)
- Python 3 (для тулчейна)

### Сборка и прошивка

```bash
git clone https://github.com/<user>/x15.Mira9-8.Mirror.Light.git
cd x15.Mira9-8.Mirror.Light/firmware
pio run -e esp32-s3-zero -t upload
pio device monitor  # serial-лог (115200 бод)
```

### Конфигурация — `firmware/src/secrets.h`

Содержит креды WiFi/MQTT и список топиков. **Не коммитить в публичный репозиторий** (добавить в `.gitignore`). В production рекомендуется provisioning через WiFiManager + NVS, см. комментарии в файле.

```cpp
const char* WIFI_SSID = "...";
const char* WIFI_PASS = "...";

const char* MQTT_SERVER = "10.0.0.1";
const int   MQTT_PORT   = 1883;
const char* MQTT_USER   = "...";
const char* MQTT_PASS   = "...";

const char* MQTT_BASE = "home/flat8/bath/mirror";
const char* HA_DISCOVERY_PREFIX = "homeassistant";
```

---

## MQTT-интерфейс

Базовый префикс: `home/flat8/bath/mirror/`.

### Команды (вход)

| Топик | Формат payload | Назначение |
|---|---|---|
| `set` | JSON (`state`, `brightness`, `color`) | Полное JSON-управление (HA JSON Light). `color.w > 0` → режим Макияж |
| `+/set` | wildcard | Подписка на любой из разделённых топиков |
| `motion/set` | `ON` / `OFF` | Включить/выключить PIR-автоматику |
| `makeup/set` | `ON` / `OFF` | Toggle режима Макияж (белый канал) |
| `effect/set` | `GO` | Запустить случайный эффект (аналог тройного клика кнопки) |

### Пример: JSON-команда «включить на 50 %» (белый тёплый)

```bash
mosquitto_pub -h 10.0.0.1 -t home/flat8/bath/mirror/set -m '{
  "state": "ON",
  "brightness": 128
}'
```

### Телеметрия (выход)

| Топик | retain | Назначение |
|---|---|---|
| `state` | да | JSON-статус: `state`, `brightness`, `brightness_pct`, `color` (rgb/w), `color_mode`, `moveDetection`, `makeup` |
| `motion/state` | да | `ON`/`OFF` — для `binary_sensor` |
| `makeup/state` | да | `ON`/`OFF` — для `switch` (режим Макияж) |

---

## Home Assistant

Зеркало публикует свою discovery-конфигурацию в `homeassistant/light/mirror_bath_flat8/config` при каждом MQTT-подключении. После этого в HA появляется `light.mirror_bath_flat8` автоматически.

В `homeassistant/configuration.yaml` дополнительно описаны: switch для автоматики PIR, binary_sensor для датчика движения и кнопка «Эффект». Объекты в УДЯ добавляются через UI / ярлыки.

---

## Интеграция с Алисой

Кастомизация capabilities и добавление объектов делается **только через UI** (ярлыки в Yandex Smart Home):

```
Settings → Devices & Services → Yandex Smart Home → Configure
```

| HA-сущность | Capability Яндекса |
|---|---|
| `light.mirror_bath_flat8` | `on_off`, `brightness` (0..100), `color_setting.rgb` |
| `switch.mirror_motion_switch` | `on_off` (PIR-автоматика) |
| `switch.mirror_makeup_switch` | `on_off` (режим Макияж — белый канал) |
| `button.mirror_effect_button` | (кнопка — сценарий «запустить случайный эффект») |

Таймеры автовыключения (15 мин) и кулдауна PIR (30 сек) — фиксированные, в УДЯ не выносятся. Эффекты запускаются через кнопку «Эффект» — одна кнопка, случайный.

### Примеры голосовых команд

- «Алиса, включи зеркало»
- «Алиса, выключи зеркало»
- «Алиса, яркость зеркала 40 процентов»
- «Алиса, сделай зеркало жёлтым» / «Алиса, цвет зеркала синий»
- «Алиса, включи макияж» / «выключи макияж»
- «Алиса, включи автоматику» / «выключи автоматику»
- «Алиса, запусти эффект на зеркале»

---

## Архитектура прошивки

### Двухядерное разделение (FreeRTOS)

```
┌─────────────────────────────────────────────────────────┐
│ Core 0 (NetworkTask)                                   │
│   - WiFi (STA, без powersave)                          │
│   - MQTT pub/sub + discovery                           │
│   - Telemetry каждые 10 сек или по событию             │
│   - Status LED                                         │
└─────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────┐
│ Core 1 (Arduino loop)                                  │
│   - Кнопка (аппаратный debounce через SN74LVC2G14)      │
│   - PIR + автоматика (ON/OFF, таймауты)                │
│   - Анимация (slide / snake / wave)                    │
│   - Adafruit NeoPixel RMT-вывод                        │
└─────────────────────────────────────────────────────────┘

       shared: stateMutex + volatile Cfg + volatile flags
```

### Эффекты

| Режим | Описание |
|---|---|
| `MODE_ON` | Сплошной цвет (RGB или белый в makeup) |
| `MODE_ANIM_ON` / `MODE_ANIM_OFF` | Плавный slide-розжиг / затухание от случайного центра |
| `MODE_EFFECT_SNAKE` | Бегущая змейка 60 LED с fade-хвостом, `dark` или `rainbow` |
| `MODE_EFFECT_WAVE` | Радиальная волна затемнения в обе стороны от центра |

### Потокобезопасность

- Все мутабельные поля `AppState` защищены `stateMutex`.
- Конфиг (`Cfg`) живёт в `volatile` и обновляется атомарно через `taskENTER_CRITICAL`.
- Флаг `needTelemetryUpdate` — `volatile bool`, синхронизирует Core 0 ↔ Core 1.
- `show()` всегда вызывается **вне мьютекса**, чтобы сетевая задача не блокировалась на RMT-передаче ~5 мс.

### Оптимизации

- `show()` вынесен из-под мьютекса → сетевая задача не блокируется ~5 мс на кадр.
- `vTaskDelay` в `loop()` → idle CPU падает со 100 % до ~1 %.
- `scale8()` вместо `float`-математики в горячих циклах эффектов.
- `JsonDocument` парсится прямо из `payload` без промежуточного `String`.
- `SnakeSize`/`WaveRadius`/таймауты — `volatile`, не `#define`, меняются на лету.

---

## Управление кнопкой

| Действие | Эффект |
|---|---|
| 1 короткий клик | Вкл / Выкл (с автовключением PIR и снятием кулдауна) |
| 2 коротких клика | Toggle режима «Макияж» (белый канал W). Если свет выключен — включает сразу в Макияже |
| 3 коротких клика | Случайный эффект (работает в обоих режимах) |
| Удержание ≥ 0.5 с | Плавное диммирование (пила 5↔255, шаг 5 каждые 30 мс) |

Антидребезг — аппаратный, через триггер Шмитта SN74LVC2G14 (инвертированная логика, `BTN_PRESSED = HIGH`).

---

## Структура репозитория

```
.
├── README.md                          # ← вы здесь (титульная страница GitHub)
├── firmware/                          # PlatformIO-проект (корень сборки)
│   ├── platformio.ini                 # конфиг сборки (esp32-s3-zero)
│   └── src/
│       ├── main.cpp                   # прошивка (v26 — Yandex-friendly)
│       └── secrets.h                  # WiFi/MQTT credentials, топики
└── homeassistant/                     # конфигурация Home Assistant
    └── configuration.yaml             # mqtt.light/switch/number/select/scene/template
```

Каталог `firmware/` самодостаточен — это корень PlatformIO-проекта. Команды сборки/прошивки выполняются из него:

```bash
cd firmware
pio run -e esp32-s3-zero -t upload
```

Каталог `homeassistant/` содержит YAML-конфиг HA. Файлы `.bak*` (если есть) — резервные копии до правок и в Git не коммитятся.

Дополнительные каталоги проекта (например, `altium/` для схем и PCB) добавляются в корень репозитория независимо.

---

## История версий

- **v26** (текущая) — NVS-конфиг, расширенный JSON, разделённые топики, HA Discovery, support Алисы.
- **v25** — двухядерная архитектура, `show()` вне мьютекса, `volatile` для shared-флагов, `scale8()`.
- **v24** — JSON Light, mutex-защита AppState.

---

## Лицензия

MIT. См. [LICENSE](LICENSE).

---

## Благодарности

- [Adafruit NeoPixel](https://github.com/adafruit/Adafruit_NeoPixel) — управление SK6812.
- [PubSubClient](https://github.com/knolleary/pubsubclient) — MQTT-клиент.
- [ArduinoJson](https://arduinojson.org/) v7 — парсинг JSON.
- [Espressif ESP-IDF](https://github.com/espressif/esp-idf) — базовый тулчейн.
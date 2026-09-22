# Проект "Умное зеркало" (интеграция HA, Yandex Алиса)

[![MCU: ESP32-S3](https://img.shields.io/badge/MCU-ESP32--S3-blueviolet)](https://www.espressif.com/en/products/socs/esp32-s3)
[![LEDs: SK6812 RGBW](https://img.shields.io/badge/LEDs-SK6812%20RGBW-ff69b4)](#аппаратная-часть)
[![MQTT: JSON Light](https://img.shields.io/badge/MQTT-JSON%20Light-orange)](#mqtt-интерфейс)
[![Home Assistant](https://img.shields.io/badge/Home%20Assistant-manual%20YAML-41bdf5)](https://www.home-assistant.io/)
[![Yandex Smart Home](https://img.shields.io/badge/Yandex-Alice-yellow)](#интеграция-с-алисой)
[![License: MIT](https://img.shields.io/badge/License-MIT-green)](#лицензия)

Умное зеркало в ванной на **ESP32-S3 Zero** с адресной лентой **SK6812-RGBW** (168 светодиодов), управляемое через **Home Assistant** и **Алису** (Yandex Smart Home).

Прошивка (v1.1.1, модульная архитектура — см. [ARCHITECTURE.md](ARCHITECTURE.md)) оптимизирована для
low-latency анимации на двух ядрах, с автодетекцией движения (PIR) и аппаратной кнопкой.

---

## Возможности

- **2 режима работы**: solid (RGB, тёплый оранжевый по умолчанию) и Макияж (белый канал W).
- **3 временных эффекта** (запускаются поверх текущего режима): тёмная змейка (`dark`), радужная змейка
  (`rainbow`), волна с затемнением (`wave`).
- **JSON Light `effect`** — одно поле в команде `set` принимает `solid, makeup, random, dark, rainbow, wave`:
  базовые режимы и запуск эффектов не требуют отдельных топиков.
- **Аппаратная кнопка** (триггер Шмитта SN74LVC2G14): 1 клик — вкл/выкл, 2 клика — toggle Макияж, 3 клика — случайный эффект, удержание — плавное диммирование.
- **Сброс настроек при выключении** — цвет возвращается к тёплому (255, 140, 50), яркость к 100%, режим — solid.
- **PIR с фиксированным кулдауном 15 секунд** — после ручного выключения автоматика «спит», чтобы свет не зажёгся в спину выходящему человеку.
- **Blackout-окно 2 секунды после любого OFF** — PIR полностью игнорируется в первые секунды, чтобы ложные импульсы от «отлипающего» датчика или отражений не запустили slide-IN поверх идущего slide-OUT.
- **«Глухое» отключение PIR через MQTT** — отдельная команда `motion_disable/set` для Node-RED: «на ночь отключить реакцию на PIR». Не выводится в HA — независимый флаг, отдельный от обычной автоматики `motion/set`. Если в момент команды зеркало горит, оно само гаснет. Снимается командой `OFF` или удержанием кнопки (≥0.5 с). Любое включение (кнопка / MQTT / makeup-on) автоматически снимает disable.
- **Автоматика PIR (`motion/set`) — постоянный переключатель.** Состояние держится до явной команды
  `motion/set ON`/`OFF` (в HA — `switch`), а не «отскакивает» самопроизвольно через 15 секунд.
- **Таймер автовыключения 15 минут** — если в ванной никого нет, свет гаснет. Любое движение сбрасывает таймер в ноль.
- **Глитч «сбой неона»** (v1.1.0) — примерно раз в минуту (случайно 45–90 с) случайный участок из 3–6 соседних
  светодиодов на 300–600 мс мерцает, как барахлящая неоновая трубка: гаснет, тлеет, вспыхивает; по краям
  (с v1.1.1) — по 2–3 светодиода плавного перехода к обычному свету. Только в solid,
  когда зеркало горит в покое (без эффекта, анимации и зажатой кнопки) и автоматика включена; состояние в HA
  не меняет. Выключается переключателем «Зеркало: глитч» (`glitch/set`).
- **Автоэффект через 4-5 минут простоя** (рандом в этом диапазоне после каждого запуска) — только в solid-режиме. В Макияже автоэффекты не запускаются; ручные (3 клик / кнопка HA) работают в обоих режимах.
- **`availability`** (LWT, retained) — Home Assistant показывает зеркало «недоступно», если оно пропало из сети (потеря питания/WiFi/MQTT).
- **`pir/state`** — отдельный retained-топик с сырым уровнем PIR (реальное движение); не путать с `motion/state` (флаг автоматики).
- **Serial-логи** — PIR rising-edge (не спамит на каждом тике цикла), mode transitions, авто-выключение, эффекты, однократно — запас стека `NetworkTask` после первого подключения к MQTT. Включаются флагом сборки `DEBUG_LOG_ENABLED` — окружение `esp32-s3-zero-debug` (1) или `esp32-s3-zero` (0, продакшен), см. `platformio.ini`.
- **Разделённые MQTT-топики** — `set` (JSON Light), `motion/set`, `motion_disable/set`, `makeup/set`, `effect/set`.
- **Network watchdog** — постоянное наблюдение за Wi-Fi + MQTT. Если любой канал суммарно недоступен **более 5 минут**, ESP перезагружается. Локальная работа зеркала (PIR, кнопка, эффекты) при отсутствии сети не страдает — watchdog триггерится только как последнее средство на случай зависания библиотек.
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
git clone git@github.com:alexam3r/x15.Bath.Smart.Led.Mirror.Light.git
cd x15.Bath.Smart.Led.Mirror.Light/firmware
pio run -e esp32-s3-zero -t upload
pio device monitor  # serial-лог (115200 бод)
```

Окружения (`firmware/platformio.ini`):

| env | Назначение |
|---|---|
| `esp32-s3-zero` | продакшен, `DEBUG_LOG_ENABLED=0` |
| `esp32-s3-zero-debug` | то же + `DEBUG_LOG_ENABLED=1` (Serial-логи PIR/режимов/эффектов через USB-CDC) |
| `native` | unit-тесты ядра (`firmware/lib/MirrorCore`) на ПК, без железа: `pio test -e native` |

Подробности сборки (закреплённые версии платформы/библиотек, флаги) — [ARCHITECTURE.md, §9](ARCHITECTURE.md).

### Конфигурация — `firmware/src/secrets.h`

Содержит только креды WiFi/MQTT и базовый MQTT-топик. **Не коммитится** (в `.gitignore`) — шаблон:
`firmware/src/secrets.h.sample`.

```cpp
#pragma once
#include <stdint.h>
// Copy to secrets.h (gitignored) and fill in. Included ONLY by Network.cpp.
constexpr char     WIFI_SSID[]   = "YOUR_WIFI_SSID";
constexpr char     WIFI_PASS[]   = "YOUR_WIFI_PASSWORD";
constexpr char     MQTT_SERVER[] = "YOUR_MQTT_HOST";
constexpr uint16_t MQTT_PORT     = 1883;
constexpr char     MQTT_USER[]   = "YOUR_MQTT_USER";
constexpr char     MQTT_PASS[]   = "YOUR_MQTT_PASSWORD";
constexpr char     MQTT_BASE[]   = "home/flat8/bath/mirror";   // no trailing slash
```

Все MQTT-топики строятся из `MQTT_BASE` при старте (`Topics::init`) — отдельных `TOPIC_*`-констант больше
нет. Флаг `DEBUG_LOG_ENABLED` (Serial-логи) переехал из `secrets.h` в `platformio.ini`
(`build_flags` окружений `esp32-s3-zero` / `esp32-s3-zero-debug`) — в `secrets.h` его больше нет.

> Старый `secrets.h` от v27 соберётся только если в нём остались имена `WIFI_SSID`, `WIFI_PASS`,
> `MQTT_SERVER`, `MQTT_PORT`, `MQTT_USER`, `MQTT_PASS`, `MQTT_BASE` — остальные v27-константы
> (`HA_DISCOVERY_PREFIX`, `TOPIC_*`) не мешают компиляции, но бесполезны. Рекомендуется пересоздать файл
> из нового `secrets.h.sample`.

---

## MQTT-интерфейс

Базовый топик — `MQTT_BASE` из `secrets.h` (в production `home/flat8/bath/mirror`). Полная спецификация
(JSON-схемы `set`/`state`, порядок применения полей, все топики) — в
[ARCHITECTURE.md, §5 «MQTT-протокол»](ARCHITECTURE.md). Коротко:

### Команды (вход)

| Топик | Payload | Назначение |
|---|---|---|
| `set` | JSON: `state`, `brightness`, `color`, `effect` | Полное управление (HA JSON Light). `effect` принимает `solid, makeup, random, dark, rainbow, wave` |
| `+/set` | wildcard | Подписка на любой из разделённых топиков |
| `motion/set` | switch-payload (`ON`/`OFF`/`1`/`0`/`TRUE`/`FALSE`, регистр не важен) | Включить/выключить PIR-автоматику. Состояние **постоянное** — держится до следующей команды. Повторный `ON`, когда автоматика уже включена, не отменяет 15-секундную паузу PIR |
| `motion_disable/set` | switch-payload | «Глухое» отключение PIR. НЕ в HA, для Node-RED. Снимается `OFF` или удержанием кнопки (≥0.5 с). Повторный `OFF`, когда ночной режим уже снят, ничего не меняет и не отменяет 15-секундную паузу PIR |
| `makeup/set` | switch-payload | Toggle режима Макияж (белый канал). Если свет был выключен — включает его |
| `effect/set` | любой | Запустить случайный эффект (аналог тройного клика кнопки) |
| `glitch/set` | switch-payload | Включить/выключить глитч «сбой неона» (v1.1.0). После перезагрузки зеркала снова включён |

Невалидный JSON или мусорный switch-payload — команда отбрасывается, пишется в лог (в debug-сборке).

### Пример (Node-RED): отключить PIR на ночь

```bash
# 23:00 — «глухо» отключить PIR. Если в этот момент свет горит — погаснет сам.
mosquitto_pub -h 10.0.0.1 -t home/flat8/bath/mirror/motion_disable/set -m 'ON'

# утром — включить обратно либо этой командой, либо удержанием кнопки (≥0.5 с)
mosquitto_pub -h 10.0.0.1 -t home/flat8/bath/mirror/motion_disable/set -m 'OFF'
```

### Пример: JSON-команда «включить на 50 %» (тёплый)

```bash
mosquitto_pub -h 10.0.0.1 -t home/flat8/bath/mirror/set -m '{
  "state": "ON",
  "brightness": 128
}'
```

### Телеметрия (выход)

| Топик | retain | Назначение |
|---|---|---|
| `state` | да | JSON-статус: `state`, `brightness`, `color`, `color_mode`, `effect`, `automation`, `night_mode`, `glitch`, `fw`, + устаревшие совместимые поля (`brightness_pct`, `moveDetection`, `makeup`) |
| `motion/state` | да | `ON`/`OFF` — флаг автоматики (`switch` в HA) |
| `motion_disable/state` | **нет** | `ON`/`OFF` — «глухой» ночной режим. Только для Node-RED, не в HA. После ребута ESP всегда `OFF` |
| `makeup/state` | да | `ON`/`OFF` — режим Макияж |
| `pir/state` | да | `ON`/`OFF` — сырой уровень PIR (реальное движение), для `binary_sensor` |
| `availability` | да | `online`/`offline` — LWT; HA показывает «недоступно», если зеркало пропало из сети |
| `glitch/state` | да | `ON`/`OFF` — флаг глитча (`switch` в HA) |

Все `*/state` публикуются целиком при (пере)подключении к MQTT, далее — только изменившиеся; основной
`state` дополнительно раз в 10 секунд (heartbeat).

---

## Home Assistant

HA MQTT Discovery в v1.0.0 **удалён** — зеркало ничего не публикует в `homeassistant/…/config`. Интеграция
настраивается вручную через YAML: [`ha_mirror.yaml`](ha_mirror.yaml) в корне репозитория. Файл можно положить
в `packages/` (`homeassistant: packages: !include_dir_named packages`) или скопировать его блок `mqtt:` в
`configuration.yaml`. Он описывает:

| Сущность | unique_id | Топики |
|---|---|---|
| light «Зеркало в душевой» — JSON Light: `brightness_scale: 255`, `supported_color_modes: [rgb]`, `effect_list: solid, makeup, random, dark, rainbow, wave` | `mirror_bath_flat8` | `set` / `state` |
| switch «Зеркало: автодетекция движения» | `mirror_motion_switch` | `motion/set` / `motion/state` |
| switch «Зеркало: режим Макияж» | `mirror_makeup_switch` | `makeup/set` / `makeup/state` |
| switch «Зеркало: глитч» (v1.1.0) | `mirror_glitch_switch` | `glitch/set` / `glitch/state` |
| binary_sensor «Движение ванная» (`device_class: motion`) | `mirror_motion_bath_flat8` | `pir/state` |
| button «Зеркало: эффект» | `mirror_effect_button` | `effect/set` |
| sensor «Зеркало: прошивка» (диагностика, поле `fw`) | `mirror_firmware_version` | `state` |

Все сущности зависят от `availability` (LWT). `unique_id` совпадают с прежним конфигом, поэтому история и
привязки в УДЯ сохраняются; «Движение ванная» теперь показывает реальный PIR, а не флаг автоматики.
Команды публикуются **без retain**: прошивка переподписывается на `set` и `+/set` после каждого
переподключения, и retained-команда выполнилась бы повторно. `transition` и `flash` отключены — прошивка
их не поддерживает.

> **Важно:** топик `motion_disable/state` и команда `motion_disable/set` **не выносятся в HA** — они нужны
> только для Node-RED (ночная автоматика). Обычный переключатель PIR-автоматики в HA/Алисе — через
> `motion/set`, теперь **постоянный** (не самовосстанавливается через 15 с, как в v27).

---

## Интеграция с Алисой

Сущности ниже описаны в `ha_mirror.yaml` (см. «Home Assistant» выше). Кастомизация capabilities и
добавление объектов делается **только через UI** (ярлыки в Yandex Smart Home):

```
Settings → Devices & Services → Yandex Smart Home → Configure
```

| HA-сущность | Capability Яндекса |
|---|---|
| `light.mirror_bath_flat8` | `on_off`, `brightness` (0..100), `color_setting.rgb` |
| `switch.mirror_motion_switch` | `on_off` (PIR-автоматика) |
| `switch.mirror_makeup_switch` | `on_off` (режим Макияж — белый канал) |
| `switch.mirror_glitch_switch` | `on_off` (глитч «сбой неона»; по желанию, если добавить в УДЯ) |
| `button.mirror_effect_button` | (кнопка — сценарий «запустить случайный эффект») |

Таймеры автовыключения (15 мин) и кулдауна PIR (15 сек) — фиксированные, в УДЯ не выносятся. Эффекты запускаются через кнопку «Эффект» — одна кнопка, случайный.

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

Два ядра ESP32-S3 общаются только через две очереди FreeRTOS (`cmdQueue`, `snapQueue`) — без мьютексов и
`volatile`-глобалов. **Core 0** (`NetworkTask`) владеет WiFi/MQTT/status LED и физически не может тронуть
ленту: `Network.cpp` даже не включает заголовки `Mirror`/`LedDriver`, так что вызвать отрисовку с сетевого
ядра невозможно на уровне компиляции. **Core 1** (`loop()`) владеет кнопкой, PIR, конечным автоматом
`Mirror` и выводом на ленту (`LedDriver`, две `Adafruit_NeoPixel`). Чистая логика — `Mirror`, эффекты
(`dark`/`rainbow`/`wave` + slide power-анимация), MQTT-протокол, автоматика PIR — живёт в
`firmware/lib/MirrorCore`: без Arduino/FreeRTOS, тестируется на ПК (`pio test -e native`, 147 тестов).
Network watchdog следит за WiFi + MQTT и перезагружает ESP при суммарном простое дольше 5 минут.

Полное описание — двухядерная модель, конечный автомат зеркала, MQTT-протокол, кольцо светодиодов,
реестр эффектов, структура модулей, критические правила — в **[ARCHITECTURE.md](ARCHITECTURE.md)**.

---

## Управление кнопкой

| Действие | Эффект |
|---|---|
| 1 короткий клик | Вкл (со slide-анимацией) / Выкл. При включении снимается ночной режим и 15-сек кулдаун; флаг автоматики (`motion/set`) не трогается |
| 2 коротких клика | Toggle режима «Макияж» (белый канал W). Если свет выключен — включает сразу в Макияже |
| 3 коротких клика | Случайный эффект (работает в обоих режимах) |
| Удержание ≥ 0.5 с | Если включён ночной режим (`motion_disable/set ON`) — снимает его и блокирует диммирование до конца этого удержания (свет сам не загорается; PIR ещё 15 с после начала удержания игнорируется, как после ручного выключения). Если зеркало было выключено — включает его slide-анимацией, диммирование начинается после её завершения. Иначе — плавное диммирование (пила 5↔255, шаг 5 каждые 30 мс) |

Антидребезг — аппаратный, через триггер Шмитта SN74LVC2G14 (инвертированная логика, `BTN_PRESSED = HIGH`).

---

## Проверка на железе после прошивки

Чек-лист ручной проверки (выполняет владелец после прошивки):

> **Результат для v1.0.0:** 22.09.2026 прошивка собрана, прошита в зеркало и работает так же, как v27.
> **Результат для v1.0.1:** 22.09.2026 прошита в зеркало; новая скорость анимации включения/выключения проверена
> вживую — отличная.

1. Нет мерцания ленты при активном WiFi/MQTT трафике (статика и все эффекты).
2. 1/2/3 клика, удержание (диммирование), удержание в ночном режиме (свет не включается — в т.ч. от PIR
   в ближайшие 15 с, даже если стоять перед зеркалом).
3. PIR: автовключение; после ручного выкл — 15 с тишины; после auto-off — 2 с blackout.
4. HA: вкл/выкл, яркость, цвет, каждый эффект из `effect_list`, `availability` при отключении питания.
5. Алиса: «включи зеркало», «включи макияж», «выключи автоматику».
6. Во время эффектов и диммирования при мигании статус-светодиода (мигает только на heartbeat `state` раз
   в 10 с; публикации по событиям его не трогают) нет сбоев кадра и перезагрузок (известный риск R12
   библиотеки NeoPixel).
7. Глитч (v1.1.1): в solid примерно раз в минуту на полсекунды мерцают 3–6 соседних светодиодов с мягкими
   краями по 2–3 светодиода; в Макияже,
   во время эффекта и при зажатой кнопке его нет; переключатель «Зеркало: глитч» его выключает.

---

## Структура репозитория

```
.
├── README.md                          # ← вы здесь (титульная страница GitHub)
├── ARCHITECTURE.md                    # спецификация модульной архитектуры v1
├── firmware/                          # PlatformIO-проект (корень сборки)
│   ├── platformio.ini                 # 3 окружения: esp32-s3-zero(-debug), native
│   ├── lib/MirrorCore/src/            # чистая логика (C++17, без Arduino/FreeRTOS) — тестируется на ПК
│   ├── src/                           # железо и сеть (Arduino/FreeRTOS)
│   │   ├── main.cpp                   # setup/loop, очереди, static-объекты Core 1
│   │   ├── Network.{h,cpp}            # NetworkTask: WiFi, MQTT, watchdog, публикация
│   │   ├── LedDriver.{h,cpp}          # 2 × Adafruit_NeoPixel, вывод на ленты
│   │   ├── StatusLed.{h,cpp}          # статусный WS2812 (Core 0)
│   │   ├── secrets.h                  # WiFi/MQTT credentials, MQTT_BASE (в .gitignore)
│   │   └── secrets.h.sample           # шаблон secrets.h (коммитится в репо)
│   └── test/                          # Unity-тесты MirrorCore, env:native
└── ha_mirror.yaml                     # HA YAML: MQTT JSON Light + switch/binary_sensor/button/sensor
```

Каталог `firmware/` самодостаточен — это корень PlatformIO-проекта. Команды сборки/прошивки выполняются из него:

```bash
cd firmware
pio run -e esp32-s3-zero -t upload
```

Дополнительные каталоги проекта (например, `Altium/` для схем и PCB) добавляются в корень репозитория независимо.

---

## История версий

- **v1.1.1** (текущая) — у глитча мягкие края: по 2–3 светодиода с каждой стороны плавно переходят от
  мерцающего ядра к обычному свету (весь участок 7–12 светодиодов). 147 unit-тестов.
- **v1.1.0** — глитч «сбой неона»: примерно раз в минуту (45–90 с) 3–6 соседних светодиодов на
  300–600 мс мерцают, как барахлящий неон; только в solid и в покое, не меняет состояние в HA; переключатель
  `glitch/set` / `glitch/state` («Зеркало: глитч» в `ha_mirror.yaml`), поле `glitch` в `state`. 146 unit-тестов.
- **v1.0.1** (тег `v1.0.1`; проверена на железе 22.09.2026 — скорость анимации отличная) —
  анимация включения/выключения на ~20% медленнее и мягче: шаг 33 мс вместо 28
  (≈3,2 с вместо ≈2,7 с), мягкий край кольца 11 светодиодов вместо 9. В HA сенсор прошивки покажет `1.0.1`.
- **v1.0.0** (проверена на железе 22.09.2026 — работает так же, как v27) — порт прошивки
  v27 с монолитного `main.cpp` на модульную архитектуру (`firmware/lib/MirrorCore` — чистая тестируемая
  логика, `firmware/src` — железо и сеть, две очереди FreeRTOS вместо мьютекса и `volatile`-глобалов,
  129 unit-тестов на ПК; в v1.0.1 — 130). Нумерация версий начата
  заново с 1; визуальное поведение (скорости, размеры, цвета) сохранено.
  Намеренные изменения поведения: `effect` в JSON Light (`solid, makeup, random, dark, rainbow, wave`);
  цвет из HA/Алисы переключает в `solid`; `motion/set OFF` теперь постоянный (раньше автоматика сама
  возвращалась через 15 с); `motion/state` — теперь только флаг автоматики, реальное движение — новый
  `pir/state`; LWT `availability`; удержание кнопки из `OFF` включает свет со slide-анимацией; включение
  во время slide-out разворачивает анимацию с текущего радиуса; единообразный switch-payload
  (`ON/OFF/1/0/TRUE/FALSE`); `brightness: 0` = выключение; HA MQTT Discovery удалён (ручной
  `ha_mirror.yaml`); стабильный MQTT client id по MAC; в `state` добавлены `effect`, `automation`,
  `night_mode`, `fw` (полный список — ARCHITECTURE.md §11.1).
  Исправленные баги v27: удержание для снятия ночного режима больше не «утекает» в диммирование и не
  включает свет; Discovery-пакет (не помещавшийся в 256-байтный буфер PubSubClient) убран вместе с
  Discovery, буфер увеличен до 512; `motion/state` теперь переопубликовывается при каждом подключении;
  таймеры переведены на разность `now − start` (переживают переполнение `millis()` через 49,7 сут);
  единственная точка переходов состояния — конечный автомат `Mirror`; повторные `motion_disable/set OFF` и
  `motion/set ON` (когда ночной режим уже снят / автоматика уже включена) больше не отменяют 15-секундную
  паузу PIR (полный список — ARCHITECTURE.md §11.2).
- **v27** — упрощения: PIR-защита (blackout + rising-edge, реакция только на `MODE_OFF`), команда `motion_disable/set` для Node-RED без авто-возврата (снимается командой `OFF` или удержанием кнопки), Serial-логи через `DEBUG_LOG_ENABLED`, `secrets.h.sample` как публичный шаблон, **network watchdog** (Wi-Fi + MQTT, ребут через 5 мин суммарного downtime), motion_disable-семантика по 5 сценариям, антиспам PIR-лога.
- **v26** — NVS-конфиг, расширенный JSON, разделённые топики, HA Discovery, support Алисы.
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
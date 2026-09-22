# План реализации v1.2.0 — новые эффекты и автоматика

> **Для агентов:** ОБЯЗАТЕЛЬНЫЙ СКИЛЛ — superpowers:subagent-driven-development (рекомендуется) или
> superpowers:executing-plans, задача за задачей. Шаги отмечаются чекбоксами (`- [ ]`).
>
> **Режим работы (требование владельца):** каждая задача — отдельный коммит со своим сообщением (на русском).
> После коммита каждой задачи — **пауза**: следующая задача начинается только после «продолжай» владельца.

**Цель:** добавить в прошивку зеркала четыре эффекта (`breathe`, `embers`, `candle`, `comet`), плавные переходы
цвета и яркости, предупреждающее затухание за минуту до автовыключения, 45-минутный таймер в режиме Макияж и
топик диагностики для Home Assistant — версия прошивки 1.2.0.

**Архитектура:** вся логика остаётся в `firmware/lib/MirrorCore` (чистый C++17, тесты на ПК). Эффекты — новые
классы `Effect` в реестре (класс + строка реестра + `effect_list`). Переходы и предупреждение — внутри
конечного автомата `Mirror`, через новый примитив `Ramp` (линейная интерполяция `uint8_t` по времени, переживает
переполнение `millis()`). Диагностика — только Core 0 (`Network.cpp`) плюс сериализация в `Protocol`.

**Стек:** PlatformIO, Arduino-ESP32 2.0.17 (espressif32@7.1.3), Adafruit NeoPixel 1.15.5, PubSubClient 2.8,
ArduinoJson 7.4.3, Unity (`pio test -e native`).

**Спецификация:** `ARCHITECTURE.md` (текущая реализация, §3–§8) + раздел «Дизайн» ниже (что меняется).

## Global Constraints

- MCU ESP32-S3 Zero, Flash 4 MB, `default.csv`; OTA и NVS **не делаем** (решение владельца: прошивка по проводу,
  сброс к дефолтам при выключении — желаемое поведение).
- C++17 (`-std=gnu++17`); ArduinoJson — только API v7 (`JsonDocument`, `isNull()`, `is<T>()`, `to<JsonObject>()`).
- `lib/MirrorCore` — без Arduino/FreeRTOS/Adafruit (кроме `Log.h` под `#ifdef ARDUINO`). Время — параметром `uint32_t now`.
- На Core 1 в горячем пути — никаких `String`, `new`, `malloc`; экземпляры эффектов статические.
- Лента — только Core 1; WiFi/MQTT/Status LED — только Core 0; обмен — только `cmdQueue`/`snapQueue`.
- Все интервалы времени — через `(uint32_t)(now − start)`.
- Шесть критических правил `start_prompt.md` §4 неприкосновенны (в этом плане ни одно не затрагивается,
  но проверка «`pio test -e native` зелёный + обе сборки без предупреждений в наших файлах» — после каждой задачи).
- Сообщения коммитов — **на русском**, завершаются строкой `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`.
  Push — только по команде владельца.
- TDD: сначала падающий тест (увидеть RED), затем минимальная реализация, затем зелёный прогон.
- Комментарии в коде — на английском (как во всём `MirrorCore`); документация — на русском.
- Команды сборки (из `/home/coe/Projects/mirror/firmware`):
  `export PLATFORMIO_CORE_DIR=/home/coe/Projects/mirror/.tools/pio-core; P=/home/coe/Projects/mirror/.tools/venv/bin/pio`
  тесты: `$P test -e native` (фокус: `-f test_mirror`), сборки: `$P run -e esp32-s3-zero`, `$P run -e esp32-s3-zero-debug`.

---

## Дизайн (что решено и почему)

| № владельца | Что | Решение |
|---|---|---|
| 6 | Плавный переход цвета/яркости | Любое изменение базового цвета, яркости или режима из команды (HA/Алиса), переключателя Макияжа или двойного клика — линейный переход за `TRANSITION_MS = 500` мс шагом 20 мс. Диммирование удержанием кнопки — мгновенное (оно и так ступенчатое каждые 30 мс). Включение из `OFF` — новый цвет сразу, без перехода (slide-анимация играет уже в нём). Эффекты видят текущий (переходный) цвет. В `state` отдаётся целевое значение сразу. |
| 9 | Затухание перед автовыключением | За `AUTO_OFF_WARN_MS = 60` с до автовыключения яркость рендера плавно (2 с) снижается до `WARN_DIM_LEVEL = 128` (50 %). Любая активность (PIR, кнопка, команда, которая и сейчас сбрасывает таймер) — возврат к полной за 1 с и таймер заново. Действует и во время эффекта (эффект рендерится притушенным). Только при включённой автоматике (как и само автовыключение). Настройка `brightness` и `state` в HA не меняются. |
| 8 | Макияж — 45 минут | `AUTO_OFF_MAKEUP_MS = 45` мин вместо 15. Предупреждение — за минуту до соответствующего лимита. Смена режима (solid ↔ makeup) считается активностью, иначе переключение из Макияжа на 30-й минуте в solid мгновенно выключило бы свет. |
| 1 | `breathe` — «дыхание» | Временный эффект: 3 цикла по 4 с (шаг 20 мс, 600 кадров), яркость всего кольца по косинусу от 100 % до 60 % (`BREATHE_MIN_LEVEL = 153`) и обратно. |
| 2 | `embers` — «угли» | Временный эффект ≈ 20 с (шаг 60 мс, 333 кадра, плавный вход/выход 2 с): у каждого светодиода свой уровень 40–100 %, каждый шаг 13 случайных светодиодов получают новую цель, движение к цели ≤ 6/шаг. |
| 3 | `candle` — «свеча» | Временный эффект ≈ 20 с (шаг 40 мс, 500 кадров, вход/выход 1 с): всё кольцо дрожит по яркости 89–100 % (`CANDLE_MIN_LEVEL = 227`), с вероятностью 1/20 — провал до 80–89 %, движение к цели ≤ 8/шаг. Владелец просил «в список эффектов» — поэтому временный, а не фоновый слой. |
| 5 | `comet` — «метеор» | Временный эффект: белая голова `{255,255,255,255}` с хвостом 40 светодиодов (квадратичное затухание), один оборот + хвост (208 шагов по 30 мс ≈ 6,2 с), вход/выход 20 шагов. Пиксель = `lerp(base, white, t)` — добавляет света и в solid, и в Макияже (там растёт RGB поверх W). |
| — | Реестр и `random` | Все четыре входят в `kEffects[]`, `effect_list` HA и в `random` (автоэффект раз в 4–5 мин выбирает из всех семи). |
| 13 | Диагностика | Отдельный retained-топик `<base>/diag` раз в `DIAG_PERIOD_MS = 60` с и при подключении: `{"uptime_s","rssi","reset_reason","free_heap","min_free_heap","fw"}`. Не в `state`: `uptime` меняется каждую секунду и не должен дёргать JSON Light. В `ha_mirror.yaml` — четыре диагностических сенсора. |
| — | Версия | `FW_VERSION = "1.2.0"` ставится в Задаче 1; тег `v1.2.0` — после проверки на железе, по команде владельца. |
| 12, 4, 7, 10, 11 | — | Не делаем (решение владельца). |

**Отклонённая альтернатива:** делать переходы и предупреждение через модификацию `brightness_`/`r_,g_,b_`. Нет —
это цели, которые уходят в HA; для рендера нужны отдельные «показанные» значения, иначе HA будет видеть промежуточные
цвета, а таймеры — путаться.

---

## Файлы

| Файл | Задачи | Ответственность |
|---|---|---|
| `firmware/lib/MirrorCore/src/Ramp.h` (новый) | 1 | Линейная интерполяция `uint8_t` по времени |
| `firmware/lib/MirrorCore/src/ColorMath.{h,cpp}` | 1 | `lerp8`, `lerp(Rgbw, Rgbw, t)` |
| `firmware/lib/MirrorCore/src/Config.h` | 1–8 | Константы `TRANSITION_*`, `AUTO_OFF_WARN_*`, `AUTO_OFF_MAKEUP_MS`, `BREATHE_*`, `EMBERS_*`, `CANDLE_*`, `COMET_*`, `DIAG_*`, `FW_VERSION` |
| `firmware/lib/MirrorCore/src/Mirror.{h,cpp}` | 1, 2, 3 | Переходы, предупреждение, лимит автовыключения по режиму |
| `firmware/lib/MirrorCore/src/Types.h` | 4–7 | Новые значения `EffectId` |
| `firmware/lib/MirrorCore/src/effects/{Breathe,Embers,Candle,Comet}.{h,cpp}` (новые) | 4–7 | Эффекты |
| `firmware/lib/MirrorCore/src/effects/EffectRegistry.cpp` | 4–7 | Строки реестра |
| `firmware/lib/MirrorCore/src/Topics.{h,cpp}`, `Protocol.{h,cpp}` | 8 | `diag`-топик, `DiagInfo`, `buildDiagJson`, `resetReasonName` |
| `firmware/src/Network.cpp` | 8 | Сбор и публикация диагностики |
| `firmware/test/test_ramp/test_main.cpp` (новый) | 1 | Ramp, lerp |
| `firmware/test/test_mirror/test_main.cpp` | 1–3 | Переходы, предупреждение, 45 мин |
| `firmware/test/test_effects/test_main.cpp` | 4–7 | Новые эффекты, реестр |
| `firmware/test/test_protocol/test_main.cpp` | 1, 4–8 | `fw`, имена эффектов, `diag` |
| `ha_mirror.yaml` | 4–8 | `effect_list`, сенсоры диагностики |
| `README.md`, `ARCHITECTURE.md` | каждая задача — свой абзац; 9 — сводка | Документация |

---

### Задача 1: `Ramp`, `lerp` и плавные переходы цвета/яркости (пункт 6)

**Файлы:**
- Создать: `firmware/lib/MirrorCore/src/Ramp.h`, `firmware/test/test_ramp/test_main.cpp`
- Изменить: `firmware/lib/MirrorCore/src/ColorMath.h`, `ColorMath.cpp`, `Config.h`, `Mirror.h`, `Mirror.cpp`,
  `firmware/test/test_mirror/test_main.cpp`, `firmware/test/test_protocol/test_main.cpp`, `README.md`, `ARCHITECTURE.md`

**Интерфейсы:**
- Производит: `struct Ramp { void start(uint8_t from, uint8_t to, uint32_t now, uint32_t durMs); void set(uint8_t v); uint8_t value(uint32_t now); bool running(uint32_t now) const; bool active; }`,
  `uint8_t lerp8(uint8_t a, uint8_t b, uint8_t t)`, `Rgbw lerp(Rgbw a, Rgbw b, uint8_t t)`,
  в `Mirror`: приватные `Rgbw targetColor() const`, `void retarget(uint32_t now, bool smooth)`, `void finishFrame()`,
  поля `shownColor_`, `shownBrightness_`, `fromColor_`, `fromBrightness_`, `Ramp trans_`, `lastTransStepMs_`.
  `baseColor()` теперь возвращает **показанный** цвет `shownColor_`.
- Константы: `cfg::TRANSITION_MS = 500`, `cfg::TRANSITION_STEP_MS = 20`, `cfg::FW_VERSION = "1.2.0"`.

- [ ] **Шаг 1: тесты `Ramp` и `lerp` (RED)**

`firmware/test/test_ramp/test_main.cpp`:

```cpp
// Ramp (linear uint8_t interpolation over time, overflow-safe) and lerp helpers.
#include <unity.h>

#include "ColorMath.h"
#include "Ramp.h"

void setUp(void) {}
void tearDown(void) {}

static void test_lerp8_endpoints_and_midpoint(void) {
    TEST_ASSERT_EQUAL_UINT8(10, lerp8(10, 200, 0));
    TEST_ASSERT_EQUAL_UINT8(200, lerp8(10, 200, 255));
    TEST_ASSERT_EQUAL_UINT8(105, lerp8(10, 200, 128));   // 10 + 190*128/255 = 105
    TEST_ASSERT_EQUAL_UINT8(100, lerp8(200, 0, 128));    // downwards: 200 - 200*128/255 = 100
}

static void test_lerp_rgbw_per_channel(void) {
    const Rgbw a{255, 140, 50, 0};
    const Rgbw b{0, 0, 255, 255};
    const Rgbw m = lerp(a, b, 255);
    TEST_ASSERT_TRUE(b == m);
    const Rgbw h = lerp(a, b, 128);
    TEST_ASSERT_EQUAL_UINT8(lerp8(255, 0, 128), h.r);
    TEST_ASSERT_EQUAL_UINT8(lerp8(0, 255, 128), h.w);
}

static void test_ramp_interpolates_linearly(void) {
    Ramp r;
    r.start(0, 200, 1000, 400);
    TEST_ASSERT_EQUAL_UINT8(0, r.value(1000));
    TEST_ASSERT_EQUAL_UINT8(100, r.value(1200));
    TEST_ASSERT_EQUAL_UINT8(150, r.value(1300));
    TEST_ASSERT_TRUE(r.running(1300));
}

static void test_ramp_retires_at_end_and_holds_target(void) {
    Ramp r;
    r.start(200, 50, 0, 100);
    TEST_ASSERT_EQUAL_UINT8(50, r.value(100));
    TEST_ASSERT_FALSE(r.active);
    TEST_ASSERT_EQUAL_UINT8(50, r.value(5000));
    r.set(77);
    TEST_ASSERT_EQUAL_UINT8(77, r.value(6000));
    TEST_ASSERT_FALSE(r.running(6000));
}

static void test_ramp_survives_millis_wraparound(void) {
    Ramp r;
    const uint32_t t0 = 0xFFFFFF00u;
    r.start(0, 100, t0, 512);
    TEST_ASSERT_EQUAL_UINT8(50, r.value(t0 + 256));  // crosses 0
    TEST_ASSERT_EQUAL_UINT8(100, r.value(t0 + 512));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_lerp8_endpoints_and_midpoint);
    RUN_TEST(test_lerp_rgbw_per_channel);
    RUN_TEST(test_ramp_interpolates_linearly);
    RUN_TEST(test_ramp_retires_at_end_and_holds_target);
    RUN_TEST(test_ramp_survives_millis_wraparound);
    return UNITY_END();
}
```

- [ ] **Шаг 2: убедиться, что RED** — `$P test -e native -f test_ramp` → ошибка компиляции `Ramp.h: No such file`.

- [ ] **Шаг 3: реализация `Ramp` и `lerp`**

`firmware/lib/MirrorCore/src/Ramp.h`:

```cpp
// Ramp — linear interpolation of a uint8_t value over a time span, using
// (now - startMs) so it survives the 49.7-day millis() wraparound. value()
// retires the ramp (active = false) once the span has elapsed and holds `to`
// from then on, like Countdown::update() (Ruling R7).
#pragma once

#include <cstdint>

struct Ramp {
    uint8_t  from = 0, to = 0;
    uint32_t startMs = 0, durMs = 0;
    bool     active = false;

    void start(uint8_t f, uint8_t t, uint32_t now, uint32_t dur) {
        from = f; to = t; startMs = now; durMs = dur; active = (dur > 0 && f != t);
        if (!active) from = t;
    }
    void set(uint8_t v) { from = to = v; active = false; }

    uint8_t value(uint32_t now) {
        if (!active) return to;
        const uint32_t elapsed = (uint32_t)(now - startMs);
        if (elapsed >= durMs) { active = false; return to; }
        return static_cast<uint8_t>(from + (static_cast<int32_t>(to) - from) * static_cast<int32_t>(elapsed) /
                                              static_cast<int32_t>(durMs));
    }
    bool running(uint32_t now) const { return active && (uint32_t)(now - startMs) < durMs; }
};
```

В `ColorMath.h` после `scale`:

```cpp
// a + (b - a) * t / 255: lerp8(a, b, 0) == a, lerp8(a, b, 255) == b.
uint8_t lerp8(uint8_t a, uint8_t b, uint8_t t);

// lerp8 applied independently to each of r, g, b, w.
Rgbw lerp(Rgbw a, Rgbw b, uint8_t t);
```

В `ColorMath.cpp`:

```cpp
uint8_t lerp8(uint8_t a, uint8_t b, uint8_t t) {
    return static_cast<uint8_t>(a + (static_cast<int32_t>(b) - a) * t / 255);
}

Rgbw lerp(Rgbw a, Rgbw b, uint8_t t) {
    return Rgbw{lerp8(a.r, b.r, t), lerp8(a.g, b.g, t), lerp8(a.b, b.b, t), lerp8(a.w, b.w, t)};
}
```

- [ ] **Шаг 4: GREEN** — `$P test -e native -f test_ramp` → 5/5.

- [ ] **Шаг 5: тесты переходов в `Mirror` (RED)**

В `firmware/test/test_mirror/test_main.cpp` (перед `int main`; `#include "Ramp.h"` не нужен):

```cpp
// --- Transitions (v1.2.0): colour/brightness/mode changes from commands
// fade over cfg::TRANSITION_MS; button dimming stays immediate. -------------

static bool channelBetween(uint8_t v, uint8_t lo, uint8_t hi) { return v >= lo && v <= hi; }

static void test_color_command_transitions_over_500ms(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    powerOnSettled(m, now);

    m.apply(lightColor(0, 0, 255), now);
    TEST_ASSERT_EQUAL_UINT8(0, m.snapshot().r);  // state reports the target at once
    run(m, now, cfg::TRANSITION_MS / 2);
    const Rgbw mid = m.frame()[0];
    TEST_ASSERT_TRUE_MESSAGE(channelBetween(mid.r, 110, 145), "red did not fade halfway at 250 ms");
    TEST_ASSERT_TRUE_MESSAGE(channelBetween(mid.b, 110, 145), "blue did not rise halfway at 250 ms");
    run(m, now, cfg::TRANSITION_MS / 2 + 50);
    TEST_ASSERT_TRUE(allPixelsEqual(m.frame(), Rgbw{0, 0, 255, 0}));
}

static void test_brightness_command_transitions(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    powerOnSettled(m, now);

    m.apply(lightBrightness(55), now);
    TEST_ASSERT_EQUAL_UINT8(55, m.snapshot().brightness);
    run(m, now, cfg::TRANSITION_MS / 2);
    TEST_ASSERT_TRUE(channelBetween(m.frame()[0].r, scale8(255, 140), scale8(255, 170)));
    run(m, now, cfg::TRANSITION_MS / 2 + 50);
    TEST_ASSERT_TRUE(allPixelsEqual(m.frame(), scale(kSolidDefault, 55)));
}

static void test_makeup_toggle_transitions(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    powerOnSettled(m, now);

    m.onButton(click(2), now);  // solid -> makeup
    run(m, now, cfg::TRANSITION_MS / 2);
    const Rgbw mid = m.frame()[0];
    TEST_ASSERT_TRUE(channelBetween(mid.r, 110, 145));
    TEST_ASSERT_TRUE(channelBetween(mid.w, 110, 145));
    run(m, now, cfg::TRANSITION_MS / 2 + 50);
    TEST_ASSERT_TRUE(allPixelsEqual(m.frame(), kMakeupColor));
}

static void test_hold_dimming_stays_immediate(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    powerOnSettled(m, now);

    m.onButton(holdStart(), now);
    now += 30;
    m.onButton(holdTick(), now);
    m.tick(now);
    TEST_ASSERT_EQUAL_UINT8(cfg::DEFAULT_BRIGHTNESS - cfg::DIM_STEP, m.frame()[0].r);
    m.onButton(holdEnd(), now);
}

static void test_power_on_from_off_uses_new_colour_at_once(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    m.apply(makeupCmd(true), now);  // from OFF: base = makeup and power on, no fade
    run(m, now, cfg::SLIDE_STEP_MS * 12);
    bool sawLit = false;
    for (uint16_t i = 0; i < Frame::kSize; ++i) {
        const Rgbw p = m.frame()[i];
        if (p.w > 0) sawLit = true;
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, p.r, "slide-in painted the old solid colour");
    }
    TEST_ASSERT_TRUE(sawLit);
}
```

И в `main()`: `RUN_TEST` для пяти тестов. Обновить существующие тесты, которые проверяют кадр сразу после
команды (теперь нужно дождаться перехода): в `test_click2_toggles_base_when_on`, `test_color_switches_to_solid`,
`test_frame_brightness_applied`, `test_glitch_cancelled_by_color_change` заменить `run(m, now, 10);` перед
`allPixelsEqual` на `run(m, now, cfg::TRANSITION_MS + 50);`. В `test_glitch_cancelled_by_color_change` также
оставить проверку `glitchSeenUntil(..., blue)` после перехода. Если после запуска упадут другие тесты с тем же
симптомом (кадр проверяется до 500 мс после команды/клика) — тот же приём.

В `firmware/test/test_protocol/test_main.cpp` заменить оба `\"fw\":\"1.1.1\"` на `\"fw\":\"1.2.0\"`.

- [ ] **Шаг 6: убедиться, что RED** — `$P test -e native -f test_mirror -f test_protocol` → новые тесты и `fw` падают.

- [ ] **Шаг 7: реализация в `Mirror`**

`Config.h` — в блок `// --- Timers (ms)` добавить:

```cpp
// --- Transitions (v1.2.0) ---------------------------------------------------
// Colour/brightness/mode changes from commands fade linearly; button dimming
// is applied at once (it is already stepped every DIM_PERIOD_MS).
constexpr uint32_t TRANSITION_MS      = 500;
constexpr uint32_t TRANSITION_STEP_MS = 20;
```

и `FW_VERSION[] = "1.2.0"`.

`Mirror.h`: `#include "Ramp.h"`; приватные методы `Rgbw targetColor() const; void retarget(uint32_t now, bool smooth); void finishFrame();`;
поля после `holdLocked_`:

```cpp
    // Shown (rendered) base colour/brightness — follow the targets r_/g_/b_/
    // base_/brightness_ through a TRANSITION_MS ramp (v1.2.0).
    Rgbw     shownColor_{cfg::DEFAULT_R, cfg::DEFAULT_G, cfg::DEFAULT_B, 0};
    uint8_t  shownBrightness_ = cfg::DEFAULT_BRIGHTNESS;
    Rgbw     fromColor_{cfg::DEFAULT_R, cfg::DEFAULT_G, cfg::DEFAULT_B, 0};
    uint8_t  fromBrightness_ = cfg::DEFAULT_BRIGHTNESS;
    Ramp     trans_;
    uint32_t lastTransStepMs_ = 0;
```

`Mirror.cpp`:

```cpp
Rgbw Mirror::targetColor() const {
    return base_ == BaseMode::Makeup ? Rgbw{0, 0, 0, 255} : Rgbw{r_, g_, b_, 0};
}

Rgbw Mirror::baseColor() const { return shownColor_; }

// Called after any change of the targets. smooth=true fades from the shown
// values while lit; otherwise (or when OFF/SLIDE_OFF) the targets apply at once.
void Mirror::retarget(uint32_t now, bool smooth) {
    const Rgbw target = targetColor();
    const bool lit = (power_ == PowerState::On || power_ == PowerState::SlideOn);
    if (smooth && lit && (!(target == shownColor_) || brightness_ != shownBrightness_)) {
        fromColor_ = shownColor_;
        fromBrightness_ = shownBrightness_;
        trans_.start(0, 255, now, cfg::TRANSITION_MS);
        lastTransStepMs_ = now;
    } else {
        shownColor_ = target;
        shownBrightness_ = brightness_;
        trans_.set(255);
    }
    staticDirty_ = true;
}

// Every render ends here: brightness applied to the whole frame, frame flagged.
void Mirror::finishFrame() {
    frame_.scale(shownBrightness_);
    frameDirty_ = true;
}
```

Правки по месту:
- удалить старое `baseColor()` (с тернарником) — его логика переехала в `targetColor()`;
- `applyDefaults()`: в конец добавить `shownColor_ = targetColor(); shownBrightness_ = brightness_; trans_.set(255);`;
- `powerOn()`: перед `power_ = PowerState::SlideOn;` добавить `if (power_ == PowerState::Off) retarget(now, false);`
  (из OFF — новый цвет сразу; из SLIDE_OFF показанный цвет уже актуален);
- `apply(Light)`: после трёх блоков (`brightness`, `color`, `effect Solid/Makeup`) и **до** `powerOff/powerOn` заменить
  три `staticDirty_ = true;` на одно `retarget(now, true);` (вызывать только если что-то из трёх изменилось —
  завести `bool changed = false;` и ставить `true` в каждом блоке);
- `apply(Makeup)`: вместо `staticDirty_ = true;` — `retarget(now, true);` (при `powerOn` из OFF `retarget(now,false)`
  внутри `powerOn` уже применит цвет; поэтому в ветке `flag` вызвать `retarget` **после** `powerOn`, он увидит `lit` и
  `target == shownColor_` → просто пометит `staticDirty_`);
- `onButton` `Click(2)` при включённом: вместо `staticDirty_ = true;` — `retarget(now, true);`;
- `onButton` `HoldTick`: вместо `staticDirty_ = true;` — `retarget(now, false);`;
- `tick()`: сразу после `gate_.tick(now);` добавить

```cpp
    if (trans_.active && (uint32_t)(now - lastTransStepMs_) >= cfg::TRANSITION_STEP_MS) {
        lastTransStepMs_ = now;
        const uint8_t t = trans_.value(now);  // retires itself at 255
        shownColor_ = lerp(fromColor_, targetColor(), t);
        shownBrightness_ = lerp8(fromBrightness_, brightness_, t);
        staticDirty_ = true;
    }
```

- все четыре места `frame_.scale(brightness_); frameDirty_ = true;` (slide, effect, static fill, `tickGlitch` ×2) заменить
  на `finishFrame();`.

- [ ] **Шаг 8: GREEN** — `$P test -e native` → все тесты зелёные (ожидается 147 + 10 новых = 157).
  Обе сборки `$P run -e esp32-s3-zero`, `-e esp32-s3-zero-debug` — без предупреждений в наших файлах.

- [ ] **Шаг 9: документация** — `ARCHITECTURE.md`: в §4.2 после таблицы абзац «Переходы (v1.2.0)» (текст из
  строки 6 таблицы «Дизайн»), в §8.1 упомянуть `Ramp.h`; `README.md`: в «Возможности» пункт «Плавные переходы цвета и
  яркости (0,5 с) при командах из HA/Алисы, переключении Макияжа и двойном клике; диммирование кнопкой — как прежде».

- [ ] **Шаг 10: коммит и пауза**

```bash
git add firmware/lib/MirrorCore/src/Ramp.h firmware/lib/MirrorCore/src/ColorMath.h firmware/lib/MirrorCore/src/ColorMath.cpp \
  firmware/lib/MirrorCore/src/Config.h firmware/lib/MirrorCore/src/Mirror.h firmware/lib/MirrorCore/src/Mirror.cpp \
  firmware/test/test_ramp/test_main.cpp firmware/test/test_mirror/test_main.cpp firmware/test/test_protocol/test_main.cpp \
  README.md ARCHITECTURE.md
git commit -m "feat: плавные переходы цвета и яркости (v1.2.0, п.6)

Изменение цвета, яркости или режима из команды HA/Алисы, переключателя
Макияжа или двойного клика теперь переходит плавно за 500 мс (шаг 20 мс).
Диммирование удержанием кнопки — мгновенное. Включение из OFF — новый цвет
сразу. Эффекты видят текущий переходный цвет; state отдаёт цель сразу.
Новый примитив Ramp (интерполяция по времени, переживает переполнение
millis) и lerp8/lerp в ColorMath. Версия прошивки 1.2.0.

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

**ПАУЗА — ждать «продолжай».**

---

### Задача 2: затухание за минуту до автовыключения (пункт 9)

**Файлы:**
- Изменить: `Config.h`, `Mirror.h`, `Mirror.cpp`, `firmware/test/test_mirror/test_main.cpp`, `README.md`, `ARCHITECTURE.md`

**Интерфейсы:**
- Потребляет: `Ramp`, `finishFrame()` из Задачи 1.
- Производит: константы `cfg::AUTO_OFF_WARN_MS = 60000`, `cfg::WARN_DIM_LEVEL = 128`, `cfg::WARN_FADE_IN_MS = 2000`,
  `cfg::WARN_FADE_OUT_MS = 1000`; в `Mirror`: `uint32_t autoOffLimit() const` (пока всегда `AUTO_OFF_MS`; Задача 3
  сделает его зависимым от режима), поля `bool warning_`, `Ramp warn_`, `uint8_t warnLevel_`, `uint32_t lastWarnStepMs_`;
  `finishFrame()` теперь масштабирует на `scale8(shownBrightness_, warnLevel_)`.

- [ ] **Шаг 1: тесты (RED)**

```cpp
// --- Pre-auto-off warning (v1.2.0): a minute before auto-off the rendered
// brightness fades to 50 %; any activity restores it and restarts the timer.

static void test_warning_dims_to_half_one_minute_before_auto_off(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    powerOnSettled(m, now);  // activity at the click (t=0)

    run(m, now, cfg::AUTO_OFF_MS - cfg::AUTO_OFF_WARN_MS - 10 - now);
    TEST_ASSERT_TRUE_MESSAGE(allPixelsEqual(m.frame(), kSolidDefault), "dimmed too early");
    run(m, now, 10 + cfg::WARN_FADE_IN_MS + 100);
    TEST_ASSERT_TRUE_MESSAGE(allPixelsEqual(m.frame(), scale(kSolidDefault, cfg::WARN_DIM_LEVEL)),
                              "not dimmed to 50 % two seconds into the warning");
    TEST_ASSERT_EQUAL_UINT8(cfg::DEFAULT_BRIGHTNESS, m.snapshot().brightness);  // HA setting untouched
    TEST_ASSERT_TRUE(PowerState::On == m.power());
}

static void test_activity_during_warning_restores_and_postpones(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    powerOnSettled(m, now);
    run(m, now, cfg::AUTO_OFF_MS - cfg::AUTO_OFF_WARN_MS + 5000 - now);
    TEST_ASSERT_FALSE(allPixelsEqual(m.frame(), kSolidDefault));  // warning running

    const uint32_t pirAt = now;
    m.onPir(true, now);
    m.onPir(false, now);
    run(m, now, cfg::WARN_FADE_OUT_MS + 100);
    TEST_ASSERT_TRUE_MESSAGE(allPixelsEqual(m.frame(), kSolidDefault), "brightness not restored after activity");

    run(m, now, cfg::AUTO_OFF_MS - cfg::AUTO_OFF_WARN_MS - 5000);  // well past the old auto-off moment
    TEST_ASSERT_TRUE_MESSAGE(PowerState::On == m.power(), "auto-off timer was not restarted");
    TEST_ASSERT_TRUE(allPixelsEqual(m.frame(), kSolidDefault));
    (void)pirAt;
}

static void test_no_warning_when_automation_off(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    powerOnSettled(m, now);
    m.apply(automationCmd(false), now);
    run(m, now, cfg::AUTO_OFF_MS + 60000);
    TEST_ASSERT_TRUE(PowerState::On == m.power());
    TEST_ASSERT_TRUE(allPixelsEqual(m.frame(), kSolidDefault));
}

static void test_warning_scales_a_running_effect(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    powerOnSettled(m, now);
    run(m, now, cfg::AUTO_OFF_MS - cfg::AUTO_OFF_WARN_MS + cfg::WARN_FADE_IN_MS + 500 - now);
    m.apply(randomEffectCmd(), now);  // zeroRandom: dark snake, head at 0 moving +1
    run(m, now, cfg::SNAKE_STEP_MS + 5);
    // Pixel 100 is >= 60 LEDs behind the head for the first ~40 steps -> plain base, dimmed.
    TEST_ASSERT_TRUE(scale(kSolidDefault, cfg::WARN_DIM_LEVEL) == m.frame()[100]);
}
```

`RUN_TEST` ×4.

- [ ] **Шаг 2: убедиться, что RED** — `$P test -e native -f test_mirror`: 4 падения (кадр не притушен).

- [ ] **Шаг 3: реализация**

`Config.h` рядом с `AUTO_OFF_MS`:

```cpp
// --- Pre-auto-off warning (v1.2.0) ----------------------------------------
// AUTO_OFF_WARN_MS before auto-off the rendered brightness ramps to
// WARN_DIM_LEVEL (50 %) over WARN_FADE_IN_MS; activity ramps it back over
// WARN_FADE_OUT_MS and restarts the timer.
constexpr uint32_t AUTO_OFF_WARN_MS = 60UL * 1000;
constexpr uint8_t  WARN_DIM_LEVEL   = 128;
constexpr uint32_t WARN_FADE_IN_MS  = 2000;
constexpr uint32_t WARN_FADE_OUT_MS = 1000;
```

`Mirror.h`: метод `uint32_t autoOffLimit() const;` и поля

```cpp
    bool     warning_       = false;  // inside the last AUTO_OFF_WARN_MS before auto-off
    Ramp     warn_;
    uint8_t  warnLevel_     = 255;    // render multiplier (255 = no warning)
    uint32_t lastWarnStepMs_ = 0;
```

`Mirror.cpp`:

```cpp
uint32_t Mirror::autoOffLimit() const { return cfg::AUTO_OFF_MS; }

void Mirror::markActivity(uint32_t now) {
    lastActivity_ = lastIdle_ = now;
    if (warning_) {
        warning_ = false;
        warn_.start(warn_.value(now), 255, now, cfg::WARN_FADE_OUT_MS);
        lastWarnStepMs_ = now;
    }
}

void Mirror::finishFrame() {
    frame_.scale(scale8(shownBrightness_, warnLevel_));
    frameDirty_ = true;
}
```

В `applyDefaults()` добавить `warning_ = false; warn_.set(255); warnLevel_ = 255;`.
В `tick()` блок автоматики заменить на:

```cpp
    if (gate_.automationActive() && power_ == PowerState::On) {
        const uint32_t idle = (uint32_t)(now - lastActivity_);
        const uint32_t limit = autoOffLimit();
        if (idle > limit) {
            MLOG("[%lu] AUTO-OFF (idle %lu ms)\n", (unsigned long)now, (unsigned long)idle);
            powerOff(false, now);
        } else {
            if (!warning_ && idle > limit - cfg::AUTO_OFF_WARN_MS) {
                warning_ = true;
                warn_.start(warnLevel_, cfg::WARN_DIM_LEVEL, now, cfg::WARN_FADE_IN_MS);
                lastWarnStepMs_ = now;
                MLOG("[%lu] AUTO-OFF WARNING (dim to 50%%)\n", (unsigned long)now);
            }
            if (effect_ == EffectId::None && base_ == BaseMode::Solid &&
                (uint32_t)(now - lastIdle_) > nextAutoEffectMs_) {
                startRandomEffect(now);
            }
        }
    }
    if (warn_.active && (uint32_t)(now - lastWarnStepMs_) >= cfg::TRANSITION_STEP_MS) {
        lastWarnStepMs_ = now;
        warnLevel_ = warn_.value(now);
        staticDirty_ = true;
    }
```

(Внимание: `warnLevel_` обновляется только пока `warn_.active`; когда ramp закончился, `value()` уже вернул конечное
значение на последнем шаге — но последний шаг мог не совпасть с концом. Поэтому после `if` добавить
`if (!warn_.active) warnLevel_ = warn_.to;` — гарантирует точные 128/255.)

- [ ] **Шаг 4: GREEN** — `$P test -e native` (все), обе сборки.

- [ ] **Шаг 5: документация** — `ARCHITECTURE.md` §4.4: пункт «Предупреждение перед автовыключением (v1.2.0)»;
  `README.md` «Возможности»: «За минуту до автовыключения свет плавно притухает до 50 % — махни рукой, и он вернётся,
  а таймер начнётся заново».

- [ ] **Шаг 6: коммит и пауза**

```bash
git add firmware/lib/MirrorCore/src/Config.h firmware/lib/MirrorCore/src/Mirror.h firmware/lib/MirrorCore/src/Mirror.cpp \
  firmware/test/test_mirror/test_main.cpp README.md ARCHITECTURE.md
git commit -m "feat: затухание за минуту до автовыключения (п.9)

За 60 с до автовыключения яркость плавно (2 с) снижается до 50 %. Любая
активность (PIR, кнопка, команда) возвращает полную яркость за 1 с и
запускает таймер заново. Действует и во время эффектов; только при
включённой автоматике. Настройка brightness в HA не меняется.

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

**ПАУЗА — ждать «продолжай».**

---

### Задача 3: 45 минут до автовыключения в режиме Макияж (пункт 8)

**Файлы:** `Config.h`, `Mirror.cpp`, `firmware/test/test_mirror/test_main.cpp`, `README.md`, `ARCHITECTURE.md`

**Интерфейсы:** `cfg::AUTO_OFF_MAKEUP_MS = 45 * 60 * 1000`; `autoOffLimit()` возвращает его при `base_ == Makeup`;
любое изменение `base_` вызывает `markActivity(now)`.

- [ ] **Шаг 1: тесты (RED)**

```cpp
// --- Makeup keeps the light for 45 min (v1.2.0) ------------------------------

static void test_makeup_auto_off_after_45_minutes(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    powerOnSettled(m, now);
    m.apply(makeupCmd(true), now);
    run(m, now, cfg::AUTO_OFF_MS + 5UL * 60 * 1000);          // 20 min: solid would be off by now
    TEST_ASSERT_TRUE_MESSAGE(PowerState::On == m.power(), "makeup switched off before 45 min");
    run(m, now, cfg::AUTO_OFF_MAKEUP_MS - (cfg::AUTO_OFF_MS + 5UL * 60 * 1000) - 60000);
    TEST_ASSERT_TRUE(PowerState::On == m.power());            // 44 min
    run(m, now, 60000 + 100 + kSlideMs);
    TEST_ASSERT_TRUE_MESSAGE(PowerState::Off == m.power(), "makeup did not auto-off after 45 min");
}

static void test_makeup_warning_at_44_minutes(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    powerOnSettled(m, now);
    m.apply(makeupCmd(true), now);
    run(m, now, cfg::AUTO_OFF_MAKEUP_MS - cfg::AUTO_OFF_WARN_MS - 10 - now);
    TEST_ASSERT_TRUE(allPixelsEqual(m.frame(), kMakeupColor));
    run(m, now, 10 + cfg::WARN_FADE_IN_MS + 100);
    TEST_ASSERT_TRUE(allPixelsEqual(m.frame(), scale(kMakeupColor, cfg::WARN_DIM_LEVEL)));
}

static void test_leaving_makeup_restarts_the_idle_timer(void) {
    Mirror m(zeroRandom);
    uint32_t now = 0;
    m.begin(now);
    powerOnSettled(m, now);
    m.apply(makeupCmd(true), now);
    run(m, now, 30UL * 60 * 1000);                              // 30 min idle in makeup
    m.apply(makeupCmd(false), now);                             // back to solid (15 min limit)
    run(m, now, cfg::AUTO_OFF_MS - 60000);
    TEST_ASSERT_TRUE_MESSAGE(PowerState::On == m.power(), "mode change did not count as activity");
    run(m, now, 60000 + 100 + kSlideMs);
    TEST_ASSERT_TRUE(PowerState::Off == m.power());
}
```

- [ ] **Шаг 2: RED** — первый и третий тесты падают (выключается через 15 мин / сразу).

- [ ] **Шаг 3: реализация** — `Config.h`: `constexpr uint32_t AUTO_OFF_MAKEUP_MS = 45UL * 60 * 1000;  // 45 min in makeup`
  под `AUTO_OFF_MS`. `Mirror.cpp`: `uint32_t Mirror::autoOffLimit() const { return base_ == BaseMode::Makeup ? cfg::AUTO_OFF_MAKEUP_MS : cfg::AUTO_OFF_MS; }`.
  В `apply(Light)` (ветки `effect Solid/Makeup` и `color` — там, где `base_` присваивается), `apply(Makeup)` и
  `onButton Click(2)` — если `base_` реально изменился, вызвать `markActivity(now);` (в `Click(2)` `onButton` уже
  вызывает `markActivity` в начале — там ничего не добавлять).

- [ ] **Шаг 4: GREEN** — все тесты, обе сборки.

- [ ] **Шаг 5: документация** — `ARCHITECTURE.md` §4.4 «Автовыключение: 15 мин в solid, **45 мин в Макияже**;
  смена режима — активность»; `README.md` «Возможности» и таблица кнопки/таймеров.

- [ ] **Шаг 6: коммит и пауза**

```bash
git add firmware/lib/MirrorCore/src/Config.h firmware/lib/MirrorCore/src/Mirror.cpp \
  firmware/test/test_mirror/test_main.cpp README.md ARCHITECTURE.md
git commit -m "feat: в режиме Макияж свет держится 45 минут (п.8)

Таймер автовыключения в Макияже — 45 мин вместо 15 (предупреждение за
минуту до него). Смена режима solid <-> makeup считается активностью,
чтобы переход в solid после долгого Макияжа не гасил свет мгновенно.

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

**ПАУЗА — ждать «продолжай».**

---

### Задача 4: эффект `breathe` — «дыхание» (пункт 1)

**Файлы:**
- Создать: `firmware/lib/MirrorCore/src/effects/Breathe.h`, `Breathe.cpp`
- Изменить: `Config.h`, `Types.h`, `effects/EffectRegistry.cpp`, `firmware/test/test_effects/test_main.cpp`,
  `firmware/test/test_protocol/test_main.cpp`, `ha_mirror.yaml`, `README.md`, `ARCHITECTURE.md`

**Интерфейсы:**
- Производит: `class Breathe : public Effect`, `EffectId::Breathe`, имя `"breathe"`,
  `cfg::BREATHE_STEP_MS = 20`, `cfg::BREATHE_CYCLE_STEPS = 200`, `cfg::BREATHE_CYCLES = 3`, `cfg::BREATHE_MIN_LEVEL = 153`.
- Правило реестра (§7): новый эффект = класс + строка `kEffects[]` + значение `EffectId` + имя в `effect_list`.

- [ ] **Шаг 1: тесты (RED)** — в `firmware/test/test_effects/test_main.cpp` добавить `#include "effects/Breathe.h"` и:

```cpp
// --- Breathe (v1.2.0): whole ring, cosine 100 % -> 60 % -> 100 %, 3 cycles ---

static bool ringUniform(const Frame& f) {
    for (uint16_t i = 1; i < Frame::kSize; ++i) {
        if (!(f[i] == f[0])) return false;
    }
    return true;
}

static void test_breathe_finishes_after_600_steps(void) {
    Breathe fx;
    Frame f;
    EffectContext ctx{kSolid, zeroRandom};
    fx.begin(ctx);
    TEST_ASSERT_EQUAL_UINT16(cfg::BREATHE_STEP_MS, fx.stepIntervalMs());
    int calls = 0;
    while (fx.step(f, ctx)) {
        ++calls;
        TEST_ASSERT_TRUE_MESSAGE(calls < 1000, "breathe did not finish");
    }
    TEST_ASSERT_EQUAL_INT(cfg::BREATHE_CYCLE_STEPS * cfg::BREATHE_CYCLES, calls + 1);
}

static void test_breathe_starts_full_and_dips_to_60_percent_at_half_cycle(void) {
    Breathe fx;
    Frame f;
    EffectContext ctx{kSolid, zeroRandom};
    fx.begin(ctx);
    fx.step(f, ctx);  // step 0
    TEST_ASSERT_TRUE(ringUniform(f));
    TEST_ASSERT_TRUE(kSolid == f[0]);
    for (int i = 0; i < cfg::BREATHE_CYCLE_STEPS / 2; ++i) fx.step(f, ctx);  // renders step 100
    TEST_ASSERT_TRUE(scale(kSolid, cfg::BREATHE_MIN_LEVEL) == f[0]);
    for (int i = 0; i < cfg::BREATHE_CYCLE_STEPS / 2; ++i) fx.step(f, ctx);  // renders step 200: full again
    TEST_ASSERT_TRUE(kSolid == f[0]);
}

static void test_breathe_never_below_60_percent(void) {
    Breathe fx;
    Frame f;
    EffectContext ctx{kMakeup, zeroRandom};
    fx.begin(ctx);
    const uint8_t floorW = scale8(255, cfg::BREATHE_MIN_LEVEL);
    while (fx.step(f, ctx)) {
        TEST_ASSERT_TRUE(f[0].w >= floorW);
        TEST_ASSERT_EQUAL_UINT8(0, f[0].r);
    }
}
```

В `test_registry_names_roundtrip` расширить массивы: `ids[] = {Dark, Rainbow, Wave, Breathe}`,
`expectedNames[] = {"dark","rainbow","wave","breathe"}`, цикл до 4. В `test_random_effect_covers_all` —
`cyclingRandom` уже берёт `% bound`, менять его не надо; поднять число итераций цикла с 3 до **4** (число записей
реестра) и добавить флаг `sawBreathe` с проверкой. `RUN_TEST` ×3. В `test_protocol` `test_light_effect_names` добавить блок
`{"effect": "breathe"}` → `Temporary` + `EffectId::Breathe`.

- [ ] **Шаг 2: RED** — ошибка компиляции `effects/Breathe.h`.

- [ ] **Шаг 3: реализация**

`Config.h` (после блока Wave):

```cpp
// --- Breathe effect (v1.2.0) -------------------------------------------------
// Whole ring, cosine brightness 100 % -> BREATHE_MIN_LEVEL -> 100 %,
// BREATHE_CYCLES cycles of BREATHE_CYCLE_STEPS steps.
constexpr uint32_t BREATHE_STEP_MS     = 20;
constexpr uint16_t BREATHE_CYCLE_STEPS = 200;  // 4 s
constexpr uint8_t  BREATHE_CYCLES      = 3;
constexpr uint8_t  BREATHE_MIN_LEVEL   = 153;  // 60 %
```

`Types.h`: `enum class EffectId : uint8_t { None = 0, Dark, Rainbow, Wave, Breathe };`

`effects/Breathe.h`:

```cpp
// Breathe (v1.2.0): the whole ring slowly dims to 60 % and back, three times.
#pragma once

#include <cstdint>

#include "Effect.h"

class Breathe : public Effect {
public:
    void begin(const EffectContext& ctx) override;
    uint16_t stepIntervalMs() const override;
    bool step(Frame& out, const EffectContext& ctx) override;

private:
    uint16_t step_ = 0;
};
```

`effects/Breathe.cpp`:

```cpp
#include "Breathe.h"

#include <cmath>

#include "../ColorMath.h"
#include "../Config.h"
#include "../Frame.h"

namespace {
constexpr uint16_t kTotalSteps = cfg::BREATHE_CYCLE_STEPS * cfg::BREATHE_CYCLES;
constexpr float kTwoPi = 6.28318530718f;
}  // namespace

void Breathe::begin(const EffectContext&) { step_ = 0; }

uint16_t Breathe::stepIntervalMs() const { return static_cast<uint16_t>(cfg::BREATHE_STEP_MS); }

bool Breathe::step(Frame& out, const EffectContext& ctx) {
    const float phase = kTwoPi * static_cast<float>(step_ % cfg::BREATHE_CYCLE_STEPS) /
                        static_cast<float>(cfg::BREATHE_CYCLE_STEPS);
    const float k = (1.0f + std::cos(phase)) * 0.5f;  // 1 at the cycle start, 0 halfway
    const uint8_t level = static_cast<uint8_t>(cfg::BREATHE_MIN_LEVEL + (255 - cfg::BREATHE_MIN_LEVEL) * k + 0.5f);
    out.fill(scale(ctx.base, level));
    ++step_;
    return step_ < kTotalSteps;
}
```

`EffectRegistry.cpp`: `#include "Breathe.h"`, `Breathe s_breathe;`, строка `{EffectId::Breathe, "breathe", &s_breathe},`.

`ha_mirror.yaml`: в `effect_list` добавить `- breathe`.

- [ ] **Шаг 4: GREEN** — `$P test -e native`, обе сборки.

- [ ] **Шаг 5: документация** — `ARCHITECTURE.md` §4.2 таблица эффектов: строка `breathe`; `README.md`: «4 временных
  эффекта» → перечислить; `effect_list` в описании MQTT.

- [ ] **Шаг 6: коммит и пауза**

```bash
git add firmware/lib/MirrorCore/src/effects/Breathe.h firmware/lib/MirrorCore/src/effects/Breathe.cpp \
  firmware/lib/MirrorCore/src/effects/EffectRegistry.cpp firmware/lib/MirrorCore/src/Config.h firmware/lib/MirrorCore/src/Types.h \
  firmware/test/test_effects/test_main.cpp firmware/test/test_protocol/test_main.cpp ha_mirror.yaml README.md ARCHITECTURE.md
git commit -m "feat: эффект breathe — «дыхание» (п.1)

Всё кольцо плавно (по косинусу) притухает до 60 % и возвращается, три
цикла по 4 с. Временный эффект: в реестре, в effect_list и в random.

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

**ПАУЗА — ждать «продолжай».**

---

### Задача 5: эффект `embers` — «угли» (пункт 2)

**Файлы:** создать `effects/Embers.{h,cpp}`; изменить `Config.h`, `Types.h`, `EffectRegistry.cpp`,
`test_effects`, `test_protocol`, `ha_mirror.yaml`, `README.md`, `ARCHITECTURE.md`.

**Интерфейсы:** `class Embers : public Effect`, `EffectId::Embers`, `"embers"`,
`cfg::EMBERS_STEP_MS = 60`, `cfg::EMBERS_STEPS = 333`, `cfg::EMBERS_FADE_STEPS = 33`, `cfg::EMBERS_CHANGES_PER_STEP = 13`,
`cfg::EMBERS_MIN_LEVEL = 102`, `cfg::EMBERS_SLEW = 6`.

- [ ] **Шаг 1: тесты (RED)**

```cpp
// --- Embers (v1.2.0): per-pixel smouldering 40..100 %, ~20 s -----------------

// rnd(TOTAL_LEDS) walks 0,1,2,... so every pixel gets a target eventually;
// rnd(level range) is always 0 -> target = EMBERS_MIN_LEVEL.
static uint32_t s_emberIdx = 0;
static uint32_t emberRandom(uint32_t bound) {
    if (bound == cfg::TOTAL_LEDS) return (s_emberIdx++) % cfg::TOTAL_LEDS;
    return 0;
}

static void test_embers_finishes_after_333_steps_and_starts_on_base(void) {
    Embers fx;
    Frame f;
    EffectContext ctx{kSolid, zeroRandom};
    fx.begin(ctx);
    TEST_ASSERT_EQUAL_UINT16(cfg::EMBERS_STEP_MS, fx.stepIntervalMs());
    TEST_ASSERT_TRUE(fx.step(f, ctx));                       // step 0: alpha 0 -> plain base
    for (uint16_t i = 0; i < Frame::kSize; ++i) TEST_ASSERT_TRUE(kSolid == f[i]);
    int calls = 1;
    while (fx.step(f, ctx)) {
        ++calls;
        TEST_ASSERT_TRUE_MESSAGE(calls < 1000, "embers did not finish");
    }
    TEST_ASSERT_EQUAL_INT(cfg::EMBERS_STEPS, calls + 1);
}

static void test_embers_pixel_slews_toward_its_target(void) {
    Embers fx;
    Frame f;
    EffectContext ctx{kSolid, zeroRandom};  // only pixel 0 ever gets a target (102)
    fx.begin(ctx);
    for (int i = 0; i <= cfg::EMBERS_FADE_STEPS; ++i) fx.step(f, ctx);  // full alpha from here
    uint8_t prev = f[0].r;
    for (int i = 0; i < 40; ++i) {
        fx.step(f, ctx);
        TEST_ASSERT_TRUE_MESSAGE(f[0].r <= prev && prev - f[0].r <= cfg::EMBERS_SLEW,
                                  "pixel 0 did not move down smoothly (<= EMBERS_SLEW per step)");
        prev = f[0].r;
        TEST_ASSERT_TRUE(kSolid == f[1]);  // untouched pixels stay base
    }
    TEST_ASSERT_TRUE(scale(kSolid, cfg::EMBERS_MIN_LEVEL) == f[0]);  // reached 40 %
}

static void test_embers_levels_stay_within_40_and_100_percent(void) {
    s_emberIdx = 0;
    Embers fx;
    Frame f;
    EffectContext ctx{kMakeup, emberRandom};
    fx.begin(ctx);
    const uint8_t floorW = scale8(255, cfg::EMBERS_MIN_LEVEL);
    while (fx.step(f, ctx)) {
        for (uint16_t i = 0; i < Frame::kSize; ++i) {
            TEST_ASSERT_TRUE(f[i].w >= floorW && f[i].w <= 255);
        }
    }
}
```

Реестр/протокол: `Embers` в `ids`/`expectedNames`, `sawEmbers`, `% 5`; `{"effect": "embers"}` в `test_light_effect_names`.

- [ ] **Шаг 2: RED.**

- [ ] **Шаг 3: реализация**

`Config.h`:

```cpp
// --- Embers effect (v1.2.0) --------------------------------------------------
// Each pixel smoulders at its own level in [EMBERS_MIN_LEVEL, 255]; every
// step EMBERS_CHANGES_PER_STEP random pixels get a new target and all move
// toward theirs by at most EMBERS_SLEW. ~20 s with a 2 s fade in/out.
constexpr uint32_t EMBERS_STEP_MS          = 60;
constexpr uint16_t EMBERS_STEPS            = 333;
constexpr uint16_t EMBERS_FADE_STEPS       = 33;
constexpr uint8_t  EMBERS_CHANGES_PER_STEP = 13;
constexpr uint8_t  EMBERS_MIN_LEVEL        = 102;  // 40 %
constexpr uint8_t  EMBERS_SLEW             = 6;
```

`Types.h`: `..., Breathe, Embers };`

`effects/Embers.h`:

```cpp
// Embers (v1.2.0): the ring smoulders — each pixel drifts between 40 % and
// 100 % of the base colour at its own pace.
#pragma once

#include <cstdint>

#include "../Config.h"
#include "Effect.h"

class Embers : public Effect {
public:
    void begin(const EffectContext& ctx) override;
    uint16_t stepIntervalMs() const override;
    bool step(Frame& out, const EffectContext& ctx) override;

private:
    uint16_t step_ = 0;
    uint8_t  level_[cfg::TOTAL_LEDS];
    uint8_t  target_[cfg::TOTAL_LEDS];
};
```

`effects/Embers.cpp`:

```cpp
#include "Embers.h"

#include "../ColorMath.h"
#include "../Frame.h"

namespace {
// Fade-in/out weight of the effect for this step, 0..255.
uint8_t fadeAlpha(uint16_t step, uint16_t total, uint16_t fade) {
    if (step < fade) return static_cast<uint8_t>(step * 255 / fade);
    if (step > total - fade) return static_cast<uint8_t>((total - step) * 255 / fade);
    return 255;
}
}  // namespace

void Embers::begin(const EffectContext&) {
    step_ = 0;
    for (uint16_t i = 0; i < cfg::TOTAL_LEDS; ++i) level_[i] = target_[i] = 255;
}

uint16_t Embers::stepIntervalMs() const { return static_cast<uint16_t>(cfg::EMBERS_STEP_MS); }

bool Embers::step(Frame& out, const EffectContext& ctx) {
    const uint8_t alpha = fadeAlpha(step_, cfg::EMBERS_STEPS, cfg::EMBERS_FADE_STEPS);

    for (uint8_t n = 0; n < cfg::EMBERS_CHANGES_PER_STEP; ++n) {
        const uint16_t i = static_cast<uint16_t>(ctx.random(cfg::TOTAL_LEDS));
        target_[i] = static_cast<uint8_t>(cfg::EMBERS_MIN_LEVEL + ctx.random(255 - cfg::EMBERS_MIN_LEVEL + 1));
    }
    for (uint16_t i = 0; i < cfg::TOTAL_LEDS; ++i) {
        if (level_[i] < target_[i]) level_[i] = static_cast<uint8_t>(level_[i] + ((target_[i] - level_[i] < cfg::EMBERS_SLEW) ? (target_[i] - level_[i]) : cfg::EMBERS_SLEW));
        else if (level_[i] > target_[i]) level_[i] = static_cast<uint8_t>(level_[i] - ((level_[i] - target_[i] < cfg::EMBERS_SLEW) ? (level_[i] - target_[i]) : cfg::EMBERS_SLEW));
        out[i] = scale(ctx.base, lerp8(255, level_[i], alpha));
    }
    ++step_;
    return step_ < cfg::EMBERS_STEPS;
}
```

Реестр: `Embers s_embers;`, `{EffectId::Embers, "embers", &s_embers},`. `ha_mirror.yaml`: `- embers`.

- [ ] **Шаг 4: GREEN** — все тесты, обе сборки (RAM +336 Б статически — ожидаемо, зафиксировать в отчёте).

- [ ] **Шаг 5: документация** — таблица эффектов в `ARCHITECTURE.md` §4.2, README.

- [ ] **Шаг 6: коммит и пауза**

```bash
git add firmware/lib/MirrorCore/src/effects/Embers.h firmware/lib/MirrorCore/src/effects/Embers.cpp \
  firmware/lib/MirrorCore/src/effects/EffectRegistry.cpp firmware/lib/MirrorCore/src/Config.h firmware/lib/MirrorCore/src/Types.h \
  firmware/test/test_effects/test_main.cpp firmware/test/test_protocol/test_main.cpp ha_mirror.yaml README.md ARCHITECTURE.md
git commit -m "feat: эффект embers — «угли» (п.2)

Каждый светодиод тлеет на своём уровне 40–100 % своего цвета: каждые 60 мс
13 случайных получают новую цель и все плавно к ней движутся; ~20 с с
плавным входом и выходом по 2 с. В реестре, effect_list и random.

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

**ПАУЗА — ждать «продолжай».**

---

### Задача 6: эффект `candle` — «свеча» (пункт 3)

**Файлы:** создать `effects/Candle.{h,cpp}`; изменить `Config.h`, `Types.h`, `EffectRegistry.cpp`, `test_effects`,
`test_protocol`, `ha_mirror.yaml`, `README.md`, `ARCHITECTURE.md`.

**Интерфейсы:** `class Candle : public Effect`, `EffectId::Candle`, `"candle"`,
`cfg::CANDLE_STEP_MS = 40`, `cfg::CANDLE_STEPS = 500`, `cfg::CANDLE_FADE_STEPS = 25`, `cfg::CANDLE_MIN_LEVEL = 227`,
`cfg::CANDLE_DIP_LEVEL = 204`, `cfg::CANDLE_DIP_CHANCE = 20`, `cfg::CANDLE_SLEW = 8`, `cfg::CANDLE_RETARGET_STEPS = 3`.

- [ ] **Шаг 1: тесты (RED)**

```cpp
// --- Candle (v1.2.0): the whole ring trembles 89..100 %, rare dips to 80 % ---

static void test_candle_finishes_after_500_steps_on_base(void) {
    Candle fx;
    Frame f;
    EffectContext ctx{kSolid, zeroRandom};
    fx.begin(ctx);
    TEST_ASSERT_EQUAL_UINT16(cfg::CANDLE_STEP_MS, fx.stepIntervalMs());
    TEST_ASSERT_TRUE(fx.step(f, ctx));
    TEST_ASSERT_TRUE(kSolid == f[0]);  // alpha 0 at step 0
    int calls = 1;
    while (fx.step(f, ctx)) ++calls;
    TEST_ASSERT_EQUAL_INT(cfg::CANDLE_STEPS, calls + 1);
}

// zeroRandom: rnd(CANDLE_DIP_CHANCE) == 0 -> every retarget is a dip to
// CANDLE_DIP_LEVEL, so the ring settles at 80 % and stays there.
static void test_candle_dips_smoothly_and_uniformly(void) {
    Candle fx;
    Frame f;
    EffectContext ctx{kSolid, zeroRandom};
    fx.begin(ctx);
    for (int i = 0; i <= cfg::CANDLE_FADE_STEPS; ++i) fx.step(f, ctx);  // full alpha
    uint8_t prev = f[0].r;
    for (int i = 0; i < 20; ++i) {
        fx.step(f, ctx);
        TEST_ASSERT_TRUE(ringUniform(f));
        TEST_ASSERT_TRUE_MESSAGE(f[0].r <= prev && prev - f[0].r <= cfg::CANDLE_SLEW, "dip is not smooth");
        prev = f[0].r;
    }
    TEST_ASSERT_TRUE(scale(kSolid, cfg::CANDLE_DIP_LEVEL) == f[0]);
}

// rnd(CANDLE_DIP_CHANCE) == 1 -> never a dip: level stays within 89..100 %.
static uint32_t noDipRandom(uint32_t bound) { return bound == cfg::CANDLE_DIP_CHANCE ? 1 : 0; }

static void test_candle_without_dips_stays_above_89_percent(void) {
    Candle fx;
    Frame f;
    EffectContext ctx{kMakeup, noDipRandom};
    fx.begin(ctx);
    const uint8_t floorW = scale8(255, cfg::CANDLE_MIN_LEVEL);
    while (fx.step(f, ctx)) {
        TEST_ASSERT_TRUE(f[0].w >= floorW);
        TEST_ASSERT_TRUE(ringUniform(f));
    }
}
```

Реестр/протокол: `Candle`, `"candle"`, `sawCandle`, `% 6`; `{"effect": "candle"}`.

- [ ] **Шаг 2: RED.**

- [ ] **Шаг 3: реализация**

`Config.h`:

```cpp
// --- Candle effect (v1.2.0) --------------------------------------------------
// The whole ring trembles between CANDLE_MIN_LEVEL and 255; every
// CANDLE_RETARGET_STEPS a new target, with a 1/CANDLE_DIP_CHANCE chance of a
// dip down to CANDLE_DIP_LEVEL; movement <= CANDLE_SLEW per step.
constexpr uint32_t CANDLE_STEP_MS        = 40;
constexpr uint16_t CANDLE_STEPS          = 500;  // ~20 s
constexpr uint16_t CANDLE_FADE_STEPS     = 25;   // 1 s
constexpr uint8_t  CANDLE_MIN_LEVEL      = 227;  // 89 %
constexpr uint8_t  CANDLE_DIP_LEVEL      = 204;  // 80 %
constexpr uint8_t  CANDLE_DIP_CHANCE     = 20;
constexpr uint8_t  CANDLE_SLEW           = 8;
constexpr uint8_t  CANDLE_RETARGET_STEPS = 3;
```

`Types.h`: `..., Embers, Candle };`

`effects/Candle.h`:

```cpp
// Candle (v1.2.0): the whole ring trembles like a candle flame — mostly
// 89..100 % of the base colour, with rare dips towards 80 %.
#pragma once

#include <cstdint>

#include "Effect.h"

class Candle : public Effect {
public:
    void begin(const EffectContext& ctx) override;
    uint16_t stepIntervalMs() const override;
    bool step(Frame& out, const EffectContext& ctx) override;

private:
    uint16_t step_   = 0;
    uint8_t  level_  = 255;
    uint8_t  target_ = 255;
};
```

`effects/Candle.cpp`:

```cpp
#include "Candle.h"

#include "../ColorMath.h"
#include "../Config.h"
#include "../Frame.h"

namespace {
uint8_t fadeAlpha(uint16_t step, uint16_t total, uint16_t fade) {
    if (step < fade) return static_cast<uint8_t>(step * 255 / fade);
    if (step > total - fade) return static_cast<uint8_t>((total - step) * 255 / fade);
    return 255;
}
}  // namespace

void Candle::begin(const EffectContext&) {
    step_ = 0;
    level_ = target_ = 255;
}

uint16_t Candle::stepIntervalMs() const { return static_cast<uint16_t>(cfg::CANDLE_STEP_MS); }

bool Candle::step(Frame& out, const EffectContext& ctx) {
    const uint8_t alpha = fadeAlpha(step_, cfg::CANDLE_STEPS, cfg::CANDLE_FADE_STEPS);

    if (step_ % cfg::CANDLE_RETARGET_STEPS == 0) {
        if (ctx.random(cfg::CANDLE_DIP_CHANCE) == 0) {
            target_ = static_cast<uint8_t>(cfg::CANDLE_DIP_LEVEL + ctx.random(cfg::CANDLE_MIN_LEVEL - cfg::CANDLE_DIP_LEVEL));
        } else {
            target_ = static_cast<uint8_t>(cfg::CANDLE_MIN_LEVEL + ctx.random(255 - cfg::CANDLE_MIN_LEVEL + 1));
        }
    }
    if (level_ < target_) level_ = static_cast<uint8_t>(level_ + ((target_ - level_ < cfg::CANDLE_SLEW) ? (target_ - level_) : cfg::CANDLE_SLEW));
    else if (level_ > target_) level_ = static_cast<uint8_t>(level_ - ((level_ - target_ < cfg::CANDLE_SLEW) ? (level_ - target_) : cfg::CANDLE_SLEW));

    out.fill(scale(ctx.base, lerp8(255, level_, alpha)));
    ++step_;
    return step_ < cfg::CANDLE_STEPS;
}
```

(Дублирование `fadeAlpha` с `Embers.cpp` — после GREEN вынести в `effects/Effect.h` как `inline uint8_t fadeAlpha(...)`
и использовать из обоих файлов; это рефакторинг при зелёных тестах.)

Реестр: `Candle s_candle;`, `{EffectId::Candle, "candle", &s_candle},`. `ha_mirror.yaml`: `- candle`.

- [ ] **Шаг 4: GREEN**, рефакторинг `fadeAlpha`, снова GREEN, обе сборки.

- [ ] **Шаг 5: документация.**

- [ ] **Шаг 6: коммит и пауза**

```bash
git add firmware/lib/MirrorCore/src/effects/Candle.h firmware/lib/MirrorCore/src/effects/Candle.cpp \
  firmware/lib/MirrorCore/src/effects/Embers.cpp firmware/lib/MirrorCore/src/effects/Effect.h \
  firmware/lib/MirrorCore/src/effects/EffectRegistry.cpp firmware/lib/MirrorCore/src/Config.h firmware/lib/MirrorCore/src/Types.h \
  firmware/test/test_effects/test_main.cpp firmware/test/test_protocol/test_main.cpp ha_mirror.yaml README.md ARCHITECTURE.md
git commit -m "feat: эффект candle — «свеча» (п.3)

Всё кольцо дрожит по яркости 89–100 %, изредка (1/20) проваливаясь до
80–89 %, движение к цели ≤ 8 за шаг 40 мс; ~20 с с плавным входом и
выходом. Общий fadeAlpha вынесен в Effect.h. В реестре, effect_list и random.

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

**ПАУЗА — ждать «продолжай».**

---

### Задача 7: эффект `comet` — «метеор» (пункт 5)

**Файлы:** создать `effects/Comet.{h,cpp}`; изменить `Config.h`, `Types.h`, `EffectRegistry.cpp`, `test_effects`,
`test_protocol`, `ha_mirror.yaml`, `README.md`, `ARCHITECTURE.md`.

**Интерфейсы:** `class Comet : public Effect`, `EffectId::Comet`, `"comet"`,
`cfg::COMET_STEP_MS = 30`, `cfg::COMET_TAIL = 40`, `cfg::COMET_FADE_STEPS = 20`. Использует `lerp` из Задачи 1 и
`fadeAlpha` из Задачи 6.

- [ ] **Шаг 1: тесты (RED)**

```cpp
// --- Comet (v1.2.0): white head, 40-LED quadratic tail, one lap ---------------

static const Rgbw kWhite{255, 255, 255, 255};

static void test_comet_finishes_after_209_steps_and_starts_on_base(void) {
    Comet fx;
    Frame f;
    EffectContext ctx{kSolid, zeroRandom};  // start 0, direction +1
    fx.begin(ctx);
    TEST_ASSERT_EQUAL_UINT16(cfg::COMET_STEP_MS, fx.stepIntervalMs());
    TEST_ASSERT_TRUE(fx.step(f, ctx));
    for (uint16_t i = 0; i < Frame::kSize; ++i) TEST_ASSERT_TRUE(kSolid == f[i]);  // alpha 0
    int calls = 1;
    while (fx.step(f, ctx)) ++calls;
    TEST_ASSERT_EQUAL_INT(cfg::TOTAL_LEDS + cfg::COMET_TAIL + 1, calls);
}

static void test_comet_head_is_white_with_quadratic_tail(void) {
    Comet fx;
    Frame f;
    EffectContext ctx{kSolid, zeroRandom};
    fx.begin(ctx);
    for (int i = 0; i <= 100; ++i) fx.step(f, ctx);  // renders step 100: head at pixel 100, full alpha
    TEST_ASSERT_TRUE(kWhite == f[100]);
    // 20 LEDs behind the head: t = ((40-20)/40)^2 = 0.25 -> lerp(base, white, 64)
    TEST_ASSERT_TRUE(lerp(kSolid, kWhite, 64) == f[80]);
    TEST_ASSERT_TRUE(kSolid == f[60]);   // 40 behind: tail end
    TEST_ASSERT_TRUE(kSolid == f[101]);  // ahead of the head
}

static void test_comet_adds_light_in_makeup(void) {
    Comet fx;
    Frame f;
    EffectContext ctx{kMakeup, zeroRandom};
    fx.begin(ctx);
    for (int i = 0; i <= 100; ++i) fx.step(f, ctx);
    TEST_ASSERT_TRUE(kWhite == f[100]);
    TEST_ASSERT_TRUE(f[90].r > 0 && f[90].w == 255);  // RGB rises on top of the white channel
}
```

Реестр/протокол: `Comet`, `"comet"`, `sawComet`, `% 7`; `{"effect": "comet"}`.

- [ ] **Шаг 2: RED.**

- [ ] **Шаг 3: реализация**

`Config.h`:

```cpp
// --- Comet effect (v1.2.0) ---------------------------------------------------
// A white head with a COMET_TAIL-LED quadratic tail travels once around the
// ring (TOTAL_LEDS + COMET_TAIL steps), fading in/out over COMET_FADE_STEPS.
constexpr uint32_t COMET_STEP_MS    = 30;
constexpr uint16_t COMET_TAIL       = 40;
constexpr uint16_t COMET_FADE_STEPS = 20;
```

`Types.h`: `..., Candle, Comet };`

`effects/Comet.h`:

```cpp
// Comet (v1.2.0): a bright white head with a fading tail flies once around
// the ring. Pixels are lerped from the base towards white, so it adds light
// in solid and in makeup alike.
#pragma once

#include <cstdint>

#include "Effect.h"

class Comet : public Effect {
public:
    void begin(const EffectContext& ctx) override;
    uint16_t stepIntervalMs() const override;
    bool step(Frame& out, const EffectContext& ctx) override;

private:
    uint16_t step_     = 0;
    uint16_t startPos_ = 0;
    int8_t   direction_ = 1;
};
```

`effects/Comet.cpp`:

```cpp
#include "Comet.h"

#include "../ColorMath.h"
#include "../Config.h"
#include "../Frame.h"

namespace {
constexpr uint16_t kTotalSteps = cfg::TOTAL_LEDS + cfg::COMET_TAIL;  // 208
constexpr Rgbw kWhite{255, 255, 255, 255};
}  // namespace

void Comet::begin(const EffectContext& ctx) {
    startPos_  = static_cast<uint16_t>(ctx.random(cfg::TOTAL_LEDS));
    direction_ = (ctx.random(2) == 0) ? 1 : -1;
    step_      = 0;
}

uint16_t Comet::stepIntervalMs() const { return static_cast<uint16_t>(cfg::COMET_STEP_MS); }

bool Comet::step(Frame& out, const EffectContext& ctx) {
    const uint8_t alpha = fadeAlpha(step_, kTotalSteps, cfg::COMET_FADE_STEPS);

    int head = static_cast<int>(startPos_) + static_cast<int>(step_) * direction_;
    while (head < 0) head += cfg::TOTAL_LEDS;
    while (head >= static_cast<int>(cfg::TOTAL_LEDS)) head -= cfg::TOTAL_LEDS;

    for (uint16_t i = 0; i < cfg::TOTAL_LEDS; ++i) {
        int dist = (direction_ == 1) ? (head - static_cast<int>(i)) : (static_cast<int>(i) - head);
        if (dist < 0) dist += cfg::TOTAL_LEDS;
        if (dist >= static_cast<int>(cfg::COMET_TAIL)) {
            out[i] = ctx.base;
            continue;
        }
        const uint16_t back = static_cast<uint16_t>(cfg::COMET_TAIL - dist);         // TAIL at the head, 1 at the end
        const uint8_t t = static_cast<uint8_t>(255UL * back * back / (cfg::COMET_TAIL * cfg::COMET_TAIL));
        out[i] = lerp(ctx.base, kWhite, scale8(t, alpha));
    }
    ++step_;
    return step_ <= kTotalSteps;
}
```

Реестр: `Comet s_comet;`, `{EffectId::Comet, "comet", &s_comet},`. `ha_mirror.yaml`: `- comet`.

- [ ] **Шаг 4: GREEN**, обе сборки.

- [ ] **Шаг 5: документация.**

- [ ] **Шаг 6: коммит и пауза**

```bash
git add firmware/lib/MirrorCore/src/effects/Comet.h firmware/lib/MirrorCore/src/effects/Comet.cpp \
  firmware/lib/MirrorCore/src/effects/EffectRegistry.cpp firmware/lib/MirrorCore/src/Config.h firmware/lib/MirrorCore/src/Types.h \
  firmware/test/test_effects/test_main.cpp firmware/test/test_protocol/test_main.cpp ha_mirror.yaml README.md ARCHITECTURE.md
git commit -m "feat: эффект comet — «метеор» (п.5)

Белая голова с хвостом из 40 светодиодов (квадратичное затухание) один
раз облетает кольцо (~6 с, шаг 30 мс). Пиксели смешиваются к белому,
поэтому эффект добавляет свет и в solid, и в Макияже.
В реестре, effect_list и random.

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

**ПАУЗА — ждать «продолжай».**

---

### Задача 8: диагностика в HA (пункт 13)

**Файлы:**
- Изменить: `Config.h`, `Topics.h`, `Topics.cpp`, `Protocol.h`, `Protocol.cpp`, `firmware/src/Network.cpp`,
  `firmware/test/test_protocol/test_main.cpp`, `ha_mirror.yaml`, `README.md`, `ARCHITECTURE.md`

**Интерфейсы:**
- Производит: `struct DiagInfo { uint32_t uptimeS; int8_t rssi; uint8_t resetReason; uint32_t freeHeap; uint32_t minFreeHeap; };`,
  `const char* resetReasonName(uint8_t code)`, `size_t buildDiagJson(const DiagInfo&, char* buf, size_t cap)`,
  `Topics::diag`, `cfg::DIAG_PERIOD_MS = 60000`, `cfg::DIAG_JSON_CAP = 192`.
- Network: `publishDiag()` при подключении и раз в `DIAG_PERIOD_MS`; `uptimeS` считается по секундам.

- [ ] **Шаг 1: тесты (RED)** — в `test_protocol`:

```cpp
// --- Diagnostics topic (v1.2.0) ---------------------------------------------

static void test_diag_topic_built_from_base(void) {
    Topics t;
    t.init(kBase);
    TEST_ASSERT_EQUAL_STRING("home/flat8/bath/mirror/diag", t.diag);
    TEST_ASSERT_TRUE(Route::Unknown == routeTopic("home/flat8/bath/mirror/diag", kBase));
}

static void test_reset_reason_names(void) {
    TEST_ASSERT_EQUAL_STRING("POWERON", resetReasonName(1));
    TEST_ASSERT_EQUAL_STRING("SW", resetReasonName(3));
    TEST_ASSERT_EQUAL_STRING("PANIC", resetReasonName(4));
    TEST_ASSERT_EQUAL_STRING("TASK_WDT", resetReasonName(6));
    TEST_ASSERT_EQUAL_STRING("BROWNOUT", resetReasonName(9));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", resetReasonName(0));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", resetReasonName(99));
}

static void test_diag_json(void) {
    DiagInfo d;
    d.uptimeS = 3723;
    d.rssi = -61;
    d.resetReason = 1;
    d.freeHeap = 231000;
    d.minFreeHeap = 198000;
    char buf[cfg::DIAG_JSON_CAP];
    const size_t n = buildDiagJson(d, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING(
        "{\"uptime_s\":3723,\"rssi\":-61,\"reset_reason\":\"POWERON\","
        "\"free_heap\":231000,\"min_free_heap\":198000,\"fw\":\"1.2.0\"}",
        buf);
    char small[16];
    TEST_ASSERT_EQUAL_UINT32(0, buildDiagJson(d, small, sizeof(small)));
}
```

- [ ] **Шаг 2: RED** — ошибки компиляции (`DiagInfo`, `diag`).

- [ ] **Шаг 3: реализация**

`Config.h` (блок Network): `constexpr uint32_t DIAG_PERIOD_MS = 60UL * 1000;` и `constexpr size_t DIAG_JSON_CAP = 192;`.
`Topics.h`: поле `char diag[96];`; `Topics.cpp`: `std::snprintf(diag, sizeof(diag), "%s/diag", base);`.
`Protocol.h`:

```cpp
// Diagnostics published on <base>/diag (v1.2.0), gathered on Core 0 only.
struct DiagInfo {
    uint32_t uptimeS = 0;
    int8_t   rssi = 0;
    uint8_t  resetReason = 0;  // esp_reset_reason_t value
    uint32_t freeHeap = 0;
    uint32_t minFreeHeap = 0;
};
const char* resetReasonName(uint8_t code);  // "POWERON", "SW", "PANIC", ... "UNKNOWN"
size_t buildDiagJson(const DiagInfo& d, char* buf, size_t cap);  // 0 if it doesn't fit
```

`Protocol.cpp`:

```cpp
const char* resetReasonName(uint8_t code) {
    switch (code) {  // esp_reset_reason_t (ESP-IDF 4.4)
        case 1: return "POWERON";
        case 2: return "EXT";
        case 3: return "SW";
        case 4: return "PANIC";
        case 5: return "INT_WDT";
        case 6: return "TASK_WDT";
        case 7: return "WDT";
        case 8: return "DEEPSLEEP";
        case 9: return "BROWNOUT";
        case 10: return "SDIO";
        default: return "UNKNOWN";
    }
}

size_t buildDiagJson(const DiagInfo& d, char* buf, size_t cap) {
    JsonDocument doc;
    doc["uptime_s"] = d.uptimeS;
    doc["rssi"] = d.rssi;
    doc["reset_reason"] = resetReasonName(d.resetReason);
    doc["free_heap"] = d.freeHeap;
    doc["min_free_heap"] = d.minFreeHeap;
    doc["fw"] = cfg::FW_VERSION;
    if (measureJson(doc) >= cap) return 0;
    return serializeJson(doc, buf, cap);
}
```

`Network.cpp` (анонимное пространство имён): переменные `uint32_t uptimeS = 0, lastUptimeTick = 0, lastDiagPublish = 0;`;

```cpp
void publishDiag() {
    DiagInfo d;
    d.uptimeS = uptimeS;
    d.rssi = static_cast<int8_t>(WiFi.RSSI());
    d.resetReason = static_cast<uint8_t>(esp_reset_reason());
    d.freeHeap = ESP.getFreeHeap();
    d.minFreeHeap = ESP.getMinFreeHeap();
    char buf[cfg::DIAG_JSON_CAP];
    if (buildDiagJson(d, buf, sizeof(buf)) == 0) {
        MLOG("diag json did not fit\n");
        return;
    }
    mqtt.publish(topics.diag, buf, true);
}
```

В цикле задачи, сразу после `const uint32_t now = millis();`: `while ((uint32_t)(now - lastUptimeTick) >= 1000) { lastUptimeTick += 1000; ++uptimeS; }`
(инициализировать `lastUptimeTick = millis()` перед циклом). После `publishAll(latest);` при подключении —
`publishDiag(); lastDiagPublish = connectedAt;`. В ветке «connected» после heartbeat:
`if ((uint32_t)(now - lastDiagPublish) >= cfg::DIAG_PERIOD_MS) { publishDiag(); lastDiagPublish = now; }`.
Заголовок `#include <esp_system.h>` для `esp_reset_reason()`.

`ha_mirror.yaml` — в `sensor:` добавить:

```yaml
    # --- Диагностика (топик diag, раз в минуту и при подключении) ---
    - name: "Зеркало: аптайм"
      unique_id: mirror_uptime
      state_topic: "home/flat8/bath/mirror/diag"
      value_template: "{{ value_json.uptime_s }}"
      availability_topic: "home/flat8/bath/mirror/availability"
      device_class: duration
      unit_of_measurement: "s"
      state_class: measurement
      entity_category: diagnostic
      icon: "mdi:timer-outline"
      device: *mirror_device

    - name: "Зеркало: WiFi"
      unique_id: mirror_wifi_rssi
      state_topic: "home/flat8/bath/mirror/diag"
      value_template: "{{ value_json.rssi }}"
      availability_topic: "home/flat8/bath/mirror/availability"
      device_class: signal_strength
      unit_of_measurement: "dBm"
      state_class: measurement
      entity_category: diagnostic
      device: *mirror_device

    - name: "Зеркало: причина перезагрузки"
      unique_id: mirror_reset_reason
      state_topic: "home/flat8/bath/mirror/diag"
      value_template: "{{ value_json.reset_reason }}"
      availability_topic: "home/flat8/bath/mirror/availability"
      entity_category: diagnostic
      icon: "mdi:restart-alert"
      device: *mirror_device

    - name: "Зеркало: свободная память"
      unique_id: mirror_free_heap
      state_topic: "home/flat8/bath/mirror/diag"
      value_template: "{{ value_json.free_heap }}"
      availability_topic: "home/flat8/bath/mirror/availability"
      device_class: data_size
      unit_of_measurement: "B"
      state_class: measurement
      entity_category: diagnostic
      icon: "mdi:memory"
      device: *mirror_device
```

Проверить YAML: `python3 -c "import yaml;yaml.safe_load(open('ha_mirror.yaml',encoding='utf-8'))"`.

- [ ] **Шаг 4: GREEN** — все тесты, обе сборки (Network.cpp — без предупреждений).

- [ ] **Шаг 5: документация** — `ARCHITECTURE.md` §5.1 строка `<base>/diag`, §5.4 «Диагностика» с JSON; `README.md`:
  таблица телеметрии, таблица HA (4 сенсора), «Возможности».

- [ ] **Шаг 6: коммит и пауза**

```bash
git add firmware/lib/MirrorCore/src/Config.h firmware/lib/MirrorCore/src/Topics.h firmware/lib/MirrorCore/src/Topics.cpp \
  firmware/lib/MirrorCore/src/Protocol.h firmware/lib/MirrorCore/src/Protocol.cpp firmware/src/Network.cpp \
  firmware/test/test_protocol/test_main.cpp ha_mirror.yaml README.md ARCHITECTURE.md
git commit -m "feat: диагностика в Home Assistant (п.13)

Новый retained-топик <base>/diag (при подключении и раз в минуту):
аптайм, уровень WiFi (RSSI), причина последней перезагрузки, свободная
и минимальная свободная куча, версия прошивки. Четыре диагностических
сенсора в ha_mirror.yaml. Собирается только на Core 0.

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

**ПАУЗА — ждать «продолжай».**

---

### Задача 9: сводная документация и релиз v1.2.0

**Файлы:** `ARCHITECTURE.md` (шапка: статус v1.2.0; §4.2 итоговая таблица из 7 эффектов; §9.1 таблица размеров;
§11.1 пункты 15–20), `README.md` (история версий v1.2.0, чек-лист проверки на железе: пункты 8–12 — переходы,
затухание перед автовыключением, 45 мин в Макияже, каждый новый эффект, сенсоры диагностики), `ha_mirror.yaml`
(шапка «прошивка v1.2.0»).

- [ ] **Шаг 1:** обновить документы, снять размеры Flash/RAM с обеих сборок, `$P test -e native` — итоговое число тестов
  в README.
- [ ] **Шаг 2: коммит**

```bash
git add ARCHITECTURE.md README.md ha_mirror.yaml
git commit -m "docs: сводка v1.2.0 — эффекты, переходы, автоматика, диагностика

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

- [ ] **Шаг 3:** слияние в `main`, push и тег `v1.2.0` — **только по команде владельца**, после проверки на железе
  (чек-лист README).

---

## Самопроверка плана

- **Покрытие:** пункты владельца 1 (Задача 4), 2 (5), 3 (6), 5 (7), 6 (1), 8 (3), 9 (2), 13 (8); 12 — уже реализовано
  (сброс к дефолтам при выключении, §4.1); 4, 7, 10, 11 — отклонены.
- **Согласованность имён:** `finishFrame()` вводится в Задаче 1 и меняется в Задаче 2; `autoOffLimit()` — Задача 2,
  уточняется в 3; `fadeAlpha` — дублируется в 5, выносится в `Effect.h` в 6, используется в 7; `lerp`/`lerp8` — Задача 1,
  используются в 5, 6, 7; `powerOnSettled`, `run`, `allPixelsEqual`, `kSolidDefault`, `kMakeupColor` — уже есть в
  `test_mirror`; `ringUniform` вводится в Задаче 4 и используется в 6; `kWhite` — Задача 7.
- **Тесты реестра:** `cyclingRandom` в `test_effects` берёт `% bound`, поэтому сам подстраивается; в
  `test_random_effect_covers_all` после каждой задачи 4–7 растёт число итераций (4, 5, 6, 7) и флагов `saw*`.
- **Плейсхолдеров нет.**

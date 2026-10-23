# Installed Strip Tuning Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Сделать Makeup стандартным профилем, унифицировать отключение через 15 минут и адаптировать эффекты к тусклой установленной ленте.

**Architecture:** Изменить существующие MirrorCore и реестр эффектов без перестройки архитектуры. Регрессионные Unity-тесты и независимая soak-модель проверяют новые правила; конфигурация Home Assistant и текущая документация следуют реализации.

**Tech Stack:** C++17, ESP32-S3, PlatformIO, native Unity, ArduinoJson 7.

**Spec:** `docs/superpowers/specs/2026-10-23-installed-strip-tuning-design.md`

## Global Constraints
- Существующий C++17 MirrorCore остаётся аппаратно-независимым; LED обслуживает только Core 1, сеть использует очереди.
- Не добавлять динамические выделения памяти в горячий путь Core 1; сохранять wrap-safe вычисления времени.
- Не менять семантику кнопки, ночного подтверждения, RGB-команд и ручной яркости.
- Не прошивать устройство, не создавать коммиты, не отправлять изменения без отдельной просьбы.

## Review Focus
- Выключение с пользовательским цветом и немедленное повторное включение: без вспышки или преждевременного сброса профиля (Task 1).
- Переполнение millis и пропущенные такты возле 15 минут: корректный предел и обновление от активности (Task 2).
- Makeup как стандарт: эффекты и помехи действительно появляются, но не мешают удержанию/переходам и не продлевают работу (Task 2).
- Старые MQTT-команды flame/embers: игнорируются, не включают свет и не меняют текущий эффект (Task 3).
- Жёлтый Comet на белом фоне: хвост хроматический и плавный, а не прежний белый или мгновенный чёрный (Task 3).

---

### Task 1: Стандартный Makeup и быстрые включение/выключение
**Files:** Modify `firmware/lib/MirrorCore/src/{Mirror.h,Mirror.cpp,Types.h,Config.h}`; Test `firmware/test/test_mirror/test_main.cpp`, `firmware/test/test_soak/test_main.cpp`.
**Interfaces:** Consumes `Mirror::begin(uint32_t)`, `Mirror::apply`, `Mirror::tick`, `Mirror::snapshot`; produces default BaseMode::Makeup / brightness255 and slide period22ms; public interfaces unchanged.
- [x] **Step 1:** Изменить тест сброса: пользовательские RGB и яркость сохраняются на SlideOff; после Off и следующего включения snapshot.base=Makeup, brightness=255, показанный цвет RGBW={0,0,0,255}. Проверить первый запуск и прерывание SlideOff. Старые тесты, которым нужен Solid, должны явно его выбирать.
- [x] **Step 2:** Запустить `pio test -d firmware -e native -f test_mirror`; Expected: новые проверки Makeup/длительности падают на старом поведении.
- [x] **Step 3:** Изменить начальные поля Mirror, `Mirror::applyDefaults()` и default Snapshot. Задать `cfg::SLIDE_STEP_MS=22`; не менять остальные fades. Обновить проверку временных шагов: при fake clock5ms шаг занимает25ms. Согласовать soak-инвариант выключенного состояния.
- [x] **Step 4:** Запустить mirror и soak: `pio test -d firmware -e native -f test_mirror -f test_soak`; Expected: PASS.

### Task 2: Общий таймер и частые эффекты на обоих фонах
**Files:** Modify `firmware/lib/MirrorCore/src/{Config.h,Mirror.cpp}`; Test `firmware/test/test_mirror/test_main.cpp`, `firmware/test/test_soak/test_main.cpp`.
**Interfaces:** Consumes Task1 default Makeup; produces unchanged `Mirror::tick`/`tickGlitch` API with universal idle limit900000ms, auto-effect pause60000..89999ms and glitch pause20000..29999ms.
- [x] **Step 1:** Заменить 45-минутные проверки Makeup на фактическое выключение после15мин; проверить15мин при automation=false и сохранение включённого состояния при активности. Добавить проверки минимального и максимального RNG для новых пауз, auto-effects/glitch в Makeup, отсутствия продления таймера автоматическими эффектами, обёртки uint32_t. Обновить независимую soak-модель и долгий тест automation=false с регулярной явной активностью.
- [x] **Step 2:** Запустить `pio test -d firmware -e native -f test_mirror -f test_soak`; Expected: FAIL по прежним45мин, mode-gates и паузам.
- [x] **Step 3:** Убрать отдельный Makeup-лимит. Вынести auto-off и предупреждение из gate автоматических эффектов; gate автоматического включения/эффектов сохранить. Убрать запрет Makeup только у auto-effect и glitch, сохранив остальные блокировки. Задать новые интервалы; автоматические эффекты не сбрасывают lastActivity_.
- [x] **Step 4:** Запустить `pio test -d firmware -e native -f test_mirror -f test_soak -f test_glitch`; Expected: PASS.

### Task 3: Четыре эффекта, жёлтый Comet, взвешенный Rainbow
**Files:** Modify `firmware/lib/MirrorCore/src/{Types.h,Config.h,effects/EffectRegistry.h,effects/EffectRegistry.cpp,effects/Comet.cpp}`; remove `effects/{Flame,Embers}.{h,cpp}` after reading; Test `firmware/test/{test_effects,test_protocol,test_soak}/test_main.cpp`; modify `ha_mirror.yaml`.
**Interfaces:** Consumes Task2 eligibility; produces registry of Dark/Rainbow/Wave/Comet, `EffectId randomEffect(RandomFn)` weighted100/115/100/100, existing name lookup rejects removed effects.
- [x] **Step 1:** Для каждого RNG-билета0..414 вызвать настоящий randomEffect, проверить суммарные частоты100/115/100/100 и границы0,99,100,214,215,314,315,414. Проверить жёлтую голову и оттенок/плавность хвоста на Solid и Makeup. Заменить protocol fixtures удалённых эффектов и проверить, что flame/embers игнорируются. Удалить тесты реализации удалённых эффектов, обновить soak максимумы шагов.
- [x] **Step 2:** Запустить `pio test -d firmware -e native -f test_effects -f test_protocol`; Expected: FAIL по весам/жёлтому/старым именам.
- [x] **Step 3:** Реализовать целочисленную рулетку в реестре с весом115 для Rainbow и100 остальных. Удалить Flame/Embers из enum, реестра, конфигурации и файлов. Поменять цель Comet на {255,255,0,0}; сохранить perceptual blending с текущим фоном. Убрать имена из Home Assistant effect_list.
- [x] **Step 4:** Запустить `pio test -d firmware -e native`; Expected: весь набор PASS.

### Task 4: Документация и итоговая проверка
**Files:** Modify `README.md`, `ARCHITECTURE.md`, при необходимости текущие version markers/fixtures; plan/ledger progress.
**Interfaces:** Consumes Task1–3 behavior, produces current docs matching firmware; historical release descriptions remain historical.
- [x] **Step 1:** Обновить текущее описание стандартного режима, сброса, скорости,15мин включая отключённую автоматику, интервалов, Makeup-эффектов, жёлтой кометы, удаления flame/embers и весов. Не переписывать историю старых версий. Версию нового функционального релиза установить1.6.0, согласовать firmware/library/fixtures/HA.
- [x] **Step 2:** Запустить `pio test -d firmware -e native`; Expected: PASS. Запустить `pio run -d firmware -e esp32-s3-zero` и `pio run -d firmware -e esp32-s3-zero-debug`; Expected: SUCCESS. Если PlatformIO недоступен, установить в изолированное окружение либо явно сообщить непроверенные сборки.
- [x] **Step 3:** Независимое read-only ревью всего diff против spec и Review Focus; исправить значимые находки через RED→GREEN и повтор полного набора. `git diff --check` Expected: пустой вывод. Оставить изменения без коммитов/прошивки.

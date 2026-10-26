# Effects and Power Animation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Correct occupied-mirror effect scheduling, revise weights and add random segmented power transitions.
**Architecture:** Separate human activity from effect timing. A bounded SegmentedAnimation handles reversible power rendering alongside the unchanged slider; Mirror chooses and dispatches.
**Tech Stack:** C++17, PlatformIO, Unity, ESP32-S3.
**Spec:** docs/superpowers/specs/2026-10-26-effects-and-power-animation-design.md

## Global Constraints
- 168 LEDs; no Core1 heap allocations or Arduino in MirrorCore.
- 15-minute inactivity cutoff remains independent of automation and effects.
- Preserve uint32 wrap safety, MQTT entities, base colour/brightness and interrupted-transition behavior.
- Version 1.7.0; no flash, commit or push without a new explicit request.

## Review Focus
- Repeated PIR/commands cannot starve effects; tests in Task 1.
- Random chunk partition cannot leave 1–7 LED tails or uncovered seams; Task 2.
- Switching off immediately must never flash; Task 2.
- Opposite animation choices and reversal preserve custom brightness and RGB/W; Task 2.
- Effect timers and both power choices work over millis wrap and irregular ticks; Tasks 1–2 plus full soak suite.

### Task 1: Periodic effects and weights
**Files:** Mirror.cpp, effects/EffectRegistry.cpp, test_mirror/test_main.cpp, test_effects/test_main.cpp, test_soak/test_main.cpp.
**Interfaces:** Preserve Mirror public API and randomEffect(RandomFn) -> EffectId.
- [x] Add regressions requiring Dark to start at 60,005 ms with continuous PIR, pulsed PIR and repeated no-op Light ON commands. Require subsequent effect 60,005 ms after completion, not after motion; reset on later power-on and preserve wrap.
- [x] Exhaustively test tickets 0–409 with boundaries 90/220/320 and counts 90/130/100/90.
- [x] Run `pio test -d firmware -e native -f test_mirror -f test_effects` and observe expected timer/weight failures.
- [x] Make markActivity update only lastActivity; explicitly initialise/reset lastIdle on begin and actual power-on. Retain existing effect-start/end resets. Update independent soak activity rule and power-on reference, keeping input effect-change tracking.
- [x] Run `pio test -d firmware -e native`; expect all pass.

### Task 2: Segmented power transitions
**Files:** Create effects/SegmentedAnimation.h/.cpp, test_segmented/test_main.cpp; modify SlideAnimation.h/.cpp, Mirror.h/.cpp, test_mirror/test_main.cpp.
**Interfaces:** SegmentedAnimation provides startOn(RandomFn), startOff(RandomFn) (fresh pattern), startOff() (reverse), reverseToOn(), turningOn(), step(Frame&, Rgbw) -> bool. Add SlideAnimation::startOff(uint16_t center) for a fresh full shutdown.
- [x] Write tests for 102 on / 103 off calls, exact dark/full endpoints, 8–12 LED cores with no uncovered pixels across seeds, random positions/order, CIE edges and max overlap, and reversible partial progress. Include fresh-slide-off after segmented on.
- [x] Observe tests fail, then implement fixed-array partitioning with feasible-remainder selection, shuffled order, 10-step fades and 3-pixel edges. Reuse slider progress semantics and interval.
- [x] Write Mirror tests forcing both random branches and independent shutdown selection, immediate-off safety, pending effects, reversals with custom RGB/brightness, and millis wrap; observe failures before integration.
- [x] Integrate via a private segmentedPower_ discriminator and animation member; select only from stable Off/On, not on reversal. Dispatch in the existing slide timing branch.
- [x] Run `pio test -d firmware -e native`; expect all pass.

### Task 3: Release consistency and verification
**Files:** Config.h, library.json, protocol version fixtures, README.md, ARCHITECTURE.md, ha_mirror.yaml.
- [x] Update version to 1.7.0 and active behavior documentation, retaining historical release descriptions.
- [x] Run full native suite, `git diff --check`, version/YAML consistency checks and ESP32 release/debug compile checks. Report every skipped or failed check faithfully.
- [x] Obtain one fresh whole-diff review, resolve important findings with failing regressions before fixes, rerun the suite. Leave changes uncommitted on feat/segmented-power-animation.

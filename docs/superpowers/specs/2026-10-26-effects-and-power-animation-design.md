# Effects and power animation v1.7.0

## Intent
Automatic effects should remain visible while someone moves in front of the mirror. Keep the existing slider and randomly alternate it with a fragmented, soft-edged power animation. Preserve all current controls, MQTT entities and inactivity shutdown.

## Timing
Code investigation found that PIR HIGH refreshed both the inactivity and effect timers on every loop. Separate them: human activity refreshes only the 15-minute shutdown timer. The automatic-effect clock starts on actual power-on, effect start and natural completion; the next effect waits 60–90 seconds after the previous effect finishes. It is not a fixed start-to-start cadence. Power-on/reversal resets the effect clock; automatic effects and glitches never count as human activity. Automation still gates effects. Preserve existing strict elapsed-time comparisons and wrap-safe subtraction.

## Probabilities
Use Wave as the comparison baseline (implementation choice): Dark 90, Rainbow 130, Wave 100, Comet 90, total 410. Actual probabilities: 21.95%, 31.71%, 24.39%, 21.95%. These are relative weights, not percentage-point adjustments.

## Power animation
User confirmed random alternation with the existing sliders. Implementation choices: independently choose slider/segments 50:50 at each fresh OFF→on and ON→off transition; an interrupted transition keeps its chosen type and pattern, reversing without re-randomisation.

Partition all 168 virtual LEDs into non-overlapping cores of 8–12 LEDs, with random ring origin, lengths and shuffled activation order. No short leftover segment. Each core fades in over 10 animation steps with three additional gradient LEDs on either side; overlapping contributions use max, not sum. Feathering uses CIE1931. All cores reach the base colour at completion, leaving no seams. Off traverses the activation progression backwards, disappearing in a chaotic order with soft edges.

Match the slider exactly: 102 on steps and 103 off steps at the same 22 ms step interval. A reversal keeps progression rather than restarting a full duration; switching off before the first step stays dark. Preserve base colour, RGB/W mode, global brightness, warning scale, deferred effects, button/night-mode behavior and completed-off defaults.

## Architecture and constraints
Add a pure C++ SegmentedAnimation beside SlideAnimation, not to the temporary-effect registry. Mirror dispatches power frames to the chosen class. Use bounded fixed arrays, no allocations on Core1, no Arduino dependency. Retain PowerState names and MQTT schema. Add a full-start-off overload to SlideAnimation for independent animation selection from ON.

## Verification
Native regressions: effects recur under continuous and pulsed PIR and repeated light commands; end-to-next-start interval, power-cycle reset and uint32 wrap; exhaustive 410-ticket weights. Animation tests cover complete coverage, legal core lengths, randomness, gradients/overlap, exact step counts, immediate reversal and mid-animation reversal. Mirror integration covers both choices, independent off selection, pending effects, reduced-brightness RGB and Makeup, no flash on immediate off, and wrap. Run the full native suite and ESP32 release/debug compile checks using temporary dummy secrets only if real secrets are absent. Do not flash, commit or push without a new explicit request.

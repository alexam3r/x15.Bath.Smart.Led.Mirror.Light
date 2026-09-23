// Task 2: Button state machine — port of v27 loop() button handling
// (ARCHITECTURE.md 4.3 / 8.1), simplified to the level-in/event-out
// contract; night-mode / dim-lock interpretation of the events belongs to
// the caller (Mirror, Task 5). All time comparisons use (uint32_t)(now -
// x), which is correct across millis() wraparound, and thresholds are
// strictly greater than, exactly like v27 (`> 500`, `> 30`, `> 400`).
#include "Button.h"

#include "Config.h"

ButtonEvent Button::update(bool pressed, uint32_t now) {
    ButtonEvent ev;

    if (pressed && !prevPressed_) {
        // Press edge: start timing this press, not holding yet.
        prevPressed_ = true;
        pressStart_  = now;
        holding_     = false;
        stuck_       = false;
        return ev;
    }

    if (!pressed && prevPressed_) {
        // Release edge.
        prevPressed_ = false;
        if (holding_) {
            // Rule #5: a hold never becomes a click.
            holding_    = false;
            clickCount_ = 0;
            ev.type     = ButtonEventType::HoldEnd;
        } else {
            if (clickCount_ == 0 || (uint32_t)(now - lastRelease_) > cfg::CLICK_GAP_MS) {
                clickCount_ = 1;
            } else if (clickCount_ < 255) {
                ++clickCount_;  // saturate at 255
            }
            lastRelease_ = now;
        }
        return ev;
    }

    if (pressed) {
        // Held level, no edge this call.
        if (!holding_) {
            if ((uint32_t)(now - pressStart_) > cfg::HOLD_MS) {
                holding_    = true;
                clickCount_ = 0;
                lastTick_   = now;
                ev.type     = ButtonEventType::HoldStart;
            }
        } else if (!stuck_ && (uint32_t)(now - pressStart_) > cfg::HOLD_STUCK_MS) {
            // Nobody dims for a minute: the button is stuck (moisture).
            // Stop ticking — no endless dimming, no endless "activity" that
            // would keep the mirror on — until the level drops. Latched, so
            // the elapsed-time difference wrapping after 49.7 days cannot
            // bring the ticks back.
            stuck_ = true;
        } else if (!stuck_ && (uint32_t)(now - lastTick_) > cfg::DIM_PERIOD_MS) {
            lastTick_ = now;
            ev.type   = ButtonEventType::HoldTick;
        }
    } else {
        // Released level, no edge this call: resolve a pending click run
        // once the inter-click gap has elapsed.
        if (clickCount_ > 0 && (uint32_t)(now - lastRelease_) > cfg::CLICK_GAP_MS) {
            ev.type     = ButtonEventType::Click;
            ev.clicks   = clickCount_;
            clickCount_ = 0;
        }
    }

    return ev;
}

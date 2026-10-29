#include "touch_button.h"

// ============ Internal state ============
static int baseline_t1 = 0;
static int baseline_t4 = 0;

static ThresholdMode thresh_mode = THRESH_AUTO;

// Pressed-state flag and debounce timestamp for each button
// Keyed by pin; linear search is sufficient for two channels
typedef struct {
    int      pin;
    bool     pressed;        // Whether the button is currently pressed
    uint32_t last_press_ms;  // Time of previous PRESS event (debounce)
} BtnState;

static BtnState btn_t1 = { TOUCH_CONFIRM_PIN, false, 0 };
static BtnState btn_t4 = { TOUCH_BOXOPEN_PIN, false, 0 };

static BtnState* find_btn(int pin) {
    if (pin == TOUCH_CONFIRM_PIN) return &btn_t1;
    if (pin == TOUCH_BOXOPEN_PIN) return &btn_t4;
    return nullptr;
}

// ============ Initialization ============
void touch_begin() {
    // Arduino touchRead() configures the pin automatically; no additional initialization needed
    // Only reset state here
    btn_t1.pressed = false;
    btn_t1.last_press_ms = 0;
    btn_t4.pressed = false;
    btn_t4.last_press_ms = 0;
}

// ============ Baseline calibration ============
void touch_calibrate() {
    Serial.println("[CALIB] Keep hands off the pads...");

    // Discard the first unstable samples (initial touchRead readings may fluctuate at startup)
    delay(50);
    touchRead(TOUCH_CONFIRM_PIN);
    touchRead(TOUCH_BOXOPEN_PIN);
    delay(50);

    long sum_t1 = 0, sum_t4 = 0;
    uint32_t t0 = millis();

    for (int i = 0; i < CALIB_SAMPLES; i++) {
        sum_t1 += touchRead(TOUCH_CONFIRM_PIN);
        sum_t4 += touchRead(TOUCH_BOXOPEN_PIN);
        delay(CALIB_INTERVAL_MS);
    }

    baseline_t1 = sum_t1 / CALIB_SAMPLES;
    baseline_t4 = sum_t4 / CALIB_SAMPLES;

    uint32_t dt = millis() - t0;
    Serial.print("[CALIB] done in ");
    Serial.print(dt / 1000.0, 1);
    Serial.println("s");
    Serial.print("[CALIB] T1 baseline=");
    Serial.print(baseline_t1);
    Serial.print("  T4 baseline=");
    Serial.println(baseline_t4);
    Serial.print("[CALIB] Recommended threshold (baseline - ");
    Serial.print(BASELINE_MARGIN);
    Serial.print("): T1=");
    Serial.print(baseline_t1 - BASELINE_MARGIN);
    Serial.print("  T4=");
    Serial.println(baseline_t4 - BASELINE_MARGIN);
}

int touch_get_baseline_t1() { return baseline_t1; }
int touch_get_baseline_t4() { return baseline_t4; }

// ============ Thresholds ============
int touch_get_threshold_t1() {
    return (thresh_mode == THRESH_AUTO) ? (baseline_t1 - BASELINE_MARGIN)
                                        : TOUCH_THRESHOLD_FIXED;
}
int touch_get_threshold_t4() {
    return (thresh_mode == THRESH_AUTO) ? (baseline_t4 - BASELINE_MARGIN)
                                        : TOUCH_THRESHOLD_FIXED;
}

void touch_set_threshold_mode(ThresholdMode mode) { thresh_mode = mode; }
ThresholdMode touch_get_threshold_mode() { return thresh_mode; }

// ============ Read events (debouncing and state tracking) ============
BtnEvent touch_read_event(int pin) {
    BtnState* b = find_btn(pin);
    if (!b) return BTN_EVENT_NONE;

    int threshold = (pin == TOUCH_CONFIRM_PIN) ? touch_get_threshold_t1()
                                               : touch_get_threshold_t4();
    int v = touchRead(pin);
    bool is_down = (v < threshold);

    if (is_down && !b->pressed) {
        // Edge: not pressed -> pressed
        // Debounce: time since last PRESS must exceed DEBOUNCE_MS
        if (millis() - b->last_press_ms > TOUCH_DEBOUNCE_MS) {
            b->pressed = true;
            b->last_press_ms = millis();
            return BTN_EVENT_PRESS;
        }
        // Ignore during debounce interval
        return BTN_EVENT_NONE;
    }
    if (!is_down && b->pressed) {
        // Edge: pressed -> released
        b->pressed = false;
        return BTN_EVENT_RELEASE;
    }
    return BTN_EVENT_NONE;
}

// ============ Raw values ============
int touch_read_raw(int pin) {
    return touchRead(pin);
}

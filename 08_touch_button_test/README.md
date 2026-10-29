# 08_touch_button_test — Dual Capacitive Touch-Button Test

> Verify that the ESP32-S3 native touch inputs (T1=GPIO1, T4=GPIO4) work correctly, independently of the touch-button logic in the final firmware.

---

## 1. Why Test This Module Separately?

The final firmware's touch-button code is embedded in its state machine (`final/src/main.cpp`) and cannot easily be tested independently. Its threshold of `30` was chosen empirically and has not been verified in a standalone test. This project:

- Verifies that `touchRead()` works on GPIO1/GPIO4.
- Displays live raw readings to **determine an appropriate threshold experimentally**.
- Adds automatic baseline calibration, which is more robust than a hard-coded threshold.
- Provides edge-triggered press/release events with 300 ms debouncing, matching the final firmware.

---

## 2. Hardware Connections

| Touch pad | ESP32-S3 | Touch channel | Purpose in final firmware |
|---|---|---|---|
| Pad 1 | **GPIO1** | T1 | Acknowledge medication (stop reminder audio and turn off LEDs) |
| Pad 2 | **GPIO4** | T4 | Detect medication-box opening |
| — | GND | — | Common ground with PCB |

> The ESP32-S3 supports native capacitive touch sensing. Connect PCB touch pads directly to GPIO1/GPIO4; **no additional IC is required**.
> For a bare-chip implementation, the hardware documentation (Section 3.5) suggests a 500 Ω series resistor and 0.1 µF filtering capacitor.

---

## 3. Flashing

```bash
cd code/08_touch_button_test
pio run -t upload
```

Serial communication uses native USB CDC (GPIO19/20 are occupied by USB) at **115200 baud**.

---

## 4. Automatic Startup Calibration

Calibration starts automatically at power-on:

```
[INFO] === CALIBRATION (power-on) ===
[INFO] Keep hands off the touch pads...
[CALIB] Keep hands off the pads...
[CALIB] done in 0.7s
[CALIB] T1 baseline=48  T4 baseline=52
[CALIB] Recommended threshold (baseline - 12): T1=36  T4=40
```

**Do not touch the pads during calibration.** Calibration takes approximately `32 × 20 ms = 0.64 s`.

> ⚠️ Baseline values vary between PCBs due to pad dimensions, copper fill, and trace lengths. Press `C` to recalibrate when changing boards.

---

## 5. Serial Commands (115200 baud)

| Command | Function |
|---|---|
| `R` | **Raw viewer:** print T1/T4 readings and thresholds every 100 ms for tuning |
| `M` | **Event monitor** (default): print `[PRESS]` and `[RELEASE]` events |
| `C` | **Recalibrate baseline:** prompt user not to touch pads and collect 32 samples |
| `T` | **Toggle threshold source:** auto (baseline − 12) ↔ fixed (30, matching final firmware) |
| `H` | Help |

### 5.1 Raw Mode (`R`)

```
T1_raw   T4_raw   | T1_thr  T4_thr  | mode
48       52       | 36      40      | auto
47       51       | 36      40      | auto
18       52       | 36      40      | auto     <- T1 touched; value drops to 18
17       51       | 36      40      | auto
49       22       | 36      40      | auto     <- T1 released; T4 touched
```

Press any key to exit Raw mode and return to Monitor mode.

### 5.2 Monitor Mode (`M`, default)

```
[PRESS]   T1 val=18
[RELEASE] T1 val=51
[PRESS]   T4 val=22
[RELEASE] T4 val=52
```

- `[PRESS]`: edge-triggered; requires more than 300 ms since the previous press (debouncing).
- `[RELEASE]`: triggered on transition from pressed to released.

### 5.3 Threshold Source (`T`)

```
> T
[INFO] Threshold source: fixed
[INFO]   T1 thr=30  T4 thr=30
```

- **auto** (default): threshold = baseline − 12; adapts to different PCBs.
- **fixed**: hard-coded threshold 30, **identical to the final firmware**, for comparison.

---

## 6. Selecting a Threshold (Important)

The final firmware's threshold of 30 is empirical. Use this project to **measure** a suitable threshold:

1. Allow automatic startup calibration; record the baseline (e.g., T1 baseline=48).
2. Press `R` to enter Raw mode.
3. Observe **untouched** readings for 10 seconds and record the minimum (noise floor).
4. **Touch T1 lightly** with realistic force and record the stable reading.
5. **Press T1 firmly** and record the stable reading.
6. The threshold should satisfy: `noise floor > threshold > light-touch reading`.
7. Recommended: `threshold = baseline - 12` (`BASELINE_MARGIN`, adjustable in `touch_button.h`).
8. Enter the measured threshold into `TOUCH_THRESHOLD` in `final/src/main.cpp`.

---

## 7. Key Parameters

| Parameter | Default | Definition | Relationship to final firmware |
|---|---|---|---|
| Debounce interval | 300 ms | `TOUCH_DEBOUNCE_MS` | **Same as final** |
| Calibration samples | 32 | `CALIB_SAMPLES` | Not present in final |
| Calibration interval | 20 ms | `CALIB_INTERVAL_MS` | Not present in final |
| Baseline margin | 12 | `BASELINE_MARGIN` | Not present in final |
| Fixed threshold | 30 | `TOUCH_THRESHOLD_FIXED` | **Same as final** |

---

## 8. Acceptance Criteria

| Test | Passing criterion |
|---|---|
| Build | `[SUCCESS]` |
| Startup calibration | Baseline in [30, 80], depending on PCB |
| Raw, untouched | Stable within baseline ±5 |
| Raw, touched | Clearly below baseline (less than 30) |
| Monitor, press | `[PRESS]` occurs with value below threshold |
| Monitor, release | `[RELEASE]` occurs with value at or above threshold |
| Debouncing | Repeated touches within 300 ms produce only one `[PRESS]` |
| `C` command | New baseline takes effect immediately |

### Troubleshooting

| Symptom | Cause / solution |
|---|---|
| Very low baseline (less than 20) | Pad touched during calibration; press `C` to recalibrate |
| Very high baseline (greater than 100) | Long trace or oversized pad; increase `BASELINE_MARGIN` |
| Frequent false presses | Power-supply noise or traces near switching supply; increase debounce or add hardware filtering |
| Touch not detected | Threshold below actual pressed reading; inspect Raw mode and retune |
| Readings fluctuate severely | Poor pad contact or bad GND connection |

---

## 9. File Structure

```
08_touch_button_test/
├── platformio.ini              # ESP32-S3 + USB CDC; no lib_deps
├── README.md                   # This file
└── src/
    ├── main.cpp                # Serial command handling (R/M/C/T/H) + three-mode state machine
    ├── touch_button.h          # Pin definitions and API declarations
    └── touch_button.cpp        # touch_read_event(), baseline calibration, raw output
```

---

## 10. Relationship to Other Projects

| Project/document | Relationship |
|---|---|
| `final/` | Independently tests the `touch_pressed()` logic in `final/src/main.cpp:233-246` |
| `硬件物料与引脚分配.md`, Section 3.5 | Pin assignment reference |
| `01_audio_record_playback` | Serial command framework reference (its `V` command corresponds to `R` here) |

---

## 11. Out of Scope

- No LED feedback; serial-only verification.
- No buzzer.
- No application state machine (`NORMAL`, `REMIND`, etc.); handled by final firmware.
- No changes to audio, IMU, e-paper, or LED-strip modules.
- No low-level ESP-IDF `touch_pad_*` API; use Arduino `touchRead()` as in final firmware.

---

## 12. References

| Source | Path |
|---|---|
| Final touch implementation | `../final/src/main.cpp`, lines 85–97 (definitions) and 233–246 (`touch_pressed()`) |
| Pin definitions | `../../硬件物料与引脚分配.md`, Section 3.5 |
| Arduino touchRead documentation | https://docs.arduino.cc/learn/microcontrollers/analog-io#touch |

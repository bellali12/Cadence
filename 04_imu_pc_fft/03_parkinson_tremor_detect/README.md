# 03_parkinson_tremor_detect

> **Project type:** Application feature (health monitoring)
> **Purpose:** Detect episodes of resting tremor in people with Parkinson's disease using spectral energy analysis in the 4–6 Hz band.
> **Controller:** ESP32-S3 (16 MB flash)
> **Status:** 🟡 Awaiting hardware testing

> 📄 **For technical principles, see [RESEARCH.md](./RESEARCH.md)** — clinical background, signal-processing pipeline, mathematical-method comparisons, parameter derivations, and references.

---

## Features

- Sample MPU6050 three-axis acceleration at 100 Hz.
- Compute acceleration magnitude → 256-point FFT (Hanning window) → extract 4–6 Hz band energy.
- **Dual-threshold decision with consecutive-window confirmation** to reduce false positives from brief movements.
- Output detection results in real time over serial.

### Serial Commands

| Command | Function |
|---------|----------|
| `S` | Start/stop detection |
| `C` | Recalibrate gyroscope offsets |
| `D` | Dump the full 0–12 Hz spectrum once (for debugging) |
| `T <0.2-0.9>` | Adjust the energy-ratio threshold, e.g. `T 0.5` |
| `H` | Help |

### Detection Output (one line every 2.56 seconds while running)

```
[DETECT] ratio=0.12 peak=2.30Hz bandE=85 totalE=720 hitCount=- | IDLE
[DETECT] ratio=0.62 peak=5.08Hz bandE=1800 totalE=2900 hitCount=+ | *** TREMOR ***
```

- `ratio`: energy in 4–6 Hz band / total spectral energy.
- `peak`: frequency of the spectral peak.
- `hitCount`: whether the current window satisfies the thresholds (+/-).
- `TREMOR`: output after three consecutive positive windows.

---

## Pins (per `../../硬件物料与引脚分配.md`, Section 3.7)

| MPU6050 | ESP32-S3 | Notes |
|---------|----------|-------|
| VCC | **3.3V** | |
| GND | GND | |
| SDA | **GPIO8** | Finalized |
| SCL | **GPIO7** | Finalized |
| AD0 | GND | Address 0x68 |
| INT | Not connected | Polling used |

---

## Key Parameters

| Parameter | Value | Description |
|-----------|-------|-------------|
| Sample rate | 100 Hz | 10 ms interval |
| FFT size | 256 points | Frequency resolution 0.39 Hz |
| Window function | Hanning | Reduces spectral leakage |
| Detection band | 4–6 Hz | Characteristic of Parkinsonian resting tremor |
| Corresponding bins | 10–15 | `f × N / fs` |
| Energy-ratio threshold | 0.4 (adjustable) | Proportion of energy in the 4–6 Hz band |
| Consecutive confirmation | 3 windows (≈8 s) | Reduces false positives from brief movements |
| Signal source | Acceleration magnitude `√(ax²+ay²+az²)` | Robust single-channel measure |

---

## Build Environment

```bash
cd code/03_parkinson_tremor_detect
pio run -t upload && pio device monitor
```

Libraries (automatically installed by `platformio.ini`):
- `rfetick/MPU6050_light @ ^1.2.0`
- `kosme/arduinoFFT @ ^2.0`

Serial communication uses native USB CDC (115200 baud).

---

## Test Procedure

### Preparation

1. Wire SDA→GPIO8, SCL→GPIO7, AD0→GND, and VCC→**3.3V**.
2. Power on; check `mpu.begin() status = 0` and successful calibration in the startup log.
3. Enter `S` in the serial terminal to start detection.

### Scenario 1: Normal Activity Should Not Trigger False Alarms

| Movement | Expected result |
|----------|-----------------|
| Board stationary on table | ratio < 0.2, `IDLE` |
| Slowly translate or lift board by hand | ratio < 0.3, `IDLE` |
| Wave hand quickly (2–4 Hz) | Ratio may occasionally increase but not consistently; ultimately `IDLE` |

### Scenario 2: Simulated Tremor Should Trigger an Alert

1. Hold the board and shake it back and forth at approximately **5 Hz** (five cycles per second).
2. Continue for **at least 8 seconds** (three analysis windows).
3. Expected: `peak ≈ 5Hz`, `ratio > 0.4`, then `*** TREMOR ***`.

### Scenario 3: Spectrum Debugging

- Enter `D` to print the current spectrum as CSV.
- At 5 Hz, expect a distinct peak near bin 13 (=5.08 Hz).
- At rest, all spectrum bins should be low.

---

## Acceptance Criteria

| Test | Pass criterion |
|------|----------------|
| `mpu.begin() status` | = 0 |
| Stationary | `IDLE`, ratio < 0.2 |
| Slow movement | `IDLE` |
| 5 Hz shaking for 8 s | `*** TREMOR ***` |
| Peak frequency while shaking | peak ∈ [4, 6] Hz (±1 Hz) |

### Troubleshooting

| Symptom | Possible cause / check |
|---------|------------------------|
| TREMOR reported while stationary | Threshold too low: raise with `T 0.5`; or excessive power/wiring noise |
| Shaking does not trigger | Threshold too high: lower with `T 0.3`; use `D` to check peak frequency |
| Peak frequency jumps around | Irregular shaking; try a more consistent 5 Hz rhythm |
| `status != 0` | I2C failure: check SDA/SCL, AD0, VCC |
| FFT never runs | Buffer not full; confirm `S` started detection and wait at least 2.56 s |

---

## File Structure

```
03_parkinson_tremor_detect/
├── platformio.ini          # ESP32-S3 and library dependencies
├── README.md               # This file
├── RESEARCH.md             # Technical report (principles, mathematics, papers)
└── src/
    ├── main.cpp            # State machine, serial commands, 100 Hz sampling schedule
    ├── tremor_detect.h     # Detector interface
    └── tremor_detect.cpp   # Ring buffer, Hanning window, FFT, dual-threshold decision
```

---

## Core Algorithm

```
[MPU6050 @ 100 Hz] -> three-axis acceleration -> magnitude sqrt(ax²+ay²+az²)
                                          |
                              256-point buffer (2.56 s)
                                          |
                         1. Subtract mean (remove gravity/DC)
                         2. Apply Hanning window
                         3. Perform 256-point arduinoFFT
                         4. Convert complex spectrum to magnitudes
                                          |
                 bandEnergy  = sum |X[k]|², k in [10,15] (4-6 Hz)
                 totalEnergy = sum |X[k]|², k in [1,127]
                 ratio       = bandEnergy / totalEnergy
                                          |
                 Positive if ratio > threshold AND peak in [4,6] Hz
                 Three consecutive positives -> *** TREMOR ***
```

---

## Out of Scope

- ❌ No machine-learning classifier (reserved for later work).
- ❌ No BLE/Wi-Fi data upload.
- ❌ No integration with medication reminders.
- ❌ No PSRAM (256-point FFT uses only ~4 KB internal RAM).
- ❌ Do not modify `02_imu_raw_data` (keep as a raw-data viewing tool).

---

## References

| Source | Path / link |
|--------|-------------|
| **Technical report** | [RESEARCH.md](./RESEARCH.md) |
| Project 02 IMU basics | `../02_imu_raw_data/` |
| Pin definitions | `../../硬件物料与引脚分配.md`, Section 3.7 |
| arduinoFFT | https://github.com/kosme/arduinoFFT |
| MPU6050_light | https://github.com/rfetick/MPU6050_light |

# IMU-Based Detection of Parkinsonian Tremor — Technical Research Report

> Associated project: `03_parkinson_tremor_detect`
> Controller: ESP32-S3 (16 MB flash / 8 MB PSRAM)
> Sensor: MPU6050 (three-axis accelerometer + three-axis gyroscope)
> Date: 2026-07-29

---

## 1. Clinical Background: Characteristics of Parkinsonian Tremor

### 1.1 Tremor Types and Frequencies

One of the principal motor symptoms of Parkinson's disease (PD) is **tremor**. Different tremor types have different frequency characteristics:

| Tremor type | Frequency range | Trigger | Clinical significance |
|-------------|-----------------|---------|-----------------------|
| **Resting tremor** | **4–6 Hz** | Occurs when a limb is fully relaxed | Characteristic of Parkinson's disease |
| Action tremor | 5–8 Hz | During voluntary movement | Advanced Parkinson's / cerebellar pathology |
| Postural tremor | 4–8 Hz | Maintaining a posture against gravity | Parkinsonian / essential tremor |
| Essential tremor | 5–8 Hz | Worsens during movement (unlike PD) | Must be differentiated from PD |
| Physiological tremor | 8–12 Hz | Stress, fatigue, caffeine | No clinical significance |

**Key conclusion:** The **4–6 Hz characteristic frequency band** of Parkinsonian resting tremor is the main target of the detection algorithm.

### 1.2 Spectral Differences from Normal Activities

| Activity | Main energy distribution |
|----------|--------------------------|
| Walking | 1–3 Hz (step cadence) |
| Slow movement (raising hand, turning) | <2 Hz; dispersed spectrum |
| Fast hand waving | 2–4 Hz; broad spectrum |
| Heartbeat / breathing interference | <1 Hz |
| Sensor-board noise | White noise without a dominant peak |

→ **Detection strategy:** Classify a signal as tremor when the proportion of energy in the 4–6 Hz band is substantially higher than in other bands.

---

## 2. Principles of IMU-Based Detection

### 2.1 Why Choose the MPU6050?

| Sensor | Advantages | Disadvantages |
|--------|------------|---------------|
| **MPU6050 (accelerometer + gyroscope)** | Six-axis synchronous sampling; inexpensive (~¥5); simple I2C; mature libraries | ±2 g / ±250 dps range can saturate under vigorous movement |
| MPU9250 (+ magnetometer) | Additional absolute orientation information | Requires geomagnetic calibration; overengineered |
| Accelerometer only (e.g., ADXL345) | Cheaper | No gyroscope; less sensitive to wrist-rotation tremor |

**Conclusion for a medication-case application:** The **MPU6050 is sufficient** when the case is worn or held by the patient.

### 2.2 Accelerometer vs. Gyroscope

| Signal | Physical meaning | Sensitivity to tremor |
|--------|------------------|-----------------------|
| Acceleration `ax,ay,az` | Linear motion + gravity component | Sensitive to back-and-forth movement in any direction |
| Gyroscope `gx,gy,gz` | Angular velocity (rotation) | Sensitive to wrist flexion/extension tremor |
| **Acceleration magnitude** `√(ax²+ay²+az²)` | Total motion intensity (after gravity removal) | **Robust single-channel signal**, selected for this project |

**Why not use one axis?** Tremor direction varies; a single axis can miss tremor perpendicular to it. The three-axis magnitude provides coverage independent of direction.

### 2.3 Sampling-Rate Selection

By the Nyquist theorem, sampling rate must be at least twice the highest signal frequency:

- Target band: 4–6 Hz → minimum sampling rate of 12 Hz.
- Accounting for bandpass-filter transition bands and FFT window length: **100 Hz recommended**.
- The MPU6050 supports a 1 kHz internal sample rate; reading 14 bytes over 400 kHz I2C takes only ~0.3 ms, so 100 Hz is easily achievable.

---

## 3. Signal-Processing Pipeline (Core Algorithm)

```
MPU6050 @ 100 Hz
    |
    v
[1] Three-axis acceleration -> magnitude -> mean removal (AC component)
    |   mag[n] = sqrt(ax²+ay²+az²) - mean(mag)
    v
[2] 256-point sliding buffer (2.56 seconds)
    |
    v
[3] Apply Hanning window (reduce spectral leakage)
    |   x[k] *= 0.5 * (1 - cos(2πk / (N-1)))
    v
[4] FFT (arduinoFFT, 256 points)
    |   -> 128 usable bins (0-50 Hz; 0.39 Hz resolution)
    v
[5] Extract energy in 4-6 Hz band (bins 10-15)
    |   bandEnergy  = sum |X[k]|², k in [10,15]
    |   totalEnergy = sum |X[k]|², k in [1,127] (exclude DC)
    |   ratio = bandEnergy / totalEnergy
    v
[6] Decision
    |   Condition 1: ratio > 0.4
    |   Condition 2: peak-frequency bin lies in 4-6 Hz
    |   Condition 3: all three consecutive windows are positive (~8 s)
    v
Output: IDLE / *** TREMOR ***
```

---

## 4. Comparison of Mathematical Methods

### 4.1 FFT (Fast Fourier Transform)

- **Principle:** Decomposes a time-domain signal into frequency components and yields the magnitude at each discrete frequency.
- **Complexity:** O(N log N); the source estimates a 256-point FFT takes <1 ms on ESP32-S3.
- **Advantages:** Full spectrum across all bands (not just 5 Hz); useful for parameter tuning and debugging.
- **Tradeoff:** Somewhat more computational work than Goertzel, but not a bottleneck for ESP32-S3.
- **Library:** [kosme/arduinoFFT](https://github.com/kosme/arduinoFFT).
- **Selected for this project.**

### 4.2 Goertzel Algorithm

- **Principle:** Efficiently computes the DFT value at **one target frequency** using a second-order IIR recurrence.
- **Complexity:** O(N), approximately log N times faster than FFT for a single frequency.
- **Advantages:** Very lightweight if only one frequency near 5 Hz needs to be detected.
- **Tradeoff:** Does not provide the full spectrum, making debugging harder.
- **Reference:** [Embedded.com article](https://www.embedded.com/the-goertzel-algorithm/).
- **Not selected:** A full spectrum is more useful for diagnostics.

### 4.3 Bandpass IIR Filter (Butterworth)

- **Principle:** Time-domain filter retaining only a target band (e.g., 2–10 Hz).
- **Complexity:** O(N).
- **Advantages:** Excellent real-time response; processes each new sample and can precede the FFT.
- **Tradeoff:** Does not directly provide frequency; needs an energy threshold.
- **Possible role:** Optional FFT preprocessing to remove breathing interference below 1 Hz and noise above 10 Hz.

### 4.4 Wavelet Transform

- Suitable for nonstationary signals whose frequency varies over time.
- Fewer libraries and more complex implementation.
- Considered excessive for this project; not used.

### 4.5 Machine-Learning Classifier

- Neural networks, SVM, or random forests.
- The source describes this as potentially most accurate, but requiring a training dataset, model storage, and inference capacity.
- ESP32-S3 can run TinyML, but the project first validates a frequency-domain approach.
- **Reserved for future development.**

### 4.6 Method-Selection Summary

| Method | Complexity | Suitable for ESP32 | Full spectrum | Role in this project |
|--------|------------|--------------------|---------------|----------------------|
| **FFT** | O(N log N) | Yes | Yes | **Core** |
| Goertzel | O(N) | Yes | No | Alternative |
| IIR bandpass | O(N) | Yes | No | Optional FFT preprocessing |
| Wavelet | O(N log N) | Limited | Yes | Not used |
| Machine learning | High | Limited | — | Future work |

---

## 5. Derivation of Key Parameters

### 5.1 Sampling Rate and FFT Size

| Parameter | Value | Formula / rationale |
|-----------|-------|---------------------|
| Sampling rate `fs` | 100 Hz | Ample margin above the 6 Hz target; manageable I2C load |
| FFT size `N` | 256 | Power of two; window length = 256/100 = 2.56 s |
| Frequency resolution `Δf` | 0.39 Hz | `Δf = fs/N = 100/256` |
| Usable frequency range | 0–50 Hz | `fs/2` (Nyquist) |

### 5.2 Characteristic Parkinsonian Frequency Band → FFT Bin Indices

`bin_index = round(f × N / fs) = round(f × 2.56)`

| Frequency | Bin index |
|-----------|-----------|
| 4 Hz | 10 |
| 5 Hz | 13 |
| 6 Hz | 15 |

→ **Detection bins [10, 15]**, inclusive (six bins covering approximately 4–6 Hz).

### 5.3 Threshold Derivation

- At rest: energy concentrates near DC (bins 0–1); energy in bins 10–15 is very low (noise floor).
- Walking (1–3 Hz → bins 2–8): target-band energy remains low.
- Simulated tremor (5 Hz → bin 13): target-band energy rises sharply and is expected to exceed 40% of total energy.

**Initial threshold:** `ratio > 0.4` (adjustable on site with serial command `T`).

### 5.4 Number of Consecutive Confirmation Windows

- One window lasts 2.56 s and can be falsely triggered by brief incidental movement.
- Choose **three consecutive windows** (approximately 8 s) to reject short events such as waving, opening a door, or coughing.

### 5.5 Estimated Memory Usage

| Buffer | Size | Description |
|--------|------|-------------|
| `float vReal[256]` | 1 KB | Real FFT components |
| `float vImag[256]` | 1 KB | Imaginary FFT components |
| `float ringbuf[256]` | 1 KB | Sliding acceleration-magnitude window |
| Other | <1 KB | Counters, threshold, etc. |
| **Total** | **~4 KB** | Fits easily in ESP32-S3 internal SRAM (512 KB); **no PSRAM needed** |

---

## 6. Library Selection

| Library | Purpose | Version | Source |
|---------|---------|---------|--------|
| `rfetick/MPU6050_light` | MPU6050 driver (calibration and readings) | ^1.2.0 | [GitHub](https://github.com/rfetick/MPU6050_light) |
| `kosme/arduinoFFT` | FFT computation | ^2.0 | [GitHub](https://github.com/kosme/arduinoFFT) |

Both libraries are installed automatically through `lib_deps` in `platformio.ini`, with no manual installation.

---

## 7. Reference Papers and Projects

| Source | Relevance |
|--------|-----------|
| [Dai et al. 2015 — Quantitative Assessment of Parkinsonian Tremor Based on IMU](https://pmc.ncbi.nlm.nih.gov/articles/PMC4634500/) | Foundational time-frequency analysis work, described in the source as having 203 citations |
| [Saez et al. 2025 — Wearable IoT Device for Tremor Monitoring](https://pmc.ncbi.nlm.nih.gov/articles/PMC12074293/) | **Directly related to this project:** MPU6050 + Butterworth bandpass |
| [Timmermans et al. 2025 — Open-source algorithm for PD tremor](https://pmc.ncbi.nlm.nih.gov/articles/PMC12246068/) | Reproducible open-source real-time monitoring algorithm |
| [Yousef et al. 2023 — Wearable Wireless Sensing Device](https://link.springer.com/article/10.1007/s42417-022-00734-2) | MPU6050 validation and measured data |
| [Embedded.com — The Goertzel Algorithm](https://www.embedded.com/the-goertzel-algorithm/) | Classic explanation of Goertzel |
| [arduinoFFT wiki](http://wiki.openmusiclabs.com/wiki/ArduinoFFT) | FFT library API reference |

---

## 8. Future Development Directions

1. **Machine-learning classifier:** Collect samples for four categories (tremor / walking / stationary / holding objects), then run TinyML inference on the ESP32-S3 to improve accuracy.
2. **Multimodal fusion:** Combine IMU with microphone signals (clothing friction) and heart rate (autonomic function) to reduce false positives.
3. **Medication-reminder feedback loop:** If tremor is detected and more than four hours have passed since the last dose, prompt the user to take medication on the e-paper display.
4. **Offline analysis:** Upload 30 seconds of raw data over Wi-Fi to a mobile app for more detailed spectral analysis in the cloud.

These are proposed future directions after this project's validation, not implemented features.

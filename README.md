---
typora-root-url: ../pic
---

# code/ Directory Index

> This directory is the central location for **all test code and final firmware for the smart medication case project**.
> Whenever a new PlatformIO, Arduino, or script project is added, register it in the table below and create a subdirectory using the naming convention.

---

## Naming convention

Subdirectory names: `<number>_<module>_<short_description>`. For example:

| Category | Examples |
|---|---|
| Module testing | `01_epd_test`, `02_mic_pdm_test`, `03_amp_playback` |
| Integrated module testing | `10_audio_loopback` (microphone + amplifier integration), `11_epd_touch_demo` |
| Final firmware | `final` or `99_final_firmware` |

- Numbers `01–49`: individual module tests
- Numbers `50–79`: integration tests involving multiple modules
- Numbers `80–99` / `final`: finished product firmware

---

## Project inventory

| No. | Subdirectory | Project type | Purpose / verification target | Controller | Key pins | Status | Notes |
|---|---|---|---|---|---|---|---|
| 01 | `01_audio_record_playback` | Module integration test | Record 10 s from microphone into PSRAM, then play through speaker; verify LMD2718 + NS4168 audio path | ESP32-S3 | MIC: 15/16, AMP: 41/42/2 | ✅ Passed | `PDM_GAIN=32` and stereo duplication (see README debugging notes) |
| 02 | `02_imu_raw_data` | Individual module test | Output MPU6050 raw data and pitch/roll angles (50 Hz CSV); verify IMU communication | ESP32-S3 | IMU: SDA=8, SCL=7 | 🟡 Awaiting hardware test | Uses `rfetick/MPU6050_light` (built-in calibration and complementary filter) |
| 03 | `03_parkinson_tremor_detect` | Application feature | Parkinsonian resting tremor detection: 100 Hz sampling, 256-point FFT, 4–6 Hz energy ratio, consecutive-window confirmation | ESP32-S3 | IMU: SDA=8, SCL=7 | 🟡 Awaiting hardware test | `arduinoFFT`, two-criterion detection, accompanying `RESEARCH.md` technical report |
| 04 | `04_imu_pc_fft` | Application feature | IMU raw-data acquisition and PC-side FFT analysis (matplotlib visualization), triggered by Record button | ESP32-S3 + PC | IMU: SDA=18, SCL=17 | 🟡 Awaiting hardware test | Firmware samples raw six-axis data at 100 Hz; PC tool `tools/imu_fft_gui.py` receives, analyzes, and plots it |
| 05 | `05_amp_beep_test` | Individual module diagnostic | Feed pure square-wave PCM to NS4168 to isolate amplifier path from microphone/recording data | ESP32-S3 | AMP: 41/42/2 | ✅ Passed | Used to troubleshoot silence on command P in project 01; hardware audio path confirmed working |
| 06 | `06_ws2812_test` | Individual module test | Verify nine WS2812 LEDs (3×3) with startup self-test, six animations, and serial control | ESP32-S3 | LED: DIN=6 | 🟡 Awaiting hardware test | FastLED + RMT peripheral; commands 0–6 switch animation and +/- adjusts brightness (8 levels) |
| 07 | `07_epd_test` | Individual module test | Verify GDEM0097F51 0.97-inch four-color e-paper display (image, four-color fill, fast refresh), automatic cycling, serial commands | ESP32-S3 | EPD: SCK=12, MOSI=11, CS=10, DC=9, RST=13, BUSY=14 | 🟡 Awaiting hardware test | Ported from `screen_arduino/`, uses `SPI.h` only; commands 1–5/F/A/H |
| 08 | `08_touch_button_test` | Individual module test | Verify two capacitive touch pads (T1=GPIO1, T4=GPIO4) with startup baseline calibration, raw-value display, and event monitoring | ESP32-S3 | T1=GPIO1, T4=GPIO4 | 🟡 Awaiting hardware test | Arduino `touchRead()`, 300 ms debounce; independently verifies final firmware touch logic |
| final | `final` | Final product firmware | Smart medication case: e-paper battery display, four-hour medication reminders, touch confirmation/case-opening detection, sunflower encouragement graphic | ESP32-S3 | EPD:9–14, AMP:41/42/2, LED:6, T1=1, T4=4 | 🟡 Awaiting hardware test | Combines features from projects 01/06/07; state machine NORMAL→REMIND→WAIT_OPEN→TAKEN |

> **When adding a new project:**
> 1. Copy a row into the appropriate position and sort by project number.
> 2. Describe clearly what the project verifies and which modules it covers.
> 3. For key pins, refer to `../硬件物料与引脚分配.md`.
> 4. Use status markers: 🟡 In progress / ✅ Passed / ❌ Failed / 📦 Archived.

---

## Status legend

| Symbol | Meaning |
|---|---|
| 🟡 | In progress (development/debugging) |
| ✅ | Verified; suitable as reference |
| ❌ | Failed or abandoned (retained for archival purposes) |
| 📦 | Archived; merged into final firmware |

---

## Related documentation

| Document | Path | Relationship |
|---|---|---|
| Bill of materials and purchasing links | `../物料.md` | Purchasing sources |
| **Hardware pin assignments** | `../硬件物料与引脚分配.md` | Authoritative pin assignments for all projects |
| Module datasheets / examples | `../datasheet/` | References for individual module test code |
| MPU6050 examples | `../MPU6050_Basic/`, `../MPU6050_TiltAngle/` | IMU test references |
| E-paper display examples | `../screen_arduino/`, `../screen/` | EPD test references |

---

## Maintenance conventions

1. **Register each project before adding it** to prevent an untraceable accumulation of subdirectories.
2. Each project directory must contain a `README.md` (or comments in `platformio.ini`) explaining its purpose.
3. Do not delete abandoned projects; mark them ❌ and document the failure in Notes (as with the archived INMP441 example under `datasheet/`).
4. Once a module test is integrated into the final firmware, mark it 📦.

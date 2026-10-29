# 01_audio_record_playback

> **Project type**: Module integration test
> **Purpose**: Verify that the LMD2718 PDM microphone and NS4168 amplifier can record and play back audio on the ESP32-S3
> **Microcontroller**: ESP32-S3 (16 MB Flash / 8 MB PSRAM)
> **Status**: ✅ Hardware test passed

---

## Features

- Serial command `R`: Record 10 seconds of audio to **PSRAM** (no LittleFS or WiFi required)
- Serial command `P`: Play the recording from PSRAM through the speaker (duplicate samples to both channels to avoid frame-slot alignment issues)
- Serial command `V`: Real-time volume meter (quickly check whether the microphone responds)
- Serial command `H`: Display the help menu

---

## Pin assignments (consistent with `../../硬件物料与引脚分配.md`)

| Module | Signal | GPIO | Notes |
|---|---|---|---|
| LMD2718 microphone | CLK | GPIO15 | I2S0 PDM clock, 2.048 MHz |
| LMD2718 microphone | DATA | GPIO16 | I2S0 PDM data |
| LMD2718 microphone | L/R | GND | Select right channel → `ONLY_RIGHT` |
| LMD2718 microphone | VDD | 3.3V | **5V will damage the device** |
| NS4168 amplifier | BCLK (board label: BCLK) | GPIO41 | I2S1 bit clock |
| NS4168 amplifier | LRC (board label: LRCLK) | GPIO42 | I2S1 word select |
| NS4168 amplifier | DIN (board label: **SDA**) | GPIO2 | I2S1 data |
| NS4168 amplifier | VDD | **5V** | |
| LMD2718 speaker | + / - | NS4168 OUT+ / OUT- | **BTL output: neither terminal is connected to ground** |

> ⚠️ The NS4168 module is labeled **BCLK / LRCLK / SDA / VCC / GND**. Here, `SDA` means the I2S data input (DIN), not I2C SDA.

---

## Key parameters

| Parameter | Value |
|---|---|
| Sample rate | 16,000 Hz |
| Bit depth | 16-bit |
| Recording channels | Mono (PDM RX `ONLY_RIGHT`) |
| Playback channels | **Stereo (`RIGHT_LEFT`, with duplicated samples)** |
| Recording duration | 10 s |
| Recording buffer | PSRAM (approximately 312 KB) |
| I2S ports | Microphone: I2S0; amplifier: I2S1 |
| PDM gain | **32.0** (see debugging notes below) |

---

## Build environment

> ⚠️ **Use PlatformIO, not Arduino IDE.**
> The default Arduino IDE environment uses the newer IDF 5.1+ I2S API, which conflicts with the legacy PDM driver and produces all-zero output.
> See `../../datasheet/麦克风扬声器/03_Mic_LMD2718/README.md`.

```bash
pio run -t upload
pio device monitor
```

Serial baud rate: **115200** (native USB CDC; GPIO19/20 are occupied by USB).

---

## Test procedure

1. **Wiring**: Connect the microphone (3.3V!) and amplifier (5V) + speaker according to the pin table.
2. **Power on**: `pio run -t upload && pio device monitor`
3. **Check microphone response**: Enter `V` in the serial monitor and speak into the microphone.
   - Silence: RMS ≈ 130–160
   - Speech: RMS ≈ 250+
4. **Record**: Enter `R` and speak into the microphone for 10 seconds.
   - `peak` is printed every second; normal speech should reach 3000–10000 (32× gain).
5. **Play back**: Enter `P`; the speaker should play the recording clearly.
6. **Adjust volume**:
   - Too quiet → increase `PDM_GAIN` in `src/audio_pdm.h` (32 → 48).
   - Distortion or clipping → decrease `PDM_GAIN` (32 → 16).

---

## Acceptance criteria (verified by testing)

| Test | Passing criterion | Observed result |
|---|---|---|
| Microphone response | RMS increases noticeably when speaking in `V` mode | ✅ |
| Recording peak | Normal speech: 3000–10000 | ✅ peak 5000–9760 |
| Amplifier playback | Clear audio playback through speaker | ✅ |
| Recording duration | Actual sample count ≥ 95% of target | ✅ 100% |

---

## Debugging notes (key lessons)

Three significant bugs were encountered during development. Their solutions may be useful for later projects.

### Bug 1: Default recording gain of 1.0 made playback nearly inaudible

**Symptom**: With `PDM_GAIN=1.0`, the reported peak was only ~150 (near the noise floor), and playback could not be heard.

**Root cause**: The raw PCM amplitude from the LMD2718 PDM microphone was very low, requiring software gain.

**Solution**: Set `PDM_GAIN = 32.0` and use `clamp_i16` to prevent overflow.

### Bug 2: Incorrect placement of peak measurement caused misleading debugging output

**Symptom**: After increasing gain to 8×, the reported peak was 780, which appeared sufficient, but playback was still inaudible.

**Root cause**: Peak was calculated **before applying gain**, so it reflected raw PCM (~98) rather than the buffered value (~780).

**Solution**: Move peak calculation after gain and clamping so the serial output reflects the values actually stored in the buffer.

> **Lesson**: Place debugging measurements at the **end of the signal-processing chain** (after gain and clamping); otherwise, reported values may differ from the actual output data.

### Bug 3: I2S channel mismatch caused silent playback

**Symptom**: Peak values exceeded 5000 (indicating valid audio data), but playback was completely silent.

**Root cause**:
- The PDM microphone (RX) was configured as `ONLY_RIGHT`, placing data in the right I2S frame slot.
- The amplifier (TX) was configured as `ONLY_LEFT`, reading the left slot.
- **ESP32 I2S does not automatically align left/right frame slots between RX and TX.**
- Data was written to the right slot while the amplifier read the empty left slot, resulting in silence.

**Unsuccessful attempt**: Changing the amplifier to `ONLY_RIGHT` still produced silence (the NS4168 may read the left slot due to internal LRCK polarity).

**Final solution**: Configure the amplifier for **`RIGHT_LEFT` stereo** and duplicate each playback sample into both L and R channels. This ensures that valid data is available regardless of which channel the NS4168 reads.

> **Lesson**: RX/TX frame-slot misalignment is a limitation of the ESP32 I2S setup. **Using stereo TX with duplicated samples** is a robust workaround that does not depend on channel alignment. This approach can be reused in subsequent audio TX projects.

---

## File structure

```
01_audio_record_playback/
├── platformio.ini      # ESP32-S3 configuration; enable PSRAM and USB CDC
├── README.md
└── src/
    ├── main.cpp         # Serial command dispatch (R/P/V/H)
    ├── audio_pdm.h/.cpp # PDM microphone initialization and PSRAM recording (PDM_GAIN=32.0)
    └── audio_amp.h/.cpp # I2S amplifier (RIGHT_LEFT stereo and sample duplication)
```

---

## Out of scope

- ❌ No LittleFS integration (recordings are stored only in PSRAM)
- ❌ No WiFi / HTTP / Base64 upload
- ❌ No VAD-based automatic stop (fixed 10-second recording)
- ❌ No changes to other modules (e-paper display / IMU / motor / LED strip)

For persistent recording storage or PC uploads, this project can be extended based on `../../datasheet/麦克风扬声器/MIC_PDM/src/main.cpp`.

---

## References

| Source | Path |
|---|---|
| Microphone example | `../../datasheet/麦克风扬声器/MIC_PDM/` |
| Amplifier example | `../../datasheet/麦克风扬声器/AMP/` |
| Microphone documentation | `../../datasheet/麦克风扬声器/03_Mic_LMD2718/README.md` |
| Amplifier documentation | `../../datasheet/麦克风扬声器/04_Amp_NS4168/README.md` |
| Pin assignments | `../../硬件物料与引脚分配.md` |
| Amplifier hardware diagnostics | `../05_amp_beep_test/` (square-wave test for hardware-level troubleshooting) |

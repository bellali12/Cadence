# 05_amp_beep_test

> **Project type:** Single-module diagnostics
> **Purpose:** Generate square-wave PCM directly on the ESP32-S3 and send it to the NS4168 to test the I2S1 → amplifier → speaker signal path independently of microphone/recording data.
> **Controller:** ESP32-S3
> **Status:** 🟡 Awaiting hardware testing

---

## Why This Separate Test Is Needed

In Project 01, recorded peak values appeared normal (suggesting the microphone works), but the `P` command produced no audio. Possible causes:

1. Recorded data itself (unlikely given the normal peak values).
2. Reading data from PSRAM (unlikely given 100% successful writes).
3. **I2S1 configuration**.
4. **NS4168 wiring, power supply, or MUTE**.
5. **Speaker**.

This test **bypasses items 1 and 2** by generating PCM mathematically as a square wave and transmitting it through I2S1. If there is still no sound, investigate items 3–5 (hardware path). If sound works, the Project 01 recording/PSRAM path is implicated.

---

## Features

- Generate square-wave PCM on the ESP32-S3 without microphone or PSRAM.
- Output to the NS4168 through I2S1.
- Adjust frequency, volume, and playback duration.

### Serial Commands

| Command | Function |
|---------|----------|
| `P` | Play 440 Hz square wave for 3 s (concert A4) |
| `L` | Play continuously until any key is pressed; check speaker vibration or current |
| `S` | Stop |
| `1` / `2` / `3` | Set frequency: 261 Hz (C4) / 440 Hz (A4) / 880 Hz (A5) |
| `V <0-100>` | Volume percentage, default 50 |
| `H` | Help |

---

## Pins (per `../../硬件物料与引脚分配.md`, Section 3.3)

| NS4168 module | ESP32-S3 | Notes |
|---------------|----------|-------|
| VDD | **5V** (not 3.3V) | ⚠️ At 3.3V output power may be very low or startup may fail |
| GND | GND | Common ground with ESP32 |
| BCLK / SCLK | **GPIO41** | Finalized |
| LRCK / WS / LRC | **GPIO42** | Finalized |
| DIN / SDATA | **GPIO2** | Finalized |
| MUTE / CTRL | **Try connecting to 5V first** | ⚠️ Some modules require explicit high level despite default enable |
| OUT+ | Speaker + | **BTL: neither speaker terminal connects to ground** |
| OUT- | Speaker − | |

---

## Test Procedure (Troubleshoot in Order)

### Step 1: Play Directly

```bash
cd code/05_amp_beep_test
pio run -t upload && pio device monitor
```

Press `P`. Expected: an audible 440 Hz A4 beep.

### Step 2: If Silent, Check MUTE

- Use a jumper wire to connect NS4168 **MUTE/CTRL to 5V**.
- Press `P` again.
- **If sound starts:** MUTE may be pulled low by default; it needs a hardware pull-up or software control.

### Step 3: If Still Silent, Check Power

- Measure DC voltage between NS4168 VDD and GND with a multimeter.
- **It must be at least 4.5V** (5V recommended).
- If only 3.3V is present, use a 5V supply (board 5V pin or external source).

### Step 4: If Power Is Correct, Check Wiring

- **Follow the actual labels printed on the module PCB** (as repeatedly emphasized by the datasheet).
- Labels may vary: `SCLK/WS/DIN`, `BCK/LRCK/SDATA`, or `BCLK/LRC/DIN`.
- Verify each connection against the printed labels.

### Step 5: If Wiring Is Correct, Check Speaker

- Measure resistance across speaker terminals; expected 4–8 Ω.
- Briefly touch the terminals with a 3.3V or 5V supply (under one second); a click should be audible.
- If neither test produces a response, the speaker may be faulty.

### Step 6: If Speaker Works, Use Continuous Mode

- Press `L` for continuous playback.
- Lightly touch the speaker cone; vibration should be perceptible.
- Measure AC voltage between OUT+ and OUT−; the source expects several volts AC.

---

## Acceptance Criteria

| Test | Pass criterion |
|------|----------------|
| `P` | Clear audible beep |
| Frequency switching | `1` low, `2` medium, `3` high; clearly different pitches |
| Volume adjustment | `V 10` quiet; `V 90` loud |
| Continuous mode `L` | Sustained sound and perceptible cone vibration |

### Diagnostic Conclusions

| Observation | Likely location of problem |
|-------------|----------------------------|
| This test works, but Project 01 `P` is silent | Project 01 recording/PSRAM data path (though normal peak values make this less likely) |
| This test is also silent | **Hardware signal path:** MUTE, VDD, wiring, or speaker; follow six steps above |

---

## Square-Wave Parameters

| Parameter | Value |
|-----------|-------|
| Sample rate | 16000 Hz |
| Bit depth | 16-bit |
| Default frequency | 440 Hz (A4) |
| Default volume | 50% (amplitude ±16000) |
| Buffer | 0.5 s × 16-bit = 16 KB (internal RAM) |

> A square wave has strong odd harmonics (fundamental + 3f + 5f + …), making speaker output more obvious than a pure sine wave and useful for a quick audible test.

---

## File Structure

```
05_amp_beep_test/
├── platformio.ini
├── README.md
└── src/
    └── main.cpp     # Square-wave generation, I2S1 output, command handling
```

---

## References

| Source | Path / link |
|--------|-------------|
| NS4168 datasheet | `../../datasheet/麦克风扬声器/04_Amp_NS4168/README.md` |
| Pin definitions | `../../硬件物料与引脚分配.md`, Section 3.3 |
| Project 01 (system being diagnosed) | `../01_audio_record_playback/` |

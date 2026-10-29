# 07_epd_test — GDEM0097F51 Color E-Paper Display Test

> ESP32-S3 driver verification for a 0.97-inch four-color e-paper display (black/white/red/yellow; 88×184 pixels; JD79661AA controller), supporting interactive serial commands and an automatic demo cycle.

Based on the official example already adapted for ESP32-S3 in `screen_arduino/`. Library files (`Display_EPD_W21*` and `Ap_29demo.h`) were copied from `screen_arduino/`, with additional changes to address **hangs during repeated refreshes** (see Section 7).

---

## 1. Hardware Wiring

| Display FPC | ESP32-S3 | Notes |
|-------------|----------|-------|
| VCC / VDD | **3.3V** | **Never use 5V; it can damage the display** |
| GND | GND | |
| SCL / SCK | **GPIO12** | Hardware SPI clock |
| SDA / MOSI / DIN | **GPIO11** | Hardware SPI data |
| CS | **GPIO10** | Chip select |
| DC | **GPIO9** | 0=command, 1=data |
| RST / RES | **GPIO13** | Active-low reset |
| BUSY | **GPIO14** | Busy-status input |
| BS | GND | Select four-wire SPI mode |

> Pins match the tested `screen_arduino/` configuration (different from the draft in `硬件物料与引脚分配.md`, Section 3.1).

---

## 2. SPI Parameters

| Item | Value |
|------|-------|
| Clock | 10 MHz |
| Mode | MODE0 (CPOL=0, CPHA=0) |
| Bit order | MSB first |
| MISO | Not connected (unidirectional four-wire SPI write) |

---

## 3. Upload Firmware

```bash
cd code/07_epd_test
pio run -t upload
```

Automatic cycling is disabled by default after upload to simplify debugging. Use serial commands to control the display.

---

## 4. Serial Commands (115200 Baud; Single Character + Newline)

| Command | Action | Duration |
|---------|--------|----------|
| `1` | Show built-in image `gImage_1` | ~16–25 s |
| `2` | Fill white | ~16–25 s |
| `3` | Fill red | ~16–25 s |
| `4` | Fill yellow | ~16–25 s |
| `5` | Fill black | ~16–25 s |
| `F` | Show image using **fast refresh** | ~12 s |
| `A` | Toggle automatic cycling (default **OFF**) | — |
| `H` | Help | — |

**Automatic sequence:** image → white → red → yellow → black → repeat (20 s between pages).

> Refresh duration varies between displays and even between refreshes on the same display. The code waits at most 30 s and proceeds as soon as BUSY goes high.

---

## 5. Four-Color Encoding (2 Bits per Pixel)

| Color | Code |
|-------|------|
| Black | `00` |
| White | `01` |
| Yellow | `10` |
| Red | `11` |

**Full-screen fill-byte patterns (four pixels per byte):**
- All black: `0x00`
- All white: `0x55`
- All yellow: `0xAA`
- All red: `0xFF`

Total frame-buffer bytes: `88 × 184 / 4 = 4048`.

---

## 6. Safety Precautions (Required)

1. **Supply 3.3V only.** A 5V supply can damage the display controller.
2. **Do not connect or disconnect the FPC ribbon cable while powered.**
3. **Refresh at least once every 24 hours** to reduce image retention (ghosting).
4. If the display will be unused for a long time, send `2` to show an all-white screen.

> ⚠️ **`EPD_DeepSleep()` note:** The official example recommends calling it after each refresh to extend display life. Testing in this project found that **hardware RST does not reliably wake the panel after deep sleep** (Section 7). The current code therefore sends only `POF` (0x02, power off) without entering deep sleep. **The RST wiring issue must be resolved before production**, after which `EPD_DeepSleep()` should be restored.

---

## 7. Known Issues and Solutions (Important)

### Issue 1: Repeated Refreshes Hang; BUSY Remains at 0

**Symptom:** First refresh completes normally (display changes and serial prints Done), but after the second refresh the serial interface stops responding and BUSY remains at 0.

**Diagnosis:** Continuous BUSY-level logging inside `lcd_chkstatus` showed:
- First refresh: BUSY goes low after command 0x12 and returns high after ~16 s — normal.
- Second refresh: BUSY is already 0 at the beginning of `EPD_init`; even a 200 ms hardware RST pulse cannot wake the display.

**Likely root cause:** After `EPD_DeepSleep()` (command `0x07 0xA5`) enters deep sleep, the **hardware RST signal does not wake the controller**. The most likely issue is wiring between GPIO13 and the display's RES pin (cold solder joint, poor contact, or broken wire), preventing the reset pulse from reaching the controller.

**Temporary workaround (implemented in the current code):**

```cpp
// Do not call EPD_DeepSleep(); send only POF (power off without deep sleep).
EPD_W21_WriteCMD(0x02);   // POF power off
EPD_W21_WriteDATA(0x00);
delay(50);
```

The controller stays in standby, so the next initialization does not require a deep-sleep wakeup. **Tradeoff:** Slightly higher power consumption (standby uses several mA more than deep sleep), acceptable during debugging.

**Permanent fix (required before production):**
1. Use a multimeter to check continuity between ESP32-S3 GPIO13 and the display FPC RES pin.
2. Check the RES wire for cold solder joints or breaks.
3. Replace the jumper wire if necessary.
4. After repair, replace the `POF` sequence at the end of `show_page()` in `main.cpp` with `EPD_DeepSleep()`.

### Issue 2: Long Refresh Times (>20 s)

**Symptom:** The datasheet specifies ~16 s for a standard refresh, but the tested panel took 16–25 s.

**Solution:** Increase the `EPD_refresh` wait limit from 20 s to **30 s**, while still checking BUSY to exit immediately after completion.

### Debug Log Interpretation

The firmware prints `[DBG]` messages for troubleshooting:

| Log | Meaning |
|-----|---------|
| `[DBG] HW reset start, BUSY=?` | BUSY level before hardware reset |
| `[DBG] HW reset done in Xms, BUSY=?` | Hardware reset duration/result (BUSY should be 1) |
| `[DBG] before/after 0x12: BUSY=?` | BUSY before/after refresh command (after should be 0) |
| `[DBG] BUSY -> 1 at Xs` | Moment refresh finishes |

**Example of a normal single refresh:**

```
[DBG] HW reset start, BUSY=1
[DBG] HW reset done in 0ms, BUSY=1
[DBG] before 0x12: BUSY=1
[DBG] after  0x12: BUSY=0
[DBG] chkstatus enter: BUSY=0 max=30s
[DBG] t=1s ... t=15s BUSY=0
[DBG] BUSY -> 1 at 16s
[INFO] Done in 17.4s
```

Disable these logs for production by removing/commenting the `Serial.print` calls in `Display_EPD_W21.cpp`.

---

## 8. Troubleshooting

| Symptom | Cause / solution |
|---------|------------------|
| Display does not respond after upload | Check 3.3V supply, BS connected to GND, and serial output at 115200 baud |
| Serial output works but display does not update | Check BUSY/SCK/MOSI/CS wiring; test with SPI clock reduced to 1 MHz |
| Display flashes during full refresh | Normal; used to clear ghosting |
| Second refresh hangs | See Issue 1 in Section 7; check RST wiring |
| BUSY stays 0 for a long time during refresh | Normal while refreshing; the 30 s timeout will eventually exit |

---

## 9. Directory Structure

```
07_epd_test/
├── platformio.ini              # PlatformIO configuration (no lib_deps)
├── README.md                   # This file
└── src/
    ├── main.cpp                # Newly written: entry point, serial state machine, POF instead of DeepSleep
    ├── Display_EPD_W21_spi.h   # Copied from screen_arduino (ESP32S3 pin branch; unmodified)
    ├── Display_EPD_W21_spi.cpp # Copied from screen_arduino (original; unmodified)
    ├── Display_EPD_W21.h       # Copied from screen_arduino (original; unmodified)
    ├── Display_EPD_W21.cpp     # Modified: hardware reset, diagnostics, 30 s timeout
    └── Ap_29demo.h             # Copied from screen_arduino (4048-byte sample image; unmodified)
```

> Changes to `Display_EPD_W21.cpp` compared with the `screen_arduino` version are described in Section 7. The original can be restored after the RST wiring is repaired.

---

## 10. Relationship to Other Projects

| Project / document | Relationship |
|--------------------|--------------|
| `screen_arduino/` | Source of copied library files, pin assignments, and initialization logic |
| `datasheet/彩色墨水屏/彩色墨水屏_驱动开发速查.md` | Quick reference for registers, color encoding, and porting |
| `datasheet/彩色墨水屏/AU-GDEM0097F51/` | Official Arduino sample |
| `硬件物料与引脚分配.md`, Section 3.1 | Pin assignment (consistent with tested screen_arduino configuration) |

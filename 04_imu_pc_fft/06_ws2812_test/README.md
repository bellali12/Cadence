# 06_ws2812_test — WS2812 LED Test (9 LEDs)

> Verify that the ESP32-S3 can drive nine WS2812 LEDs, with six animations and serial control.

---

## 1. Hardware Wiring

| WS2812 | ESP32-S3 | Notes |
|--------|----------|-------|
| VCC | **5V** (boost converter output) | Nine LEDs at full white draw approximately 540 mA |
| GND | GND | **Must share ground with the ESP32-S3** |
| DIN | **GPIO6** | Finalized pin assignment |

A 300–500 Ω series resistor before the first LED's DIN pin is recommended to suppress ringing (hardware document, Section 3.6).

---

## 2. Firmware Upload

```bash
cd code/06_ws2812_test
pio run -t upload
```

On startup, a **sequential red-LED self-test** checks that all nine LEDs can be driven. The default scrolling-rainbow animation then starts.

---

## 3. Serial Commands (115200 Baud)

| Command | Animation |
|---------|-----------|
| `0` | Off |
| `1` | Solid white (maximum-brightness self-test) |
| `2` | Scrolling rainbow (default) |
| `3` | Breathing (red → green → blue; approximately 7.7 s per color) |
| `4` | Chasing light (single LED with trailing fade) |
| `5` | Progressive fill (light LEDs in sequence, then turn off and reset) |
| `6` | Three-color cycle (3 red / 3 green / 3 blue) |
| `+` / `-` | Increase/decrease brightness (8 levels: 5/20/40/80/130/180/230/255) |
| `H` | Help |

---

## 4. Key Implementation Details

| Item | Implementation |
|------|----------------|
| Library | `fastled/FastLED @ ^3.6.0` (automatically uses the ESP32-S3 RMT peripheral for precise timing) |
| LED count | `NUM_LEDS = 9` |
| Pin | `LED_PIN = 6` |
| Color order | `GRB` (WS2812B standard) |
| Frame rate | One frame every 30 ms ≈ 33 fps |

---

## 5. Verification Tests

| Scenario | Expected result |
|----------|-----------------|
| Startup | All nine LEDs light red in sequence for 120 ms each |
| Enter `1` | All nine LEDs turn white |
| Enter `+` repeatedly | Brightness gradually increases from level 0 to 7 |
| Enter `0` | All LEDs turn off |
| Enter `2` | Smooth scrolling-rainbow effect |

---

## 6. Troubleshooting

| Symptom | Cause / solution |
|---------|------------------|
| Flickering or incorrect colors | DIN signal ringing; add a 300–500 Ω series resistor |
| First LED works, others do not | Insufficient current from the 5V boost converter or missing common ground |
| No LEDs light | Check that VCC is actually 5V and DIN connects to GPIO6 |
| Red and green are swapped | WS2812 and WS2812B may use different color orders; try changing `GRB` to `RGB` |

---

## 7. Relationship to Other Projects

- Use the pin assignment from `../../硬件物料与引脚分配.md`, Section 3.6 (DIN=GPIO6).
- FastLED uses the RMT peripheral, not SPI, avoiding conflicts with the e-paper display.

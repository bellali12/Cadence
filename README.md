# 02_imu_raw_data

> **Project type**: Single-module test
> **Purpose**: Verify that the MPU6050 communicates with the ESP32-S3 and outputs acceleration, temperature, gyroscope data, and orientation angles.
> **Controller**: ESP32-S3 (16 MB flash)
> **Status**: 🟡 Awaiting hardware testing

---

## Library selection

**Avoid reinventing the wheel**: Use the [`rfetick/MPU6050_light`](https://github.com/rfetick/MPU6050_light) library:

- ✅ Built-in startup calibration (`calcGyroOffsets` automatically calculates gyroscope zero offsets)
- ✅ Built-in complementary filter for direct pitch/roll/yaw angle output
- ✅ No third-party dependencies (only `Wire.h`)
- ✅ Good ESP32 compatibility
- ✅ Lightweight and fast

Compared with direct register-level implementation, this avoids writing calibration logic, measurement-range conversions, and filtering algorithms. Future medication-compartment tilt detection can directly use `getAngleX/Y`.

| Alternative library | Output | Selection rationale |
|---|---|---|
| **MPU6050_light** ✅ | Raw data + angles | Good balance, simple API, built-in calibration and filtering |
| Adafruit_MPU6050 | Raw data only | No angle calculation; more dependencies |
| MPU6050_tockn | Raw data + angles | Less maintenance; filtering considered inferior to MPU6050_light |
| ElectronicCats (DMP) | Quaternions | Most accurate full 3D output, but complicated configuration |

---

## Features

- Initialize at startup (`Wire.begin(8, 7)` specifies the finalized project pins)
- Automatic sensor calibration (library prints progress)
- 50 Hz serial output

### Serial commands

- `C`: Recalibrate (keep still for approximately 3 seconds)
- `R`: Toggle output between CSV and text
- `H`: Help

---

## Pin connections (consistent with section 3.7 of `../../硬件物料与引脚分配.md`)

| MPU6050 | ESP32-S3 | Notes |
|---|---|---|
| VCC | **3.3V** | Do not use 5V |
| GND | GND | |
| SDA | **GPIO8** | ★ Finalized |
| SCL | **GPIO7** | ★ Finalized |
| AD0 | GND | Address 0x68 (0x69 if connected to VCC) |
| INT | Unconnected | Polling is used, not interrupts |
| XDA/XCL | Unconnected | No secondary sensor connected |

> ⚠️ **Pull-up resistors**: GY-521 breakout boards usually include 4.7 kΩ pull-ups. Bare-chip designs need a 4.7 kΩ pull-up from each of SDA and SCL to 3.3V.

---

## Output format

### CSV (default)

```
ax,ay,az,temp,gx,gy,gz,pitch,roll,yaw
0.01,-0.02,1.00,25.3,0.12,-0.08,0.05,0.10,-0.05,0.00
```

- `ax/ay/az`: Acceleration (g)
- `temp`: Temperature (°C)
- `gx/gy/gz`: Gyroscope readings (degrees per second, dps)
- `pitch/roll/yaw`: Angles from the complementary filter (degrees)

### Text mode (toggle with `R`)

Human-readable, multi-column output for direct viewing in the serial monitor.

---

## Build environment

PlatformIO with the Arduino framework. Dependencies are installed automatically via `lib_deps` in `platformio.ini`:

```ini
lib_deps = rfetick/MPU6050_light @ ^1.2.0
```

```bash
pio run -t upload
pio device monitor
```

Serial communication uses native USB CDC (GPIO19/20 are occupied by USB).

---

## Testing procedure

1. **Wiring**: Connect as shown above (SDA→GPIO8, SCL→GPIO7, AD0→GND, VCC→**3.3V**).
2. **Power on**: Run `pio run -t upload && pio device monitor`.
3. **Check startup logs**:
   - `mpu.begin() status = 0` (0 = success; nonzero = failure)
   - Calibration messages appear (keep the board still).
4. **Observe CSV output while the board is stationary and level**:
   - `|ax|,|ay| < 0.05`, `az ≈ 1.00` (±0.02)
   - `|gx|,|gy|,|gz| < 0.5`
   - `pitch, roll ≈ 0` (±1°)
5. **Tilt the board**: Pitch and roll should change accordingly (within ±90°).

---

## Acceptance criteria

| Test | Passing criterion |
|---|---|
| `mpu.begin() status` | = 0 |
| Stationary az | ≈ 1.00g (±0.02) |
| Stationary gyroscope | All three axes < 0.5 dps |
| Stationary pitch/roll | < 1° |
| Tilt response | Pitch/roll track movement |
| Rotation response | Corresponding gyroscope axis changes |

### Common issues

| Symptom | Possible cause |
|---|---|
| `status != 0` | I2C communication failure: swapped/disconnected SDA/SCL, floating AD0, or VCC disconnected |
| Stationary pitch/roll drift | Board moved during calibration; press `C` to recalibrate |
| az far from 1g | Board not level or incorrect measurement range |
| Sudden data jumps | I2C wires too long (>30 cm), power-supply noise, or missing pull-ups |

---

## File structure

```
02_imu_raw_data/
├── platformio.ini       # ESP32-S3 + MPU6050_light library dependency
├── README.md
└── src/
    └── main.cpp          # Initialization, 50 Hz output, and command handling
```

---

## Out of scope

- ❌ No application-level tilt detection logic (medication-compartment orientation detection is reserved for the final firmware)
- ❌ No interrupts (DRDY pin not connected)
- ❌ No Kalman filtering (complementary filtering is sufficient for the medication-compartment application)
- ❌ No changes to other modules

---

## References

| Source | Path / link |
|---|---|
| MPU6050_light library | https://github.com/rfetick/MPU6050_light |
| PlatformIO Registry | https://registry.platformio.org/libraries/rfetick/MPU6050_light |
| Official Arduino documentation | https://docs.arduino.cc/libraries/mpu6050_light/ |
| Previous basic example | `../../MPU6050_Basic/MPU6050_Basic.ino` |
| Previous tilt-angle example | `../../MPU6050_TiltAngle/MPU6050_TiltAngle.ino` |
| Pin definitions | `../../硬件物料与引脚分配.md`, section 3.7 |

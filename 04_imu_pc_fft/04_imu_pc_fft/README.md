# 04_imu_pc_fft — MPU6050 Raw-Data Acquisition + PC-Side FFT Analysis

> The ESP32-S3 collects raw six-axis IMU data at 100 Hz. A PC application performs FFT and visualization, triggered by a Record button.

---

## 1. Directory Structure

```
04_imu_pc_fft/
├── platformio.ini              # PlatformIO configuration
├── README.md                   # This file
├── src/
│   └── main.cpp                # ESP32 firmware (100 Hz sampling + serial protocol)
└── tools/
    ├── imu_fft_gui.py          # PC application (serial reception + FFT + matplotlib)
    ├── requirements.txt        # Python dependencies
    └── captures/               # Automatically generated CSV captures
```

---

## 2. Hardware Wiring

| MPU6050 | ESP32-S3 | Notes |
|---------|----------|-------|
| VCC | 3.3V | Do not use 5V |
| GND | GND | |
| SDA | **GPIO18** | Actual wiring used |
| SCL | **GPIO17** | Actual wiring used |
| AD0 | GND | Address 0x68 |
| INT | Not connected | Not used in this project |

> Note: The draft `硬件物料与引脚分配.md` specifies GPIO7/8, but GPIO8 is occupied by PSRAM on the N16R8 module. This project therefore uses GPIO17/18.

---

## 3. Firmware Upload

```bash
cd code/04_imu_pc_fft
pio run -t upload
```

After uploading, verify manually using the Serial Monitor at 115200 baud:

```
> S
$START,100
$IMU,12345,0.01,-0.02,1.00,0.12,-0.08,0.05
$IMU,12355,...
> S
$STOP,287
```

---

## 4. Serial Protocol (115200 Baud, One ASCII CSV Record per Line)

| Tag | Meaning |
|-----|---------|
| `$READY` | Device ready after boot (sent once) |
| `$START,<rate_hz>` | Acquisition started (rate=100) |
| `$IMU,<t_ms>,<ax>,<ay>,<az>,<gx>,<gy>,<gz>` | Data sample |
| `$STOP,<count>` | Acquisition stopped |

- `ax/ay/az`: acceleration in g.
- `gx/gy/gz`: angular velocity in degrees per second (dps).
- `t_ms`: milliseconds since boot; the PC uses this to recalculate the actual sample rate.

Commands accepted by the ESP32 (from the PC or Serial Monitor, newline terminated):

| Command | Action |
|---------|--------|
| `S` | Toggle acquisition |
| `C` | Recalibrate gyroscope offsets |
| `H` | Help |

---

## 5. Using the PC Application

### Install Dependencies

```bash
cd code/04_imu_pc_fft/tools
pip install -r requirements.txt
```

Dependencies: `pyserial`, `numpy`, and `matplotlib` (no SciPy or pandas required).

### Run

```bash
python imu_fft_gui.py
```

> **Close PlatformIO Serial Monitor and any other program using the serial port before launching**, or the application will be unable to open the COM port.

### Workflow

1. Select a COM port under "Serial Connection" and click **Connect**.
2. After `$READY Device ready` appears, click **● Record** to begin acquisition.
3. Leave the IMU stationary or move it for 3–10 seconds.
4. Click **■ Stop** to end acquisition.
5. A 2×2 figure opens automatically:
   - **Top left:** time-domain acceleration ax/ay/az.
   - **Top right:** FFT magnitude spectrum (4–6 Hz tremor band and peak frequency marked).
   - **Bottom left:** time-domain gyroscope gx/gy/gz.
   - **Bottom right:** analysis results (sample rate, peak, tremor-band energy ratio).
6. A CSV is automatically saved to `tools/captures/imu_<timestamp>.csv`.

### Tremor-Band Energy Ratio

As described in `03_parkinson_tremor_detect/RESEARCH.md`:
- The 4–6 Hz band is characteristic of Parkinsonian resting tremor.
- An **energy ratio above 20%** may indicate tremor (for reference only; **not a diagnosis**).

---

## 6. Verification Tests

| Scenario | Expected result |
|----------|-----------------|
| IMU stationary on a table | Near-DC time-domain signal plus noise; spectral noise floor except at 0 Hz; energy ratio < 5% |
| Handheld IMU shaken periodically at ~5 Hz | Distinct spectral peak at 4–6 Hz; energy ratio > 20%; peak frequency ≈ 5 Hz |
| Rapid rotation (>1 Hz) | Large gyroscope amplitude in time domain; low-frequency spectral peak |

---

## 7. Relationship to Other Projects

| Project | Relationship |
|---------|--------------|
| `02_imu_raw_data` | Firmware adapted from Project 02, reusing the driver and command-handling framework |
| `03_parkinson_tremor_detect` | FFT settings (Hanning window, 4–6 Hz band, energy-ratio threshold) based on Project 03 |
| `datasheet/麦克风扬声器/MIC_PDM/pdm_gui.py` | Reference for the PC application's serial and multithreading architecture |

**Difference:** Project 03 performs FFT on the ESP32 (fixed output), while this project performs FFT on the PC (adjustable parameters and visualization).

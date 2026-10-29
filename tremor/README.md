# tremor/ — Parkinsonian Tremor Detection Module

> Source: ported from `code/03_parkinson_tremor_detect/src/` into the final firmware.
> For algorithm details, see `code/03_parkinson_tremor_detect/RESEARCH.md`.

## API

```cpp
#include "tremor/tremor_detect.h"

tremor_init();                                       // Initialize once in setup()
tremor_feed_sample(ax, ay, az);                      // Feed samples at 100 Hz (every 10 ms in loop())
TremorResult r;
if (tremor_analyze(&r)) {                            // Returns true when buffer is full (2.56 s)
    if (r.tremorConfirmed) { /* Tremor confirmed; report event */ }
}
```

## Key parameters (`tremor_detect.h`)

| Parameter | Value | Meaning |
|---|---|---|
| `SAMPLE_RATE_HZ` | 100 | IMU sampling frequency |
| `FFT_N` | 256 | FFT window size (2.56 seconds) |
| `TREMOR_FREQ_LOW/HIGH_HZ` | **4–8** | Target frequency band (broad band to prioritize sensitivity) |
| `DEFAULT_RATIO_THRESH` | 0.40 | Spectral energy ratio threshold (requires empirical tuning) |
| `CONFIRM_WINDOWS` | 3 | Required consecutive positive windows (approximately 8 seconds) |

## Detection logic

An FFT is performed every 2.56 seconds to calculate the fraction of spectral energy in the 4–8 Hz band (`ratio`) and the peak spectral frequency (`peakFreq`):

```
hit       = (ratio > 0.4) AND (peakFreq ∈ [4, 8] Hz)
confirmed = hit in 3 consecutive windows
```

## Changes from project 03 during porting

1. **Target band widened from 4–6 Hz to 4–8 Hz:** covers action and postural tremor and reduces missed detections near frequency boundaries.
2. **Removed redundant library `dcRemoval` and `windowing` calls:** project 03 applied a Hann window manually and then called the library windowing function, effectively windowing twice. The port retains only manual windowing, reducing CPU work slightly.
3. **Added `hitCount` to `TremorResult`:** included in the extra JSON fields for reporting.

## Debug commands (final application)

| Serial command | Action |
|---|---|
| `M` | Toggle tremor detection |
| `P` | Print current IMU readings |
| `I` | Check Wi-Fi/NTP status |

## Known limitations

- The 0.4 threshold is empirical. Body-worn and handheld measurements have different spectral characteristics; collect data with the PC GUI in `code/04_imu_pc_fft/` to calibrate it.
- Acceleration alone cannot distinguish resting (Parkinsonian) tremor from action (cerebellar) tremor.
- Windows do not overlap. A future V2 could use 50% overlap to improve sensitivity near window boundaries.

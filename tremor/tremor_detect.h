#ifndef TREMOR_DETECT_H
#define TREMOR_DETECT_H

#include <Arduino.h>
#include <stdint.h>

// ============ Tremor detection parameters ============
// Algorithm source: code/03_parkinson_tremor_detect (previously validated)
// Porting notes: see README.md in this directory

// Sampling rate: 100 Hz (window duration = N/fs = 2.56 seconds)
#define SAMPLE_RATE_HZ        100.0f

// FFT size (must be a power of two)
#define FFT_N                 256

// Characteristic Parkinsonian tremor frequency band: 4–8 Hz
// Use 4–8 Hz (broad band), rather than 4–6 Hz (typical resting tremor), to prioritize sensitivity;
// covers resting (4–6 Hz), action (5–8 Hz), and postural (4–8 Hz) tremors,
// reducing missed detections near lower (3.5–4 Hz) or higher (6.5–7 Hz) boundaries.
// Trade-off: essential tremor or ordinary motion may cause false positives; mitigate using the ratio threshold and three-window confirmation.
#define TREMOR_FREQ_LOW_HZ    4.0f
#define TREMOR_FREQ_HIGH_HZ   8.0f

// Corresponding FFT bin index: bin = f * N / fs = f * 2.56
// 4 Hz → bin 10, 8 Hz → bin 20 (computed dynamically at runtime; shown for reference)

// Detection threshold: fraction of total energy within 4–8 Hz
// 0.4 is an empirical initial value; tune experimentally (worn and handheld spectra differ)
#define DEFAULT_RATIO_THRESH  0.40f

// Required consecutive positive windows (2.56 s each; three ≈ 8 s)
// Prevents transient motions (coughing, reaching, knocking) from triggering detection
#define CONFIRM_WINDOWS       3

// ============ Detection results ============
typedef struct {
    float ratio;          // Fraction of energy within 4–8 Hz [0, 1]
    float peakFreq;       // Peak spectral frequency (Hz)
    float bandEnergy;     // Energy within 4–8 Hz band
    float totalEnergy;    // Total energy (excluding DC)
    bool  tremorHit;      // Whether the current window meets both detection criteria
    bool  tremorConfirmed;// Whether consecutive windows confirm tremor (final alert)
    int   hitCount;       // Current consecutive hit count (for reporting)
} TremorResult;

// ============ API ============

// Initialize detector (construct FFT window and clear buffers)
void tremor_init();

// Feed one three-axis acceleration sample (recommended at 100 Hz)
// ax/ay/az are measured in g
void tremor_feed_sample(float ax, float ay, float az);

// Call when buffer contains FFT_N samples to perform FFT analysis
// Returns true when a new result is available (result updated)
bool tremor_analyze(TremorResult* result);

// Set/get threshold (runtime adjustable)
void tremor_set_threshold(float ratio);
float tremor_get_threshold();

// Get most recent spectrum (for debugging; length FFT_N/2)
const float* tremor_get_spectrum();

#endif // TREMOR_DETECT_H

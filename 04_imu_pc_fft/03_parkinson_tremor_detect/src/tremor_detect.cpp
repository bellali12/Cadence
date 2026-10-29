#include "tremor_detect.h"
#include "arduinoFFT.h"
#include <math.h>

// ============ arduinoFFT instance (float version) ============
ArduinoFFT<float> FFT;

// ============ Buffers ============
// Sliding acceleration-magnitude buffer (AC component after mean removal)
static float  ring_buf[FFT_N];
static size_t ring_idx = 0;
static size_t ring_count = 0;

// FFT working buffers (arduinoFFT float version requires separate vReal/vImag arrays)
static float vReal[FFT_N];
static float vImag[FFT_N];

// Hanning window (precomputed)
static float hanning_win[FFT_N];

// Spectrum results (only the first N/2 valid bins)
static float spectrum[FFT_N / 2];

// Current threshold (adjustable at runtime)
static float ratio_threshold = DEFAULT_RATIO_THRESH;

// Consecutive positive-window counter (for CONFIRM_WINDOWS decision)
static int hit_count = 0;

// ============ Precompute Hanning window ============
static void build_hanning() {
  for (size_t k = 0; k < FFT_N; k++) {
    hanning_win[k] = 0.5f * (1.0f - cosf(2.0f * PI * k / (FFT_N - 1)));
  }
}

// ============ Initialization ============
void tremor_init() {
  ring_idx = 0;
  ring_count = 0;
  hit_count = 0;
  build_hanning();
  for (size_t i = 0; i < FFT_N / 2; i++) spectrum[i] = 0;
}

// ============ Feed samples ============
void tremor_feed_sample(float ax, float ay, float az) {
  // Three-axis magnitude (mean removal in analyze() removes the DC/gravity component and accommodates slow drift)
  float mag = sqrtf(ax * ax + ay * ay + az * az);
  ring_buf[ring_idx] = mag;
  ring_idx = (ring_idx + 1) % FFT_N;
  if (ring_count < FFT_N) ring_count++;
}

// ============ Set/get threshold ============
void tremor_set_threshold(float ratio) {
  if (ratio < 0.05f) ratio = 0.05f;
  if (ratio > 0.95f) ratio = 0.95f;
  ratio_threshold = ratio;
}
float tremor_get_threshold() { return ratio_threshold; }

const float* tremor_get_spectrum() { return spectrum; }

// ============ FFT analysis ============
bool tremor_analyze(TremorResult* result) {
  // Buffer not full: skip analysis
  if (ring_count < FFT_N) {
    if (result) {
      result->tremorHit = false;
      result->tremorConfirmed = false;
    }
    return false;
  }

  // 1. Copy ring buffer into vReal and compute the window mean (remove DC/gravity component)
  float mean = 0;
  for (size_t k = 0; k < FFT_N; k++) {
    size_t src = (ring_idx + k) % FFT_N;   // Start from the oldest sample
    vReal[k] = ring_buf[src];
    mean += vReal[k];
  }
  mean /= FFT_N;

  // 2. Subtract mean and apply Hanning window; clear vImag
  for (size_t k = 0; k < FFT_N; k++) {
    vReal[k] = (vReal[k] - mean) * hanning_win[k];
    vImag[k] = 0;
  }

  // 3. Perform FFT
  FFT.dcRemoval(vReal, FFT_N);
  FFT.windowing(vReal, FFT_N, FFT_WIN_TYP_HANN, FFT_FORWARD);  // Library-provided Hanning window (equivalent to above; applied as an additional precaution)
  FFT.compute(vReal, vImag, FFT_N, FFT_FORWARD);
  FFT.complexToMagnitude(vReal, vImag, FFT_N);

  // 4. Keep the first N/2 bins (vReal[0] is DC; vReal[1..N/2-1] contains the valid spectrum)
  for (size_t k = 0; k < FFT_N / 2; k++) {
    spectrum[k] = vReal[k];
  }

  // 5. Calculate energy in the target frequency band
  // bin = round(f * N / fs) = round(f * FFT_N / SAMPLE_RATE_HZ)
  size_t bin_low  = (size_t)(TREMOR_FREQ_LOW_HZ  * FFT_N / SAMPLE_RATE_HZ + 0.5f);
  size_t bin_high = (size_t)(TREMOR_FREQ_HIGH_HZ * FFT_N / SAMPLE_RATE_HZ + 0.5f);
  if (bin_low < 1) bin_low = 1;
  if (bin_high >= FFT_N / 2) bin_high = FFT_N / 2 - 1;

  float bandEnergy = 0;
  float totalEnergy = 0;
  size_t peak_bin = 1;
  float peak_val = 0;

  for (size_t k = 1; k < FFT_N / 2; k++) {  // Skip DC (bin 0)
    float e = spectrum[k] * spectrum[k];
    totalEnergy += e;
    if (k >= bin_low && k <= bin_high) bandEnergy += e;
    if (spectrum[k] > peak_val) {
      peak_val = spectrum[k];
      peak_bin = k;
    }
  }

  float ratio = (totalEnergy > 1e-6f) ? (bandEnergy / totalEnergy) : 0;
  float peakFreq = peak_bin * SAMPLE_RATE_HZ / FFT_N;

  // 6. Apply two-threshold decision
  bool hit = (ratio > ratio_threshold)
          && (peakFreq >= TREMOR_FREQ_LOW_HZ && peakFreq <= TREMOR_FREQ_HIGH_HZ);

  // Confirm across consecutive windows
  if (hit) hit_count++;
  else     hit_count = 0;

  bool confirmed = (hit_count >= CONFIRM_WINDOWS);

  if (result) {
    result->ratio = ratio;
    result->peakFreq = peakFreq;
    result->bandEnergy = bandEnergy;
    result->totalEnergy = totalEnergy;
    result->tremorHit = hit;
    result->tremorConfirmed = confirmed;
  }

  // Clear the ring buffer so the next window starts fresh (nonoverlapping windows for simplicity)
  ring_count = 0;
  ring_idx = 0;

  return true;
}

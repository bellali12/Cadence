#include "audio_pdm.h"
#include <driver/i2s.h>
#include <math.h>

// I2S port: the microphone uses I2S0, independently of the amplifier on I2S1
#define PDM_I2S_PORT  I2S_NUM_0

// Number of samples per i2s_read call
#define CHUNK_SAMPLES  512

static bool pdm_initialized = false;

bool pdm_init() {
  if (pdm_initialized) {
    i2s_driver_uninstall(PDM_I2S_PORT);
    delay(50);
  }

  // PDM receive configuration
  // Note: with L/R connected to GND, use ONLY_RIGHT (ONLY_LEFT reads noise from the inactive channel)
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM),
    .sample_rate = PDM_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
    .communication_format = I2S_COMM_FORMAT_PCM,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 256,
    .use_apll = false,
  };

  // PDM pins: ws_io_num = CLK output; data_in_num = DATA input
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_PIN_NO_CHANGE,    // PDM does not use BCK
    .ws_io_num = PDM_CLK_PIN,           // GPIO15 = PDM CLK (2.048 MHz)
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = PDM_DATA_PIN,        // GPIO16 = PDM DATA
  };

  if (i2s_driver_install(PDM_I2S_PORT, &i2s_config, 0, NULL) != ESP_OK) {
    Serial.println("[PDM] driver install failed");
    return false;
  }
  if (i2s_set_pin(PDM_I2S_PORT, &pin_config) != ESP_OK) {
    Serial.println("[PDM] set pin failed");
    return false;
  }

  // PDM downsampling: CLK = sample_rate × 128 = 2.048 MHz
  i2s_set_pdm_rx_down_sample(PDM_I2S_PORT, I2S_PDM_DSR_16S);

  pdm_initialized = true;
  Serial.print("[PDM] LMD2718 init OK  CLK=GPIO");
  Serial.print(PDM_CLK_PIN);
  Serial.print(" DATA=GPIO");
  Serial.println(PDM_DATA_PIN);
  return true;
}

// Clamp to int16 range (prevent overflow when applying gain)
static inline int16_t clamp_i16(int32_t v) {
  if (v > 32767) return 32767;
  if (v < -32768) return -32768;
  return (int16_t)v;
}

size_t pdm_record(float seconds, int16_t* buf) {
  if (!pdm_initialized || !buf || seconds <= 0) return 0;

  size_t total_samples = (size_t)(seconds * PDM_SAMPLE_RATE);
  size_t got = 0;
  int16_t chunk[CHUNK_SAMPLES];

  Serial.print("[PDM] recording ");
  Serial.print(seconds, 1);
  Serial.print("s (");
  Serial.print(total_samples);
  Serial.println(" samples)...");

  uint32_t t0 = millis();
  size_t print_counter = 0;

  while (got < total_samples) {
    size_t want = CHUNK_SAMPLES;
    if (want > total_samples - got) want = total_samples - got;

    size_t bytes_read = 0;
    if (i2s_read(PDM_I2S_PORT, chunk, want * sizeof(int16_t),
                 &bytes_read, portMAX_DELAY) != ESP_OK || bytes_read == 0) {
      continue;
    }
    size_t samples_read = bytes_read / sizeof(int16_t);
    if (samples_read == 0) continue;

    // Apply gain and clamp to the int16 range
    int16_t peak = 0;
    for (size_t i = 0; i < samples_read; i++) {
      int16_t v = clamp_i16((int32_t)(chunk[i] * PDM_GAIN));  // Amplified value
      int16_t a = abs(v);
      if (a > peak) peak = a;
      buf[got + i] = v;
    }
    got += samples_read;

    // Print peak approximately every second (31 chunks × 512 ≈ 15,872 samples ≈ 1 s)
    print_counter++;
    if (print_counter % 31 == 0) {
      uint32_t elapsed = (millis() - t0) / 1000;
      Serial.print("[PDM] t=");
      Serial.print(elapsed);
      Serial.print("s peak=");
      Serial.println(peak);
    }
  }

  Serial.print("[PDM] done, ");
  Serial.print(got);
  Serial.println(" samples captured");
  return got;
}

uint16_t pdm_read_volume() {
  if (!pdm_initialized) return 0;

  int16_t buf[256];
  size_t bytes_read = 0;
  if (i2s_read(PDM_I2S_PORT, buf, sizeof(buf),
               &bytes_read, portMAX_DELAY) != ESP_OK || bytes_read == 0) {
    return 0;
  }
  size_t n = bytes_read / sizeof(int16_t);
  if (n == 0) return 0;

  uint64_t sum_sq = 0;
  for (size_t i = 0; i < n; i++) {
    sum_sq += (uint64_t)buf[i] * buf[i];
  }
  return (uint16_t)sqrt((double)sum_sq / n);
}

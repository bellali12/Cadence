#include "audio_amp.h"
#include <driver/i2s.h>

// I2S port: the amplifier uses I2S1, independently of the microphone on I2S0
#define AMP_I2S_PORT  I2S_NUM_1

// Number of samples per write (mono samples)
#define AMP_CHUNK_SAMPLES  256

// Stereo buffer (each mono sample is duplicated into two 16-bit L/R samples)
static int16_t stereo_buf[AMP_CHUNK_SAMPLES * 2];

static bool amp_initialized = false;

bool amp_init() {
  if (amp_initialized) {
    i2s_driver_uninstall(AMP_I2S_PORT);
    delay(50);
  }

  // Standard I2S transmit configuration
  i2s_config_t config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = AMP_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    // Stereo output: duplicate recorded samples into both L and R channels,
    // avoiding frame-slot alignment issues regardless of which channel the NS4168 uses
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 256,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0,
  };

  i2s_pin_config_t pins = {
    .bck_io_num = AMP_BCLK,
    .ws_io_num = AMP_LRC,
    .data_out_num = AMP_DIN,
    .data_in_num = I2S_PIN_NO_CHANGE,
  };

  if (i2s_driver_install(AMP_I2S_PORT, &config, 0, NULL) != ESP_OK) {
    Serial.println("[AMP] driver install failed");
    return false;
  }
  if (i2s_set_pin(AMP_I2S_PORT, &pins) != ESP_OK) {
    Serial.println("[AMP] set pin failed");
    return false;
  }

  amp_initialized = true;
  Serial.print("[AMP] NS4168 init OK  BCLK=GPIO");
  Serial.print(AMP_BCLK);
  Serial.print(" LRC=GPIO");
  Serial.print(AMP_LRC);
  Serial.print(" DIN=GPIO");
  Serial.println(AMP_DIN);
  return true;
}

size_t amp_play(const int16_t* data, size_t samples) {
  if (!amp_initialized || !data || samples == 0) return 0;

  Serial.print("[AMP] playing ");
  Serial.print(samples);
  Serial.print(" samples (");
  Serial.print((float)samples / AMP_SAMPLE_RATE, 2);
  Serial.println("s)...");

  size_t played = 0;
  while (played < samples) {
    size_t want = AMP_CHUNK_SAMPLES;
    if (want > samples - played) want = samples - played;

    // Duplicate each mono sample into L and R samples for stereo playback
    for (size_t i = 0; i < want; i++) {
      int16_t s = data[played + i];
      stereo_buf[i * 2]     = s;   // Left channel
      stereo_buf[i * 2 + 1] = s;   // Right channel
    }

    size_t bytes_written = 0;
    // Stereo: want mono samples = want*2 int16 values = want*4 bytes
    if (i2s_write(AMP_I2S_PORT, stereo_buf, want * 2 * sizeof(int16_t),
                  &bytes_written, portMAX_DELAY) != ESP_OK) {
      break;
    }
    played += bytes_written / (2 * sizeof(int16_t));   // Byte count / 4 = number of mono samples
  }

  // Clear the DMA buffer to prevent residual audio noise
  i2s_zero_dma_buffer(AMP_I2S_PORT);

  Serial.print("[AMP] done, ");
  Serial.print(played);
  Serial.println(" samples played");
  return played;
}

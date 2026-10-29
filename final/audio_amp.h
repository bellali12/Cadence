#ifndef AUDIO_AMP_H
#define AUDIO_AMP_H

#include <Arduino.h>
#include <stdint.h>

// ============ Pins (consistent with 硬件物料与引脚分配.md) ============
// NS4168 amplifier + LMD2718 speaker, I2S1 bus
// BTL output: neither speaker terminal is grounded
#define AMP_BCLK  41
#define AMP_LRC   42
#define AMP_DIN   2

// Match sample rate to the microphone
#define AMP_SAMPLE_RATE  16000

// ============ Module API ============

// Initialize amplifier (I2S_NUM_1, standard I2S TX)
bool amp_init();

// Play PCM data (16-bit mono; blocks until complete)
// samples: number of samples, not bytes
// Return number of samples actually written
size_t amp_play(const int16_t* data, size_t samples);

// === Nonblocking playback (used by final firmware state machine) ===
// Start playback and return immediately; main loop must call amp_loop() to advance
void amp_play_async(const int16_t* data, size_t samples);
// Call in main loop to write chunks to I2S; false means playback is complete
bool amp_loop();
// Whether audio is playing
bool amp_is_playing();
// Stop playback and clear DMA buffer
void amp_stop();

#endif // AUDIO_AMP_H

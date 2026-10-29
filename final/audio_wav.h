#ifndef AUDIO_WAV_H
#define AUDIO_WAV_H

#include <stddef.h>
#include <stdint.h>

// ============================================================
// WAV file playback module
// ============================================================
// Read WAV from LittleFS, parse RIFF/PCM header, load entire file into PSRAM buffer,
// then play via amp_play_async in audio_amp.h
//
// Required WAV format:
// PCM (uncompressed)
// 16 kHz sample rate (matches amplifier; other rates require adjusting I2S settings)
//   - 16-bit
// Mono preferred; for stereo, left channel is selected automatically
//
// Usage:
// Call in setup
// Trigger playback
// Advance playback in main loop
//   }

// Initialize LittleFS (mount filesystem)
// Return false if mounting fails (filesystem not uploaded or corrupted)
bool wav_init();

// Synchronous playback (blocks until complete); true on success
// Example path: "/remind.wav"
bool wav_play_sync(const char* path);

// === Nonblocking playback (recommended for state machine) ===
// Start playback and return immediately; main loop must call wav_loop()
// True on success; false if file missing or invalid format
bool wav_play_async(const char* path);

// Call in main loop to advance playback; false when complete or on error
bool wav_loop();

// Whether playback is active
bool wav_is_playing();

// Stop playback
void wav_stop();

#endif // AUDIO_WAV_H

#ifndef VOICE_REMIND_H
#define VOICE_REMIND_H

#include <stdint.h>
#include <Arduino.h>

// ============================================================
// Placeholder reminder tone (three beeps)
// ============================================================
// Can later be replaced with a C array converted from a real WAV recording ("Time to take your medication")
// Currently generates an 800 Hz square wave at runtime (0.4 s on + 0.2 s silence, three times; 1.8 s total)
// Replacement procedure:
// 1. Convert remind.wav to an int16_t array using Python:
//        python tools/wav_to_h.py remind.wav > voice_remind_real.h
// 2. Include voice_remind_real.h in main.cpp and play its PROGMEM array with amp_play

#define VOICE_SAMPLE_RATE   16000
// Generate a 1.8 s reminder tone (three 0.4 s beeps separated by 0.2 s silence)
#define VOICE_SAMPLES       (int)(VOICE_SAMPLE_RATE * 1.8f)   // 28800

// Placeholder buffer (generated at runtime, about 56 KB SRAM)
extern int16_t voice_remind_buf[VOICE_SAMPLES];

// Initialization (call once in setup)
void voice_remind_init();

// Get sample count
inline size_t voice_remind_samples() { return VOICE_SAMPLES; }

#endif // VOICE_REMIND_H

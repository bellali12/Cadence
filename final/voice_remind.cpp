#include "voice_remind.h"

// Placeholder reminder-tone buffer (56 KB SRAM, acceptable)
int16_t voice_remind_buf[VOICE_SAMPLES];

void voice_remind_init() {
    // Generate three beeps: 0.4 s on + 0.2 s silence
    // On segment: 800 Hz square wave, amplitude 6000/32767 (about -14 dB, gentle)
    const int sample_rate = VOICE_SAMPLE_RATE;
    const int freq = 800;
    const int amplitude = 6000;
    const int on_samples  = sample_rate * 0.4f;    // 6400
    const int off_samples = sample_rate * 0.2f;    // 3200
    const int period_samples = sample_rate / freq; // 20

    int idx = 0;
    for (int beep = 0; beep < 3; beep++) {
        // On segment: square wave
        for (int i = 0; i < on_samples && idx < VOICE_SAMPLES; i++, idx++) {
            int half = (i % period_samples) < (period_samples / 2) ? 1 : -1;
            // Envelope: fade in/out over the first and last 200 samples to prevent clicks
            int env = amplitude;
            if (i < 200) env = amplitude * i / 200;
            else if (i > on_samples - 200) env = amplitude * (on_samples - i) / 200;
            voice_remind_buf[idx] = (int16_t)(half * env);
        }
        // Silent segment
        for (int i = 0; i < off_samples && idx < VOICE_SAMPLES; i++, idx++) {
            voice_remind_buf[idx] = 0;
        }
    }
    // Zero-pad remaining samples
    while (idx < VOICE_SAMPLES) {
        voice_remind_buf[idx++] = 0;
    }
}

#include "audio_wav.h"
#include "audio_amp.h"
#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>

// ============================================================
// Implementation notes
// ============================================================
// 1. wav_init: mount LittleFS (automatically formatted on initial PlatformIO upload)
// 2. wav_load: open WAV and parse RIFF/WAVE/fmt/data headers
// 3. Read the entire data chunk into PSRAM (heap_caps_malloc with MALLOC_CAP_SPIRAM)
// 4. Play with amp_play_async (amplifier expands mono into stereo internally)
//
// WAV header layout (standard 44-byte PCM header):
//   0   "RIFF"
//   4   file_size - 8
//   8   "WAVE"
//   12  "fmt "
//   16  fmt_chunk_size (16 for PCM)
//   20  audio_format (1 = PCM)
//   22  num_channels (1=mono, 2=stereo)
//   24  sample_rate
//   28  byte_rate
//   32  block_align
//   34  bits_per_sample
//   36  "data"
//   40  data_size
// 44: PCM data

// PSRAM buffer (up to 10 s of 16 kHz / 16-bit / mono = 320 KB)
#define WAV_MAX_BYTES   (320 * 1024)

static int16_t* wav_buf = nullptr;       // PSRAM buffer
static size_t   wav_samples = 0;         // Sample count (mono)

// Read WAV header; return start offset of data chunk, or 0 on failure
static size_t parse_wav_header(File& f, uint16_t& channels, uint32_t& sample_rate,
                               uint16_t& bits_per_sample, size_t& data_size) {
    if (f.size() < 44) return 0;

    uint8_t hdr[44];
    if (f.read(hdr, 44) != 44) return 0;

    // Check RIFF/WAVE signatures
    if (memcmp(hdr + 0, "RIFF", 4) != 0) return 0;
    if (memcmp(hdr + 8, "WAVE", 4) != 0) return 0;
    if (memcmp(hdr + 12, "fmt ", 4) != 0) return 0;

    uint16_t audio_format = hdr[20] | (hdr[21] << 8);
    if (audio_format != 1) {
        Serial.println("[WAV] not PCM format");
        return 0;
    }

    channels       = hdr[22] | (hdr[23] << 8);
    sample_rate    = hdr[24] | (hdr[25] << 8) | (hdr[26] << 16) | (hdr[27] << 24);
    bits_per_sample = hdr[34] | (hdr[35] << 8);

    // Find data chunk starting at offset 36 (some WAV files contain additional chunks)
    size_t pos = 36;
    f.seek(36);
    while (pos < f.size() - 8) {
        uint8_t chunk[8];
        if (f.read(chunk, 8) != 8) return 0;
        uint32_t chunk_size = chunk[4] | (chunk[5] << 8) | (chunk[6] << 16) | (chunk[7] << 24);
        if (memcmp(chunk, "data", 4) == 0) {
            data_size = chunk_size;
            return pos + 8;
        }
        pos += 8 + chunk_size;
        f.seek(pos);
    }
    return 0;
}

bool wav_init() {
    Serial.println("[WAV] mounting LittleFS...");
    if (!LittleFS.begin(true)) {   // true = automatically format on first upload
        Serial.println("[WAV] LittleFS mount FAILED");
        return false;
    }
    size_t total = LittleFS.totalBytes();
    size_t used = LittleFS.usedBytes();
    Serial.print("[WAV] LittleFS mounted: total=");
    Serial.print(total / 1024);
    Serial.print("KB used=");
    Serial.print(used / 1024);
    Serial.println("KB");

    // List files in root directory (for debugging)
    Serial.println("[WAV] files in /:");
    File root = LittleFS.open("/");
    File file = root.openNextFile();
    while (file) {
        Serial.print("  ");
        Serial.print(file.name());
        Serial.print(" (");
        Serial.print(file.size());
        Serial.println(" bytes)");
        file = root.openNextFile();
    }
    return true;
}

// Load WAV file into PSRAM buffer
// Return true on success; wav_buf/wav_samples will be populated
static bool wav_load(const char* path) {
    File f = LittleFS.open(path, "r");
    if (!f) {
        Serial.print("[WAV] open failed: ");
        Serial.println(path);
        return false;
    }

    uint16_t channels, bits;
    uint32_t sample_rate;
    size_t   data_size;
    size_t   data_offset = parse_wav_header(f, channels, sample_rate, bits, data_size);
    if (data_offset == 0) {
        Serial.print("[WAV] invalid WAV: ");
        Serial.println(path);
        f.close();
        return false;
    }

    Serial.print("[WAV] ");
    Serial.print(path);
    Serial.print(" rate=");
    Serial.print(sample_rate);
    Serial.print(" ch=");
    Serial.print(channels);
    Serial.print(" bits=");
    Serial.print(bits);
    Serial.print(" data=");
    Serial.print(data_size);
    Serial.println(" bytes");

    if (sample_rate != AMP_SAMPLE_RATE) {
        Serial.print("[WAV] WARN sample rate mismatch (expect ");
        Serial.print(AMP_SAMPLE_RATE);
        Serial.print("), playback pitch/speed will be off");
    }
    if (bits != 16) {
        Serial.println("[WAV] ERR only 16-bit supported");
        f.close();
        return false;
    }

    // Calculate sample count (mono)
    size_t bytes_to_read = data_size;
    if (bytes_to_read > WAV_MAX_BYTES) bytes_to_read = WAV_MAX_BYTES;

    // For stereo WAV, keep only the left channel (first two bytes of each four-byte frame)
    bool need_downmix = (channels == 2);
    size_t mono_bytes = need_downmix ? bytes_to_read / 2 : bytes_to_read;

    // Allocate PSRAM
    if (wav_buf) {
        free(wav_buf);
        wav_buf = nullptr;
    }
    wav_buf = (int16_t*)heap_caps_malloc(mono_bytes, MALLOC_CAP_SPIRAM);
    if (!wav_buf) {
        Serial.println("[WAV] PSRAM alloc failed");
        f.close();
        return false;
    }

    // Read PCM data
    f.seek(data_offset);
    if (need_downmix) {
        // Stereo: four bytes per frame (L16 + R16); use L
        uint8_t frame[4];
        size_t mono_idx = 0;
        size_t frames = bytes_to_read / 4;
        for (size_t i = 0; i < frames; i++) {
            if (f.read(frame, 4) != 4) break;
            wav_buf[mono_idx++] = (int16_t)(frame[0] | (frame[1] << 8));
        }
        wav_samples = mono_idx;
    } else {
        // Mono: read directly
        size_t read = f.read((uint8_t*)wav_buf, mono_bytes);
        wav_samples = read / 2;
    }
    f.close();

    Serial.print("[WAV] loaded ");
    Serial.print(wav_samples);
    Serial.print(" samples (");
    Serial.print((float)wav_samples / AMP_SAMPLE_RATE, 2);
    Serial.println("s)");
    return true;
}

bool wav_play_sync(const char* path) {
    if (!wav_load(path)) return false;
    size_t played = amp_play(wav_buf, wav_samples);  // Blocking playback
    free(wav_buf);
    wav_buf = nullptr;
    return played > 0;
}

bool wav_play_async(const char* path) {
    if (!wav_load(path)) return false;
    amp_play_async(wav_buf, wav_samples);
    return true;
}

bool wav_loop() {
    return amp_loop();
}

bool wav_is_playing() {
    return amp_is_playing();
}

void wav_stop() {
    amp_stop();
    if (wav_buf) {
        free(wav_buf);
        wav_buf = nullptr;
    }
}

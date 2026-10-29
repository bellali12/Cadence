Place WAV files in this directory, then upload them to the ESP32-S3 LittleFS partition using `pio run -t uploadfs`.

==== WAV File Requirements ====

Format: PCM (uncompressed WAV, not MP3)
Sample rate: 16000 Hz (matches amplifier configuration)
Bit depth: 16-bit
Channels: Mono preferred; for stereo, the left channel is selected automatically

==== Recognized Files ====

/remind.wav     - Spoken medication reminder (e.g., "It's time to take your medication"), 3-5 seconds
                  Falls back to code-generated triple-beep square wave if missing
/encourage.wav  - Encouraging message after taking medication (e.g., "Great job taking your medication on time"), 2-3 seconds
                  Falls back to a short square wave if missing

==== How to Create WAV Files ====

Method 1: Online text-to-speech (TTS)
  - Generate speech using Alibaba Cloud, iFlytek, or Google TTS
  - Export as 16 kHz / 16-bit / mono WAV

Method 2: Record audio
  - Record on a phone (16 kHz or 44.1 kHz sample rate)
  - Convert in Audacity to 16 kHz / 16-bit / mono:
      File > Export > Export as WAV
      Choose "WAV (Microsoft) 16-bit PCM"
      Set sample rate to 16000

Method 3: Convert existing audio
  ffmpeg -i input.mp3 -ar 16000 -ac 1 -ab 16 -f wav remind.wav

==== Upload Command ====

pio run -t uploadfs

(Before the first upload, run `pio run` to build the filesystem image.)

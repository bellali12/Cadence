/*
 * Parkinson's disease tremor detection example
 *
 * Function: acquire three-axis acceleration with the MPU6050 -> compute magnitude -> 256-point FFT
 * -> calculate the proportion of spectral energy in the 4-6 Hz band -> alert after three consecutive positive windows
 *
 * See the technical report in RESEARCH.md.
 *
 * Serial commands (115200 baud, native USB CDC):
 * S           Start/stop detection (toggle)
 * C           Recalibrate gyroscope offsets
 * D           Dump the full spectrum once (0-12 Hz)
 * T <0.2-0.9> Adjust the energy-ratio threshold (e.g., T 0.5)
 * H           Help
 *
 * Pins: SDA=GPIO8, SCL=GPIO7 (finalized project pinout)
 */

#include <Arduino.h>
#include <Wire.h>
#include "MPU6050_light.h"
#include "tremor_detect.h"

// ============ Pins (finalized project pinout) ============
#define MPU_SDA_PIN   8
#define MPU_SCL_PIN   7

// Sampling interval: 10 ms = 100 Hz
#define SAMPLE_INTERVAL_MS  10

// MPU object
MPU6050 mpu(Wire);

// State
static bool detecting = false;
static String input_buf;

// ============ Help ============
static void show_help() {
  Serial.println();
  Serial.println("==========================================");
  Serial.println("  Parkinson Tremor Detection");
  Serial.println("  ESP32-S3 | MPU6050 | arduinoFFT 256pt");
  Serial.print  ("  Target band: 4-6 Hz | thresh: ");
  Serial.print(tremor_get_threshold(), 2);
  Serial.print(" | confirm windows: ");
  Serial.println(CONFIRM_WINDOWS);
  Serial.println("==========================================");
  Serial.println("Commands:");
  Serial.println("  S           Start/Stop detection");
  Serial.println("  C           Re-calibrate gyro offsets");
  Serial.println("  D           Dump spectrum (0-12Hz, one shot)");
  Serial.println("  T <0.2-0.9> Set ratio threshold");
  Serial.println("  H           Show this help");
  Serial.println();
}

// ============ Output ============
static void print_result(const TremorResult& r) {
  Serial.print("[DETECT] ratio=");
  Serial.print(r.ratio, 3);
  Serial.print(" peak=");
  Serial.print(r.peakFreq, 2);
  Serial.print("Hz bandE=");
  Serial.print(r.bandEnergy, 0);
  Serial.print(" totalE=");
  Serial.print(r.totalEnergy, 0);
  Serial.print(" hitCount=");
  Serial.print(r.tremorHit ? "+" : "-");
  Serial.print(" | ");
  if (r.tremorConfirmed) {
    Serial.println("*** TREMOR ***");
  } else {
    Serial.println("IDLE");
  }
}

// Dump the 0-12 Hz spectrum (for plotting on the host computer)
static void dump_spectrum() {
  const float* s = tremor_get_spectrum();
  // Bins 0-31 correspond to 0-12.5 Hz (bin frequency = fs/N * k = 0.39 Hz * k)
  Serial.println("--- SPECTRUM (0-12Hz) ---");
  Serial.println("freq_hz,magnitude");
  size_t max_bin = (size_t)(12.0f * FFT_N / SAMPLE_RATE_HZ + 0.5f);
  if (max_bin > FFT_N / 2) max_bin = FFT_N / 2;
  for (size_t k = 1; k <= max_bin; k++) {
    float freq = k * SAMPLE_RATE_HZ / FFT_N;
    Serial.print(freq, 2);
    Serial.print(',');
    Serial.println(s[k], 2);
  }
  Serial.println("--- END ---");
}

// ============ Command handling ============
static void process_cmd(const String& cmd) {
  String s = cmd; s.trim(); s.toUpperCase();
  if (s.length() == 0) return;

  if (s == "S") {
    detecting = !detecting;
    Serial.print("[INFO] Detection: ");
    Serial.println(detecting ? "STARTED" : "STOPPED");
    if (detecting) tremor_init();
    return;
  }
  if (s == "C") {
    Serial.println("[INFO] Re-calibrating, keep IMU still...");
    // Compatible with MPU6050_light versions: calcOffsets(bool accel, bool print)
    // calcGyroOffsets(true) is incompatible with the older v1.0 API (no-argument version)
    mpu.calcOffsets(false, true);
    Serial.println("[INFO] Calibration done.");
    return;
  }
  if (s == "D") {
    dump_spectrum();
    return;
  }
  if (s == "H") { show_help(); return; }

  if (s.startsWith("T")) {
    int sp = s.indexOf(' ');
    if (sp < 0) {
      Serial.println("[INFO] current threshold: " + String(tremor_get_threshold(), 2));
      Serial.println("[INFO] usage: T <0.2-0.9>");
      return;
    }
    float v = s.substring(sp + 1).toFloat();
    if (v < 0.2f || v > 0.9f) {
      Serial.println("[ERR] threshold must be in [0.2, 0.9]");
      return;
    }
    tremor_set_threshold(v);
    Serial.print("[INFO] threshold set to ");
    Serial.println(tremor_get_threshold(), 2);
    return;
  }

  Serial.print("[ERR] Unknown: ");
  Serial.println(s);
}

// ============ Entry point ============
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("############################################");
  Serial.println("#   Parkinson Tremor Detection");
  Serial.println("#   ESP32-S3 | MPU6050 | arduinoFFT 256pt");
  Serial.println("############################################");

  // Initialize I2C (specify ESP32-S3 pins)
  Wire.begin(MPU_SDA_PIN, MPU_SCL_PIN);
  Wire.setClock(400000);

  byte status = mpu.begin();
  Serial.print("[INFO] mpu.begin() status = ");
  Serial.println(status);
  if (status != 0) {
    Serial.println("[ERR] MPU6050 not found! Check wiring.");
    Serial.println("       SDA->GPIO8  SCL->GPIO7  AD0->GND  VCC->3.3V");
    while (true) { delay(1000); }
  }
  Serial.println("[INFO] MPU6050 init OK");

  // Calibrate while stationary (gyroscope offset; not needed for FFT but stabilizes yaw)
  Serial.println("[INFO] === CALIBRATION ===");
  Serial.println("[INFO] Keep the board still...");
  // Compatible with MPU6050_light versions: calcOffsets(bool accel, bool print)
  mpu.calcOffsets(false, true);
  Serial.println("[INFO] Calibration done.");

  // Initialize detector (build Hanning window and clear buffers)
  tremor_init();

  show_help();
  Serial.println("[INFO] Press 'S' to start detection.");
}

// ============ Main loop ============
// Two scheduled tasks:
// (a) Feed acceleration samples at 100 Hz
// (b) Run FFT and output results once the buffer fills (every 2.56 seconds)
void loop() {
  static uint32_t last_sample = 0;
  uint32_t now = millis();

  // Sample at 100 Hz
  if (detecting && (now - last_sample >= SAMPLE_INTERVAL_MS)) {
    last_sample = now;
    mpu.update();
    tremor_feed_sample(mpu.getAccX(), mpu.getAccY(), mpu.getAccZ());
  }

  // The detector analyzes automatically once its internal ring buffer is full
  if (detecting) {
    TremorResult r;
    if (tremor_analyze(&r)) {
      print_result(r);
    }
  }

  // Serial command handling
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (input_buf.length() > 0) {
        Serial.print("> ");
        Serial.println(input_buf);
        process_cmd(input_buf);
        input_buf = "";
      }
    } else {
      input_buf += c;
    }
  }

  delay(1);
}

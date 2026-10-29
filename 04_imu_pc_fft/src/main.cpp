/*
 * MPU6050 raw-data acquisition + PC-side FFT analysis (firmware)
 *
 * Goal: sample raw six-axis IMU data at 100 Hz and transmit ASCII CSV over serial when Record is requested.
 * The PC application (tools/imu_fft_gui.py) receives the data, performs FFT, and visualizes the results.
 *
 * Protocol (115200 baud, native USB CDC; one record per line):
 * $READY                         Device ready (sent once at startup)
 * $START,<rate_hz>               Start acquisition (sent on 'S'; rate fixed at 100)
 * $IMU,<t_ms>,<ax>,<ay>,<az>,<gx>,<gy>,<gz>   Sample (streamed after START)
 * $STOP,<count>                  Stop acquisition (sent on next 'S'; count = samples in this session)
 *
 * Units:
 * ax/ay/az = g (acceleration)
 * gx/gy/gz = dps (angular velocity)
 * t_ms     = milliseconds since boot (used by the PC to recalculate the actual sampling rate)
 *
 * Serial commands (sent by PC or Serial Monitor; newline terminated):
 * S   Toggle acquisition (start/stop)
 * C   Recalibrate gyroscope offsets (keep stationary for several seconds)
 * H   Show help
 *
 * Pins (actual wiring, not the documented GPIO8/7 pinout):
 *   SDA = GPIO18
 *   SCL = GPIO17
 * AD0 -> GND (I2C address 0x68)
 *   VCC → 3.3V
 *
 * Library: rfetick/MPU6050_light @ ^1.1.0 (older calcOffsets API; check arguments)
 */

#include <Arduino.h>
#include <Wire.h>
#include "MPU6050_light.h"

// ============ Pins ============
#define MPU_SDA_PIN   8
#define MPU_SCL_PIN   7

// ============ Sampling parameters ============
#define SAMPLE_INTERVAL_MS  10      // 100 Hz
#define SAMPLE_RATE_HZ      100

// ============ Protocol tags ============
static const char* TAG_READY = "$READY";
static const char* TAG_START = "$START";
static const char* TAG_STOP  = "$STOP";
static const char* TAG_IMU   = "$IMU";

// MPU6050_light object
MPU6050 mpu(Wire);

// Acquisition state
static bool streaming = false;
static uint32_t sample_count = 0;     // Total samples collected since the current START
static uint32_t last_sample = 0;

// Serial command buffer
static String input_buf;

// ============ Help ============
static void show_help() {
  Serial.println();
  Serial.println("=====================================");
  Serial.println("  IMU + PC FFT (04_imu_pc_fft)");
  Serial.println("  ESP32-S3 | SDA=GPIO18 SCL=GPIO17");
  Serial.println("  Sample: 100Hz, 6-axis raw");
  Serial.println("=====================================");
  Serial.println("Commands:");
  Serial.println("  S   Toggle streaming (start/stop)");
  Serial.println("  C   Re-calibrate gyro offsets");
  Serial.println("  H   Show this help");
  Serial.println();
  Serial.println("Protocol:");
  Serial.println("  $READY                   (once on boot)");
  Serial.println("  $START,<rate_hz>         (on start)");
  Serial.println("  $IMU,<t_ms>,ax,ay,az,gx,gy,gz");
  Serial.println("  $STOP,<count>            (on stop)");
  Serial.println();
}

// ============ Acquisition control ============
static void start_streaming() {
  if (streaming) return;
  streaming = true;
  sample_count = 0;
  last_sample = millis();
  // Notify PC: acquisition started and actual sample rate
  Serial.print(TAG_START);
  Serial.print(',');
  Serial.println(SAMPLE_RATE_HZ);
}

static void stop_streaming() {
  if (!streaming) return;
  streaming = false;
  // Notify PC: acquisition ended and sample count
  Serial.print(TAG_STOP);
  Serial.print(',');
  Serial.println(sample_count);
}

// Output one sample (called only while streaming)
static void emit_sample() {
  mpu.update();
  sample_count++;
  Serial.print(TAG_IMU);
  Serial.print(','); Serial.print(millis());
  Serial.print(','); Serial.print(mpu.getAccX(), 4);
  Serial.print(','); Serial.print(mpu.getAccY(), 4);
  Serial.print(','); Serial.print(mpu.getAccZ(), 4);
  Serial.print(','); Serial.print(mpu.getGyroX(), 3);
  Serial.print(','); Serial.print(mpu.getGyroY(), 3);
  Serial.print(','); Serial.println(mpu.getGyroZ(), 3);
}

// ============ Command handling ============
static void process_cmd(const String& cmd) {
  String s = cmd;
  s.trim();
  s.toUpperCase();
  if (s.length() == 0) return;

  if (s == "S") {
    if (streaming) stop_streaming();
    else           start_streaming();
    return;
  }
  if (s == "C") {
    // Stop acquisition during calibration to avoid I2C/Serial interference
    bool was_streaming = streaming;
    if (was_streaming) stop_streaming();
    Serial.println("[INFO] Re-calibrating, keep IMU still...");
    mpu.calcOffsets(false, true);  // false = do not calibrate accelerometer; true = print progress
    Serial.println("[INFO] Calibration done.");
    return;
  }
  if (s == "H") { show_help(); return; }

  Serial.print("[ERR] Unknown command: ");
  Serial.println(s);
}

// ============ Entry point ============
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("#####################################");
  Serial.println("#  IMU + PC FFT (04_imu_pc_fft)");
  Serial.println("#  ESP32-S3 | SDA=GPIO18 SCL=GPIO17");
  Serial.println("#  100Hz 6-axis raw -> PC does FFT");
  Serial.println("#####################################");

  Wire.begin(MPU_SDA_PIN, MPU_SCL_PIN);
  Wire.setClock(400000);

  byte status = mpu.begin();
  Serial.print("[INFO] mpu.begin() status = ");
  Serial.println(status);
  if (status != 0) {
    Serial.println("[ERR] MPU6050 not found! Check wiring:");
    Serial.println("       SDA->GPIO18  SCL->GPIO17  AD0->GND  VCC->3.3V");
    Serial.println("[ERR] Halting.");
    while (true) { delay(1000); }
  }
  Serial.println("[INFO] MPU6050 init OK");

  // Calibrate gyroscope offsets while stationary
  Serial.println("[INFO] === CALIBRATION ===");
  Serial.println("[INFO] Keep the board still...");
  mpu.calcOffsets(false, true);
  Serial.println("[INFO] Calibration done.");

  show_help();
  Serial.println(TAG_READY);
}

void loop() {
  uint32_t now = millis();

  // Sample at 100 Hz (output only while acquisition is active)
  if (streaming && (now - last_sample >= SAMPLE_INTERVAL_MS)) {
    last_sample = now;
    emit_sample();
  }

  // Handle serial commands
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (input_buf.length() > 0) {
        process_cmd(input_buf);
        input_buf = "";
      }
    } else {
      input_buf += c;
    }
  }

  delay(1);
}

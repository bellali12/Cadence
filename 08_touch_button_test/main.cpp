/*
 * Dual capacitive touch-button test (T1 + T4)
 *
 * Goal: verify that the ESP32-S3 native touch interface reads GPIO1/GPIO4 correctly,
 * providing an independent test of the final firmware's touch-button logic.
 *
 * Three core functions:
 * 1. Raw viewer (R command): print raw touchRead values in real time to tune thresholds
 * 2. Event monitor (M command, default): print press/release events with 300 ms debounce
 * 3. Baseline calibration (C command): automatic startup and on-demand calibration
 *
 * Serial commands (115200 baud, native USB CDC):
 * R       Raw viewer (print T1/T4 values every 100 ms)
 * M       Event monitor (default)
 * C       Recalibrate baseline (do not touch pads; collect 32 samples)
 * T       Toggle threshold source: auto (baseline - 12) / fixed (30, as in final firmware)
 * H       Help
 *
 * Pin reference: ../../硬件物料与引脚分配.md, Section 3.5
 * T1 = GPIO1 (confirmation button)
 * T4 = GPIO4 (box-opening button)
 *
 * Corresponding final firmware: touch_pressed() in ../final/src/main.cpp
 */

#include <Arduino.h>
#include "touch_button.h"

// ============ Modes ============
typedef enum {
    MODE_MONITOR = 0,   // Default
    MODE_RAW           // Enter with R command
} WorkMode;

static WorkMode current_mode = MODE_MONITOR;

// ============ Help ============
static void show_help() {
  Serial.println();
  Serial.println("=====================================");
  Serial.println("  Touch Button Test (T1 + T4)");
  Serial.println("  ESP32-S3 | T1=GPIO1  T4=GPIO4");
  Serial.print  ("  Debounce: ");
  Serial.print(TOUCH_DEBOUNCE_MS);
  Serial.println("ms (edge-triggered)");
  Serial.println("=====================================");
  Serial.println("Commands:");
  Serial.println("  R       Raw viewer (print touchRead every 100ms)");
  Serial.println("  M       Monitor events (default)");
  Serial.println("  C       Re-calibrate baseline (keep hands off)");
  Serial.println("  T       Toggle threshold source: auto <-> fixed(30)");
  Serial.println("  H       Show this help");
  Serial.println();
  Serial.println("Mode: MONITOR = print [PRESS]/[RELEASE] events");
  Serial.println("      RAW     = print raw values for threshold tuning");
  Serial.println();
}

// ============ Raw viewer ============
static void run_raw_mode() {
  Serial.println("[INFO] === RAW MODE (press M to return) ===");
  Serial.println("[INFO] Tip: value drops below baseline when touched");
  Serial.println("[INFO] Observe idle vs touched values to pick threshold");
  Serial.println();
  Serial.println("T1_raw   T4_raw   | T1_thr  T4_thr  | mode");

  while (true) {
    int v1 = touch_read_raw(TOUCH_CONFIRM_PIN);
    int v4 = touch_read_raw(TOUCH_BOXOPEN_PIN);
    int t1 = touch_get_threshold_t1();
    int t4 = touch_get_threshold_t4();

    Serial.print(v1);
    Serial.print(v1 < 1000 ? "   " : "  ");
    Serial.print(v4);
    Serial.print(v4 < 1000 ? "   " : "  ");
    Serial.print(" | ");
    Serial.print(t1);
    Serial.print(t1 < 1000 ? "   " : "  ");
    Serial.print(t4);
    Serial.print(t4 < 1000 ? "   " : "  ");
    Serial.print(" | ");
    Serial.println(touch_get_threshold_mode() == THRESH_AUTO ? "auto" : "fixed");

    // Exit Raw mode when serial input is received
    if (Serial.available()) {
      Serial.println("[INFO] Exit RAW mode");
      // Consume this character so the main loop does not immediately process it as a command
      while (Serial.available()) Serial.read();
      current_mode = MODE_MONITOR;
      return;
    }
    delay(100);
  }
}

// ============ Monitor mode ============
static const char* pin_name(int pin) {
  if (pin == TOUCH_CONFIRM_PIN) return "T1";
  if (pin == TOUCH_BOXOPEN_PIN) return "T4";
  return "??";
}

static void run_monitor_tick() {
  // Check events for each channel
  int pins[2] = { TOUCH_CONFIRM_PIN, TOUCH_BOXOPEN_PIN };
  for (int i = 0; i < 2; i++) {
    int pin = pins[i];
    BtnEvent ev = touch_read_event(pin);
    if (ev == BTN_EVENT_NONE) continue;

    int v = touch_read_raw(pin);   // Read current value once for printing
    if (ev == BTN_EVENT_PRESS) {
      Serial.print("[PRESS]   ");
      Serial.print(pin_name(pin));
      Serial.print(" val=");
      Serial.println(v);
    } else if (ev == BTN_EVENT_RELEASE) {
      Serial.print("[RELEASE] ");
      Serial.print(pin_name(pin));
      Serial.print(" val=");
      Serial.println(v);
    }
  }
}

// ============ Command handling ============
static String input_buf = "";

static void process_cmd(const String& cmd) {
  String s = cmd;
  s.trim();
  s.toUpperCase();
  if (s.length() == 0) return;

  if (s == "R") {
    current_mode = MODE_RAW;
    run_raw_mode();
    return;
  }
  if (s == "M") {
    current_mode = MODE_MONITOR;
    Serial.println("[INFO] Mode: MONITOR");
    return;
  }
  if (s == "C") {
    // Clear state before recalibration to avoid interference from a pressed button
    touch_begin();
    touch_calibrate();
    Serial.print("[INFO] New threshold: T1=");
    Serial.print(touch_get_threshold_t1());
    Serial.print("  T4=");
    Serial.println(touch_get_threshold_t4());
    return;
  }
  if (s == "T") {
    ThresholdMode m = touch_get_threshold_mode();
    touch_set_threshold_mode(m == THRESH_AUTO ? THRESH_FIXED : THRESH_AUTO);
    Serial.print("[INFO] Threshold source: ");
    Serial.println(touch_get_threshold_mode() == THRESH_AUTO ? "auto" : "fixed");
    Serial.print("[INFO]   T1 thr=");
    Serial.print(touch_get_threshold_t1());
    Serial.print("  T4 thr=");
    Serial.println(touch_get_threshold_t4());
    return;
  }
  if (s == "H") {
    show_help();
    return;
  }

  Serial.print("[ERR] Unknown: ");
  Serial.println(s);
  Serial.println("[INFO] Press H for help");
}

// ============ Entry point ============
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("#####################################");
  Serial.println("#  Touch Button Test (T1 + T4)");
  Serial.println("#  ESP32-S3 | T1=GPIO1  T4=GPIO4");
  Serial.println("#####################################");

  touch_begin();

  // Automatically calibrate baseline at startup (do not touch the pads)
  Serial.println("[INFO] === CALIBRATION (power-on) ===");
  Serial.println("[INFO] Keep hands off the touch pads...");
  touch_calibrate();

  show_help();
  Serial.print("[INFO] Mode: MONITOR  Threshold source: ");
  Serial.println(touch_get_threshold_mode() == THRESH_AUTO ? "auto" : "fixed");
  Serial.print("[INFO]   T1 thr=");
  Serial.print(touch_get_threshold_t1());
  Serial.print("  T4 thr=");
  Serial.println(touch_get_threshold_t4());
  Serial.println("[INFO] Ready.");
}

void loop() {
  // 1) Continuously check events in Monitor mode
  if (current_mode == MODE_MONITOR) {
    run_monitor_tick();
  }

  // 2) Serial commands (Raw mode handles input internally)
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

  delay(5);
}

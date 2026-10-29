/*
 * GDEM0097F51 color e-paper display test (ESP32-S3)
 *
 * Goal: verify that the ESP32-S3 can drive a 0.97-inch four-color e-paper display (black/white/red/yellow; JD79661AA controller),
 * with serial commands for individual page tests and an optional automatic demo cycle.
 *
 * Library source: ../../screen_arduino/ (official example adapted for ESP32-S3, originally unmodified)
 *
 * Pins (tested screen_arduino wiring; differs from the draft hardware pinout document):
 * SCK  = GPIO12 (hardware SPI)
 * MOSI = GPIO11 (hardware SPI)
 *   CS   = GPIO10
 *   DC   = GPIO9
 *   RST  = GPIO13
 *   BUSY = GPIO14
 * BS -> GND (four-wire SPI mode)
 * VCC -> 3.3V (never connect 5V)
 *
 * Serial commands (115200 baud; single character followed by newline):
 * 1   Show built-in image (gImage_1); normal refresh ~16 s
 * 2   All white
 * 3   All red
 * 4   All yellow
 * 5   All black
 * F   Show image with fast refresh (~12 s)
 * A   Toggle automatic cycling (described here as default on)
 * H   Show help
 *
 * Safety notes:
 * Call EPD_sleep() after each refresh (as in the demo)
 * If unused for a long period, send '2' to show white and reduce ghosting (refresh at least once every 24 h)
 */

#include <Arduino.h>
#include <SPI.h>
#include "Display_EPD_W21_spi.h"
#include "Display_EPD_W21.h"
#include "Ap_29demo.h"

// ============ Pins (tested screen_arduino wiring) ============
//   SCK=GPIO12  MOSI=GPIO11  CS=GPIO10  DC=GPIO9  RST=GPIO13  BUSY=GPIO14
#define EPD_BUSY_PIN   14
#define EPD_RST_PIN    13
#define EPD_DC_PIN      9
#define EPD_CS_PIN     10
#define EPD_SCK_PIN    12
#define EPD_MOSI_PIN   11

// ============ Automatic cycling parameters ============
#define AUTO_CYCLE_INTERVAL_MS  20000UL  // Delay between pages: 20 seconds (excluding refresh time)

// Automatic sequence: image -> white -> red -> yellow -> black -> repeat
static const int AUTO_SEQUENCE[] = {1, 2, 3, 4, 5};
static const int AUTO_SEQ_LEN = sizeof(AUTO_SEQUENCE) / sizeof(AUTO_SEQUENCE[0]);

// ============ State ============
static bool auto_cycle = false;           // Automatic cycling disabled by default to avoid hangs during debugging
static int auto_index = 0;                // Current automatic sequence index
static uint32_t last_page_done_ms = 0;    // Timestamp of the previous completed refresh
static String input_buf;

// ============ Help ============
static void show_help() {
  Serial.println();
  Serial.println("=====================================");
  Serial.println("  GDEM0097F51 4-color EPD Test");
  Serial.println("  ESP32-S3 | 88x184, JD79661AA");
  Serial.println("  SCK=12 MOSI=11 CS=10 DC=9 RST=13 BUSY=14");
  Serial.println("=====================================");
  Serial.println("Commands (single char + newline):");
  Serial.println("  1   Show image gImage_1   (~16-25s)");
  Serial.println("  2   All white");
  Serial.println("  3   All red");
  Serial.println("  4   All yellow");
  Serial.println("  5   All black");
  Serial.println("  F   Fast refresh image   (~12s)");
  Serial.println("  A   Toggle auto-cycle (now: ON)");
  Serial.println("  H   Show this help");
  Serial.println();
  Serial.println("NOTE: each full refresh takes ~16-25s.");
  Serial.println("      flicker during refresh is normal.");
  Serial.println();
}

// ============ Display one page ============
// page: 1=image, 2=white, 3=red, 4=yellow, 5=black
// fast=true uses EPD_init_Fast instead of EPD_init (only relevant for page=1)
static void show_page(int page, bool fast = false) {
  const char* name = "?";
  switch (page) {
    case 1: name = "image gImage_1"; break;
    case 2: name = "all white"; break;
    case 3: name = "all red"; break;
    case 4: name = "all yellow"; break;
    case 5: name = "all black"; break;
  }

  Serial.print("[INFO] Refreshing -> ");
  Serial.print(name);
  if (fast) Serial.print(" (FAST mode)");
  Serial.println(" ... (~16-25s, please wait)");

  uint32_t t0 = millis();

  // Initialize (required before each full-screen refresh)
  if (fast && page == 1) {
    EPD_init_Fast();
  } else {
    EPD_init();
  }

  // Transfer data and trigger refresh
  switch (page) {
    case 1: PIC_display(gImage_1);   break;
    case 2: Display_All_White();     break;
    case 3: Display_All_Red();       break;
    case 4: Display_All_Yellow();    break;
    case 5: Display_All_Black();     break;
    default:
      Serial.println("[ERR] unknown page");
      return;
  }

  // Note: EPD_DeepSleep() is no longer called here.
  // Debugging showed that hardware RST does not reliably wake the display from deep sleep (BUSY never goes high),
  // causing the second refresh to fail. Send only POF (0x02, power off) instead of entering deep sleep (0x07),
  // so the display controller remains in standby and the next initialization does not require a deep-sleep wakeup.
  // Tradeoff: slightly higher display power consumption, acceptable during debugging.
  // TODO: Before production, verify RST wiring (GPIO13 <-> display RES) and restore EPD_DeepSleep() once fixed.
  EPD_W21_WriteCMD(0x02);   // POF power off
  EPD_W21_WriteDATA(0x00);
  delay(50);

  uint32_t dt = millis() - t0;
  Serial.print("[INFO] Done in ");
  Serial.print(dt / 1000.0, 1);
  Serial.println("s");
}

// ============ Command handling ============
static void process_cmd(const String& cmd) {
  String s = cmd;
  s.trim();
  if (s.length() == 0) return;

  // Take the first character and convert to uppercase
  char c = s.charAt(0);
  if (c >= 'a' && c <= 'z') c -= 32;

  switch (c) {
    case '1': show_page(1);       break;
    case '2': show_page(2);       break;
    case '3': show_page(3);       break;
    case '4': show_page(4);       break;
    case '5': show_page(5);       break;
    case 'F': show_page(1, true); break;
    case 'A':
      auto_cycle = !auto_cycle;
      Serial.print("[INFO] Auto-cycle: ");
      Serial.println(auto_cycle ? "ON" : "OFF");
      if (auto_cycle) {
        // Reset cycle position and timer
        auto_index = 0;
        last_page_done_ms = millis();
      }
      break;
    case 'H': show_help();        break;
    default:
      Serial.print("[ERR] Unknown: ");
      Serial.println(s);
      Serial.println("[INFO] Press H for help");
      return;
  }

  // Reset automatic cycle timer after a manual command to avoid immediately triggering another refresh
  last_page_done_ms = millis();
}

// ============ Entry point ============
void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println();
  Serial.println("#####################################");
  Serial.println("#  GDEM0097F51 4-color EPD Test");
  Serial.println("#  ESP32-S3 | 88x184 JD79661AA");
  Serial.println("#  SCK=12 MOSI=11 CS=10 DC=9 RST=13 BUSY=14");
  Serial.println("#####################################");

  // Initialize pins (identical to screen_arduino.ino)
  pinMode(EPD_BUSY_PIN, INPUT);    // BUSY
  pinMode(EPD_RST_PIN,  OUTPUT);   // RST
  pinMode(EPD_DC_PIN,   OUTPUT);   // DC
  pinMode(EPD_CS_PIN,   OUTPUT);   // CS

  // ESP32-S3 custom SPI pins: SPI.begin(sck, miso, mosi, cs); pass -1 for unused MISO
  SPI.begin(EPD_SCK_PIN, -1, EPD_MOSI_PIN, EPD_CS_PIN);
  SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));

  Serial.println("[INFO] SPI ready @ 10MHz MODE0");
  show_help();

  Serial.println("[INFO] Auto-cycle: OFF (send 'A' to enable)");
  Serial.println();

  // Automatic cycling disabled; do not trigger immediately
  last_page_done_ms = millis();
}

void loop() {
  // 1) Process serial commands first (high priority; immediate response)
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

  // 2) Automatic cycling (nonblocking timer)
  if (auto_cycle && (millis() - last_page_done_ms >= AUTO_CYCLE_INTERVAL_MS)) {
    int page = AUTO_SEQUENCE[auto_index];
    show_page(page);
    auto_index = (auto_index + 1) % AUTO_SEQ_LEN;
    last_page_done_ms = millis();
  }

  delay(1);
}

/*
 * WS2812 LED test (9 LEDs, 3x3)
 *
 * Goal: verify that the ESP32-S3 can drive the WS2812 LED strip
 * and demonstrate multiple animations with serial control.
 *
 * Library: FastLED (automatically uses the RMT peripheral for accurate timing)
 *
 * Serial commands (115200 baud, native USB CDC):
 * 0-6     Select animation:
 * 0 = Off
 * 1 = Solid white (maximum-brightness self-test)
 * 2 = Scrolling rainbow
 * 3 = Breathing (red -> green -> blue cycle)
 * 4 = Chasing light (one LED with trailing fade)
 * 5 = Progressive fill (LEDs light up in sequence)
 * 6 = Three-color cycle (3 red, 3 green, 3 blue)
 * + / -   Increase/decrease brightness (8 levels)
 * H       Show help
 *
 * Pin source: ../../硬件物料与引脚分配.md
 * LED_DIN = GPIO6 (finalized)
 * VCC -> 5V (boost converter output)
 * GND -> GND (must share ground with ESP32-S3)
 *
 * Notes:
 * Add a 300-500 ohm series resistor before the first WS2812 DIN to reduce ringing (hardware document section 3.6)
 * Nine LEDs at full white brightness draw approximately 540 mA; ensure sufficient 5V power capacity
 */

#include <Arduino.h>
#include <FastLED.h>

// ============ Pins / LED count ============
#define LED_PIN       6
#define NUM_LEDS      9
#define BRIGHTNESS_STEPS  8    // Number of brightness levels

// Default FastLED brightness (0-255)
#define DEFAULT_BRIGHTNESS  60

CRGB leds[NUM_LEDS];

// ============ State ============
static uint8_t current_anim = 2;          // Default: rainbow
static uint8_t brightness_idx = 3;        // Default brightness level (medium)
static const uint8_t BRIGHTNESS_TABLE[BRIGHTNESS_STEPS] = {5, 20, 40, 80, 130, 180, 230, 255};
static String input_buf;

// ============ Help ============
static void show_help() {
  Serial.println();
  Serial.println("=====================================");
  Serial.println("  WS2812 Test (9 LEDs, 3x3)");
  Serial.println("  ESP32-S3 | DIN=GPIO6");
  Serial.println("=====================================");
  Serial.println("Commands:");
  Serial.println("  0   Off");
  Serial.println("  1   Solid white (self-test)");
  Serial.println("  2   Rainbow scroll  (default)");
  Serial.println("  3   Breathe R->G->B");
  Serial.println("  4   Comet chase (one pixel)");
  Serial.println("  5   Fill cascade");
  Serial.println("  6   3R + 3G + 3B");
  Serial.println("  +/- Brightness up/down");
  Serial.println("  H   Show this help");
  Serial.println();
}

static uint8_t current_brightness() {
  return BRIGHTNESS_TABLE[brightness_idx];
}

static void apply_brightness() {
  FastLED.setBrightness(current_brightness());
  Serial.print("[INFO] Brightness idx=");
  Serial.print(brightness_idx);
  Serial.print(" value=");
  Serial.println(current_brightness());
}

// ============ Animation implementations ============

// Turn all LEDs off
static void anim_off() {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
}

// Solid white self-test
static void anim_white() {
  fill_solid(leds, NUM_LEDS, CRGB::White);
}

// Scrolling rainbow (global hue offset)
static void anim_rainbow(uint8_t hue_offset) {
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CHSV((hue_offset + i * 28) & 0xFF, 255, 255);
  }
}

// Breathing: cycle through three colors
static void anim_breathe(uint8_t step) {
  // Cycle step 0-255 in three segments: red -> green -> blue -> red
  uint8_t phase = (step / 86) % 3;     // 0/1/2
  uint8_t t = (step % 86) * 3;          // Progress within segment (0-255)
  uint8_t brightness_val = (t < 128) ? t * 2 : (255 - t) * 2;  // Triangle-wave brightness: 0 -> 255 -> 0
  CRGB color = CRGB::Black;
  if      (phase == 0) color = CRGB(brightness_val, 0, 0);
  else if (phase == 1) color = CRGB(0, brightness_val, 0);
  else                 color = CRGB(0, 0, brightness_val);
  fill_solid(leds, NUM_LEDS, color);
}

// Chasing light: one bright LED, others dim
static void anim_comet(uint8_t step) {
  fadeToBlackBy(leds, NUM_LEDS, 80);    // Trailing fade effect
  int pos = step % NUM_LEDS;
  leds[pos] = CHSV((step * 16) & 0xFF, 255, 255);
}

// Progressive fill: light LEDs in order, then reset
static void anim_fill(uint8_t step) {
  int pos = step % (NUM_LEDS + 1);
  if (pos == 0) {
    fill_solid(leds, NUM_LEDS, CRGB::Black);
  } else {
    leds[pos - 1] = CHSV(((pos - 1) * 28) & 0xFF, 255, 255);
  }
}

// Three colors: 3 red, 3 green, 3 blue
static void anim_three_color() {
  for (int i = 0; i < NUM_LEDS; i++) {
    if      (i < 3) leds[i] = CRGB::Red;
    else if (i < 6) leds[i] = CRGB::Green;
    else            leds[i] = CRGB::Blue;
  }
}

// ============ Command handling ============
static void process_cmd(const String& cmd) {
  String s = cmd;
  s.trim();
  if (s.length() == 0) return;

  // Single-character commands
  char c = s.charAt(0);
  if (c >= '0' && c <= '6') {
    current_anim = c - '0';
    // Clear previous frame when changing animations to prevent residual colors
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
    Serial.print("[INFO] Animation -> ");
    Serial.println(current_anim);
    return;
  }
  if (c == '+') {
    if (brightness_idx < BRIGHTNESS_STEPS - 1) brightness_idx++;
    apply_brightness();
    return;
  }
  if (c == '-') {
    if (brightness_idx > 0) brightness_idx--;
    apply_brightness();
    return;
  }
  if (c == 'H' || c == 'h') { show_help(); return; }

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
  Serial.println("#  WS2812 Test (9 LEDs, 3x3)");
  Serial.println("#  ESP32-S3 | DIN=GPIO6");
  Serial.println("#####################################");

  // Initialize FastLED: WS2812B protocol, DIN=GPIO6
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(current_brightness());

  // Startup self-test: light each LED red in sequence to verify all nine work
  Serial.println("[INFO] === Self-test: sweep red ===");
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
  delay(300);
  for (int i = 0; i < NUM_LEDS; i++) {
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    leds[i] = CRGB::Red;
    FastLED.show();
    delay(120);
  }
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();

  show_help();
  Serial.print("[INFO] Default animation: ");
  Serial.println(current_anim);
  Serial.print("[INFO] Brightness idx=");
  Serial.print(brightness_idx);
  Serial.print(" value=");
  Serial.println(current_brightness());
}

void loop() {
  // Frame counter (each animation uses its own timing)
  static uint32_t last_frame = 0;
  static uint16_t frame = 0;
  const uint16_t FRAME_INTERVAL_MS = 30;   // ~33 fps

  uint32_t now = millis();
  if (now - last_frame >= FRAME_INTERVAL_MS) {
    last_frame = now;
    frame++;

    switch (current_anim) {
      case 0: anim_off();        break;
      case 1: anim_white();      break;
      case 2: anim_rainbow(frame * 4);     break;
      case 3: anim_breathe(frame);          break;  // ~256 frames / 33 fps = 7.7 s per color
      case 4: anim_comet(frame);            break;
      case 5: anim_fill(frame / 3);          break;  // Slow down
      case 6: anim_three_color();           break;
      default: anim_off();       break;
    }
    FastLED.show();
  }

  // Serial commands
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

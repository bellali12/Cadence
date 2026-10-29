/*
 * Smart medication case - final firmware
 *
 * Application: medication reminder device
 *
 * Operating cycle:
 * [NORMAL] Display remaining charge (4 h full); refresh every 10 min
 * When remaining time <= 20 min
 * [REMIND] Play spoken reminder and flash red LEDs
 * Touch button 1 (GPIO1/T1) to acknowledge medication reminder
 * [WAIT_OPEN] Stop audio and LEDs; wait for box opening
 * Touch button 2 (GPIO4/T4) to detect box opening
 * [TAKEN] Display sunflower encouragement, celebratory LEDs, and reset 4 h timer
 * After 30 s
 * [NORMAL] Show full-charge icon and start next cycle
 *
 * Serial commands (115200 baud; single character plus newline; debugging):
 * T   Trigger reminder immediately (skip waiting)
 * C   Simulate pressing the medication confirmation button
 * O   Simulate opening the medication box
 * R   Reset timer to full charge
 * S   Display current state
 * H   Help
 *
 * Pins (reference: 硬件物料与引脚分配.md)
 * E-paper SPI: BUSY=14 RST=13 DC=9 CS=10 SCK=12 MOSI=11
 * Amplifier I2S: BCLK=41 LRC=42 DIN=2
 * WS2812: DIN=6 (9 LEDs)
 * Touch button 1: GPIO1 (T1, confirm medication)
 * Touch button 2: GPIO4 (T4, box opening)
 */

#include <Arduino.h>
#include <SPI.h>
#include <FastLED.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <time.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include "MPU6050_light.h"
#include "tremor/tremor_detect.h"

// ============ Server reporting configuration (overridable by platformio.ini build_flags) ============
#ifndef PC_SERVER_IP
#define PC_SERVER_IP   "192.168.1.100"
#endif
#ifndef PC_SERVER_PORT
#define PC_SERVER_PORT 5000
#endif
#ifndef WIFI_SSID
#define WIFI_SSID      "YOUR_WIFI_NAME"
#endif
#ifndef WIFI_PASS
#define WIFI_PASS      "YOUR_WIFI_PASSWORD"
#endif
#ifndef DEVICE_ID
#define DEVICE_ID      "pillbox_01"
#endif
#define HTTP_TIMEOUT_MS 3000
#define HTTP_RETRY_MAX  1
#define NTP_SERVER      "ntp.aliyun.com"
#define TZ_INFO         "CST-8"

// ============ FreeRTOS task architecture (separate screen-refresh and HTTP-report tasks) ============
// Addresses two blocking operations:
// 1. Screen refresh blocks for 16-30 s -> separate task; state machine queues request and returns immediately
// 2. HTTP POST may block for up to 6 s -> separate task; state machine queues request and returns immediately
// Heartbeat: report to /api/heartbeat every HEARTBEAT_INTERVAL_MS (not stored in event table)
#define HEARTBEAT_INTERVAL_MS  15000UL   // Send every 15 s; PC marks device offline after 30 s

// Event-report request (state machine -> network task)
struct ReportReq {
    char  event[24];
    char  extra_json[128];   // Empty string means no extra data
};

// Screen-refresh request (state machine -> display task)
struct RefreshReq {
    uint8_t reserved;   // Current epd_show_canvas() takes no arguments; reserved for extension
};

static QueueHandle_t report_queue  = nullptr;   // Queue capacity: 8
static QueueHandle_t refresh_queue = nullptr;   // Queue capacity: 2

// Module headers
#include "epd_driver/Display_EPD_W21_spi.h"
#include "epd_driver/Display_EPD_W21.h"
#include "ui/ui_draw.h"
#include "audio/audio_amp.h"
#include "audio/audio_wav.h"
#include "assets/voice_remind.h"

// ============ Pins ============
#define EPD_BUSY_PIN   14
#define EPD_RST_PIN    13
#define EPD_DC_PIN      9
#define EPD_CS_PIN     10
#define EPD_SCK_PIN    12
#define EPD_MOSI_PIN   11

#define LED_PIN         6
#define NUM_LEDS        9

#define TOUCH_CONFIRM_PIN   1   // T1
#define TOUCH_BOXOPEN_PIN   4   // T4

#define IMU_SDA_PIN         8   // MPU6050 I2C data line (finalized hardware pin GPIO8)
#define IMU_SCL_PIN         7   // MPU6050 I2C clock line (GPIO7)

// ============ Application parameters ============
#define FULL_CHARGE_MIN      240UL    // 4 h = 240 min (can be shortened for debugging)
#define REFRESH_INTERVAL_MIN 10UL     // Screen refresh interval: 10 min
#define REMIND_THRESHOLD_MIN 20L      // Trigger reminder when <= 20 min remain
#define TAKEN_HOLD_MS        30000UL  // Hold TAKEN state for 30 s
#define TOUCH_THRESHOLD      30       // Treat touchRead < 30 as pressed
#define TOUCH_DEBOUNCE_MS    300      // Button debounce interval

// ============ State machine ============
enum AppState {
    STATE_NORMAL = 0,
    STATE_REMIND,
    STATE_WAIT_OPEN,
    STATE_TAKEN,
};

static AppState state = STATE_NORMAL;
static uint32_t cycle_start_ms = 0;       // Start of current 4 h cycle
static uint32_t last_refresh_ms = 0;      // Previous screen refresh
static uint32_t state_enter_ms = 0;       // Time current state was entered
static uint32_t last_touch_confirm_ms = 0;
static uint32_t last_touch_boxopen_ms = 0;

// ============ Tremor detection (MPU6050 + FFT) ============
static MPU6050* mpu = nullptr;          // Allocate in setup (Wire reference requires Wire.begin first)
static bool imu_ready = false;          // IMU initialization complete
static bool tremor_detecting = false;   // Whether sampling is active (controlled by state machine)
static uint32_t last_sample_ms = 0;     // Previous IMU sample time (100 Hz scheduling)
static uint32_t last_tremor_report_ms = 0;  // Previous tremor report time (30 s rate limit)

// Canvas (4048 bytes, global static)
static Canvas canvas;

// FastLED buffer
static CRGB leds[NUM_LEDS];

// Serial command buffer
static String input_buf;

// ============ Help ============
static void show_help() {
    Serial.println();
    Serial.println("=====================================");
    Serial.println("  Smart Pillbox (final)");
    Serial.println("  ESP32-S3 | 4h cycle");
    Serial.println("=====================================");
    Serial.println("Touch buttons:");
    Serial.println("  GPIO1 (T1) = confirm pill");
    Serial.println("  GPIO4 (T4) = box opened");
    Serial.println("Serial debug commands:");
    Serial.println("  T   trigger remind now");
    Serial.println("  C   simulate confirm button");
    Serial.println("  O   simulate box-open button");
    Serial.println("  R   reset to full charge");
    Serial.println("  S   show status");
    Serial.println("  H   help");
    Serial.println("  W   send test event to PC");
    Serial.println("  I   show WiFi/NTP info");
    Serial.println("  M   toggle tremor detection");
    Serial.println("  P   print IMU data");
    Serial.println();
}

// ============ E-paper display helpers ============
static void epd_init_hw() {
    pinMode(EPD_BUSY_PIN, INPUT);
    pinMode(EPD_RST_PIN, OUTPUT);
    pinMode(EPD_DC_PIN, OUTPUT);
    pinMode(EPD_CS_PIN, OUTPUT);
    SPI.begin(EPD_SCK_PIN, -1, EPD_MOSI_PIN, EPD_CS_PIN);
    SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));
}

// Refresh screen once with specified canvas
static void epd_show_canvas() {
    EPD_init();
    PIC_display_raw(canvas.buf);
    // Do not enter deep sleep (use POF only, as fixed in 07_epd_test)
    EPD_W21_WriteCMD(0x02);
    EPD_W21_WriteDATA(0x00);
    delay(50);
}

// ============ FastLED ============
static void fastled_init() {
    FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(80);
    FastLED.clear();
    FastLED.show();
}

static void fastled_off() {
    FastLED.clear();
    FastLED.show();
}

// Red blinking (toggle every 500 ms)
static void fastled_blink_red() {
    static uint32_t last = 0;
    static bool on = false;
    if (millis() - last >= 500) {
        last = millis();
        on = !on;
        fill_solid(leds, NUM_LEDS, on ? CRGB::Red : CRGB::Black);
        FastLED.show();
    }
}

// Celebratory rainbow (advance hue each frame)
static void fastled_celebrate() {
    static uint32_t last = 0;
    static uint8_t hue = 0;
    if (millis() - last >= 30) {
        last = millis();
        for (int i = 0; i < NUM_LEDS; i++) {
            leds[i] = CHSV((hue + i * 28) & 0xFF, 255, 255);
        }
        FastLED.show();
        hue += 4;
    }
}

// ============ State output ============
static const char* state_name(AppState s) {
    switch (s) {
        case STATE_NORMAL:    return "NORMAL";
        case STATE_REMIND:    return "REMIND";
        case STATE_WAIT_OPEN: return "WAIT_OPEN";
        case STATE_TAKEN:     return "TAKEN";
    }
    return "?";
}

static void enter_state(AppState s) {
    state = s;
    state_enter_ms = millis();
    Serial.print("[STATE] -> ");
    Serial.println(state_name(s));
}

// ============ Touch button (debounced) ============
// Return true only for a newly detected press (edge triggered)
static bool touch_pressed(int pin, uint32_t& last_trig_ms) {
    int v = touchRead(pin);
    if (v < TOUCH_THRESHOLD) {
        if (millis() - last_trig_ms > TOUCH_DEBOUNCE_MS) {
            last_trig_ms = millis();
            Serial.print("[TOUCH] pin=");
            Serial.print(pin);
            Serial.print(" val=");
            Serial.println(v);
            return true;
        }
    }
    return false;
}

// ============ Remaining-charge calculation ============
static int remaining_minutes() {
    uint32_t elapsed = (millis() - cycle_start_ms) / 60000UL;
    if (elapsed >= FULL_CHARGE_MIN) return 0;
    return (int)(FULL_CHARGE_MIN - elapsed);
}

// ============ Reminder audio (prefer WAV; fall back to square wave) ============
// Try playing /remind.wav from LittleFS first
// If missing, fall back to voice_remind_buf (generated three-beep square wave)
static bool wav_available = false;   // Whether LittleFS contains remind.wav

static void remind_play_async() {
    if (wav_available && wav_play_async("/remind.wav")) {
        return;
    }
    // Fall back to placeholder square wave
    amp_play_async(voice_remind_buf, voice_remind_samples());
}

// Whether WAV or square-wave playback is active
static bool remind_is_playing() {
    return amp_is_playing();
}

// Advance playback
static void remind_loop() {
    amp_loop();
}

// Stop playback
static void remind_stop() {
    if (wav_available) wav_stop();
    else               amp_stop();
}

// ============ Wi-Fi / NTP / event reporting ============
static bool ntp_started = false;

static void wifi_on_disconnect(WiFiEvent_t, WiFiEventInfo_t) {
    Serial.println("[WIFI] disconnected, reconnect...");
    WiFi.reconnect();
}

static void wifi_setup() {
    WiFi.onEvent(wifi_on_disconnect, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);              // Disable modem sleep to avoid touch/HTTP timing issues
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.println("[WIFI] connecting...");
}

static void ntp_setup() {
    configTime(0, 0, NTP_SERVER);
    setenv("TZ", TZ_INFO, 1); tzset();
    ntp_started = true;
    Serial.println("[NTP] started");
}

// Call from loop: detect connection changes and start NTP after connecting
static void wifi_loop() {
    static bool last_connected = false;
    bool now = (WiFi.status() == WL_CONNECTED);
    if (now && !last_connected) {
        Serial.print("[WIFI] connected IP=");
        Serial.println(WiFi.localIP());
        if (!ntp_started) ntp_setup();
    }
    last_connected = now;
}

// Return ISO8601 local timestamp; empty if NTP not synchronized
static String get_iso_time() {
    if (time(nullptr) < 1700000000) return String("");
    struct tm t;
    getLocalTime(&t, 0);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S%z", &t);
    return String(buf);
}

// Send event to PC server (synchronous, 3 s timeout; log failures without buffering)
// Called by TaskNetwork; state machine should queue via request_report()
static void report_event_blocking(const char* type, const char* extra_json = nullptr) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.print("[REPORT] skip (wifi down): ");
        Serial.println(type);
        return;
    }
    String ts = get_iso_time();
    for (int attempt = 0; attempt <= HTTP_RETRY_MAX; attempt++) {
        HTTPClient http;
        http.setTimeout(HTTP_TIMEOUT_MS);
        String url = "http://" + String(PC_SERVER_IP) + ":" + String(PC_SERVER_PORT) + "/api/events";
        http.begin(url);
        http.addHeader("Content-Type", "application/json");

        JsonDocument doc;
        doc["device"]    = DEVICE_ID;
        doc["event"]     = type;
        doc["uptime_ms"] = (long)millis();
        if (ts.length()) doc["timestamp"] = ts;
        if (extra_json) {
            JsonDocument extra;
            deserializeJson(extra, extra_json);
            doc["extra"] = extra.as<JsonObject>();
        }
        String body;
        serializeJson(doc, body);

        int code = http.POST(body);
        http.end();
        if (code == 201) {
            Serial.print("[REPORT] ok ");
            Serial.println(type);
            return;
        }
        Serial.print("[REPORT] fail code=");
        Serial.print(code);
        Serial.print(" type=");
        Serial.println(type);
    }
}

// ============ Heartbeat reporting (lightweight POST, no retries) ============
// Use separate /api/heartbeat endpoint; PC updates memory only, not event database
static void heartbeat_once() {
    if (WiFi.status() != WL_CONNECTED) return;
    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);   // Heartbeat also uses a 3 s timeout, but sends only once without retry
    String url = "http://" + String(PC_SERVER_IP) + ":"
               + String(PC_SERVER_PORT) + "/api/heartbeat";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    String body = String("{\"device\":\"") + DEVICE_ID + "\",\"uptime_ms\":" + millis() + "}";
    int code = http.POST(body);
    http.end();
    if (code != 200) {
        Serial.print("[HEARTBEAT] fail code=");
        Serial.println(code);
    }
}

// ============ State-machine queue wrappers (nonblocking) ============
// Queue event report; discard oldest when full to avoid blocking state machine
static void request_report(const char* event, const char* extra_json = nullptr) {
    if (!report_queue) {
        // Queue not initialized yet (early setup): fall back to synchronous call
        report_event_blocking(event, extra_json);
        return;
    }
    ReportReq req;
    strncpy(req.event, event, sizeof(req.event) - 1);
    req.event[sizeof(req.event) - 1] = '\0';
    if (extra_json) {
        strncpy(req.extra_json, extra_json, sizeof(req.extra_json) - 1);
        req.extra_json[sizeof(req.extra_json) - 1] = '\0';
    } else {
        req.extra_json[0] = '\0';
    }
    if (xQueueSend(report_queue, &req, 0) != pdTRUE) {
        // If queue is full, discard oldest and retry
        ReportReq dropped;
        xQueueReceive(report_queue, &dropped, 0);
        xQueueSend(report_queue, &req, 0);
        Serial.print("[REPORT] queue full, dropped old: ");
        Serial.println(dropped.event);
    }
}

// Queue screen refresh; discard oldest when full
static void request_refresh() {
    if (!refresh_queue) {
        epd_show_canvas();   // Queue not initialized yet (early setup): fall back to synchronous call
        return;
    }
    RefreshReq req = { 0 };
    if (xQueueSend(refresh_queue, &req, 0) != pdTRUE) {
        RefreshReq dropped;
        xQueueReceive(refresh_queue, &dropped, 0);
        xQueueSend(refresh_queue, &req, 0);
        Serial.println("[EPD] refresh queue full, dropped old request");
    }
}

// ============ FreeRTOS tasks ============

// Network task (Core 0): periodic heartbeat and queued event reports
static void task_network(void*) {
    uint32_t last_heartbeat = 0;
    // Send heartbeat immediately at startup so PC detects device quickly
    heartbeat_once();
    last_heartbeat = millis();
    while (true) {
        // Heartbeat schedule
        if (millis() - last_heartbeat >= HEARTBEAT_INTERVAL_MS) {
            heartbeat_once();
            last_heartbeat = millis();
        }
        // Dequeue event report (wait up to 1 s, then check heartbeat again)
        ReportReq req;
        if (xQueueReceive(report_queue, &req, pdMS_TO_TICKS(1000)) == pdTRUE) {
            report_event_blocking(req.event,
                                  req.extra_json[0] ? req.extra_json : nullptr);
        }
    }
}

// Display task (Core 1): perform blocking 16-30 s screen refresh serially
static void task_epd_refresh(void*) {
    RefreshReq req;
    while (true) {
        if (xQueueReceive(refresh_queue, &req, portMAX_DELAY) == pdTRUE) {
            uint32_t t0 = millis();
            epd_show_canvas();
            Serial.print("[EPD] refresh done in ");
            Serial.print((millis() - t0) / 1000.0, 1);
            Serial.println("s");
        }
    }
}

// ============ Tremor detection: IMU initialization and sampling ============
static void imu_setup() {
    Wire.begin(IMU_SDA_PIN, IMU_SCL_PIN);
    mpu = new MPU6050(Wire);
    byte status = mpu->begin();
    if (status != 0) {
        Serial.println("[IMU] init FAILED");
        imu_ready = false;
        return;
    }
    Serial.println("[IMU] init OK, calibrating gyro (keep still)...");
    mpu->calcGyroOffsets();              // Blocking gyroscope calibration (~3 s)
    tremor_init();                       // Initialize FFT window and buffers
    imu_ready = true;
    tremor_detecting = true;             // Sample continuously by default (state machine switches based on state)
    Serial.println("[IMU] ready, tremor detection ON");
}

// Feed one sample into FFT ring buffer (every 10 ms, 100 Hz)
static void tremor_sample_if_due() {
    if (!imu_ready || !tremor_detecting) return;
    uint32_t now = millis();
    if (now - last_sample_ms < 10) return;   // 100 Hz rate limit
    last_sample_ms = now;
    mpu->update();
    tremor_feed_sample(mpu->getAccX(), mpu->getAccY(), mpu->getAccZ());
}

// Check FFT result and report confirmed tremor (at most once every 30 s)
static void tremor_check_and_report() {
    if (!imu_ready || !tremor_detecting) return;
    TremorResult r;
    if (!tremor_analyze(&r)) return;     // Return false if buffer is not full
    // Print each window for debugging
    Serial.print("[TREMOR] ratio=");
    Serial.print(r.ratio);
    Serial.print(" peak=");
    Serial.print(r.peakFreq);
    Serial.print("Hz hit=");
    Serial.print(r.hitCount);
    Serial.println(r.tremorConfirmed ? " *** TREMOR ***" : " idle");
    // Confirm and rate-limit reports to one per 30 s
    if (r.tremorConfirmed && (millis() - last_tremor_report_ms > 30000UL)) {
        last_tremor_report_ms = millis();
        char extra[96];
        snprintf(extra, sizeof(extra),
                 "{\"ratio\":%.3f,\"peak_hz\":%.2f,\"hit_count\":%d}",
                 r.ratio, r.peakFreq, r.hitCount);
        request_report("TREMOR_SUSPECTED", extra);
    }
}

// ============ State machine execution ============
static void app_step(bool btn_confirm, bool btn_boxopen) {
    uint32_t now = millis();
    int rem = remaining_minutes();

    switch (state) {
        case STATE_NORMAL: {
            tremor_detecting = true;   // Sample during normal state (most of the 4 h cycle)
            // Refresh screen every REFRESH_INTERVAL_MIN minutes
            if (now - last_refresh_ms >= REFRESH_INTERVAL_MIN * 60000UL) {
                Serial.print("[INFO] refresh battery, remaining=");
                Serial.print(rem);
                Serial.println("min");
                ui_draw_battery(canvas, rem, (int)FULL_CHARGE_MIN);
                request_refresh();
                last_refresh_ms = now;
            }
            // Enter reminder state
            if (rem <= REMIND_THRESHOLD_MIN) {
                ui_draw_battery(canvas, rem, (int)FULL_CHARGE_MIN);
                request_refresh();   // Show low-charge indicator (red)
                remind_play_async();
                request_report("REMIND_START");
                enter_state(STATE_REMIND);
            }
            break;
        }

        case STATE_REMIND: {
            tremor_detecting = false;  // Disable tremor sampling during reminder/LED activity because user movement may confound readings
            // Repeat spoken reminder
            if (!remind_is_playing()) {
                remind_play_async();
            } else {
                remind_loop();
            }
            fastled_blink_red();
            // User acknowledgement
            if (btn_confirm) {
                remind_stop();
                fastled_off();
                request_report("MED_CONFIRM");
                enter_state(STATE_WAIT_OPEN);
            }
            break;
        }

        case STATE_WAIT_OPEN: {
            tremor_detecting = true;   // Resume sampling during quiet waiting state
            // Wait for box opening
            if (btn_boxopen) {
                // Reset 4 h timer
                cycle_start_ms = now;
                // Show sunflower and celebratory LEDs
                ui_draw_sunflower(canvas);
                request_refresh();
                fastled_celebrate();
                // Play completion audio once (encourage.wav; fall back to first third of square wave)
                if (wav_available && wav_play_async("/encourage.wav")) {
                    // ok
                } else {
                    amp_play_async(voice_remind_buf, voice_remind_samples() / 3);
                }
                request_report("BOX_OPEN");
                enter_state(STATE_TAKEN);
            }
            break;
        }

        case STATE_TAKEN: {
            tremor_detecting = false;  // Pause tremor sampling during 30 s celebration because movement may confound readings
            // Advance playback and LEDs
            if (remind_is_playing()) remind_loop();
            fastled_celebrate();
            // Return to NORMAL after 30 s
            if (now - state_enter_ms >= TAKEN_HOLD_MS) {
                fastled_off();
                ui_draw_battery(canvas, (int)FULL_CHARGE_MIN, (int)FULL_CHARGE_MIN);
                request_refresh();
                last_refresh_ms = now;
                enter_state(STATE_NORMAL);
            }
            break;
        }
    }
}

// ============ Serial commands (debugging) ============
static void process_cmd(const String& cmd) {
    String s = cmd; s.trim();
    if (s.length() == 0) return;
    char c = s.charAt(0);
    if (c >= 'a' && c <= 'z') c -= 32;

    switch (c) {
        case 'T':
            // Force REMIND by setting cycle_start_ms to 221 min ago
            cycle_start_ms = millis() - (FULL_CHARGE_MIN - REMIND_THRESHOLD_MIN) * 60000UL;
            Serial.println("[CMD] forced remind trigger");
            break;
        case 'C':
            last_touch_confirm_ms = 0;  // Allow touch event immediately
            app_step(true, false);
            Serial.println("[CMD] simulated confirm");
            break;
        case 'O':
            last_touch_boxopen_ms = 0;
            app_step(false, true);
            Serial.println("[CMD] simulated box open");
            break;
        case 'R':
            cycle_start_ms = millis();
            last_refresh_ms = 0;
            Serial.println("[CMD] reset to full charge");
            break;
        case 'S':
            Serial.print("[STATUS] state=");
            Serial.print(state_name(state));
            Serial.print(" remaining=");
            Serial.print(remaining_minutes());
            Serial.print("min playing=");
            Serial.print(amp_is_playing() ? "yes" : "no");
            Serial.print(" wav=");
            Serial.println(wav_available ? "enabled" : "fallback-beep");
            break;
        case 'H': show_help(); break;
        case 'W':
            request_report("TEST", "{\"cmd\":\"W\"}");
            Serial.println("[CMD] queued test event");
            break;
        case 'I':
            Serial.print("[WIFI] status=");
            Serial.println(WiFi.status() == WL_CONNECTED ? "connected" : "down");
            if (WiFi.status() == WL_CONNECTED) {
                Serial.print("[WIFI] IP=");
                Serial.print(WiFi.localIP());
                Serial.print(" RSSI=");
                Serial.println(WiFi.RSSI());
            }
            Serial.print("[NTP] ");
            Serial.println(time(nullptr) > 1700000000 ? "synced" : "not_synced");
            break;
        case 'M':
            tremor_detecting = !tremor_detecting;
            Serial.print("[CMD] tremor detection ");
            Serial.println(tremor_detecting ? "ON" : "OFF");
            break;
        case 'P':
            if (imu_ready && mpu) {
                mpu->update();
                Serial.print("[IMU] ax="); Serial.print(mpu->getAccX());
                Serial.print(" ay="); Serial.print(mpu->getAccY());
                Serial.print(" az="); Serial.println(mpu->getAccZ());
            } else {
                Serial.println("[IMU] not ready");
            }
            break;
        default:
            Serial.print("[ERR] unknown: ");
            Serial.println(s);
            break;
    }
}

// ============ Entry point ============
void setup() {
    Serial.begin(115200);
    delay(300);

    Serial.println();
    Serial.println("#####################################");
    Serial.println("#  Smart Pillbox (final)");
    Serial.println("#  ESP32-S3 | 4h cycle");
    Serial.println("#####################################");

    epd_init_hw();
    Serial.println("[INIT] EPD SPI ready");

    fastled_init();
    Serial.println("[INIT] FastLED ready");

    amp_init();
    Serial.println("[INIT] AMP ready");

    voice_remind_init();
    Serial.println("[INIT] voice remind buffer ready");

    // Initialize LittleFS (WAV storage)
    if (wav_init()) {
        Serial.println("[INIT] LittleFS ready");
        // Check whether remind.wav exists to determine WAV availability
        if (LittleFS.exists("/remind.wav")) {
            wav_available = true;
            Serial.println("[INIT] /remind.wav found, using WAV playback");
        } else {
            wav_available = false;
            Serial.println("[INIT] /remind.wav NOT found, fallback to beep");
        }
        if (LittleFS.exists("/encourage.wav")) {
            Serial.println("[INIT] /encourage.wav found");
        }
    } else {
        Serial.println("[INIT] LittleFS FAILED, fallback to beep");
        wav_available = false;
    }

    // Touch-button pins (Arduino touchRead configures automatically)
    pinMode(TOUCH_CONFIRM_PIN, INPUT);
    pinMode(TOUCH_BOXOPEN_PIN, INPUT);

    // Startup self-test: show full-charge icon
    ui_draw_battery(canvas, (int)FULL_CHARGE_MIN, (int)FULL_CHARGE_MIN);
    epd_show_canvas();
    Serial.println("[INIT] first display done");

    cycle_start_ms = millis();
    last_refresh_ms = millis();
    enter_state(STATE_NORMAL);

    // Start Wi-Fi asynchronously; connection failure does not block state machine
    wifi_setup();
    Serial.println("[INIT] WiFi started (async)");

    // Initialize IMU (blocking ~3 s gyro calibration; last to avoid delaying first display)
    imu_setup();

    // Create queues and start background tasks (display refresh and network)
    report_queue  = xQueueCreate(8, sizeof(ReportReq));
    refresh_queue = xQueueCreate(2, sizeof(RefreshReq));
    xTaskCreatePinnedToCore(task_network,     "net", 6144, NULL, 3, NULL, 0);  // Core 0
    xTaskCreatePinnedToCore(task_epd_refresh, "epd", 4096, NULL, 1, NULL, 1);  // Core 1
    Serial.println("[INIT] FreeRTOS tasks started (net@Core0, epd@Core1)");

    show_help();
}

void loop() {
    // Update Wi-Fi status and start NTP after connection
    wifi_loop();

    // Tremor detection: 100 Hz sampling, FFT decision, and confirmed report (controlled by tremor_detecting)
    tremor_sample_if_due();
    tremor_check_and_report();

    // Sample touch inputs
    bool btn_confirm = touch_pressed(TOUCH_CONFIRM_PIN, last_touch_confirm_ms);
    bool btn_boxopen = touch_pressed(TOUCH_BOXOPEN_PIN, last_touch_boxopen_ms);

    // Run state machine
    app_step(btn_confirm, btn_boxopen);

    // Process serial commands
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

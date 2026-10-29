#include "ui_draw.h"
#include <string.h>
#include <math.h>

// ============ Canvas implementation ============

void Canvas::clear(uint8_t color) {
    // Four pixels per byte; assign the same 2-bit color to each pixel
    uint8_t b = 0;
    for (int i = 0; i < 4; i++) {
        b |= (uint8_t)((color & 0x03) << (i * 2));
    }
    memset(buf, b, UI_BYTES);
}

void Canvas::setPixel(int x, int y, uint8_t color) {
    if (x < 0 || x >= UI_WIDTH || y < 0 || y >= UI_HEIGHT) return;
    int byte_idx = (y * UI_WIDTH + x) / 4;
    int bit_off  = (x % 4) * 2;
    buf[byte_idx] = (uint8_t)((buf[byte_idx] & ~(0x03 << bit_off))
                              | ((color & 0x03) << bit_off));
}

void Canvas::fillRect(int x0, int y0, int x1, int y1, uint8_t color) {
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            setPixel(x, y, color);
        }
    }
}

void Canvas::drawRect(int x0, int y0, int x1, int y1, uint8_t color) {
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    for (int x = x0; x < x1; x++) {
        setPixel(x, y0, color);
        setPixel(x, y1 - 1, color);
    }
    for (int y = y0; y < y1; y++) {
        setPixel(x0, y, color);
        setPixel(x1 - 1, y, color);
    }
}

// Midpoint circle algorithm (eight-way symmetry)
static void draw_circle_8(Canvas& cv, int cx, int cy, int x, int y, uint8_t color) {
    cv.setPixel(cx + x, cy + y, color);
    cv.setPixel(cx - x, cy + y, color);
    cv.setPixel(cx + x, cy - y, color);
    cv.setPixel(cx - x, cy - y, color);
    cv.setPixel(cx + y, cy + x, color);
    cv.setPixel(cx - y, cy + x, color);
    cv.setPixel(cx + y, cy - x, color);
    cv.setPixel(cx - y, cy - x, color);
}

void Canvas::drawCircle(int cx, int cy, int r, uint8_t color) {
    if (r <= 0) { setPixel(cx, cy, color); return; }
    int x = 0, y = r;
    int d = 3 - 2 * r;
    while (x <= y) {
        draw_circle_8(*this, cx, cy, x, y, color);
        if (d < 0) {
            d += 4 * x + 6;
        } else {
            d += 4 * (x - y) + 10;
            y--;
        }
        x++;
    }
}

void Canvas::fillCircle(int cx, int cy, int r, uint8_t color) {
    if (r <= 0) { setPixel(cx, cy, color); return; }
    // Draw horizontal spans outward from center for a simpler implementation
    for (int y = -r; y <= r; y++) {
        int dx = (int)(sqrtf((float)(r * r - y * y)) + 0.5f);
        for (int x = -dx; x <= dx; x++) {
            setPixel(cx + x, cy + y, color);
        }
    }
}

// ============ UI: battery icon ============
// Screen dimensions: 88×184 (portrait).
// Design: battery centered toward upper screen, approximately 60×100 pixels
//   Battery outline (black): x=14–73, y=40–139
//   Battery terminal: x=39–49, y=33–40
//   Battery fill: rises from bottom according to percentage
//   Reserve bottom area for percentage text (no font rendering implemented)
void ui_draw_battery(Canvas& cv, int remaining_min, int full_min) {
    // Background: white
    cv.clear(UI_WHITE);

    // Battery terminal (small top rectangle)
    cv.fillRect(38, 30, 50, 40, UI_BLACK);

    // Battery outline (black border, 4-pixel inset)
    cv.drawRect(14, 40, 74, 145, UI_BLACK);

    // Calculate charge fraction (0–1)
    float ratio = 0.0f;
    if (full_min > 0 && remaining_min > 0) {
        ratio = (float)remaining_min / (float)full_min;
        if (ratio > 1.0f) ratio = 1.0f;
    }

    // Fill color: yellow above 50% and from 20–50%; red at or below 20%
    uint8_t fill_color = UI_YELLOW;
    if (ratio <= 0.25f) fill_color = UI_RED;

    // Fill area: x=18–70, filling upward from y=144 (bottom)
    // Inner frame height = 144 - 44 = 100 pixels
    int inner_x0 = 18, inner_x1 = 70;
    int inner_y_bottom = 144;       // Exclusive; y=143 is the lowest filled row
    int inner_y_top = 44;
    int inner_h = inner_y_bottom - inner_y_top;   // 100
    int fill_h = (int)(inner_h * ratio);
    if (fill_h > 0) {
        int fill_y0 = inner_y_bottom - fill_h;
        int fill_y1 = inner_y_bottom;
        cv.fillRect(inner_x0, fill_y0, inner_x1, fill_y1, fill_color);
    }

    // Reserve lower screen area for charge indicator (simple rectangular markers, no font)
    // In y=155–178 area, draw four divisions for 25%/50%/75%/100% marks
    for (int i = 1; i < 4; i++) {
        int tick_y = inner_y_top + inner_h * i / 4;
        for (int x = inner_x0; x < inner_x1; x += 4) {
            cv.setPixel(x, tick_y, UI_BLACK);
        }
    }
}

// ============ UI: encouraging sunflower graphic ============
// Yellow background, black center, and eight oval petals
// Center at (44, 92) (screen center)
void ui_draw_sunflower(Canvas& cv) {
    // Background: yellow
    cv.clear(UI_YELLOW);

    const int cx = 44, cy = 92;

    // Eight black oval petals around the center
    // Simplified implementation: each petal is a thick radial line
    for (int i = 0; i < 12; i++) {
        float angle = i * (2.0f * 3.14159265f / 12.0f);
        // Petals extend from radius 20 to 45
        for (int r = 20; r <= 48; r += 1) {
            int px = cx + (int)(r * cosf(angle));
            int py = cy + (int)(r * sinf(angle));
            // Give petals thickness by drawing 2×2 black pixels
            cv.setPixel(px,     py,     UI_BLACK);
            cv.setPixel(px + 1, py,     UI_BLACK);
            cv.setPixel(px,     py + 1, UI_BLACK);
            cv.setPixel(px + 1, py + 1, UI_BLACK);
        }
    }

    // Center: filled black circle (radius 18)
    cv.fillCircle(cx, cy, 18, UI_BLACK);

    // Add yellow dots inside center to suggest sunflower seeds
    for (int i = 0; i < 8; i++) {
        float angle = i * (2.0f * 3.14159265f / 8.0f);
        for (int r = 4; r <= 14; r += 5) {
            int px = cx + (int)(r * cosf(angle));
            int py = cy + (int)(r * sinf(angle));
            cv.setPixel(px, py, UI_YELLOW);
            cv.setPixel(px + 1, py, UI_YELLOW);
        }
    }
}

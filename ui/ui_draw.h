#ifndef UI_DRAW_H
#define UI_DRAW_H

#include <stdint.h>

// ============ Screen dimensions (from epd_driver/Display_EPD_W21.h)============
#define UI_WIDTH   88     // Source_BITS (horizontal pixel count)
#define UI_HEIGHT  184    // Gate_BITS (vertical pixel count)
#define UI_BYTES   4048   // 88*184/4

// ============ Color encoding (2 bits/pixel, matching Display_All_*)============
// Note: this differs from the input encoding used by Color_get inside PIC_display.
// This module generates native controller encoding and sends it using PIC_display_raw().
#define UI_BLACK   0x00   // 00
#define UI_WHITE   0x01   // 01
#define UI_YELLOW  0x02   // 10
#define UI_RED     0x03   // 11

// ============ Canvas: 4048-byte 2-bit bitmap buffer ============
class Canvas {
public:
    uint8_t buf[UI_BYTES];

    // Fill entire canvas with specified color
    void clear(uint8_t color);

    // Set one pixel (x=0–87 horizontally, y=0–183 vertically; origin at upper left)
    void setPixel(int x, int y, uint8_t color);

    // Fill rectangle (inclusive x0,y0; exclusive x1,y1)
    void fillRect(int x0, int y0, int x1, int y1, uint8_t color);

    // Draw rectangle outline
    void drawRect(int x0, int y0, int x1, int y1, uint8_t color);

    // Draw filled circle (midpoint algorithm)
    void fillCircle(int cx, int cy, int r, uint8_t color);

    // Draw circle outline
    void drawCircle(int cx, int cy, int r, uint8_t color);
};

// ============ UI drawing: battery icon ============
// Draw battery icon and charge percentage on canvas
//   remaining_min: minutes remaining
//   full_min: full-charge duration in minutes (for percentage calculation)
// Above 50% show yellow fill; at or below 50% red; at or below 20% flashing red (here solid red)
void ui_draw_battery(Canvas& cv, int remaining_min, int full_min);

// ============ UI drawing: encouraging sunflower graphic ============
// Yellow background with black petals and center
void ui_draw_sunflower(Canvas& cv);

#endif // UI_DRAW_H

#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_io.h"

#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 160

// Font cell dimensions (5px glyph + 1px gap)
#define FONT_W 6
#define FONT_H 8

// Colors are standard 24-bit hex codes (0xRRGGBB), the same format as web/design tools.
// The display driver converts to 18-bit RGB666 internally when writing to the panel.
// Tuned for the JD-T1800: compensates for its cool white point, compressed greens,
// and tendency to oversaturate blues.
#define COLOR_BLACK   0x141410  // Raised slightly — panel backlight lifts true black
#define COLOR_WHITE   0xECE8DC  // Warm ivory — offsets the panel's cool white point
#define COLOR_GRAY    0x585850  // Neutral mid-gray
#define COLOR_RED     0xDC1840  // Pure-leaning red — panel adds slight warmth
#define COLOR_GREEN   0x10C448  // Pushed toward pure green — panel compresses greens
#define COLOR_BLUE    0x2498DC  // Slightly muted — panel oversaturates blues
#define COLOR_YELLOW  0xF8C010  // Warm yellow
#define COLOR_CYAN    0x00ACCC  // Rich cyan
#define COLOR_MAGENTA 0xC81460  // Deep magenta
#define RGB(r, g, b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

esp_err_t display_init(void);
void display_clear(uint32_t color);
void display_fill_rect(int x, int y, int w, int h, uint32_t color);
void display_draw_pixel(int x, int y, uint32_t color);
void display_draw_bitmap(int x, int y, int w, int h, const uint8_t *bitmap);
void display_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg);
void display_draw_text(int x, int y, const char *text, uint32_t fg, uint32_t bg);
esp_lcd_panel_io_handle_t display_get_io(void);

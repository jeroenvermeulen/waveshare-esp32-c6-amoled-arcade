#pragma once

#include <stdbool.h>
#include <stdint.h>

// Pixels are RGB565 stored byte-swapped, the order the panel expects on the wire
typedef uint16_t color_t;

#define RGB565(r, g, b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3))
#define RGB(r, g, b)    ((color_t)(((RGB565(r, g, b) >> 8) & 0xFF) | ((RGB565(r, g, b) & 0xFF) << 8)))
#define HEX(h)          RGB(((h) >> 16) & 0xFF, ((h) >> 8) & 0xFF, (h) & 0xFF)

#define WHITE   HEX(0xFFFFFF)
#define BLACK   HEX(0x000000)

#define FONT_W  6   // glyph advance at scale 1
#define FONT_H  8

void gfx_target(uint16_t *buf, int y0, int h);
bool gfx_visible(int y, int h);     // does [y, y+h) overlap the current band?

void gfx_clear(color_t c);
void gfx_fill_rect(int x, int y, int w, int h, color_t c);
void gfx_rect(int x, int y, int w, int h, int t, color_t c);
void gfx_fill_round_rect(int x, int y, int w, int h, int r, color_t c);
void gfx_fill_circle(int cx, int cy, int r, color_t c);
void gfx_darken(int x, int y, int w, int h);

void gfx_text(int x, int y, const char *s, int scale, color_t c);
void gfx_text_center(int cx, int y, const char *s, int scale, color_t c);
void gfx_text_shadow(int x, int y, const char *s, int scale, color_t c);
int  gfx_text_width(const char *s, int scale);

color_t gfx_mix(color_t a, color_t b, int t);   // t = 0 (a) .. 256 (b)

// Pixel-art sprite: one string per row, one char per pixel. '.' is transparent and
// a digit indexes pal. Each pixel is drawn as a scale x scale block.
enum { SPRITE_FLIP_X = 1, SPRITE_FLIP_Y = 2 };
void gfx_sprite(const char *const *rows, int w, int h, int x, int y, int scale, const color_t *pal, int flags);

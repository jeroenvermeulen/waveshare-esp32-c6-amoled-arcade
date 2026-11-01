#include <string.h>
#include "gfx.h"
#include "board.h"
#include "font5x7.h"

static uint16_t *s_buf;
static int s_y0, s_y1;

void gfx_target(uint16_t *buf, int y0, int h)
{
    s_buf = buf;
    s_y0 = y0;
    s_y1 = y0 + h;
}

bool gfx_visible(int y, int h)
{
    return y < s_y1 && y + h > s_y0;
}

void gfx_clear(color_t c)
{
    uint32_t v = c | ((uint32_t)c << 16);
    uint32_t *p = (uint32_t *)s_buf;
    for (int n = (s_y1 - s_y0) * LCD_W / 2; n > 0; n--) {
        *p++ = v;
    }
}

// x1 is exclusive; y must already be inside the band
static inline void hspan(int x0, int x1, int y, color_t c)
{
    if (x0 < 0) x0 = 0;
    if (x1 > LCD_W) x1 = LCD_W;
    uint16_t *p = s_buf + (y - s_y0) * LCD_W + x0;
    for (int n = x1 - x0; n > 0; n--) {
        *p++ = c;
    }
}

void gfx_fill_rect(int x, int y, int w, int h, color_t c)
{
    int y0 = y < s_y0 ? s_y0 : y;
    int y1 = y + h > s_y1 ? s_y1 : y + h;
    for (int yy = y0; yy < y1; yy++) {
        hspan(x, x + w, yy, c);
    }
}

void gfx_rect(int x, int y, int w, int h, int t, color_t c)
{
    gfx_fill_rect(x, y, w, t, c);
    gfx_fill_rect(x, y + h - t, w, t, c);
    gfx_fill_rect(x, y + t, t, h - 2 * t, c);
    gfx_fill_rect(x + w - t, y + t, t, h - 2 * t, c);
}

static int isqrt(int v)
{
    if (v <= 0) return 0;
    int r = 0, bit = 1 << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

void gfx_fill_round_rect(int x, int y, int w, int h, int r, color_t c)
{
    if (2 * r > h) r = h / 2;
    if (2 * r > w) r = w / 2;
    int y0 = y < s_y0 ? s_y0 : y;
    int y1 = y + h > s_y1 ? s_y1 : y + h;
    for (int yy = y0; yy < y1; yy++) {
        int d = 0;   // rows into the corner, 1..r
        if (yy < y + r) d = y + r - yy;
        else if (yy >= y + h - r) d = yy - (y + h - r) + 1;
        int inset = 0;
        if (d > 0) {
            int dy2 = 2 * d - 1;
            inset = r - isqrt(4 * r * r - dy2 * dy2) / 2;
        }
        hspan(x + inset, x + w - inset, yy, c);
    }
}

void gfx_fill_circle(int cx, int cy, int r, color_t c)
{
    int y0 = cy - r < s_y0 ? s_y0 : cy - r;
    int y1 = cy + r + 1 > s_y1 ? s_y1 : cy + r + 1;
    for (int y = y0; y < y1; y++) {
        int dy = y - cy;
        int dx = isqrt(r * r - dy * dy + r);
        hspan(cx - dx, cx + dx + 1, y, c);
    }
}

void gfx_darken(int x, int y, int w, int h)
{
    int x0 = x < 0 ? 0 : x, x1 = x + w > LCD_W ? LCD_W : x + w;
    int y0 = y < s_y0 ? s_y0 : y, y1 = y + h > s_y1 ? s_y1 : y + h;
    for (int yy = y0; yy < y1; yy++) {
        uint16_t *p = s_buf + (yy - s_y0) * LCD_W + x0;
        for (int n = x1 - x0; n > 0; n--, p++) {
            uint16_t v = __builtin_bswap16(*p);
            *p = __builtin_bswap16((v >> 1) & 0x7BEF);
        }
    }
}

void gfx_text(int x, int y, const char *s, int scale, color_t c)
{
    if (!gfx_visible(y, FONT_H * scale)) return;
    for (; *s; s++, x += FONT_W * scale) {
        unsigned ch = (unsigned char)*s;
        if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
        if (ch < 32 || ch > 95) ch = '?';
        const uint8_t *glyph = font5x7[ch - 32];
        for (int col = 0; col < 5; col++) {
            uint8_t bits = glyph[col];
            for (int row = 0; bits; row++, bits >>= 1) {
                if (bits & 1) {
                    gfx_fill_rect(x + col * scale, y + row * scale, scale, scale, c);
                }
            }
        }
    }
}

int gfx_text_width(const char *s, int scale)
{
    int n = strlen(s);
    return n ? n * FONT_W * scale - scale : 0;
}

void gfx_text_center(int cx, int y, const char *s, int scale, color_t c)
{
    gfx_text(cx - gfx_text_width(s, scale) / 2, y, s, scale, c);
}

void gfx_text_shadow(int x, int y, const char *s, int scale, color_t c)
{
    int off = scale > 2 ? scale / 2 + 1 : 1;
    gfx_text(x + off, y + off, s, scale, BLACK);
    gfx_text(x, y, s, scale, c);
}

color_t gfx_mix(color_t a, color_t b, int t)
{
    uint16_t pa = __builtin_bswap16(a), pb = __builtin_bswap16(b);
    int r = ((pa >> 11) * (256 - t) + (pb >> 11) * t) >> 8;
    int g = (((pa >> 5) & 0x3F) * (256 - t) + ((pb >> 5) & 0x3F) * t) >> 8;
    int bl = ((pa & 0x1F) * (256 - t) + (pb & 0x1F) * t) >> 8;
    return __builtin_bswap16((uint16_t)((r << 11) | (g << 5) | bl));
}

void gfx_sprite(const char *const *rows, int w, int h, int x, int y, int scale, const color_t *pal, int flags)
{
    if (!gfx_visible(y, h * scale)) return;
    for (int r = 0; r < h; r++) {
        int ry = y + r * scale;
        if (!gfx_visible(ry, scale)) continue;
        const char *row = rows[flags & SPRITE_FLIP_Y ? h - 1 - r : r];
        // Draw runs of equal pixels as one rect
        for (int c = 0; c < w;) {
            char ch = row[flags & SPRITE_FLIP_X ? w - 1 - c : c];
            int n = 1;
            while (c + n < w && row[flags & SPRITE_FLIP_X ? w - 1 - c - n : c + n] == ch) n++;
            if (ch != '.') gfx_fill_rect(x + c * scale, ry, n * scale, scale, pal[ch - '0']);
            c += n;
        }
    }
}

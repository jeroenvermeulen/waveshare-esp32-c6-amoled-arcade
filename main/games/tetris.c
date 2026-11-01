// Tetris: falling-block puzzle. Tap rotates, drag moves, flick down drops.

#include <stdio.h>
#include <string.h>
#include "audio.h"
#include "game.h"
#include "util.h"

#define COLS        10
#define ROWS        20
#define CELL        22
#define WELL_X      130
#define WELL_Y      20
#define DRAG_STEP   26
#define LOCK_DELAY  0.5f
#define MAX_LOCK_RESETS 15
#define CLEAR_TIME  0.3f
#define DAS_DELAY   0.17f
#define DAS_REPEAT  0.05f

#define BG          HEX(0x0A0A1C)
#define LABEL       HEX(0x8A8AB0)

// (row, col) of each cell inside an n x n box, spawn orientation
static const int8_t s_cells[7][4][2] = {
    {{1, 0}, {1, 1}, {1, 2}, {1, 3}},   // I
    {{0, 0}, {0, 1}, {1, 0}, {1, 1}},   // O
    {{0, 1}, {1, 0}, {1, 1}, {1, 2}},   // T
    {{0, 1}, {0, 2}, {1, 0}, {1, 1}},   // S
    {{0, 0}, {0, 1}, {1, 1}, {1, 2}},   // Z
    {{0, 0}, {1, 0}, {1, 1}, {1, 2}},   // J
    {{0, 2}, {1, 0}, {1, 1}, {1, 2}},   // L
};
static const uint8_t s_box[7] = { 4, 2, 3, 3, 3, 3, 3 };
static const uint32_t s_hex[7] = { 0x3DD6F5, 0xF5D33D, 0xB45CF0, 0x5CE065, 0xF0504A, 0x4A78F0, 0xF59A3D };

static color_t s_shade[8][3];   // [kind + 1] = base, light, dark

static uint8_t s_board[ROWS][COLS];   // 0 empty, else kind + 1
static int s_kind, s_rot, s_px, s_py, s_next;
static uint8_t s_bag[7];
static int s_bag_left;
static int s_score, s_lines, s_level;
static float s_fall_t, s_lock_t, s_das_t, s_clear_t, s_age;
static int s_lock_resets;
static uint32_t s_clear_mask;
static bool s_over;
static int s_drag_x, s_drag_y;

static void cells_of(int kind, int rot, int out[4][2])
{
    int n = s_box[kind];
    for (int i = 0; i < 4; i++) {
        int r = s_cells[kind][i][0], c = s_cells[kind][i][1];
        for (int k = 0; k < (rot & 3); k++) {
            int t = r;
            r = c;
            c = n - 1 - t;
        }
        out[i][0] = r;
        out[i][1] = c;
    }
}

static bool fits(int kind, int rot, int px, int py)
{
    int cells[4][2];
    cells_of(kind, rot, cells);
    for (int i = 0; i < 4; i++) {
        int r = py + cells[i][0], c = px + cells[i][1];
        if (c < 0 || c >= COLS || r >= ROWS) return false;
        if (r >= 0 && s_board[r][c]) return false;
    }
    return true;
}

static bool grounded(void)
{
    return !fits(s_kind, s_rot, s_px, s_py + 1);
}

static int draw_from_bag(void)
{
    if (!s_bag_left) {
        for (int i = 0; i < 7; i++) s_bag[i] = i;
        for (int i = 6; i > 0; i--) {
            int j = rnd(i + 1);
            uint8_t t = s_bag[i];
            s_bag[i] = s_bag[j];
            s_bag[j] = t;
        }
        s_bag_left = 7;
    }
    return s_bag[--s_bag_left];
}

static void spawn(void)
{
    s_kind = s_next;
    s_next = draw_from_bag();
    s_rot = 0;
    s_px = (COLS - s_box[s_kind]) / 2;
    s_py = s_kind == 0 ? -1 : 0;
    s_fall_t = s_lock_t = 0;
    s_lock_resets = 0;
    if (!fits(s_kind, s_rot, s_px, s_py)) s_over = true;
}

static void touched_while_grounded(void)
{
    if (grounded() && s_lock_resets < MAX_LOCK_RESETS) {
        s_lock_t = 0;
        s_lock_resets++;
    }
}

static bool try_move(int dx, int dy)
{
    if (!fits(s_kind, s_rot, s_px + dx, s_py + dy)) return false;
    s_px += dx;
    s_py += dy;
    if (dx) touched_while_grounded();
    return true;
}

static void try_rotate(int dir)
{
    static const int8_t kicks[][2] = { {0, 0}, {-1, 0}, {1, 0}, {-2, 0}, {2, 0}, {0, -1} };
    int rot = (s_rot + dir) & 3;
    for (int i = 0; i < (int)(sizeof(kicks) / sizeof(kicks[0])); i++) {
        if (fits(s_kind, rot, s_px + kicks[i][0], s_py + kicks[i][1])) {
            s_rot = rot;
            s_px += kicks[i][0];
            s_py += kicks[i][1];
            touched_while_grounded();
            sfx(SFX_ROTATE);
            return;
        }
    }
}

static void lock_piece(void)
{
    int cells[4][2];
    cells_of(s_kind, s_rot, cells);
    for (int i = 0; i < 4; i++) {
        int r = s_py + cells[i][0], c = s_px + cells[i][1];
        if (r < 0) s_over = true;
        else s_board[r][c] = s_kind + 1;
    }

    s_clear_mask = 0;
    int n = 0;
    for (int r = 0; r < ROWS; r++) {
        bool full = true;
        for (int c = 0; c < COLS && full; c++) full = s_board[r][c];
        if (full) {
            s_clear_mask |= 1u << r;
            n++;
        }
    }
    if (n) {
        s_clear_t = CLEAR_TIME;
        sfx(n == 4 ? SFX_QUAD : SFX_LINE);
    } else {
        sfx(SFX_LOCK);
        if (!s_over) spawn();
    }
}

static void finish_clear(void)
{
    int n = 0, dst = ROWS - 1;
    for (int r = ROWS - 1; r >= 0; r--) {
        if (s_clear_mask & (1u << r)) {
            n++;
            continue;
        }
        if (dst != r) memcpy(s_board[dst], s_board[r], COLS);
        dst--;
    }
    for (; dst >= 0; dst--) memset(s_board[dst], 0, COLS);
    s_clear_mask = 0;

    static const int points[5] = { 0, 100, 300, 500, 800 };
    s_score += points[n] * s_level;
    s_lines += n;
    int level = 1 + s_lines / 10;
    if (level > s_level) {
        s_level = level;
        sfx(SFX_LEVEL_UP);
    }
    spawn();
}

static float gravity(void)
{
    float g = 0.8f;
    for (int i = 1; i < s_level; i++) g *= 0.82f;
    return g < 0.04f ? 0.04f : g;
}

static void init_shades(void)
{
    for (int k = 0; k < 7; k++) {
        color_t base = HEX(s_hex[k]);
        s_shade[k + 1][0] = base;
        s_shade[k + 1][1] = gfx_mix(base, WHITE, 100);
        s_shade[k + 1][2] = gfx_mix(base, BLACK, 110);
    }
}

static void tetris_start(void)
{
    memset(s_board, 0, sizeof(s_board));
    init_shades();
    s_bag_left = 0;
    s_score = s_lines = 0;
    s_level = 1;
    s_over = false;
    s_clear_mask = 0;
    s_age = 0;
    s_next = draw_from_bag();
    spawn();
}

static bool tetris_update(const input_t *in, float dt)
{
    s_age += dt;
    if (s_over) return false;
    if (s_clear_mask) {
        s_clear_t -= dt;
        if (s_clear_t <= 0) finish_clear();
        return !s_over;
    }

    // Side buttons with auto-repeat
    if (in->pressed & BTN_LEFT) {
        if (try_move(-1, 0)) sfx(SFX_MOVE);
        s_das_t = DAS_DELAY;
    }
    if (in->pressed & BTN_RIGHT) {
        if (try_move(1, 0)) sfx(SFX_MOVE);
        s_das_t = DAS_DELAY;
    }
    int held = in->held & (BTN_LEFT | BTN_RIGHT);
    if ((held == BTN_LEFT || held == BTN_RIGHT) && !(in->pressed & held)) {
        s_das_t -= dt;
        while (s_das_t <= 0) {
            if (try_move(held == BTN_LEFT ? -1 : 1, 0)) sfx(SFX_MOVE);
            s_das_t += DAS_REPEAT;
        }
    }

    // Dragging moves one cell per DRAG_STEP pixels; dragging down soft-drops
    if (in->touch_start) {
        s_drag_x = in->x;
        s_drag_y = in->y;
    }
    if (in->touching) {
        while (in->x - s_drag_x >= DRAG_STEP) {
            if (try_move(1, 0)) sfx(SFX_MOVE);
            s_drag_x += DRAG_STEP;
        }
        while (s_drag_x - in->x >= DRAG_STEP) {
            if (try_move(-1, 0)) sfx(SFX_MOVE);
            s_drag_x -= DRAG_STEP;
        }
        while (in->y - s_drag_y >= DRAG_STEP) {
            if (try_move(0, 1)) {
                s_score += 1;
                s_fall_t = 0;
            }
            s_drag_y += DRAG_STEP;
        }
        if (in->y < s_drag_y) s_drag_y = in->y;
    }

    if (in->swipe == SWIPE_DOWN) {
        int dist = 0;
        while (try_move(0, 1)) dist++;
        s_score += 2 * dist;
        sfx(SFX_DROP);
        lock_piece();
        return !s_over;
    }
    if (in->swipe == SWIPE_UP) try_rotate(-1);
    if (in->tap || (in->pressed & BTN_PWR)) try_rotate(1);

    if (!grounded()) {
        s_lock_t = 0;
        s_fall_t += dt;
        float g = gravity();
        while (s_fall_t >= g) {
            s_fall_t -= g;
            if (!try_move(0, 1)) break;
        }
    } else {
        s_fall_t = 0;
        s_lock_t += dt;
        if (s_lock_t >= LOCK_DELAY) lock_piece();
    }
    return !s_over;
}

static void draw_cell(int x, int y, int size, int kind1)
{
    const color_t *s = s_shade[kind1];
    int b = size > 16 ? 3 : 2;
    gfx_fill_rect(x + 1, y + 1, size - 2, size - 2, s[0]);
    gfx_fill_rect(x + 1, y + 1, size - 2, b, s[1]);
    gfx_fill_rect(x + 1, y + 1, b, size - 2, s[1]);
    gfx_fill_rect(x + 1, y + size - 1 - b, size - 2, b, s[2]);
    gfx_fill_rect(x + size - 1 - b, y + 1 + b, b, size - 2 - b, s[2]);
}

static void draw_piece(int kind, int rot, int ox, int oy, int size)
{
    int cells[4][2];
    cells_of(kind, rot, cells);
    for (int i = 0; i < 4; i++) {
        draw_cell(ox + cells[i][1] * size, oy + cells[i][0] * size, size, kind + 1);
    }
}

static void draw_stat(int x, int y, const char *label, int value)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", value);
    gfx_text(x, y, label, 2, LABEL);
    gfx_text(x, y + 24, buf, gfx_text_width(buf, 3) <= 110 ? 3 : 2, WHITE);
}

static void tetris_draw(void)
{
    gfx_clear(BG);

    gfx_fill_rect(WELL_X - 4, WELL_Y - 4, COLS * CELL + 8, ROWS * CELL + 8, HEX(0x3C3C78));
    gfx_fill_rect(WELL_X, WELL_Y, COLS * CELL, ROWS * CELL, HEX(0x0E0E22));
    for (int c = 1; c < COLS; c++) {
        gfx_fill_rect(WELL_X + c * CELL, WELL_Y, 1, ROWS * CELL, HEX(0x17173A));
    }

    bool flash = ((int)(s_clear_t * 20)) & 1;
    for (int r = 0; r < ROWS; r++) {
        int y = WELL_Y + r * CELL;
        if (!gfx_visible(y, CELL)) continue;
        if ((s_clear_mask & (1u << r)) && flash) {
            gfx_fill_rect(WELL_X, y + 1, COLS * CELL, CELL - 2, WHITE);
            continue;
        }
        for (int c = 0; c < COLS; c++) {
            if (s_board[r][c]) draw_cell(WELL_X + c * CELL, y, CELL, s_board[r][c]);
        }
    }

    if (!s_clear_mask && !s_over) {
        int gy = s_py;
        while (fits(s_kind, s_rot, s_px, gy + 1)) gy++;
        int cells[4][2];
        cells_of(s_kind, s_rot, cells);
        for (int i = 0; i < 4; i++) {
            int r = gy + cells[i][0];
            if (r < 0) continue;
            gfx_rect(WELL_X + (s_px + cells[i][1]) * CELL + 2, WELL_Y + r * CELL + 2, CELL - 4, CELL - 4, 2,
                     s_shade[s_kind + 1][2]);
        }
        for (int i = 0; i < 4; i++) {
            int r = s_py + cells[i][0];
            if (r < 0) continue;
            draw_cell(WELL_X + (s_px + cells[i][1]) * CELL, WELL_Y + r * CELL, CELL, s_kind + 1);
        }
    }

    draw_stat(14, 60, "SCORE", s_score);
    draw_stat(14, 150, "LINES", s_lines);
    draw_stat(366, 200, "LEVEL", s_level);

    gfx_text(366, 60, "NEXT", 2, LABEL);
    gfx_fill_round_rect(362, 84, 100, 80, 10, HEX(0x15152E));
    int cells[4][2], minr = 9, maxr = 0, minc = 9, maxc = 0;
    cells_of(s_next, 0, cells);
    for (int i = 0; i < 4; i++) {
        if (cells[i][0] < minr) minr = cells[i][0];
        if (cells[i][0] > maxr) maxr = cells[i][0];
        if (cells[i][1] < minc) minc = cells[i][1];
        if (cells[i][1] > maxc) maxc = cells[i][1];
    }
    int sz = 18;
    int ox = 362 + (100 - (maxc - minc + 1) * sz) / 2 - minc * sz;
    int oy = 84 + (80 - (maxr - minr + 1) * sz) / 2 - minr * sz;
    draw_piece(s_next, 0, ox, oy, sz);

    if (s_age < 4.0f) {
        gfx_fill_round_rect(WELL_X + 6, 300, COLS * CELL - 12, 92, 12, HEX(0x15152E));
        gfx_text_center(WELL_X + COLS * CELL / 2, 312, "TAP/PWR: ROTATE", 2, WHITE);
        gfx_text_center(WELL_X + COLS * CELL / 2, 338, "DRAG: MOVE", 2, WHITE);
        gfx_text_center(WELL_X + COLS * CELL / 2, 364, "FLICK DOWN: DROP", 2, WHITE);
    }
}

static int tetris_score(void)
{
    return s_score;
}

static void tetris_icon(int x, int y, int size)
{
    int c = size / 4;
    static const int8_t layout[][3] = {
        {0, 1, 2}, {1, 0, 2}, {1, 1, 2}, {1, 2, 2},             // T
        {2, 0, 0}, {3, 0, 0}, {3, 1, 0}, {3, 2, 0},             // I-ish row
        {2, 3, 6}, {3, 3, 6}, {1, 3, 6}, {0, 3, 6},             // L column
    };
    if (!s_shade[1][0]) init_shades();
    for (int i = 0; i < (int)(sizeof(layout) / sizeof(layout[0])); i++) {
        draw_cell(x + layout[i][1] * c, y + layout[i][0] * c, c, layout[i][2] + 1);
    }
}

const game_t game_tetris = {
    .name = "TETRIS",
    .nvs_key = "hi_tetris",
    .accent = 0x3DD6F5,
    .uses_pwr = true,
    .start = tetris_start,
    .update = tetris_update,
    .draw = tetris_draw,
    .score = tetris_score,
    .draw_icon = tetris_icon,
};

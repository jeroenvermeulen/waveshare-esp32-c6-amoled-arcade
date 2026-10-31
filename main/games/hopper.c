// Hopper: guide Blip forward across endless lanes of traffic and rivers.
// Tap to hop forward, swipe or use the side buttons to turn.

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "audio.h"
#include "board.h"
#include "game.h"
#include "util.h"

#define T           48          // tile size
#define NCOLS       (LCD_W / T)
#define RING        16          // lanes kept in memory, power of two
#define WRAP        624.0f      // lane loop length in px: screen + 3 tiles
#define XMIN        -144.0f
#define HOP_TIME    0.13f
#define PLAYER_ROW_ON_SCREEN 3
#define MAX_AHEAD   6           // camera never lets the player get further up than this

enum { L_GRASS, L_ROAD, L_RIVER };
enum { DIR_UP, DIR_RIGHT, DIR_DOWN, DIR_LEFT };
enum { DEATH_NONE, DEATH_CAR, DEATH_WATER, DEATH_CAUGHT };

static const int8_t s_dcol[4] = { 0, 1, 0, -1 };
static const int8_t s_drow[4] = { 1, 0, -1, 0 };
static const uint32_t s_car_hex[] = { 0xF0504A, 0x3D9AF5, 0xF5D33D, 0xE05CC8, 0xF8F8F8 };

typedef struct {
    uint8_t type;
    int8_t dir;
    uint8_t n;
    uint16_t trees;     // grass: bit per column
    float speed;
    float x[4];
    uint8_t len[4];
    uint8_t color[4];
} lane_t;

static lane_t s_lanes[RING];
static int s_gen_top;       // rows below this are generated
static int s_group_type, s_group_left;

static int s_row, s_face, s_queued, s_score, s_death;
static float s_x;           // player left edge in px; fractional while riding a log
static bool s_hopping;
static float s_hop_t, s_hop_fx, s_hop_tx;
static int s_hop_frow, s_hop_trow;
static float s_cam, s_age, s_dead_t;

static lane_t *lane(int r)
{
    return &s_lanes[r & (RING - 1)];
}

static void gen_lane(int r)
{
    lane_t *l = lane(r);
    memset(l, 0, sizeof(*l));
    if (r < 3) return;   // safe grass to start on

    if (s_group_left <= 0) {
        if (s_group_type != L_GRASS && rnd(100) < 55) s_group_type = L_GRASS;
        else s_group_type = rnd(100) < 62 ? L_ROAD : L_RIVER;
        s_group_left = s_group_type == L_GRASS ? 1 + rnd(2) : s_group_type == L_ROAD ? 1 + rnd(4) : 1 + rnd(3);
    }
    s_group_left--;
    l->type = s_group_type;

    float diff = 1.0f + r / 90.0f;
    if (diff > 2.2f) diff = 2.2f;

    if (l->type == L_GRASS) {
        for (int i = rnd(4); i > 0; i--) l->trees |= 1 << rnd(NCOLS);
        return;
    }

    l->dir = rnd(2) ? 1 : -1;
    if (l->type == L_ROAD) {
        l->speed = (55 + rnd(70)) * diff;
        l->n = 1 + rnd(3);
        for (int i = 0; i < l->n; i++) {
            l->len[i] = rnd(5) == 0 ? 2 : 1;
            l->color[i] = rnd(sizeof(s_car_hex) / sizeof(s_car_hex[0]));
        }
    } else {
        l->speed = (35 + rnd(45)) * diff;
        l->n = 3;
        for (int i = 0; i < l->n; i++) l->len[i] = 2 + rnd(3);
    }
    float slot = WRAP / l->n;
    for (int i = 0; i < l->n; i++) {
        float slack = slot - l->len[i] * T - T;
        l->x[i] = XMIN + i * slot + (slack > 0 ? rndf() * slack : 0);
    }
}

static void hopper_start(void)
{
    s_gen_top = 0;
    s_group_type = L_GRASS;
    s_group_left = 0;
    s_row = 1;
    s_x = (NCOLS / 2) * T;
    s_face = DIR_UP;
    s_queued = -1;
    s_score = 0;
    s_death = DEATH_NONE;
    s_hopping = false;
    s_cam = 0;
    s_age = 0;
    s_dead_t = 0;
    while (s_gen_top < RING - 2) gen_lane(s_gen_top++);
}

static void try_hop(int dir)
{
    s_face = dir;
    int col = (int)lroundf(s_x / T);
    if (col < 0) col = 0;
    if (col >= NCOLS) col = NCOLS - 1;
    int tc = col + s_dcol[dir], tr = s_row + s_drow[dir];
    if (tc < 0 || tc >= NCOLS || tr < 0 || tr >= s_gen_top) return;
    const lane_t *l = lane(tr);
    if (l->type == L_GRASS && (l->trees >> tc) & 1) return;

    s_hopping = true;
    s_hop_t = 0;
    s_hop_fx = s_x;
    s_hop_tx = tc * T;
    s_hop_frow = s_row;
    s_hop_trow = tr;
    sfx(SFX_HOP);
}

static void die(int how)
{
    s_death = how;
    s_dead_t = 0;
    s_hopping = false;
    sfx(how == DEATH_WATER ? SFX_SPLASH : SFX_CRASH);
}

static int log_under(const lane_t *l, float x)
{
    float cx = x + T / 2;
    for (int i = 0; i < l->n; i++) {
        if (cx >= l->x[i] + 4 && cx <= l->x[i] + l->len[i] * T - 4) return i;
    }
    return -1;
}

static bool hopper_update(const input_t *in, float dt)
{
    s_age += dt;
    if (s_death) {
        s_dead_t += dt;
        return s_dead_t < 1.2f;
    }

    int dir = -1;
    if (in->tap || in->swipe == SWIPE_UP) dir = DIR_UP;
    else if (in->swipe == SWIPE_DOWN) dir = DIR_DOWN;
    else if (in->swipe == SWIPE_LEFT) dir = DIR_LEFT;
    else if (in->swipe == SWIPE_RIGHT) dir = DIR_RIGHT;
    if (in->pressed & BTN_LEFT) dir = DIR_LEFT;
    if (in->pressed & BTN_RIGHT) dir = DIR_RIGHT;
    if (dir >= 0) {
        if (!s_hopping) try_hop(dir);
        else if (s_hop_t > HOP_TIME * 0.4f) s_queued = dir;
    }

    int first = (int)s_cam - 2;
    for (int r = first < 0 ? 0 : first; r < s_gen_top; r++) {
        lane_t *l = lane(r);
        if (l->type == L_GRASS) continue;
        for (int i = 0; i < l->n; i++) {
            l->x[i] += l->dir * l->speed * dt;
            if (l->x[i] >= LCD_W) l->x[i] -= WRAP;
            else if (l->x[i] < XMIN) l->x[i] += WRAP;
        }
    }

    if (s_hopping) {
        s_hop_t += dt;
        if (s_hop_t >= HOP_TIME) {
            s_hopping = false;
            s_row = s_hop_trow;
            s_x = s_hop_tx;
            if (s_row > s_score) s_score = s_row;
            if (lane(s_row)->type == L_RIVER && log_under(lane(s_row), s_x) < 0) {
                die(DEATH_WATER);
                return true;
            }
            if (s_queued >= 0) {
                try_hop(s_queued);
                s_queued = -1;
            }
        }
    } else if (lane(s_row)->type == L_RIVER) {
        const lane_t *l = lane(s_row);
        s_x += l->dir * l->speed * dt;
        if (s_x + T / 2 < 0 || s_x + T / 2 > LCD_W) {
            die(DEATH_WATER);
            return true;
        }
    }

    // Cars: count the player as in the target lane once past the hop's midpoint
    float k = s_hopping ? s_hop_t / HOP_TIME : 0;
    int row = s_hopping ? (k < 0.5f ? s_hop_frow : s_hop_trow) : s_row;
    float px = s_hopping ? s_hop_fx + (s_hop_tx - s_hop_fx) * k : s_x;
    const lane_t *l = lane(row);
    if (l->type == L_ROAD) {
        for (int i = 0; i < l->n; i++) {
            if (px + T - 10 > l->x[i] + 3 && px + 10 < l->x[i] + l->len[i] * T - 3) {
                die(DEATH_CAR);
                return true;
            }
        }
    }

    // Camera follows the player and slowly creeps forward once the run has started
    float target = s_row - PLAYER_ROW_ON_SCREEN;
    if (s_cam < target) s_cam += (target - s_cam) * clampf(dt * 3, 0, 1);
    if (s_score > 0) {
        float creep = 0.15f + s_score * 0.004f;
        s_cam += (creep > 0.55f ? 0.55f : creep) * dt;
    }
    if (s_cam < s_row - MAX_AHEAD) s_cam = s_row - MAX_AHEAD;
    while (s_gen_top < (int)s_cam + 13) gen_lane(s_gen_top++);

    if (s_row + 1 - s_cam < 0.4f) die(DEATH_CAUGHT);
    return true;
}

static int row_y(int r)
{
    return (int)(LCD_H - (r + 1 - s_cam) * T);
}

static void draw_car(int x, int y, int len, int dir, color_t c)
{
    int w = len * T - 8;
    color_t wheel = HEX(0x1A1A1A);
    gfx_fill_rect(x + 10, y + 5, 10, 5, wheel);
    gfx_fill_rect(x + w - 16, y + 5, 10, 5, wheel);
    gfx_fill_rect(x + 10, y + 38, 10, 5, wheel);
    gfx_fill_rect(x + w - 16, y + 38, 10, 5, wheel);
    gfx_fill_round_rect(x + 4, y + 8, w, 32, 9, c);
    gfx_fill_round_rect(x + 4 + w / 4, y + 13, w / 2, 22, 5, gfx_mix(c, BLACK, 70));
    int wx = dir > 0 ? x + 4 + w * 3 / 4 - 2 : x + 4 + w / 4 - 6;
    gfx_fill_rect(wx, y + 14, 8, 20, HEX(0xA8D8F0));
}

static void draw_lane(int r, int y)
{
    const lane_t *l = lane(r);
    switch (l->type) {
    case L_GRASS:
        gfx_fill_rect(0, y, LCD_W, T, (r & 1) ? HEX(0x48A84B) : HEX(0x52B455));
        for (int c = 0; c < NCOLS; c++) {
            if (!((l->trees >> c) & 1)) continue;
            int x = c * T;
            gfx_fill_rect(x + 20, y + 28, 8, 14, HEX(0x7A4A22));
            gfx_fill_circle(x + 24, y + 22, 17, HEX(0x2E7D32));
            gfx_fill_circle(x + 18, y + 16, 6, HEX(0x43A047));
        }
        break;
    case L_ROAD:
        gfx_fill_rect(0, y, LCD_W, T, HEX(0x3B3B46));
        if (lane(r + 1)->type == L_ROAD) {
            for (int x = 0; x < LCD_W; x += T) gfx_fill_rect(x + 11, y, 26, 3, HEX(0xBDBDC8));
        }
        for (int i = 0; i < l->n; i++) {
            draw_car((int)l->x[i], y, l->len[i], l->dir, HEX(s_car_hex[l->color[i]]));
        }
        break;
    case L_RIVER:
        gfx_fill_rect(0, y, LCD_W, T, HEX(0x2F74D0));
        for (int i = 0; i < 6; i++) {
            int wx = ((int)(s_age * 18 * l->dir) + i * 83 + r * 37) % LCD_W;
            if (wx < 0) wx += LCD_W;
            gfx_fill_rect(wx, y + 12 + (i & 1) * 18, 20, 3, HEX(0x5A95E6));
        }
        for (int i = 0; i < l->n; i++) {
            int x = (int)l->x[i], w = l->len[i] * T - 6;
            gfx_fill_round_rect(x + 3, y + 8, w, 32, 12, HEX(0x8B5A2B));
            gfx_fill_rect(x + 14, y + 13, w - 22, 4, HEX(0xA8743F));
            gfx_fill_circle(x + 3 + w - 10, y + 24, 7, HEX(0x6B4220));
        }
        break;
    }
}

static void draw_player(void)
{
    float k = s_hopping ? s_hop_t / HOP_TIME : 0;
    float rowf = s_hopping ? s_hop_frow + (s_hop_trow - s_hop_frow) * k : s_row;
    float xf = s_hopping ? s_hop_fx + (s_hop_tx - s_hop_fx) * k : s_x;
    int x = (int)xf + T / 2;
    int y = (int)(LCD_H - (rowf + 1 - s_cam) * T) + T / 2;
    int lift = s_hopping ? (int)(sinf(k * 3.14159f) * 12) : 0;
    color_t body = HEX(0xFFD23F);

    if (s_death == DEATH_CAR) {
        gfx_fill_round_rect(x - 22, y - 7, 44, 14, 7, body);
        gfx_fill_rect(x - 10, y - 2, 4, 4, BLACK);
        gfx_fill_rect(x + 6, y - 2, 4, 4, BLACK);
        return;
    }
    if (s_death == DEATH_WATER) {
        int r = 8 + (int)(s_dead_t * 30);
        gfx_fill_circle(x, y, r, HEX(0xCFE6FF));
        gfx_fill_circle(x, y, r - 4 > 0 ? r - 4 : 0, HEX(0x2F74D0));
        return;
    }

    gfx_fill_round_rect(x - 14, y + 10, 28, 8, 4, gfx_mix(HEX(0x000000), HEX(0x48A84B), 120));
    y -= lift;
    gfx_fill_rect(x - 10, y + 10, 7, 6, HEX(0xF59A3D));
    gfx_fill_rect(x + 3, y + 10, 7, 6, HEX(0xF59A3D));
    gfx_fill_circle(x, y, 16, body);
    gfx_fill_circle(x - 4, y - 5, 5, HEX(0xFFE88A));

    int ex = s_dcol[s_face] * 5, ey = -s_drow[s_face] * 5;
    if (s_face == DIR_DOWN || s_face == DIR_UP) {
        gfx_fill_circle(x - 6 + ex, y - 2 + ey, 5, WHITE);
        gfx_fill_circle(x + 6 + ex, y - 2 + ey, 5, WHITE);
        gfx_fill_circle(x - 6 + ex, y - 2 + ey * 3 / 2, 2, BLACK);
        gfx_fill_circle(x + 6 + ex, y - 2 + ey * 3 / 2, 2, BLACK);
    } else {
        gfx_fill_circle(x + ex, y - 6, 5, WHITE);
        gfx_fill_circle(x + ex, y + 4, 5, WHITE);
        gfx_fill_circle(x + ex * 3 / 2, y - 6, 2, BLACK);
        gfx_fill_circle(x + ex * 3 / 2, y + 4, 2, BLACK);
    }
}

static void hopper_draw(void)
{
    int bottom = (int)floorf(s_cam);
    for (int r = bottom - 1; r <= bottom + 10; r++) {
        int y = row_y(r);
        if (!gfx_visible(y, T)) continue;
        if (r < 0) gfx_fill_rect(0, y, LCD_W, T, HEX(0x3E8E41));
        else draw_lane(r, y);
    }
    draw_player();

    char buf[12];
    snprintf(buf, sizeof(buf), "%d", s_score);
    gfx_text_shadow(32, 24, buf, 5, WHITE);

    if (s_score == 0 && !s_death && s_age < 6.0f) {
        gfx_text_center(LCD_W / 2 + 2, 152, "TAP TO HOP", 3, BLACK);
        gfx_text_center(LCD_W / 2, 150, "TAP TO HOP", 3, WHITE);
        gfx_text_center(LCD_W / 2 + 1, 191, "SWIPE OR BUTTONS TO TURN", 2, BLACK);
        gfx_text_center(LCD_W / 2, 190, "SWIPE OR BUTTONS TO TURN", 2, WHITE);
    }
}

static int hopper_score(void)
{
    return s_score;
}

static void hopper_icon(int x, int y, int size)
{
    int lane_h = size / 3;
    gfx_fill_round_rect(x, y, size, size, 10, HEX(0x52B455));
    gfx_fill_rect(x, y + lane_h, size, lane_h, HEX(0x3B3B46));
    for (int i = 0; i < 3; i++) gfx_fill_rect(x + 4 + i * 28, y + lane_h * 3 / 2 - 1, 16, 3, HEX(0xBDBDC8));
    gfx_fill_round_rect(x + size - 34, y + lane_h + 4, 30, lane_h - 8, 5, HEX(0xF0504A));
    int cx = x + size / 2 - 10, cy = y + lane_h * 5 / 2;
    gfx_fill_circle(cx, cy, 11, HEX(0xFFD23F));
    gfx_fill_circle(cx - 4, cy - 3, 3, WHITE);
    gfx_fill_circle(cx + 4, cy - 3, 3, WHITE);
    gfx_fill_circle(cx - 4, cy - 4, 1, BLACK);
    gfx_fill_circle(cx + 4, cy - 4, 1, BLACK);
}

const game_t game_hopper = {
    .name = "HOPPER",
    .nvs_key = "hi_hopper",
    .accent = 0x5CE065,
    .start = hopper_start,
    .update = hopper_update,
    .draw = hopper_draw,
    .score = hopper_score,
    .draw_icon = hopper_icon,
};

// Pip's Quest: side-scrolling platformer. Run and jump through generated levels to
// reach the portal. Stomp slimes and bats, avoid urchins and spikes, bump crates
// and gem boxes from below.

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "audio.h"
#include "board.h"
#include "game.h"
#include "util.h"

#define T           32      // tile size: the screen is 15 x 15 tiles, one display band per row
#define ROWS        15
#define MAX_COLS    220
#define PW          18      // player hitbox
#define PH          26
#define FW          26      // walker hitbox
#define FH          18
#define BW          22      // bat hitbox
#define BH          12
#define GRAVITY     1900.0f
#define JUMP_V      660.0f
#define JUMP_CUT    250.0f  // letting go of jump early caps the rise speed at this
#define STOMP_V     460.0f
#define MAX_FALL    900.0f
#define RUN_MAX     230.0f
#define RUN_ACCEL   1500.0f
#define AIR_ACCEL   1100.0f
#define FRICTION    1800.0f
#define COYOTE      0.08f
#define JUMP_BUF    0.12f
#define SUBSTEP     (1.0f / 120)
#define CAM_LEAD    190
#define START_LIVES 3
#define MAX_LIVES   9
#define DIE_TIME    1.6f
#define CLEAR_TIME  1.8f
#define INVULN_T    1.5f
#define MAX_FOES    24
#define MAX_ITEMS   4
#define MAX_PARTS   48
#define MAX_POPS    6

enum { TL_EMPTY, TL_GROUND, TL_STONE, TL_CRATE, TL_BOX, TL_BOX_P, TL_USED, TL_PLANK, TL_SPIKE, TL_GEM };
enum { FOE_SLIME, FOE_URCHIN, FOE_BAT };
enum { FS_ALIVE, FS_SQUASHED, FS_FLIPPED };
enum { ITEM_SHIELD, ITEM_FEATHER };

typedef struct { float x, y, vy, x0, y0, t; int kind, state, dir; bool on; } foe_t;
typedef struct { float x, y, y0, vx, vy, t; int kind; bool on, landed; } item_t;
typedef struct { float x, y, vx, vy, life; color_t c; } part_t;
typedef struct { float x, y, t; bool on; } pop_t;

typedef struct {
    uint32_t sky_top, sky_bot, hill_far, hill_near, cloud, grass, grass_hi, dirt, dirt_dk;
    bool stars;
} theme_t;

static const theme_t s_themes[3] = {
    { 0x3A8EE8, 0xBDE6FF, 0x8CC7D8, 0x5EAE6A, 0xFFFFFF, 0x4CB84C, 0x8EE070, 0x9A6440, 0x6E4428, false },
    { 0x5A3A9A, 0xF5A05C, 0xB0608A, 0x7A4070, 0xFFD0B0, 0x4C9A50, 0x86C870, 0x8A5A40, 0x5E3A28, false },
    { 0x0A0C2A, 0x2A3070, 0x22285A, 0x1A3A4A, 0x5A6090, 0x2E7A50, 0x56A870, 0x5A4A50, 0x3A2E38, true },
};

// Pip faces right; the last two rows (legs) come from s_legs
static const char *const s_pip_px[12] = {
    "...222222...",
    "..21111112..",
    ".2111111112.",
    ".2113333312.",
    ".2113434312.",
    ".2113333312.",
    "..21333312..",
    ".555555555..",
    "55.2111112..",
    "...2111112..",
    "...2111112..",
    "...2222222..",
};
enum { LEGS_IDLE, LEGS_RUN1, LEGS_RUN2, LEGS_JUMP };
static const char *const s_legs[4][2] = {
    { "...77...77..", "...77...77.." },
    { "..77.....77.", ".77.......77" },
    { "....7777....", "....77.77..." },
    { "...77.77....", "..77...77..." },
};
static const char *const s_slime_px[7] = {
    "...3333...",
    "..333333..",
    ".33843843.",
    ".33333333.",
    "3333333333",
    "3333333333",
    ".33.33.33.",
};
static const char *const s_urchin_px[7] = {
    "9.9.99.9.9",
    ".99999999.",
    "9338433849",
    "9333333339",
    "9333333339",
    ".99999999.",
    "9.9....9.9",
};
static const char *const s_bat_px[2][6] = {
    { "1..........1", "11..1111..11", "111181181111", ".1111111111.", "....1111....", "............" },
    { "............", "....1111....", "..11811811..", ".1111111111.", "111.1111.111", "11........11" },
};
static const char *const s_feather_px[8] = {
    "......55",
    ".....555",
    "....5885",
    "...5885.",
    "..5885..",
    ".5885...",
    ".555....",
    "7.......",
};
static const char *const s_heart_px[6] = {
    ".11.11.",
    "1111111",
    "1111111",
    ".11111.",
    "..111..",
    "...1...",
};

static const color_t s_pip_pal[8] = {
    0, HEX(0x2EC4B6), HEX(0x17707A), HEX(0xFFD6A8), HEX(0x101018), HEX(0xF5D33D), 0, HEX(0x6A3D2A),
};
static const color_t s_slime_pal[10] = { 0, 0, 0, HEX(0xA05CF0), HEX(0x201030), 0, 0, 0, WHITE, 0 };
static const color_t s_urchin_pal[10] = { 0, 0, 0, HEX(0xF59A3D), HEX(0x201030), 0, 0, 0, WHITE, HEX(0xE8E8F0) };
static const color_t s_bat_pal[10] = { 0, HEX(0x4A3A80), 0, 0, 0, 0, 0, 0, HEX(0xF5D33D), 0 };
static const color_t s_feather_pal[10] = { 0, 0, 0, 0, 0, HEX(0xF0F0FF), 0, HEX(0x8A5A30), HEX(0x9AD8FF), 0 };
static const color_t s_heart_pal[2] = { 0, HEX(0xF0504A) };

static uint8_t s_map[ROWS][MAX_COLS];
static int s_w;
static foe_t s_foes[MAX_FOES];
static item_t s_items[MAX_ITEMS];
static part_t s_parts[MAX_PARTS];
static pop_t s_pops[MAX_POPS];
static int s_part_next;
static uint32_t s_gen;

static float s_x, s_y, s_vx, s_vy, s_coyote, s_jump_buf, s_run_dist, s_inv_t;
static float s_cam, s_time, s_banner_t, s_die_t, s_clear_t;
static float s_goal_x, s_goal_y;
static int s_face, s_score, s_gems, s_lives, s_level;
static bool s_ground, s_full_jump, s_shield, s_feather, s_double_used;
static bool s_dying, s_clearing, s_over, s_hint;

static int grnd(int n)
{
    s_gen = s_gen * 1664525u + 1013904223u;
    return (int)((s_gen >> 8) % (uint32_t)n);
}

static uint32_t hash2(int a, int b)
{
    uint32_t h = (uint32_t)a * 73856093u ^ (uint32_t)b * 19349663u;
    return (h ^ (h >> 13)) * 0x5bd1e995u;
}

static int tile_at(int c, int r)
{
    if (c < 0 || c >= s_w) return TL_STONE;   // walls at both ends of the level
    if (r < 0 || r >= ROWS) return TL_EMPTY;
    return s_map[r][c];
}

static bool solid(int t)
{
    return t >= TL_GROUND && t <= TL_USED;
}

static void ground(int c, int h)
{
    for (int r = h; r < ROWS; r++) s_map[r][c] = TL_GROUND;
}

static void add_foe(int kind, int c, int h)
{
    for (int i = 0; i < MAX_FOES; i++) {
        foe_t *f = &s_foes[i];
        if (f->on) continue;
        *f = (foe_t){ .kind = kind, .on = true, .dir = -1 };
        if (kind == FOE_BAT) {
            f->x = f->x0 = c * T + 5;
            f->y = f->y0 = (h - 2) * T;
        } else {
            f->x = c * T + 3;
            f->y = h * T - FH;
        }
        return;
    }
}

// Levels are built from random chunks, seeded by the level number, so a retry
// gets the same layout
static void build_level(void)
{
    memset(s_map, 0, sizeof(s_map));
    memset(s_foes, 0, sizeof(s_foes));
    memset(s_items, 0, sizeof(s_items));
    memset(s_parts, 0, sizeof(s_parts));
    memset(s_pops, 0, sizeof(s_pops));
    s_gen = 0x9E3779B9u * (uint32_t)s_level + 7;

    int lv = s_level;
    int len = 100 + 14 * lv;
    if (len > MAX_COLS) len = MAX_COLS;
    int h = 12, c = 0;
    while (c < 10) ground(c++, h);

    while (c < len - 18) {
        int n;
        switch (grnd(9)) {
        case 0:     // pit
            c += 2 + grnd(lv >= 3 ? 3 : 2);
            for (int i = 0; i < 3; i++) ground(c++, h);
            break;
        case 1:     // step up or down
            h += (grnd(2) ? 1 : -1) * (1 + grnd(2));
            h = h < 8 ? 8 : h > 12 ? 12 : h;
            for (int i = 0; i < 4; i++) ground(c++, h);
            break;
        case 2: {   // crates and gem boxes to bump, maybe with a slime below
            for (int i = 0; i < 8; i++) ground(c + i, h);
            int p = grnd(4);
            for (int i = 0; i < 4; i++) {
                s_map[h - 4][c + 2 + i] = i == p && grnd(2) ? TL_BOX_P : (i & 1) ? TL_BOX : TL_CRATE;
            }
            if (grnd(2)) add_foe(FOE_SLIME, c + 6, h);
            c += 8;
            break;
        }
        case 3:     // wide pit with a floating plank and gems over it
            for (int i = 2; i <= 4; i++) {
                s_map[h - 3][c + i] = TL_PLANK;
                s_map[h - 4][c + i] = TL_GEM;
            }
            c += 7;
            for (int i = 0; i < 3; i++) ground(c++, h);
            break;
        case 4:     // stone steps up and down, a gem on top
            for (int i = 0; i < 9; i++) ground(c + i, h);
            for (int i = 0; i < 4; i++) {
                int tall = i < 3 ? i + 1 : 3;
                for (int k = 0; k < tall; k++) {
                    s_map[h - 1 - k][c + 1 + i] = TL_STONE;
                    s_map[h - 1 - k][c + 7 - i] = TL_STONE;
                }
            }
            s_map[h - 5][c + 4] = TL_GEM;
            c += 9;
            break;
        case 5: {   // arc of gems
            static const int8_t arc[6] = { 2, 3, 4, 4, 3, 2 };
            for (int i = 0; i < 8; i++) ground(c + i, h);
            for (int i = 0; i < 6; i++) s_map[h - arc[i]][c + 1 + i] = TL_GEM;
            c += 8;
            break;
        }
        case 6:     // spikes from level 2, a slime before that
            for (int i = 0; i < 8; i++) ground(c + i, h);
            if (lv >= 2) s_map[h - 1][c + 3] = s_map[h - 1][c + 4] = TL_SPIKE;
            else add_foe(FOE_SLIME, c + 4, h);
            c += 8;
            break;
        case 7:     // walkers
            n = 6 + grnd(4);
            for (int i = 0; i < n; i++) ground(c + i, h);
            add_foe(lv >= 3 && grnd(3) == 0 ? FOE_URCHIN : FOE_SLIME, c + n - 2, h);
            if (lv >= 2 && grnd(2)) add_foe(FOE_SLIME, c + 2, h);
            c += n;
            break;
        default:    // a bat from level 2, a row of gems before that
            for (int i = 0; i < 8; i++) ground(c + i, h);
            if (lv >= 2) add_foe(FOE_BAT, c + 4, h);
            else for (int i = 3; i <= 5; i++) s_map[h - 2][c + i] = TL_GEM;
            c += 8;
            break;
        }
    }
    while (c < len) ground(c++, h);
    s_w = len;
    s_goal_x = (len - 6) * T + T / 2;
    s_goal_y = h * T;
}

static void restart_level(void)
{
    build_level();
    s_x = 2 * T;
    s_y = 12 * T - PH;
    s_vx = s_vy = 0;
    s_face = 1;
    s_cam = 0;
    s_jump_buf = s_coyote = s_inv_t = 0;
    s_dying = s_clearing = false;
    s_banner_t = 1.5f;
}

static void pipsquest_start(void)
{
    s_level = 1;
    s_lives = START_LIVES;
    s_score = s_gems = 0;
    s_shield = s_feather = false;
    s_over = false;
    s_hint = true;
    s_time = 0;
    restart_level();
}

static void burst(float x, float y, int n, float speed, color_t c)
{
    for (int i = 0; i < n; i++) {
        float a = rndf() * 6.2832f, v = speed * (0.4f + 0.6f * rndf());
        s_parts[s_part_next] = (part_t){ x, y, cosf(a) * v, sinf(a) * v - speed * 0.5f, 0.4f + 0.4f * rndf(), c };
        s_part_next = (s_part_next + 1) % MAX_PARTS;
    }
}

static void add_gem(void)
{
    s_gems++;
    s_score += 10;
    if (s_gems % 100 == 0 && s_lives < MAX_LIVES) {
        s_lives++;
        sfx(SFX_LEVEL_UP);
    } else {
        sfx(SFX_GEM);
    }
}

static void kill_player(void)
{
    s_dying = true;
    s_die_t = 0;
    s_vy = s_y < LCD_H ? -520 : 0;
    s_lives--;
    s_feather = false;
    sfx(SFX_LOSE_LIFE);
}

static void hurt(void)
{
    if (s_inv_t > 0 || s_dying) return;
    if (s_shield) {
        s_shield = false;
        s_inv_t = INVULN_T;
        burst(s_x + PW / 2, s_y + PH / 2, 12, 200, HEX(0x9AF0FF));
        sfx(SFX_HURT);
    } else {
        kill_player();
    }
}

static void flip_foe(foe_t *f)
{
    f->state = FS_FLIPPED;
    f->vy = -360;
    f->t = 0;
    s_score += 100;
}

static void bump(int c, int r)
{
    if (c < 0 || c >= s_w) return;
    int t = s_map[r][c];
    float cx = c * T + T / 2, cy = r * T + T / 2;
    if (t == TL_CRATE) {
        s_map[r][c] = TL_EMPTY;
        burst(cx, cy, 8, 220, HEX(0xB87A3A));
        s_score += 50;
        sfx(SFX_BREAK);
    } else if (t == TL_BOX) {
        s_map[r][c] = TL_USED;
        for (int i = 0; i < MAX_POPS; i++) {
            if (s_pops[i].on) continue;
            s_pops[i] = (pop_t){ cx, r * T - 4, 0, true };
            break;
        }
        add_gem();
    } else if (t == TL_BOX_P) {
        s_map[r][c] = TL_USED;
        for (int i = 0; i < MAX_ITEMS; i++) {
            if (s_items[i].on) continue;
            s_items[i] = (item_t){
                .x = c * T + 4, .y = r * T + 4, .y0 = r * T + 4, .vx = s_face * 80.0f,
                .kind = s_shield ? ITEM_FEATHER : ITEM_SHIELD, .on = true,
            };
            break;
        }
        sfx(SFX_SPROUT);
    } else {
        sfx(SFX_BUMP);
    }
    // Knock out anything standing on the bumped tile
    for (int i = 0; i < MAX_FOES; i++) {
        foe_t *f = &s_foes[i];
        if (f->on && f->state == FS_ALIVE && f->kind != FOE_BAT && fabsf(f->y + FH - r * T) < 3 &&
            f->x + FW > c * T && f->x < c * T + T) {
            flip_foe(f);
        }
    }
}

static void move_x(float d)
{
    s_x += d;
    int r0 = (int)floorf(s_y / T), r1 = (int)floorf((s_y + PH - 1) / T);
    if (d > 0) {
        int c = (int)floorf((s_x + PW) / T);
        for (int r = r0; r <= r1; r++) {
            if (solid(tile_at(c, r))) {
                s_x = c * T - PW;
                s_vx = 0;
                return;
            }
        }
    } else if (d < 0) {
        int c = (int)floorf(s_x / T);
        for (int r = r0; r <= r1; r++) {
            if (solid(tile_at(c, r))) {
                s_x = (c + 1) * T;
                s_vx = 0;
                return;
            }
        }
    }
}

static void move_y(float d)
{
    float old_bottom = s_y + PH;
    s_y += d;
    int c0 = (int)floorf(s_x / T), c1 = (int)floorf((s_x + PW - 0.01f) / T);
    if (d > 0) {
        int r = (int)floorf((s_y + PH) / T);
        for (int c = c0; c <= c1; c++) {
            int t = tile_at(c, r);
            if (solid(t) || (t == TL_PLANK && old_bottom <= r * T + 0.01f)) {
                s_y = r * T - PH;
                s_vy = 0;
                s_ground = true;
                return;
            }
        }
    } else if (d < 0) {
        int r = (int)floorf(s_y / T), hit = -1;
        float mid = s_x + PW / 2;
        for (int c = c0; c <= c1; c++) {
            if (!solid(tile_at(c, r))) continue;
            // Bump the block nearest Pip's middle
            if (hit < 0 || fabsf(c * T + T / 2 - mid) < fabsf(hit * T + T / 2 - mid)) hit = c;
        }
        if (hit >= 0) {
            s_y = (r + 1) * T;
            s_vy = 0;
            bump(hit, r);
        }
    }
}

static bool overlaps(float ax, float ay, float aw, float ah, float bx, float by, float bw, float bh)
{
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

static void touch_tiles(void)
{
    int c0 = (int)floorf(s_x / T), c1 = (int)floorf((s_x + PW - 1) / T);
    int r0 = (int)floorf(s_y / T), r1 = (int)floorf((s_y + PH - 1) / T);
    for (int r = r0 < 0 ? 0 : r0; r <= r1 && r < ROWS; r++) {
        for (int c = c0; c <= c1; c++) {
            int t = tile_at(c, r);
            if (t == TL_GEM) {
                s_map[r][c] = TL_EMPTY;
                add_gem();
            } else if (t == TL_SPIKE && overlaps(s_x, s_y, PW, PH, c * T + 3, r * T + T - 14, T - 6, 14)) {
                hurt();
            }
        }
    }
}

static void update_foes(bool jump_held, float dt)
{
    for (int i = 0; i < MAX_FOES; i++) {
        foe_t *f = &s_foes[i];
        if (!f->on) continue;
        if (f->state == FS_ALIVE && (f->x < s_cam - 64 || f->x > s_cam + LCD_W + 96)) continue;   // asleep
        f->t += dt;
        if (f->state == FS_SQUASHED) {
            if (f->t > 0.5f) f->on = false;
            continue;
        }
        if (f->state == FS_FLIPPED) {
            f->vy += GRAVITY * dt;
            f->y += f->vy * dt;
            if (f->y > LCD_H + 40) f->on = false;
            continue;
        }

        float w = FW, h = FH;
        if (f->kind == FOE_BAT) {
            w = BW;
            h = BH;
            f->x = f->x0 + sinf(f->t * 1.3f) * 64;
            f->y = f->y0 + sinf(f->t * 3.1f) * 14;
            f->dir = cosf(f->t * 1.3f) > 0 ? 1 : -1;
        } else {
            float nx = f->x + f->dir * (f->kind == FOE_URCHIN ? 40 : 50) * dt;
            int front = (int)floorf((f->dir > 0 ? nx + FW : nx) / T);
            int body = (int)floorf((f->y + FH / 2) / T), below = (int)floorf((f->y + FH + 1) / T);
            // Turn around at walls and ledges
            if (solid(tile_at(front, body)) || !solid(tile_at(front, below))) f->dir = -f->dir;
            else f->x = nx;
        }

        if (s_dying || s_clearing || !overlaps(s_x, s_y, PW, PH, f->x, f->y, w, h)) continue;
        bool from_above = s_vy > 0 && s_y + PH - f->y < 14;
        if (from_above && f->kind != FOE_URCHIN) {
            if (f->kind == FOE_BAT) {
                flip_foe(f);
            } else {
                f->state = FS_SQUASHED;
                f->t = 0;
                s_score += 100;
            }
            s_vy = jump_held ? -JUMP_V * 0.85f : -STOMP_V;
            s_full_jump = false;
            s_double_used = false;
            sfx(SFX_STOMP);
        } else {
            hurt();
        }
    }
}

static void update_effects(float dt)
{
    for (int i = 0; i < MAX_PARTS; i++) {
        part_t *p = &s_parts[i];
        if (p->life <= 0) continue;
        p->vy += GRAVITY * 0.5f * dt;
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        p->life -= dt;
    }
    for (int i = 0; i < MAX_POPS; i++) {
        if (s_pops[i].on && (s_pops[i].t += dt) > 0.45f) s_pops[i].on = false;
    }
    for (int i = 0; i < MAX_ITEMS; i++) {
        item_t *it = &s_items[i];
        if (!it->on) continue;
        it->t += dt;
        // Rise out of the box, then hop off in the direction Pip faces and fall to
        // the ground. It passes through blocks, which are too high to stand on.
        if (it->t < 0.4f) {
            it->y = it->y0 - T * it->t / 0.4f;
            it->vy = -280;
        } else if (!it->landed) {
            it->vy = fminf(it->vy + GRAVITY * 0.6f * dt, MAX_FALL);
            it->x += it->vx * dt;
            it->y += it->vy * dt;
            int r = (int)floorf((it->y + 24) / T);
            int t0 = tile_at((int)floorf(it->x / T), r), t1 = tile_at((int)floorf((it->x + 23) / T), r);
            if (it->vy > 0 && (t0 == TL_GROUND || t0 == TL_STONE || t1 == TL_GROUND || t1 == TL_STONE)) {
                it->y = r * T - 24;
                it->landed = true;
            }
            if (it->y > LCD_H) it->on = false;
        }
        if (it->t > 0.2f && !s_dying && overlaps(s_x, s_y, PW, PH, it->x, it->y, 24, 24)) {
            it->on = false;
            s_score += 500;
            if (it->kind == ITEM_SHIELD) s_shield = true;
            else s_feather = true;
            sfx(SFX_POWERUP);
        }
    }
}

static bool pipsquest_update(const input_t *in, float dt)
{
    if (s_over) return false;
    s_time += dt;
    if (s_banner_t > 0) s_banner_t -= dt;
    if (s_inv_t > 0) s_inv_t -= dt;
    if (in->touch_start || in->pressed || s_time > 6.0f) s_hint = false;
    update_effects(dt);

    if (s_dying) {
        s_die_t += dt;
        if (s_die_t > 0.3f) {
            s_vy += GRAVITY * dt;
            s_y += s_vy * dt;
        }
        if (s_die_t > DIE_TIME) {
            if (s_lives <= 0) {
                s_over = true;
                return false;
            }
            restart_level();
        }
        return true;
    }
    if (s_clearing) {
        s_clear_t += dt;
        if (s_clear_t > CLEAR_TIME) {
            s_level++;
            restart_level();
        }
        return true;
    }

    // Buttons, or hold a finger on the left / right half to run. PWR, a tap or a
    // flick up jumps; with a finger down, a flick up keeps running.
    int dir = 0;
    if ((in->held & BTN_LEFT) || (in->touching && in->x < LCD_W / 2)) dir--;
    if ((in->held & BTN_RIGHT) || (in->touching && in->x >= LCD_W / 2)) dir++;
    bool jump_held = (in->held & BTN_PWR) || in->touching;
    if ((in->pressed & BTN_PWR) || in->swipe == SWIPE_UP || in->tap) {
        s_jump_buf = JUMP_BUF;
        s_full_jump = in->tap;  // a tap is over before the jump starts, so don't cut it short
    } else if (s_jump_buf > 0) {
        s_jump_buf -= dt;
    }

    if (dir) {
        s_vx += dir * (s_ground ? RUN_ACCEL : AIR_ACCEL) * dt;
        s_face = dir;
    } else if (s_ground) {
        float f = FRICTION * dt;
        s_vx = fabsf(s_vx) <= f ? 0 : s_vx - copysignf(f, s_vx);
    }
    s_vx = clampf(s_vx, -RUN_MAX, RUN_MAX);

    if (s_ground) {
        s_coyote = COYOTE;
        s_double_used = false;
    } else {
        s_coyote -= dt;
    }
    if (s_jump_buf > 0) {
        if (s_coyote > 0) {
            s_vy = -JUMP_V;
            s_coyote = s_jump_buf = 0;
            sfx(SFX_JUMP);
        } else if (s_feather && !s_double_used) {
            s_vy = -JUMP_V * 0.9f;
            s_double_used = true;
            s_jump_buf = 0;
            burst(s_x + PW / 2, s_y + PH, 6, 120, WHITE);
            sfx(SFX_JUMP);
        }
    }
    if (!jump_held && !s_full_jump && s_vy < -JUMP_CUT) s_vy = -JUMP_CUT;

    s_ground = false;
    int steps = 1 + (int)(dt / SUBSTEP);
    for (int i = 0; i < steps; i++) {
        float h = dt / steps;
        s_vy = fminf(s_vy + GRAVITY * h, MAX_FALL);
        move_x(s_vx * h);
        move_y(s_vy * h);
    }
    if (s_ground) s_run_dist += fabsf(s_vx) * dt;
    touch_tiles();
    update_foes(jump_held, dt);

    if (!s_dying && s_y > LCD_H + 10) kill_player();
    if (!s_dying && fabsf(s_x + PW / 2 - s_goal_x) < 14 && s_y + PH > s_goal_y - 96) {
        s_clearing = true;
        s_clear_t = 0;
        s_score += 1000;
        burst(s_goal_x, s_goal_y - 48, 16, 240, HEX(0x9AF0FF));
        sfx(SFX_PORTAL);
    }

    float target = clampf(s_x + PW / 2 - CAM_LEAD, 0, s_w * T - LCD_W);
    s_cam += (target - s_cam) * clampf(dt * 10, 0, 1);
    return true;
}

static void draw_sky(const theme_t *th)
{
    color_t top = HEX(th->sky_top), bot = HEX(th->sky_bot);
    for (int y = 0; y < LCD_H; y += 8) {
        if (gfx_visible(y, 8)) gfx_fill_rect(0, y, LCD_W, 8, gfx_mix(top, bot, y * 256 / LCD_H));
    }
    if (th->stars) {
        for (int i = 0; i < 30; i++) {
            uint32_t h = hash2(i, 99);
            int x = (int)((h % 1600) - s_cam * 0.05f) % 1600;
            if (x < 0) x += 1600;
            if (x < LCD_W) gfx_fill_rect(x, (h >> 12) % 260, 2, 2, (h >> 20) & 1 ? WHITE : HEX(0x9AA0D0));
        }
    }

    // Clouds and two layers of hills, each scrolling slower than the level
    for (int layer = 0; layer < 3; layer++) {
        static const float speed[3] = { 0.12f, 0.25f, 0.5f };
        static const int spacing[3] = { 260, 300, 220 };
        float off = s_cam * speed[layer];
        int k0 = (int)(off / spacing[layer]) - 1;
        for (int k = k0; k < k0 + LCD_W / spacing[layer] + 3; k++) {
            uint32_t h = hash2(k, layer);
            int cx = k * spacing[layer] + (int)(h % 80) - (int)off;
            if (layer == 0) {
                int cy = 50 + (int)((h >> 8) % 110);
                if (!gfx_visible(cy - 22, 48)) continue;
                color_t c = HEX(th->cloud);
                gfx_fill_round_rect(cx - 50, cy, 100, 24, 12, c);
                gfx_fill_circle(cx - 14, cy, 20, c);
                gfx_fill_circle(cx + 16, cy + 2, 15, c);
            } else if (layer == 1) {
                gfx_fill_circle(cx, 430, 140 + (int)((h >> 8) % 60), HEX(th->hill_far));
            } else {
                gfx_fill_circle(cx, 470, 90 + (int)((h >> 8) % 40), HEX(th->hill_near));
            }
        }
    }
}

static void draw_gem(int cx, int y, int w, color_t c)
{
    static const uint8_t pct[5] = { 30, 70, 100, 70, 30 };
    for (int i = 0; i < 5; i++) {
        int rw = w * pct[i] / 100;
        if (rw < 2) rw = 2;
        gfx_fill_rect(cx - rw / 2, y + i * 3, rw, 3, i == 1 ? gfx_mix(c, WHITE, 140) : c);
    }
}

static void draw_tile(const theme_t *th, int t, int x, int y, int c, int r)
{
    uint32_t h = hash2(c, r);
    switch (t) {
    case TL_GROUND: {
        color_t dirt_dk = HEX(th->dirt_dk);
        gfx_fill_rect(x, y, T, T, HEX(th->dirt));
        gfx_fill_rect(x + 4 + (h & 15), y + 12 + ((h >> 4) & 15), 4, 4, dirt_dk);
        gfx_fill_rect(x + 2 + ((h >> 8) & 15), y + 4 + ((h >> 12) & 15), 4, 4, dirt_dk);
        if (tile_at(c, r - 1) != TL_GROUND) {
            gfx_fill_rect(x, y, T, 10, HEX(th->grass));
            gfx_fill_rect(x, y, T, 3, HEX(th->grass_hi));
            gfx_fill_rect(x + 4 + ((h >> 16) & 15), y + 10, 4, 4, HEX(th->grass));
        }
        break;
    }
    case TL_STONE:
        gfx_fill_rect(x, y, T, T, HEX(0x5A5E74));
        gfx_fill_rect(x, y, T - 3, T - 3, HEX(0xB8BCD0));
        gfx_fill_rect(x + 3, y + 3, T - 6, T - 6, HEX(0x8A8FA8));
        break;
    case TL_CRATE:
        gfx_fill_rect(x, y, T, T, HEX(0xB87A3A));
        gfx_rect(x, y, T, T, 3, HEX(0x7A4A22));
        gfx_fill_rect(x + 3, y + 10, T - 6, 2, HEX(0x8A5A2A));
        gfx_fill_rect(x + 3, y + 20, T - 6, 2, HEX(0x8A5A2A));
        gfx_fill_rect(x + 5, y + 5, 2, 2, HEX(0xE0C080));
        gfx_fill_rect(x + T - 7, y + 5, 2, 2, HEX(0xE0C080));
        break;
    case TL_BOX:
    case TL_BOX_P: {
        int pulse = (int)(128 + 127 * sinf(s_time * 5));
        gfx_fill_rect(x, y, T, T, HEX(0x1E5AA0));
        gfx_fill_rect(x + 3, y + 3, T - 6, T - 6, HEX(0x3D9AF5));
        gfx_fill_rect(x + 3, y + 3, T - 6, 3, HEX(0x8AC8FF));
        draw_gem(x + T / 2, y + 9, 14, gfx_mix(HEX(0x9AF0FF), WHITE, pulse));
        break;
    }
    case TL_USED:
        gfx_fill_rect(x, y, T, T, HEX(0x3A4660));
        gfx_fill_rect(x + 3, y + 3, T - 6, T - 6, HEX(0x5A6A8A));
        break;
    case TL_PLANK:
        gfx_fill_rect(x, y, T, 8, HEX(0xC89050));
        gfx_fill_rect(x, y + 8, T, 3, HEX(0x7A4A22));
        gfx_fill_rect(x + (c & 1 ? 6 : 20), y + 3, 2, 2, HEX(0x7A4A22));
        break;
    case TL_SPIKE:
        for (int k = 0; k < 3; k++) {
            for (int i = 0; i < 5; i++) {
                gfx_fill_rect(x + 1 + k * 10 + i, y + T - 3 * (i + 1), 10 - 2 * i, 3, i ? HEX(0xC8CCD8) : HEX(0x8A8FA8));
            }
        }
        break;
    case TL_GEM:
        draw_gem(x + T / 2, y + 8, 2 + (int)(14 * fabsf(cosf(s_time * 3 + c * 0.7f))), HEX(0x5CE0F5));
        break;
    }
}

static void draw_portal(int x, int gy)
{
    if (!gfx_visible(gy - 100, 100)) return;
    int pulse = (int)(128 + 120 * sinf(s_time * 6));
    gfx_fill_round_rect(x - 26, gy - 98, 52, 98, 26, HEX(0x8A5CF0));
    gfx_fill_round_rect(x - 20, gy - 92, 40, 88, 20, gfx_mix(HEX(0x3DD6F5), WHITE, pulse));
    gfx_fill_round_rect(x - 13, gy - 82, 26, 72, 13, HEX(0x1A1050));
    for (int i = 0; i < 4; i++) {
        float a = s_time * 2.5f + i * 1.5708f;
        gfx_fill_rect(x + (int)(cosf(a) * 32) - 2, gy - 50 + (int)(sinf(a) * 54) - 2, 4, 4, WHITE);
    }
}

static void draw_pip(int x, int y)
{
    int legs = LEGS_IDLE;
    if (!s_ground || s_dying) legs = LEGS_JUMP;
    else if (fabsf(s_vx) > 20) legs = ((int)(s_run_dist / 24) & 1) ? LEGS_RUN2 : LEGS_RUN1;
    int flags = s_face < 0 ? SPRITE_FLIP_X : 0;
    gfx_sprite(s_pip_px, 12, 12, x, y, 2, s_pip_pal, flags);
    gfx_sprite(s_legs[legs], 12, 2, x, y + 24, 2, s_pip_pal, flags);
    if (s_feather) {
        int wx = s_face > 0 ? x - 2 : x + 22;
        gfx_fill_rect(wx, y + 14, 4, 4, WHITE);
        gfx_fill_rect(wx + (s_face > 0 ? -2 : 2), y + 10, 4, 4, WHITE);
    }
    if (s_shield) {
        int cx = x + 12, cy = y + 14, rr = 21 + (int)(2 * sinf(s_time * 8));
        for (int k = 0; k < 16; k++) {
            float a = k * 0.3927f + s_time;
            gfx_fill_rect(cx + (int)(cosf(a) * rr) - 2, cy + (int)(sinf(a) * rr) - 2, 4, 4, HEX(0x9AF0FF));
        }
    }
}

static void pipsquest_draw(void)
{
    const theme_t *th = &s_themes[(s_level - 1) % 3];
    int cam = (int)s_cam;
    draw_sky(th);

    for (int r = 0; r < ROWS; r++) {
        if (!gfx_visible(r * T, T)) continue;
        for (int c = cam / T; c <= (cam + LCD_W) / T && c < s_w; c++) {
            int t = s_map[r][c];
            if (t) draw_tile(th, t, c * T - cam, r * T, c, r);
        }
    }
    draw_portal((int)s_goal_x - cam, (int)s_goal_y);

    for (int i = 0; i < MAX_POPS; i++) {
        const pop_t *p = &s_pops[i];
        if (p->on) draw_gem((int)p->x - cam, (int)(p->y - 30 - p->t * 60), 14, HEX(0x5CE0F5));
    }
    for (int i = 0; i < MAX_ITEMS; i++) {
        const item_t *it = &s_items[i];
        if (!it->on) continue;
        int x = (int)it->x - cam, y = (int)it->y - (it->landed ? 3 + (int)(3 * sinf(it->t * 4)) : 0);
        if (it->kind == ITEM_SHIELD) {
            gfx_fill_circle(x + 12, y + 12, 12, HEX(0x3DD6F5));
            gfx_fill_circle(x + 12, y + 12, 8, HEX(0x9AF0FF));
            gfx_fill_rect(x + 7, y + 6, 4, 4, WHITE);
        } else {
            gfx_sprite(s_feather_px, 8, 8, x + 4, y + 4, 2, s_feather_pal, 0);
        }
    }

    for (int i = 0; i < MAX_FOES; i++) {
        const foe_t *f = &s_foes[i];
        if (!f->on) continue;
        int x = (int)f->x - cam, y = (int)f->y;
        if (x < -40 || x > LCD_W + 10) continue;
        int flags = (f->state == FS_FLIPPED ? SPRITE_FLIP_Y : 0) | (f->dir > 0 ? SPRITE_FLIP_X : 0);
        if (f->kind == FOE_BAT) {
            gfx_sprite(s_bat_px[((int)(f->t * 8)) & 1], 12, 6, x - 1, y, 2, s_bat_pal, flags & SPRITE_FLIP_Y);
        } else if (f->state == FS_SQUASHED) {
            gfx_fill_round_rect(x - 2, y + FH - 8, 30, 8, 4, HEX(0xA05CF0));
        } else {
            gfx_sprite(f->kind == FOE_URCHIN ? s_urchin_px : s_slime_px, 10, 7, x - 2, y - 3, 3,
                       f->kind == FOE_URCHIN ? s_urchin_pal : s_slime_pal, flags);
        }
    }

    bool hidden = (s_inv_t > 0 && ((int)(s_inv_t * 12) & 1)) || (s_clearing && s_clear_t > 0.25f);
    if (!hidden) draw_pip((int)s_x - cam - 3, (int)s_y - 2);

    for (int i = 0; i < MAX_PARTS; i++) {
        const part_t *p = &s_parts[i];
        if (p->life > 0) gfx_fill_rect((int)p->x - cam - 3, (int)p->y - 3, 6, 6, p->c);
    }

    char buf[24];
    snprintf(buf, sizeof(buf), "%d", s_score);
    gfx_text_shadow(20, 14, buf, 3, WHITE);
    draw_gem(28, 48, 14, HEX(0x5CE0F5));
    snprintf(buf, sizeof(buf), "%d", s_gems);
    gfx_text_shadow(42, 48, buf, 2, WHITE);
    for (int i = 0; i < s_lives; i++) gfx_sprite(s_heart_px, 7, 6, 96 + i * 18, 50, 2, s_heart_pal, 0);
    snprintf(buf, sizeof(buf), "LEVEL %d", s_level);
    gfx_text_shadow(LCD_W / 2 - gfx_text_width(buf, 2) / 2, 20, buf, 2, WHITE);

    if (s_clearing) {
        gfx_text_center(LCD_W / 2 + 3, 163, "LEVEL CLEAR!", 4, HEX(0x8A5CF0));
        gfx_text_center(LCD_W / 2, 160, "LEVEL CLEAR!", 4, WHITE);
    } else if (s_banner_t > 0 && !s_hint) {
        snprintf(buf, sizeof(buf), "LEVEL %d", s_level);
        gfx_text_center(LCD_W / 2 + 3, 163, buf, 4, HEX(0x17707A));
        gfx_text_center(LCD_W / 2, 160, buf, 4, WHITE);
    }
    if (s_hint) {
        gfx_fill_round_rect(60, 120, 360, 104, 14, HEX(0x15152E));
        gfx_text_center(LCD_W / 2, 134, "HOLD LEFT / RIGHT: RUN", 2, WHITE);
        gfx_text_center(LCD_W / 2, 160, "TAP OR FLICK UP: JUMP", 2, WHITE);
        gfx_text_center(LCD_W / 2, 192, "OR BUTTONS + PWR", 2, HEX(0xF5D33D));
    }
}

static int pipsquest_score(void)
{
    return s_score;
}

static void pipsquest_icon(int x, int y, int size)
{
    const theme_t *th = &s_themes[0];
    gfx_fill_round_rect(x, y, size, size, 12, HEX(th->sky_top));
    gfx_fill_rect(x, y + size - 18, size, 18, HEX(th->dirt));
    gfx_fill_rect(x, y + size - 18, size, 6, HEX(th->grass));
    gfx_fill_rect(x + 48, y + 8, 24, 24, HEX(0x1E5AA0));
    gfx_fill_rect(x + 50, y + 10, 20, 20, HEX(0x3D9AF5));
    draw_gem(x + 60, y + 13, 10, HEX(0x9AF0FF));
    gfx_sprite(s_pip_px, 12, 12, x + 14, y + 26, 2, s_pip_pal, 0);
    gfx_sprite(s_legs[LEGS_JUMP], 12, 2, x + 14, y + 50, 2, s_pip_pal, 0);
}

const game_t game_pipsquest = {
    .name = "PIP'S QUEST",
    .nvs_key = "hi_pipsquest",
    .accent = 0xF5D33D,
    .uses_pwr = true,
    .start = pipsquest_start,
    .update = pipsquest_update,
    .draw = pipsquest_draw,
    .score = pipsquest_score,
    .draw_icon = pipsquest_icon,
};

// Space Shooter: vertical scrolling shooter. The ship follows your finger's movement
// (so the finger never covers it) and fires on its own. PWR sets off a nova bomb.

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "audio.h"
#include "board.h"
#include "game.h"
#include "util.h"

#define SHIP_S      3       // sprite scale
#define SHIP_W      13
#define SHIP_H      12
#define SHIP_MIN_Y  180
#define SHIP_MAX_Y  444
#define SHIP_SPEED  340.0f  // with the side buttons
#define DRAG_GAIN   1.3f
#define HIT_R       7
#define FIRE_TIME   0.15f
#define SHOT_SPEED  760.0f
#define BOLT_SPEED  210.0f
#define CAP_SPEED   110.0f
#define WAVE_TIME   25.0f
#define RESPAWN_T   1.4f
#define INVULN_T    2.2f
#define START_LIVES 3
#define START_BOMBS 2
#define MAX_BOMBS   5
#define MAX_SHOTS   36
#define MAX_FOES    16
#define MAX_BOLTS   24
#define MAX_PARTS   128
#define MAX_RINGS   8
#define MAX_CAPS    3
#define RING_PTS    16
#define SEG_H       96      // height of one slice of the side scenery
#define NSTARS      48

#define BG          HEX(0x07061A)
#define NEON_C      HEX(0x3DD6F5)
#define NEON_M      HEX(0xF05CC8)

// Sprites for gfx_sprite
static const char *const s_ship_px[SHIP_H] = {
    "......1......",
    "......1......",
    ".....111.....",
    ".....121.....",
    "....11211....",
    "....12221....",
    "...1122211...",
    "...1112111...",
    "..411111114..",
    ".44111111144.",
    "4441111111444",
    "44..33.33..44",
};
static const char *const s_ufo_px[6] = {
    "...22222...",
    "..2225222..",
    ".111111111.",
    "11311311311",
    ".111111111.",
    "...1...1...",
};
static const char *const s_saucer_px[7] = {
    ".....22222.....",
    "...222252222...",
    "..22222222222..",
    ".1111111111111.",
    "131131131131131",
    ".1111111111111.",
    "...44.....44...",
};

static const color_t s_ship_pal[] = { 0, HEX(0xE6E8FF), NEON_C, HEX(0xF59A3D), NEON_M };
static const color_t s_fire_cols[4] = { HEX(0xFFFFFF), HEX(0xF5D33D), HEX(0xF59A3D), NEON_M };
static const color_t s_zap_cols[4] = { HEX(0xFFFFFF), NEON_C, HEX(0x9AF0FF), NEON_C };

enum { FOE_SCOUT, FOE_DIVER, FOE_SAUCER };
enum { CAP_GUN, CAP_BOMB };

typedef struct {
    float x, y, vx, vy, x0, t, fire_t, hit_t;
    int kind, hp;
    bool on, diving;
} foe_t;
typedef struct { float x, y, vx, vy; bool on; } obj_t;
typedef struct { float x, y, vx, vy, life, max; color_t c; } part_t;
typedef struct { float x, y, r, speed, life; color_t c; } ring_t;
typedef struct { float x, y; int kind; bool on; } cap_t;

static foe_t s_foes[MAX_FOES];
static obj_t s_shots[MAX_SHOTS], s_bolts[MAX_BOLTS];
static part_t s_parts[MAX_PARTS];
static ring_t s_rings[MAX_RINGS];
static cap_t s_caps[MAX_CAPS];
static int s_part_next, s_ring_next;
static float s_ring_dir[RING_PTS][2];
static struct { int16_t x, y; uint8_t layer; } s_stars[NSTARS];

static float s_px, s_py, s_time, s_scroll, s_fire_t, s_spawn_t, s_dead_t, s_inv_t, s_flash, s_banner_t;
static int s_drag_x, s_drag_y;
static int s_score, s_lives, s_bombs, s_gun, s_wave;
static bool s_alive, s_hint;

static void spacewars_start(void)
{
    memset(s_foes, 0, sizeof(s_foes));
    memset(s_shots, 0, sizeof(s_shots));
    memset(s_bolts, 0, sizeof(s_bolts));
    memset(s_parts, 0, sizeof(s_parts));
    memset(s_rings, 0, sizeof(s_rings));
    memset(s_caps, 0, sizeof(s_caps));
    for (int i = 0; i < RING_PTS; i++) {
        s_ring_dir[i][0] = cosf(i * 6.2832f / RING_PTS);
        s_ring_dir[i][1] = sinf(i * 6.2832f / RING_PTS);
    }
    for (int i = 0; i < NSTARS; i++) {
        s_stars[i].x = rnd(LCD_W);
        s_stars[i].y = rnd(LCD_H);
        s_stars[i].layer = i % 3;
    }
    s_px = LCD_W / 2;
    s_py = 400;
    s_time = s_scroll = s_flash = s_dead_t = s_inv_t = 0;
    s_fire_t = FIRE_TIME;
    s_spawn_t = 1.5f;
    s_score = 0;
    s_lives = START_LIVES;
    s_bombs = START_BOMBS;
    s_gun = 1;
    s_wave = 1;
    s_banner_t = 1.5f;
    s_alive = true;
    s_hint = true;
}

static void explode(float x, float y, int n, float speed, const color_t *cols)
{
    for (int i = 0; i < n; i++) {
        float a = rndf() * 6.2832f, v = speed * (0.3f + 0.7f * rndf());
        float life = 0.35f + 0.45f * rndf();
        s_parts[s_part_next] = (part_t){ x, y, cosf(a) * v, sinf(a) * v, life, life, cols[rnd(4)] };
        s_part_next = (s_part_next + 1) % MAX_PARTS;
    }
}

static void add_ring(float x, float y, float speed, float life, color_t c)
{
    s_rings[s_ring_next] = (ring_t){ x, y, 4, speed, life, c };
    s_ring_next = (s_ring_next + 1) % MAX_RINGS;
}

static foe_t *spawn_foe(int kind, float x, float y)
{
    for (int i = 0; i < MAX_FOES; i++) {
        foe_t *f = &s_foes[i];
        if (f->on) continue;
        *f = (foe_t){ .x = x, .y = y, .x0 = x, .kind = kind, .on = true, .t = rndf() * 3 };
        switch (kind) {
        case FOE_SCOUT:
            f->hp = 1;
            f->vy = fminf(80.0f + 8.0f * s_wave, 180.0f);
            break;
        case FOE_DIVER:
            f->hp = 1;
            f->vy = 110;
            break;
        default:
            f->hp = 5 + s_wave;
            f->vy = 40;
            f->vx = rnd(2) ? 60 : -60;
            f->fire_t = 1.0f + rndf();
            break;
        }
        return f;
    }
    return NULL;
}

static int count_foes(int kind)
{
    int n = 0;
    for (int i = 0; i < MAX_FOES; i++) n += s_foes[i].on && s_foes[i].kind == kind;
    return n;
}

static void spawn_next(void)
{
    int roll = rnd(100);
    float x = 50 + rnd(LCD_W - 100);
    if (s_wave >= 2 && roll < 18 && count_foes(FOE_SAUCER) < 2) {
        spawn_foe(FOE_SAUCER, x, -30);
    } else if (s_wave >= 3 && roll < 42) {
        spawn_foe(FOE_DIVER, x, -24);
    } else if (roll >= 84) {
        // A V of four scouts flying in step
        float cx = 110 + rnd(LCD_W - 220), t = rndf() * 3;
        for (int i = 0; i < 4; i++) {
            foe_t *f = spawn_foe(FOE_SCOUT, cx + (i - 1.5f) * 50, -24 - fabsf(i - 1.5f) * 34);
            if (f) f->t = t;
        }
    } else {
        spawn_foe(FOE_SCOUT, x, -24);
    }
}

static void foe_size(const foe_t *f, float *rx, float *ry)
{
    *rx = f->kind == FOE_SAUCER ? 22 : 16;
    *ry = f->kind == FOE_SAUCER ? 12 : 10;
}

static void kill_foe(foe_t *f)
{
    f->on = false;
    if (f->kind == FOE_SAUCER) {
        s_score += 400;
        explode(f->x, f->y, 28, 240, s_fire_cols);
        add_ring(f->x, f->y, 260, 0.45f, NEON_M);
        sfx(SFX_BOOM_BIG);
        for (int i = 0; i < MAX_CAPS; i++) {
            if (s_caps[i].on) continue;
            bool gun = s_gun < 3 && rnd(2);
            if (gun || rnd(3) == 0) s_caps[i] = (cap_t){ f->x, f->y, gun ? CAP_GUN : CAP_BOMB, true };
            break;
        }
    } else {
        s_score += f->kind == FOE_DIVER ? 150 : 100;
        explode(f->x, f->y, 14, 170, s_fire_cols);
        add_ring(f->x, f->y, 180, 0.3f, HEX(0xF5D33D));
        sfx(SFX_BOOM);
    }
}

static void fire(void)
{
    static const float pattern[3][3][2] = {   // x offset, vx
        { { 0, 0 } },
        { { -8, 0 }, { 8, 0 } },
        { { 0, 0 }, { -10, -150 }, { 10, 150 } },
    };
    int n = s_gun;
    for (int k = 0, i = 0; k < n && i < MAX_SHOTS; i++) {
        if (s_shots[i].on) continue;
        s_shots[i] = (obj_t){ s_px + pattern[n - 1][k][0], s_py - 18, pattern[n - 1][k][1], -SHOT_SPEED, true };
        k++;
    }
    sfx(SFX_SHOOT);
}

static void nova_bomb(void)
{
    s_bombs--;
    s_flash = 1;
    add_ring(s_px, s_py, 620, 0.8f, WHITE);
    add_ring(s_px, s_py, 420, 0.8f, NEON_C);
    for (int i = 0; i < MAX_FOES; i++) {
        if (s_foes[i].on && s_foes[i].y > -20) kill_foe(&s_foes[i]);
    }
    memset(s_bolts, 0, sizeof(s_bolts));
    sfx(SFX_BOMB);
}

static void ship_hit(void)
{
    s_alive = false;
    s_dead_t = 0;
    s_lives--;
    if (s_gun > 1) s_gun--;
    explode(s_px, s_py, 40, 260, s_fire_cols);
    explode(s_px, s_py, 16, 120, s_zap_cols);
    add_ring(s_px, s_py, 300, 0.6f, WHITE);
    memset(s_bolts, 0, sizeof(s_bolts));
    sfx(SFX_CRASH);
}

static void fire_bolt(const foe_t *f)
{
    float dx = s_px - f->x, dy = s_py - f->y, len = sqrtf(dx * dx + dy * dy);
    if (len < 1) return;
    for (int i = 0; i < MAX_BOLTS; i++) {
        if (s_bolts[i].on) continue;
        s_bolts[i] = (obj_t){ f->x, f->y + 12, dx / len * BOLT_SPEED, dy / len * BOLT_SPEED, true };
        sfx(SFX_LASER);
        return;
    }
}

static void update_foe(foe_t *f, float dt)
{
    f->t += dt;
    if (f->hit_t > 0) f->hit_t -= dt;
    switch (f->kind) {
    case FOE_SCOUT:
        f->y += f->vy * dt;
        f->x = f->x0 + sinf(f->t * 2.2f) * 44;
        break;
    case FOE_DIVER:
        if (!f->diving && f->y > 130) {
            float dx = s_px - f->x, dy = s_py - f->y, len = fmaxf(sqrtf(dx * dx + dy * dy), 1);
            f->vx = dx / len * 330;
            f->vy = dy / len * 330;
            f->diving = true;
        }
        f->x += f->vx * dt;
        f->y += f->vy * dt;
        break;
    default:
        f->x += f->vx * dt;
        f->y += f->vy * dt;
        if (f->x < 40 || f->x > LCD_W - 40) {
            f->vx = -f->vx;
            f->x = clampf(f->x, 40, LCD_W - 40);
        }
        f->fire_t -= dt;
        if (f->fire_t <= 0) {
            f->fire_t = fmaxf(1.7f - 0.08f * s_wave, 0.8f);
            if (s_alive && f->y > 0) fire_bolt(f);
        }
        break;
    }
    if (f->y > LCD_H + 40 || f->y < -200 || f->x < -60 || f->x > LCD_W + 60) f->on = false;
}

static bool spacewars_update(const input_t *in, float dt)
{
    s_time += dt;
    s_scroll += (70.0f + 6.0f * s_wave) * dt;
    if (s_flash > 0) s_flash -= dt * 2.5f;
    if (s_banner_t > 0) s_banner_t -= dt;
    if (s_inv_t > 0) s_inv_t -= dt;
    if (in->touch_start || in->pressed || s_time > 5.0f) s_hint = false;

    int wave = 1 + (int)(s_time / WAVE_TIME);
    if (wave != s_wave) {
        s_wave = wave;
        s_banner_t = 1.5f;
        sfx(SFX_LEVEL_UP);
    }

    // The ship moves with the finger, relative to where the finger went down
    if (in->touch_start) {
        s_drag_x = in->x;
        s_drag_y = in->y;
    }
    if (s_alive) {
        if (in->touching) {
            s_px += (in->x - s_drag_x) * DRAG_GAIN;
            s_py += (in->y - s_drag_y) * DRAG_GAIN;
        }
        if (in->held & BTN_LEFT) s_px -= SHIP_SPEED * dt;
        if (in->held & BTN_RIGHT) s_px += SHIP_SPEED * dt;
        s_px = clampf(s_px, 20, LCD_W - 20);
        s_py = clampf(s_py, SHIP_MIN_Y, SHIP_MAX_Y);

        s_fire_t -= dt;
        if (s_fire_t <= 0) {
            s_fire_t = FIRE_TIME;
            fire();
        }
        if ((in->pressed & BTN_PWR) && s_bombs > 0) nova_bomb();
    } else {
        s_dead_t += dt;
        if (s_dead_t > RESPAWN_T) {
            if (s_lives <= 0) return false;
            s_alive = true;
            s_inv_t = INVULN_T;
            s_px = LCD_W / 2;
            s_py = 400;
        }
    }
    if (in->touching) {
        s_drag_x = in->x;
        s_drag_y = in->y;
    }

    s_spawn_t -= dt;
    if (s_spawn_t <= 0) {
        float gap = fmaxf(1.25f - 0.1f * s_wave, 0.38f);
        s_spawn_t = gap * (0.7f + 0.6f * rndf());
        spawn_next();
    }

    for (int i = 0; i < MAX_FOES; i++) {
        if (s_foes[i].on) update_foe(&s_foes[i], dt);
    }

    for (int i = 0; i < MAX_SHOTS; i++) {
        obj_t *s = &s_shots[i];
        if (!s->on) continue;
        s->x += s->vx * dt;
        s->y += s->vy * dt;
        if (s->y < -20 || s->x < -10 || s->x > LCD_W + 10) {
            s->on = false;
            continue;
        }
        for (int j = 0; j < MAX_FOES; j++) {
            foe_t *f = &s_foes[j];
            float rx, ry;
            foe_size(f, &rx, &ry);
            if (!f->on || fabsf(s->x - f->x) > rx || fabsf(s->y - f->y) > ry + 8) continue;
            s->on = false;
            f->hit_t = 0.06f;
            explode(s->x, s->y, 3, 90, s_zap_cols);
            if (--f->hp <= 0) kill_foe(f);
            else sfx(SFX_ZAP);
            break;
        }
    }

    bool vulnerable = s_alive && s_inv_t <= 0;
    for (int i = 0; i < MAX_BOLTS; i++) {
        obj_t *b = &s_bolts[i];
        if (!b->on) continue;
        b->x += b->vx * dt;
        b->y += b->vy * dt;
        if (b->y > LCD_H + 10 || b->y < -10 || b->x < -10 || b->x > LCD_W + 10) b->on = false;
        else if (vulnerable && fabsf(b->x - s_px) < HIT_R + 4 && fabsf(b->y - s_py) < HIT_R + 4) {
            ship_hit();
            vulnerable = false;
        }
    }

    for (int i = 0; i < MAX_FOES && vulnerable; i++) {
        foe_t *f = &s_foes[i];
        float rx, ry;
        foe_size(f, &rx, &ry);
        if (f->on && fabsf(f->x - s_px) < rx + HIT_R && fabsf(f->y - s_py) < ry + HIT_R) {
            kill_foe(f);
            ship_hit();
            vulnerable = false;
        }
    }

    for (int i = 0; i < MAX_CAPS; i++) {
        cap_t *c = &s_caps[i];
        if (!c->on) continue;
        c->y += CAP_SPEED * dt;
        if (s_alive && fabsf(c->x - s_px) < 28 && fabsf(c->y - s_py) < 28) {
            c->on = false;
            s_score += 50;
            if (c->kind == CAP_GUN && s_gun < 3) s_gun++;
            else if (c->kind == CAP_BOMB && s_bombs < MAX_BOMBS) s_bombs++;
            sfx(SFX_POWERUP);
        } else if (c->y > LCD_H + 20) {
            c->on = false;
        }
    }

    float drag = 1.0f - clampf(2.5f * dt, 0, 1);
    for (int i = 0; i < MAX_PARTS; i++) {
        part_t *p = &s_parts[i];
        if (p->life <= 0) continue;
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        p->vx *= drag;
        p->vy *= drag;
        p->life -= dt;
    }
    for (int i = 0; i < MAX_RINGS; i++) {
        ring_t *r = &s_rings[i];
        if (r->life <= 0) continue;
        r->r += r->speed * dt;
        r->life -= dt;
    }
    return true;
}

static uint32_t seg_hash(int id, uint32_t salt)
{
    uint32_t h = (uint32_t)id * 2654435761u ^ salt;
    return (h ^ (h >> 15)) * 2246822519u >> 16;
}

static void draw_scenery(void)
{
    static const uint8_t speed16[3] = { 4, 8, 16 };    // star layer speed, 1/16ths of the scroll
    static const color_t star_col[3] = { HEX(0x3A3470), HEX(0x7A78B0), HEX(0xD8DCFF) };
    int scroll = (int)s_scroll;
    for (int i = 0; i < NSTARS; i++) {
        int l = s_stars[i].layer;
        int y = (s_stars[i].y + scroll * speed16[l] / 16) % LCD_H;
        gfx_fill_rect(s_stars[i].x, y, l ? 2 : 1, l == 2 ? 3 : 2, star_col[l]);
    }

    // Station walls on both sides, scrolling at full speed, with neon edges
    int offset = scroll % SEG_H, base = scroll / SEG_H;
    bool blink = (int)(s_time * 3) & 1;
    for (int i = 0; i <= LCD_H / SEG_H + 1; i++) {
        int y = offset + (i - 1) * SEG_H;
        if (!gfx_visible(y, SEG_H)) continue;
        int id = base - i;
        int wl = 8 + (int)(seg_hash(id, 0x1234) % 4) * 10;
        int wr = 8 + (int)(seg_hash(id, 0x9876) % 4) * 10;
        int wl_up = 8 + (int)(seg_hash(id + 1, 0x1234) % 4) * 10;
        int wr_up = 8 + (int)(seg_hash(id + 1, 0x9876) % 4) * 10;

        gfx_fill_rect(0, y, wl, SEG_H, HEX(0x16123A));
        gfx_fill_rect(wl - 4, y, 2, SEG_H, gfx_mix(BG, NEON_C, 110));
        gfx_fill_rect(wl - 2, y, 2, SEG_H, NEON_C);
        gfx_fill_rect(0, y, wl > wl_up ? wl : wl_up, 2, NEON_C);    // ledge where the width changes

        gfx_fill_rect(LCD_W - wr, y, wr, SEG_H, HEX(0x1E0E30));
        gfx_fill_rect(LCD_W - wr + 2, y, 2, SEG_H, gfx_mix(BG, NEON_M, 110));
        gfx_fill_rect(LCD_W - wr, y, 2, SEG_H, NEON_M);
        gfx_fill_rect(LCD_W - (wr > wr_up ? wr : wr_up), y, wr > wr_up ? wr : wr_up, 2, NEON_M);

        if (wl >= 18 && (seg_hash(id, 7) & 1)) {
            gfx_fill_rect(wl / 2 - 3, y + SEG_H / 2 - 3, 6, 6, blink ? HEX(0xF5D33D) : HEX(0x4A3A20));
        }
        if (wr >= 18 && (seg_hash(id, 8) & 1)) {
            gfx_fill_rect(LCD_W - wr / 2 - 3, y + SEG_H / 2 - 3, 6, 6, blink ? HEX(0x4A3A20) : HEX(0xF5D33D));
        }
    }
}

static void draw_foe(const foe_t *f)
{
    static const color_t white[6] = { 0, WHITE, WHITE, WHITE, WHITE, WHITE };
    bool lit = ((int)(f->t * 6)) & 1;
    color_t light = lit ? HEX(0xF5D33D) : WHITE;
    if (f->kind == FOE_SAUCER) {
        color_t pal[6] = { 0, HEX(0xB45CF0), HEX(0x9AF0FF), light, lit ? NEON_C : HEX(0x1A6A80), WHITE };
        gfx_sprite(s_saucer_px, 15, 7, (int)f->x - 22, (int)f->y - 10, 3, f->hit_t > 0 ? white : pal, 0);
    } else {
        color_t body = f->kind == FOE_DIVER ? HEX(0xF0504A) : HEX(0x5CE065);
        color_t pal[6] = { 0, body, HEX(0x9AF0FF), light, 0, WHITE };
        gfx_sprite(s_ufo_px, 11, 6, (int)f->x - 16, (int)f->y - 9, 3, f->hit_t > 0 ? white : pal, 0);
    }
}

static void draw_ship(int cx, int cy, int s)
{
    int x = cx - SHIP_W * s / 2, y = cy - SHIP_H * s / 2;
    int flame = (3 + (((int)(s_time * 30)) & 1) * 2) * s;
    if (gfx_visible(y + SHIP_H * s, flame)) {
        gfx_fill_rect(x + 4 * s, y + SHIP_H * s, 2 * s, flame, HEX(0xF59A3D));
        gfx_fill_rect(x + 7 * s, y + SHIP_H * s, 2 * s, flame, HEX(0xF59A3D));
        gfx_fill_rect(x + 4 * s + s / 2, y + SHIP_H * s, s, flame - s, HEX(0xF5D33D));
        gfx_fill_rect(x + 7 * s + s / 2, y + SHIP_H * s, s, flame - s, HEX(0xF5D33D));
    }
    gfx_sprite(s_ship_px, SHIP_W, SHIP_H, x, y, s, s_ship_pal, 0);
}

static void spacewars_draw(void)
{
    gfx_clear(s_flash > 0 ? gfx_mix(BG, HEX(0x8060C0), (int)(s_flash * 200)) : BG);
    draw_scenery();

    for (int i = 0; i < MAX_CAPS; i++) {
        const cap_t *c = &s_caps[i];
        if (!c->on || !gfx_visible((int)c->y - 11, 22)) continue;
        color_t col = c->kind == CAP_GUN ? NEON_C : NEON_M;
        if ((int)(s_time * 8) & 1) col = gfx_mix(col, WHITE, 120);
        gfx_fill_round_rect((int)c->x - 16, (int)c->y - 11, 32, 22, 8, col);
        gfx_text_center((int)c->x, (int)c->y - 7, c->kind == CAP_GUN ? "P" : "B", 2, BLACK);
    }

    for (int i = 0; i < MAX_FOES; i++) {
        if (s_foes[i].on) draw_foe(&s_foes[i]);
    }

    for (int i = 0; i < MAX_BOLTS; i++) {
        const obj_t *b = &s_bolts[i];
        if (!b->on) continue;
        gfx_fill_rect((int)b->x - 5, (int)b->y - 5, 10, 10, gfx_mix(BG, NEON_M, 150));
        gfx_fill_rect((int)b->x - 3, (int)b->y - 3, 6, 6, NEON_M);
        gfx_fill_rect((int)b->x - 1, (int)b->y - 1, 2, 2, WHITE);
    }

    for (int i = 0; i < MAX_SHOTS; i++) {
        const obj_t *s = &s_shots[i];
        if (!s->on) continue;
        gfx_fill_rect((int)s->x - 3, (int)s->y - 8, 6, 16, gfx_mix(BG, NEON_C, 120));
        gfx_fill_rect((int)s->x - 1, (int)s->y - 8, 2, 16, HEX(0xCFF6FF));
    }

    if (s_alive && !(s_inv_t > 0 && ((int)(s_inv_t * 10) & 1))) draw_ship((int)s_px, (int)s_py, SHIP_S);

    for (int i = 0; i < MAX_PARTS; i++) {
        const part_t *p = &s_parts[i];
        if (p->life <= 0) continue;
        float k = p->life / p->max;
        int sz = 2 + (int)(3 * k);
        gfx_fill_rect((int)p->x - sz / 2, (int)p->y - sz / 2, sz, sz, gfx_mix(p->c, BG, (int)((1 - k) * 200)));
    }
    for (int i = 0; i < MAX_RINGS; i++) {
        const ring_t *r = &s_rings[i];
        if (r->life <= 0 || !gfx_visible((int)(r->y - r->r) - 2, (int)(2 * r->r) + 4)) continue;
        color_t c = gfx_mix(r->c, BG, (int)(clampf(1 - r->life * 2.5f, 0, 1) * 220));
        for (int k = 0; k < RING_PTS; k++) {
            gfx_fill_rect((int)(r->x + s_ring_dir[k][0] * r->r) - 2, (int)(r->y + s_ring_dir[k][1] * r->r) - 2, 4, 4, c);
        }
    }

    char buf[24];
    snprintf(buf, sizeof(buf), "%d", s_score);
    gfx_text_shadow(24, 16, buf, 3, WHITE);
    for (int i = 0; i < s_lives - (s_alive ? 1 : 0); i++) draw_ship(31 + i * 18, 56, 1);
    for (int i = 0; i < s_bombs; i++) {
        gfx_fill_circle(30 + i * 16, 78, 5, NEON_M);
        gfx_fill_rect(29 + i * 16, 77, 2, 2, WHITE);
    }

    if (s_banner_t > 0) {
        snprintf(buf, sizeof(buf), "WAVE %d", s_wave);
        gfx_text_center(LCD_W / 2 + 3, 183, buf, 4, NEON_M);
        gfx_text_center(LCD_W / 2, 180, buf, 4, WHITE);
    }
    if (s_hint) {
        gfx_text_center(LCD_W / 2, 250, "DRAG TO FLY", 2, WHITE);
        gfx_text_center(LCD_W / 2, 276, "PWR: NOVA BOMB", 2, NEON_C);
    }
}

static int spacewars_score(void)
{
    return s_score;
}

static void spacewars_icon(int x, int y, int size)
{
    static const color_t ufo_pal[6] = { 0, HEX(0x5CE065), HEX(0x9AF0FF), HEX(0xF5D33D), 0, WHITE };
    gfx_fill_rect(x + 8, y + 40, 2, 2, HEX(0x7A78B0));
    gfx_fill_rect(x + 66, y + 30, 2, 2, HEX(0xD8DCFF));
    gfx_fill_rect(x + 70, y + 60, 2, 2, HEX(0x7A78B0));
    gfx_sprite(s_ufo_px, 11, 6, x + size / 2 - 16, y + 2, 3, ufo_pal, 0);
    gfx_fill_rect(x + size / 2 - 3, y + 24, 6, 16, gfx_mix(BG, NEON_C, 120));
    gfx_fill_rect(x + size / 2 - 1, y + 24, 2, 16, HEX(0xCFF6FF));
    gfx_sprite(s_ship_px, SHIP_W, SHIP_H, x + size / 2 - SHIP_W * 3 / 2, y + size - SHIP_H * 3, 3, s_ship_pal, 0);
}

const game_t game_spacewars = {
    .name = "SPACE WARS",
    .nvs_key = "hi_spacewars",
    .accent = 0xF05CC8,
    .uses_pwr = true,
    .start = spacewars_start,
    .update = spacewars_update,
    .draw = spacewars_draw,
    .score = spacewars_score,
    .draw_icon = spacewars_icon,
};

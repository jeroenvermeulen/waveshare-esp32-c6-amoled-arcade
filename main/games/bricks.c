// Brick Bash: paddle-and-ball brick breaker. The paddle follows your finger or
// the side buttons; lifting your finger launches the ball.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio.h"
#include "board.h"
#include "game.h"
#include "util.h"

#define WALL_L      8
#define WALL_R      472
#define TOP_Y       60
#define PAD_Y       432
#define PAD_H       14
#define PAD_W       84
#define PAD_W_WIDE  132
#define PAD_SPEED   560.0f
#define BALL_R      7
#define BCOLS       10
#define BROWS       8
#define BW          44
#define BH          18
#define BPITCH_X    46
#define BPITCH_Y    22
#define BX0         11
#define BY0         92
#define STEEL       9
#define MAX_BALLS   3
#define MAX_CAPS    4
#define CAP_W       38
#define CAP_H       18
#define CAP_SPEED   140.0f
#define AUTO_LAUNCH 3.0f
#define MAX_LIVES   5

enum { CAP_WIDE, CAP_MULTI, CAP_SLOW, CAP_LIFE };
static const char *const s_cap_label[] = { "W", "M", "S", "+" };
static const uint32_t s_cap_hex[] = { 0x3DD6F5, 0xE05CC8, 0xF59A3D, 0x5CE065 };
static const uint32_t s_row_hex[BROWS] = {
    0xF0504A, 0xF5913D, 0xF5D33D, 0x7CE05C, 0x3DD6C8, 0x3D9AF5, 0x8A5CF0, 0xE05CC8,
};

typedef struct { float x, y, vx, vy; bool on; } ball_t;
typedef struct { float x, y; int kind; bool on; } cap_t;

static uint8_t s_bricks[BROWS][BCOLS];
static ball_t s_balls[MAX_BALLS];
static cap_t s_caps[MAX_CAPS];
static float s_pad_x, s_pad_w, s_wide_t, s_slow_t, s_attach_t, s_banner_t, s_attach_off;
static bool s_attached, s_over;
static int s_lives, s_level, s_score, s_remaining;

static float ball_speed(void)
{
    float s = 290.0f + 22.0f * (s_level - 1);
    if (s > 470.0f) s = 470.0f;
    return s_slow_t > 0 ? s * 0.7f : s;
}

static void build_level(void)
{
    int pattern = (s_level - 1) % 5;
    int max_hp = 1 + (s_level - 1) / 2;
    if (max_hp > 3) max_hp = 3;

    s_remaining = 0;
    for (int r = 0; r < BROWS; r++) {
        for (int c = 0; c < BCOLS; c++) {
            int dx = 2 * c - (BCOLS - 1);   // -9..9
            bool on;
            switch (pattern) {
            case 0:  on = r >= 1 && r < 7; break;
            case 1:  on = r < 7 && (dx < 0 ? -dx : dx) <= 2 * (7 - r) - 1; break;
            case 2:  on = r < 7 && ((r + c) & 1) == 0; break;
            case 3:  on = (dx < 0 ? -dx : dx) + abs(2 * r - 7) <= 9; break;
            default: on = (r % 2 == 0 && r < 7) || (r == 5 && c % 3 == 0); break;
            }
            uint8_t hp = 0;
            if (on) {
                hp = 1 + ((BROWS - 1 - r) * max_hp) / BROWS;
                if (hp > 3) hp = 3;
                if (pattern == 4 && r == 5) hp = STEEL;
                if (hp != STEEL) s_remaining++;
            }
            s_bricks[r][c] = hp;
        }
    }
}

static void attach_ball(void)
{
    memset(s_balls, 0, sizeof(s_balls));
    memset(s_caps, 0, sizeof(s_caps));
    s_balls[0].on = true;
    s_attached = true;
    s_attach_t = 0;
    s_attach_off = (rndf() - 0.5f) * 30.0f;
    s_wide_t = 0;
}

static void launch(void)
{
    float a = (rndf() - 0.5f) * 0.7f;
    float s = ball_speed();
    s_balls[0].vx = s * sinf(a);
    s_balls[0].vy = -s * cosf(a);
    s_attached = false;
}

static void bricks_start(void)
{
    s_lives = 3;
    s_level = 1;
    s_score = 0;
    s_over = false;
    s_slow_t = 0;
    s_pad_x = LCD_W / 2;
    s_pad_w = PAD_W;
    s_banner_t = 1.5f;
    build_level();
    attach_ball();
}

static void spawn_cap(float x, float y)
{
    for (int i = 0; i < MAX_CAPS; i++) {
        if (s_caps[i].on) continue;
        int roll = rnd(100);
        s_caps[i] = (cap_t){
            .x = x, .y = y, .on = true,
            .kind = roll < 35 ? CAP_WIDE : roll < 65 ? CAP_MULTI : roll < 90 ? CAP_SLOW : CAP_LIFE,
        };
        return;
    }
}

static void apply_cap(int kind)
{
    sfx(SFX_POWERUP);
    switch (kind) {
    case CAP_WIDE:
        s_wide_t = 12.0f;
        break;
    case CAP_SLOW:
        s_slow_t = 10.0f;
        break;
    case CAP_LIFE:
        if (s_lives < MAX_LIVES) s_lives++;
        break;
    case CAP_MULTI: {
        ball_t *src = NULL;
        for (int i = 0; i < MAX_BALLS && !src; i++) {
            if (s_balls[i].on) src = &s_balls[i];
        }
        if (!src || s_attached) break;
        float spread = 0.45f;
        for (int i = 0; i < MAX_BALLS; i++) {
            if (s_balls[i].on) continue;
            float c = cosf(spread), s = sinf(spread);
            s_balls[i] = (ball_t){ src->x, src->y, src->vx * c - src->vy * s, src->vx * s + src->vy * c, true };
            spread = -spread;
        }
        break;
    }
    }
}

static void hit_brick(int r, int c)
{
    uint8_t *b = &s_bricks[r][c];
    if (*b == STEEL) {
        sfx(SFX_STEEL);
        return;
    }
    (*b)--;
    s_score += 10;
    if (*b) {
        sfx(SFX_BRICK_HIT);
        return;
    }
    s_score += 20;
    s_remaining--;
    sfx(SFX_BRICK);
    if (rnd(8) == 0) spawn_cap(BX0 + c * BPITCH_X + BW / 2, BY0 + r * BPITCH_Y + BH / 2);
}

static void step_ball(ball_t *b, float h)
{
    float ox = b->x, oy = b->y;
    b->x += b->vx * h;
    b->y += b->vy * h;

    if (b->x - BALL_R < WALL_L) {
        b->x = WALL_L + BALL_R;
        b->vx = fabsf(b->vx);
        sfx(SFX_WALL);
    } else if (b->x + BALL_R > WALL_R) {
        b->x = WALL_R - BALL_R;
        b->vx = -fabsf(b->vx);
        sfx(SFX_WALL);
    }
    if (b->y - BALL_R < TOP_Y) {
        b->y = TOP_Y + BALL_R;
        b->vy = fabsf(b->vy);
        sfx(SFX_WALL);
    }

    // Paddle: the bounce angle depends on where the ball hits, up to ~60 degrees
    if (b->vy > 0 && b->y + BALL_R >= PAD_Y && oy + BALL_R <= PAD_Y + 6 &&
        fabsf(b->x - s_pad_x) <= s_pad_w / 2 + BALL_R) {
        float rel = clampf((b->x - s_pad_x) / (s_pad_w / 2), -1.0f, 1.0f);
        float a = rel * 1.05f, s = ball_speed();
        b->vx = s * sinf(a);
        b->vy = -s * cosf(a);
        b->y = PAD_Y - BALL_R;
        sfx(SFX_PADDLE);
        return;
    }

    int c0 = (int)floorf((b->x - BALL_R - BX0) / BPITCH_X), c1 = (int)floorf((b->x + BALL_R - BX0) / BPITCH_X);
    int r0 = (int)floorf((b->y - BALL_R - BY0) / BPITCH_Y), r1 = (int)floorf((b->y + BALL_R - BY0) / BPITCH_Y);
    for (int r = r0 < 0 ? 0 : r0; r <= r1 && r < BROWS; r++) {
        for (int c = c0 < 0 ? 0 : c0; c <= c1 && c < BCOLS; c++) {
            if (!s_bricks[r][c]) continue;
            float bx = BX0 + c * BPITCH_X, by = BY0 + r * BPITCH_Y;
            float nx = clampf(b->x, bx, bx + BW), ny = clampf(b->y, by, by + BH);
            float dx = b->x - nx, dy = b->y - ny;
            if (dx * dx + dy * dy > BALL_R * BALL_R) continue;

            bool was_out_x = ox < bx || ox > bx + BW;
            bool was_out_y = oy < by || oy > by + BH;
            bool flip_y = was_out_y && (!was_out_x || fabsf(dy) >= fabsf(dx));
            if (flip_y) b->vy = oy < by ? -fabsf(b->vy) : fabsf(b->vy);
            else b->vx = ox < bx ? -fabsf(b->vx) : fabsf(b->vx);
            b->x = ox;
            b->y = oy;
            hit_brick(r, c);
            return;
        }
    }
}

static bool bricks_update(const input_t *in, float dt)
{
    if (s_over) return false;
    if (s_banner_t > 0) s_banner_t -= dt;
    if (s_wide_t > 0) s_wide_t -= dt;
    if (s_slow_t > 0) s_slow_t -= dt;

    if (in->touching) s_pad_x = in->x;
    if (in->held & BTN_LEFT) s_pad_x -= PAD_SPEED * dt;
    if (in->held & BTN_RIGHT) s_pad_x += PAD_SPEED * dt;
    float target_w = s_wide_t > 0 ? PAD_W_WIDE : PAD_W;
    s_pad_w += (target_w - s_pad_w) * clampf(dt * 8, 0, 1);
    s_pad_x = clampf(s_pad_x, WALL_L + s_pad_w / 2, WALL_R - s_pad_w / 2);

    if (s_attached) {
        s_balls[0].x = s_pad_x + s_attach_off;
        s_balls[0].y = PAD_Y - BALL_R - 1;
        s_attach_t += dt;
        if (in->touch_end || s_attach_t > AUTO_LAUNCH) launch();
    } else {
        float speed = ball_speed();
        int active = 0;
        for (int i = 0; i < MAX_BALLS; i++) {
            ball_t *b = &s_balls[i];
            if (!b->on) continue;
            float v = sqrtf(b->vx * b->vx + b->vy * b->vy);
            if (v > 0) {
                b->vx *= speed / v;
                b->vy *= speed / v;
            }
            // Never let the ball settle into a near-horizontal path
            if (fabsf(b->vy) < speed * 0.25f) {
                b->vy = (b->vy < 0 ? -1 : 1) * speed * 0.25f;
            }
            int steps = 1 + (int)(speed * dt / 4.0f);
            for (int s = 0; s < steps; s++) step_ball(b, dt / steps);
            if (b->y - BALL_R > LCD_H) b->on = false;
            else active++;
        }
        if (!active) {
            sfx(SFX_LOSE_LIFE);
            if (--s_lives <= 0) {
                s_over = true;
                return false;
            }
            attach_ball();
        }
    }

    for (int i = 0; i < MAX_CAPS; i++) {
        cap_t *c = &s_caps[i];
        if (!c->on) continue;
        c->y += CAP_SPEED * dt;
        if (c->y + CAP_H / 2 >= PAD_Y && c->y - CAP_H / 2 <= PAD_Y + PAD_H &&
            fabsf(c->x - s_pad_x) <= s_pad_w / 2 + CAP_W / 2) {
            c->on = false;
            apply_cap(c->kind);
        } else if (c->y > LCD_H + CAP_H) {
            c->on = false;
        }
    }

    if (s_remaining <= 0) {
        s_score += 100 * s_level;
        s_level++;
        build_level();
        attach_ball();
        s_banner_t = 1.5f;
        sfx(SFX_LEVEL_UP);
    }
    return true;
}

static void draw_brick(int x, int y, int w, int h, int row, int hp)
{
    if (hp == STEEL) {
        gfx_fill_rect(x, y, w, h, HEX(0x5A5F70));
        gfx_fill_rect(x + 2, y + 2, w - 4, h - 4, HEX(0x8A8FA0));
        gfx_fill_rect(x + 2, y + 2, w - 4, 3, HEX(0xC0C4D0));
        return;
    }
    color_t base = HEX(s_row_hex[row % BROWS]);
    gfx_fill_rect(x, y, w, h, base);
    gfx_fill_rect(x, y, w, 3, gfx_mix(base, WHITE, 110));
    gfx_fill_rect(x, y + h - 3, w, 3, gfx_mix(base, BLACK, 100));
    if (hp >= 2) gfx_fill_rect(x + 6, y + 6, w - 12, h - 12, gfx_mix(base, BLACK, 120));
    if (hp >= 3) gfx_fill_rect(x + 10, y + 7, w - 20, h - 14, gfx_mix(base, WHITE, 180));
}

static void bricks_draw(void)
{
    char buf[24];
    gfx_clear(HEX(0x08081A));

    snprintf(buf, sizeof(buf), "%d", s_score);
    gfx_text(30, 18, buf, 3, WHITE);
    snprintf(buf, sizeof(buf), "LV %d", s_level);
    gfx_text_center(LCD_W / 2, 22, buf, 2, HEX(0x8A8AB0));
    for (int i = 0; i < s_lives; i++) {
        gfx_fill_round_rect(424 - i * 32, 24, 26, 8, 4, HEX(0xF5913D));
    }

    color_t wall = HEX(0x2A2A5A);
    gfx_fill_rect(WALL_L - 4, TOP_Y - 4, WALL_R - WALL_L + 8, 4, wall);
    gfx_fill_rect(WALL_L - 4, TOP_Y, 4, LCD_H - TOP_Y, wall);
    gfx_fill_rect(WALL_R, TOP_Y, 4, LCD_H - TOP_Y, wall);

    for (int r = 0; r < BROWS; r++) {
        int y = BY0 + r * BPITCH_Y;
        if (!gfx_visible(y, BH)) continue;
        for (int c = 0; c < BCOLS; c++) {
            if (s_bricks[r][c]) draw_brick(BX0 + c * BPITCH_X, y, BW, BH, r, s_bricks[r][c]);
        }
    }

    for (int i = 0; i < MAX_CAPS; i++) {
        const cap_t *c = &s_caps[i];
        if (!c->on) continue;
        int x = (int)c->x - CAP_W / 2, y = (int)c->y - CAP_H / 2;
        gfx_fill_round_rect(x, y, CAP_W, CAP_H, 9, HEX(s_cap_hex[c->kind]));
        gfx_text_center((int)c->x, y + 2, s_cap_label[c->kind], 2, BLACK);
    }

    int px = (int)(s_pad_x - s_pad_w / 2), pw = (int)s_pad_w;
    color_t pad = s_slow_t > 0 ? HEX(0xF59A3D) : HEX(0xE8E8F0);
    gfx_fill_round_rect(px, PAD_Y, pw, PAD_H, 7, pad);
    gfx_fill_rect(px + 10, PAD_Y + 5, pw - 20, 4, HEX(0xE05CC8));

    for (int i = 0; i < MAX_BALLS; i++) {
        if (s_balls[i].on) gfx_fill_circle((int)s_balls[i].x, (int)s_balls[i].y, BALL_R, WHITE);
    }

    if (s_banner_t > 0) {
        snprintf(buf, sizeof(buf), "LEVEL %d", s_level);
        gfx_text_center(LCD_W / 2, 300, buf, 4, WHITE);
    } else if (s_attached) {
        gfx_text_center(LCD_W / 2, 360, "DRAG, THEN LET GO", 2, HEX(0x8A8AB0));
    }
}

static int bricks_score(void)
{
    return s_score;
}

static void bricks_icon(int x, int y, int size)
{
    int bw = size / 4 - 2, bh = size / 8;
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 4; c++) {
            draw_brick(x + c * (bw + 2), y + r * (bh + 3), bw, bh, r, 1);
        }
    }
    gfx_fill_circle(x + size / 2 + 6, y + size * 5 / 8, 6, WHITE);
    gfx_fill_round_rect(x + size / 4, y + size - 10, size / 2, 9, 4, HEX(0xE8E8F0));
}

const game_t game_bricks = {
    .name = "BRICK BASH",
    .nvs_key = "hi_bricks",
    .accent = 0xF5913D,
    .start = bricks_start,
    .update = bricks_update,
    .draw = bricks_draw,
    .score = bricks_score,
    .draw_icon = bricks_icon,
};

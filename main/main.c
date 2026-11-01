#include <math.h>
#include <stdio.h>
#include "audio.h"
#include "board.h"
#include "display.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "game.h"
#include "gfx.h"
#include "input.h"
#include "store.h"
#include "util.h"

#define FRAME_US            33333
#define DIM_AFTER_US        (60LL * 1000000)
#define POWER_OFF_AFTER_US  (5LL * 60 * 1000000)
#define BRIGHT_FULL         255
#define BRIGHT_DIM          40

#define TILE_X      24
#define TILE_W      432
#define TILE_H      100
#define TILE_Y0     96
#define TILE_STEP   112
#define MENU_TOP    (TILE_Y0 - 8)   // tiles scroll under the header above this
#define MENU_BOTTOM (LCD_H - 12)

#define PANEL_X     60
#define PANEL_Y     130
#define PANEL_W     360
#define PANEL_H     220
#define BTN_Y       (PANEL_Y + 140)
#define BTN_W       150
#define BTN_H       60
#define BTN1_X      (PANEL_X + 20)
#define BTN2_X      (PANEL_X + PANEL_W - 20 - BTN_W)

// In-game close button (top right) that opens the pause screen
#define CLOSE_CX    436
#define CLOSE_CY    38
#define CLOSE_R     19
#define CLOSE_HIT_X 392
#define CLOSE_HIT_H 84

#define BG          HEX(0x0B0B1A)
#define GREY        HEX(0x9A9AB8)

typedef enum { ST_MENU, ST_PLAY, ST_PAUSE, ST_OVER } state_t;

static const game_t *const s_games[] = { &game_tetris, &game_breakout, &game_crossyroad, &game_spacewars, &game_pipsquest };
#define NGAMES (int)(sizeof(s_games) / sizeof(s_games[0]))

static state_t s_state = ST_MENU;
static int s_sel;
static const game_t *s_cur;
static uint32_t s_best[NGAMES];
static bool s_new_best;
static float s_state_t;     // seconds since the last state change
static int s_battery = -1;
static bool s_charging;
static float s_menu_t;
static bool s_close_touch;  // the current touch started on the close button
static float s_scroll, s_scroll_v;  // menu scroll offset (px) and fling speed
static bool s_follow_sel;   // scroll to the selected tile after a button press
static int s_drag_y;

static struct { int16_t x, y; uint8_t speed, bright; } s_stars[40];

static void set_state(state_t st)
{
    s_state = st;
    s_state_t = 0;
}

static bool tapped(const input_t *in, int x, int y, int w, int h)
{
    return in->tap && in->x >= x && in->x < x + w && in->y >= y && in->y < y + h;
}

static void start_game(int idx)
{
    s_sel = idx;
    s_cur = s_games[idx];
    s_cur->start();
    s_close_touch = false;
    sfx(SFX_START);
    set_state(ST_PLAY);
}

static void end_game(void)
{
    uint32_t score = s_cur->score();
    s_new_best = score > s_best[s_sel];
    if (s_new_best) {
        s_best[s_sel] = score;
        store_set(s_cur->nvs_key, score);
    }
    sfx(SFX_GAME_OVER);
    set_state(ST_OVER);
}

static int tile_y(int i)
{
    return TILE_Y0 + i * TILE_STEP - (int)s_scroll;
}

static float max_scroll(void)
{
    float m = TILE_Y0 + (NGAMES - 1) * TILE_STEP + TILE_H - MENU_BOTTOM;
    return m > 0 ? m : 0;
}

// Drag to scroll, with a fling that slows down; buttons scroll the selection into view
static void menu_scroll(const input_t *in, float dt)
{
    if (in->touch_start) {
        s_drag_y = in->y;
        s_scroll_v = 0;
        s_follow_sel = false;
    }
    if (in->touching) {
        float d = s_drag_y - in->y;
        s_scroll += d;
        s_scroll_v = s_scroll_v * 0.5f + (dt > 0 ? d / dt : 0) * 0.5f;
        s_drag_y = in->y;
    } else if (s_follow_sel) {
        float top = TILE_Y0 + s_sel * TILE_STEP;
        float target = clampf(s_scroll, top + TILE_H - MENU_BOTTOM, top - TILE_Y0);
        s_scroll += (target - s_scroll) * clampf(dt * 12, 0, 1);
    } else if (s_scroll_v != 0) {
        s_scroll += s_scroll_v * dt;
        s_scroll_v *= 1 - clampf(dt * 4, 0, 1);
        if (fabsf(s_scroll_v) < 10) s_scroll_v = 0;
    }
    s_scroll = clampf(s_scroll, 0, max_scroll());
}

static void update(const input_t *in, float dt)
{
    s_state_t += dt;
    switch (s_state) {
    case ST_MENU:
        s_menu_t += dt;
        menu_scroll(in, dt);
        for (int i = 0; i < NGAMES && in->y >= MENU_TOP; i++) {
            if (tapped(in, TILE_X, tile_y(i), TILE_W, TILE_H)) {
                start_game(i);
                return;
            }
        }
        if (tapped(in, 0, 0, 100, 80)) {
            audio_set_enabled(!audio_enabled());
            store_set("sound", audio_enabled());
            sfx(SFX_SELECT);
        }
        if (in->pressed & (BTN_LEFT | BTN_RIGHT)) {
            s_sel = (s_sel + (in->pressed & BTN_LEFT ? NGAMES - 1 : 1)) % NGAMES;
            s_follow_sel = true;
            sfx(SFX_SELECT);
        }
        if (in->pressed & BTN_PWR) start_game(s_sel);
        break;

    case ST_PLAY: {
        // A touch that starts on the close button belongs to it, not to the game
        input_t gin = *in;
        bool close = false;
        if (in->touch_start) s_close_touch = in->x >= CLOSE_HIT_X && in->y < CLOSE_HIT_H;
        if (s_close_touch) {
            close = tapped(in, CLOSE_HIT_X, 0, LCD_W - CLOSE_HIT_X, CLOSE_HIT_H);
            if (!in->touching) s_close_touch = false;
            gin.touching = gin.touch_start = gin.touch_end = gin.tap = false;
            gin.swipe = SWIPE_NONE;
        }
        if (close || (!s_cur->uses_pwr && (in->pressed & BTN_PWR))) {
            sfx(SFX_PAUSE);
            set_state(ST_PAUSE);
        } else if (!s_cur->update(&gin, dt)) {
            end_game();
        }
        break;
    }

    case ST_PAUSE:
        if ((in->pressed & BTN_PWR) || tapped(in, BTN1_X, BTN_Y, BTN_W, BTN_H)) {
            set_state(ST_PLAY);
        } else if (tapped(in, BTN2_X, BTN_Y, BTN_W, BTN_H)) {
            set_state(ST_MENU);
        }
        break;

    case ST_OVER:
        if (s_state_t < 0.8f) break;   // don't let a frantic last tap skip the result
        if (tapped(in, BTN1_X, BTN_Y, BTN_W, BTN_H) || (in->pressed & (BTN_LEFT | BTN_RIGHT))) {
            start_game(s_sel);
        } else if (tapped(in, BTN2_X, BTN_Y, BTN_W, BTN_H) || (in->pressed & BTN_PWR)) {
            set_state(ST_MENU);
        }
        break;
    }
}

static void draw_sound_icon(int x, int y)
{
    color_t c = audio_enabled() ? WHITE : GREY;
    gfx_fill_rect(x, y + 10, 10, 14, c);
    for (int i = 0; i < 10; i++) {
        gfx_fill_rect(x + 10 + i, y + 10 - i, 1, 14 + 2 * i, c);
    }
    if (audio_enabled()) {
        gfx_fill_rect(x + 26, y + 11, 3, 12, c);
        gfx_fill_rect(x + 33, y + 6, 3, 22, c);
    } else {
        for (int i = 0; i < 14; i++) {
            gfx_fill_rect(x + 26 + i, y + 10 + i, 3, 3, HEX(0xF0504A));
            gfx_fill_rect(x + 39 - i, y + 10 + i, 3, 3, HEX(0xF0504A));
        }
    }
}

static void draw_battery(int x, int y)
{
    if (s_battery < 0) return;
    color_t fill = s_charging ? HEX(0x3DD6F5) : s_battery > 50 ? HEX(0x5CE065)
                 : s_battery > 20 ? HEX(0xF5D33D) : HEX(0xF0504A);
    gfx_rect(x, y, 46, 24, 3, WHITE);
    gfx_fill_rect(x + 46, y + 7, 4, 10, WHITE);
    gfx_fill_rect(x + 5, y + 5, 36 * s_battery / 100, 14, fill);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", s_battery);
    gfx_text(x - gfx_text_width(buf, 2) - 10, y + 5, buf, 2, GREY);
}

static void draw_stars(int y_max)
{
    for (int i = 0; i < (int)(sizeof(s_stars) / sizeof(s_stars[0])); i++) {
        int y = (s_stars[i].y + (int)(s_menu_t * s_stars[i].speed)) % LCD_H;
        if (y >= y_max) continue;
        int b = s_stars[i].bright;
        gfx_fill_rect(s_stars[i].x, y, 2, 2, RGB(b, b, b + 30 > 255 ? 255 : b + 30));
    }
}

static void draw_menu(void)
{
    gfx_clear(BG);
    draw_stars(LCD_H);

    for (int i = 0; i < NGAMES; i++) {
        const game_t *g = s_games[i];
        int y = tile_y(i);
        if (y + TILE_H <= MENU_TOP || !gfx_visible(y, TILE_H)) continue;
        color_t accent = HEX(g->accent);
        bool sel = i == s_sel;
        if (sel) gfx_fill_round_rect(TILE_X, y, TILE_W, TILE_H, 20, accent);
        gfx_fill_round_rect(TILE_X + 4, y + 4, TILE_W - 8, TILE_H - 8, 16, sel ? HEX(0x232344) : HEX(0x181830));
        g->draw_icon(TILE_X + 18, y + 10, 80);
        int scale = gfx_text_width(g->name, 4) <= TILE_W - 140 ? 4 : 3;     // long names drop a size
        gfx_text_shadow(TILE_X + 120, y + (scale == 4 ? 20 : 24), g->name, scale, sel ? WHITE : HEX(0xD8D8EC));
        char buf[24];
        snprintf(buf, sizeof(buf), "BEST %lu", (unsigned long)s_best[i]);
        gfx_text(TILE_X + 122, y + 64, buf, 2, sel ? accent : GREY);
    }

    float max = max_scroll();
    if (max > 0) {
        int track = MENU_BOTTOM - MENU_TOP - 8;
        int thumb = track * (MENU_BOTTOM - MENU_TOP) / (MENU_BOTTOM - MENU_TOP + (int)max);
        int ty = MENU_TOP + 4 + (int)((track - thumb) * s_scroll / max);
        gfx_fill_round_rect(464, MENU_TOP + 4, 6, track, 3, HEX(0x1C1C38));
        gfx_fill_round_rect(464, ty, 6, thumb, 3, GREY);
    }

    // The header covers tiles that have scrolled up under it
    if (gfx_visible(0, MENU_TOP)) {
        gfx_fill_rect(0, 0, LCD_W, MENU_TOP, BG);
        draw_stars(MENU_TOP);
        gfx_text_center(LCD_W / 2 + 3, 31, "ARCADE", 5, HEX(0xB45CF0));
        gfx_text_center(LCD_W / 2, 28, "ARCADE", 5, WHITE);
        draw_sound_icon(34, 26);
        draw_battery(400, 34);
    }
}

static void draw_button(int x, int y, const char *label, color_t bg)
{
    gfx_fill_round_rect(x, y, BTN_W, BTN_H, 16, bg);
    gfx_text_center(x + BTN_W / 2, y + (BTN_H - 16) / 2, label, 2, WHITE);
}

static void draw_close_button(void)
{
    if (!gfx_visible(CLOSE_CY - CLOSE_R, 2 * CLOSE_R)) return;
    gfx_fill_circle(CLOSE_CX, CLOSE_CY, CLOSE_R, HEX(0x8A8AB0));
    gfx_fill_circle(CLOSE_CX, CLOSE_CY, CLOSE_R - 2, HEX(0x2A2A4C));
    for (int i = -7; i <= 7; i++) {
        gfx_fill_rect(CLOSE_CX + i - 1, CLOSE_CY + i - 1, 3, 3, WHITE);
        gfx_fill_rect(CLOSE_CX - i - 1, CLOSE_CY + i - 1, 3, 3, WHITE);
    }
}

static void draw_overlay(void)
{
    char buf[32];
    s_cur->draw();
    gfx_darken(0, 0, LCD_W, LCD_H);
    color_t accent = HEX(s_cur->accent);
    gfx_fill_round_rect(PANEL_X - 3, PANEL_Y - 3, PANEL_W + 6, PANEL_H + 6, 26, accent);
    gfx_fill_round_rect(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 24, HEX(0x15152C));

    if (s_state == ST_PAUSE) {
        gfx_text_center(LCD_W / 2, PANEL_Y + 34, "PAUSED", 5, WHITE);
        gfx_text_center(LCD_W / 2, PANEL_Y + 96, "PWR TO RESUME", 2, GREY);
        draw_button(BTN1_X, BTN_Y, "RESUME", gfx_mix(accent, BLACK, 90));
        draw_button(BTN2_X, BTN_Y, "QUIT", HEX(0x3A3A5C));
    } else {
        gfx_text_center(LCD_W / 2, PANEL_Y + 24, "GAME OVER", 4, WHITE);
        snprintf(buf, sizeof(buf), "%d", s_cur->score());
        gfx_text_center(LCD_W / 2, PANEL_Y + 70, buf, 5, accent);
        if (s_new_best) {
            if ((int)(s_state_t * 4) & 1) gfx_text_center(LCD_W / 2, PANEL_Y + 118, "NEW BEST!", 2, HEX(0xF5D33D));
        } else {
            snprintf(buf, sizeof(buf), "BEST %lu", (unsigned long)s_best[s_sel]);
            gfx_text_center(LCD_W / 2, PANEL_Y + 118, buf, 2, GREY);
        }
        draw_button(BTN1_X, BTN_Y, "AGAIN", gfx_mix(accent, BLACK, 90));
        draw_button(BTN2_X, BTN_Y, "MENU", HEX(0x3A3A5C));
    }
}

static void draw(void)
{
    switch (s_state) {
    case ST_MENU:  draw_menu(); break;
    case ST_PLAY:  s_cur->draw(); draw_close_button(); break;
    default:       draw_overlay(); break;
    }
}

static void refresh_battery(void)
{
    s_battery = pmu_battery_percent();
    s_charging = pmu_is_charging();
}

void app_main(void)
{
    store_init();
    board_init();
    display_init();
    input_init();
    audio_init();
    rnd_seed(esp_random());

    audio_set_enabled(store_get("sound", 1));
    for (int i = 0; i < NGAMES; i++) {
        s_best[i] = store_get(s_games[i]->nvs_key, 0);
    }
    for (int i = 0; i < (int)(sizeof(s_stars) / sizeof(s_stars[0])); i++) {
        s_stars[i].x = rnd(LCD_W);
        s_stars[i].y = rnd(LCD_H);
        s_stars[i].speed = 6 + rnd(20);
        s_stars[i].bright = 50 + rnd(120);
    }
    refresh_battery();

    input_t in;
    int64_t last = esp_timer_get_time();
    int64_t last_active = last;
    bool dimmed = false;
    int frame = 0;

    for (;;) {
        int64_t now = esp_timer_get_time();
        float dt = (now - last) / 1e6f;
        last = now;
        if (dt > 0.05f) dt = 0.05f;

        input_poll(&in);
        if (in.activity) {
            last_active = now;
            if (dimmed) {
                // The touch that wakes the screen shouldn't also press something
                dimmed = false;
                display_set_brightness(BRIGHT_FULL);
                in = (input_t){ 0 };
            }
        }

        update(&in, dt);

        if (++frame % 60 == 0) refresh_battery();

        int64_t idle = now - last_active;
        if (s_state != ST_PLAY) {
            if (!dimmed && idle > DIM_AFTER_US) {
                dimmed = true;
                display_set_brightness(BRIGHT_DIM);
            }
            if (idle > POWER_OFF_AFTER_US && s_battery >= 0 && !pmu_usb_powered()) {
                pmu_power_off();
            }
        }

        display_frame(draw);

        int64_t spent = esp_timer_get_time() - now;
        int wait_ms = (int)((FRAME_US - spent) / 1000);
        vTaskDelay(pdMS_TO_TICKS(wait_ms > 1 ? wait_ms : 1));
    }
}

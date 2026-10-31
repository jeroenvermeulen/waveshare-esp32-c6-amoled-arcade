#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    BTN_LEFT  = 1 << 0,
    BTN_RIGHT = 1 << 1,
    BTN_PWR   = 1 << 2,   // holding it ~4 s powers the board off in hardware
};

enum { SWIPE_NONE, SWIPE_LEFT, SWIPE_RIGHT, SWIPE_UP, SWIPE_DOWN };

typedef struct {
    bool touching;          // finger currently down at (x, y)
    int x, y;               // also the release point on touch_end / tap
    bool touch_start;       // finger went down this frame
    bool touch_end;         // finger lifted this frame
    bool tap;               // short touch without much movement, reported on release
    int swipe;              // fast flick, reported once while the finger is still down
    uint8_t held;           // buttons currently pressed
    uint8_t pressed;        // buttons that went down this frame
    bool activity;
} input_t;

void input_init(void);
void input_poll(input_t *in);

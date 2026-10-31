#pragma once

#include <stdbool.h>
#include "gfx.h"
#include "input.h"

typedef struct {
    const char *name;
    const char *nvs_key;        // high score key, max 15 chars
    uint32_t accent;            // 0xRRGGBB
    void (*start)(void);
    bool (*update)(const input_t *in, float dt);    // false once the game is over
    void (*draw)(void);
    int  (*score)(void);
    void (*draw_icon)(int x, int y, int size);
} game_t;

extern const game_t game_blocks;
extern const game_t game_bricks;
extern const game_t game_hopper;

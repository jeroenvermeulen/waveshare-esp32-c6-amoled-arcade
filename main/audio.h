#pragma once

#include <stdbool.h>

typedef enum {
    SFX_SELECT,
    SFX_START,
    SFX_PAUSE,
    SFX_GAME_OVER,
    SFX_LEVEL_UP,
    // blocks
    SFX_MOVE,
    SFX_ROTATE,
    SFX_LOCK,
    SFX_DROP,
    SFX_LINE,
    SFX_QUAD,
    // bricks
    SFX_WALL,
    SFX_PADDLE,
    SFX_BRICK,
    SFX_BRICK_HIT,
    SFX_STEEL,
    SFX_POWERUP,
    SFX_LOSE_LIFE,
    // hopper
    SFX_HOP,
    SFX_SPLASH,
    SFX_CRASH,
    SFX_COUNT
} sfx_t;

void audio_init(void);
void audio_set_enabled(bool on);
bool audio_enabled(void);
void sfx(sfx_t id);

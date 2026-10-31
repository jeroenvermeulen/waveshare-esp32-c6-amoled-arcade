#pragma once

#include <stdint.h>

void display_init(void);
void display_set_brightness(uint8_t level);   // 0..255

// Renders a full frame. `draw` is called once per horizontal band with the
// gfx target set to that band, so it must draw the whole scene each call.
void display_frame(void (*draw)(void));

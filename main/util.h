#pragma once

#include <stdint.h>

void rnd_seed(uint32_t seed);
uint32_t rnd_u32(void);
int rnd(int n);         // 0 .. n-1
float rndf(void);       // 0 .. 1

static inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

#include "util.h"

static uint32_t s_state = 0x12345678;

void rnd_seed(uint32_t seed)
{
    s_state = seed ? seed : 0x12345678;
}

uint32_t rnd_u32(void)
{
    uint32_t x = s_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return s_state = x;
}

int rnd(int n)
{
    return n > 0 ? (int)(rnd_u32() % (uint32_t)n) : 0;
}

float rndf(void)
{
    return (rnd_u32() >> 8) * (1.0f / 16777216.0f);
}

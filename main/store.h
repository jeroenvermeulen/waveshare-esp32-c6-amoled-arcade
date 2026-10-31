#pragma once

#include <stdint.h>

void store_init(void);
uint32_t store_get(const char *key, uint32_t def);
void store_set(const char *key, uint32_t value);

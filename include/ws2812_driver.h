#ifndef PI_HOUR_WS2812_DRIVER_H
#define PI_HOUR_WS2812_DRIVER_H

#include <stdint.h>

void ws2812_init(void);

// Push GRB buffer (24-bit per LED, MSB first in each color as typical NeoPixel)
void ws2812_show(const uint32_t *pixels, unsigned count);

#endif

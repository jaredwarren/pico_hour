#ifndef PI_HOUR_HTTP_SERVER_H
#define PI_HOUR_HTTP_SERVER_H

#include <stdbool.h>
#include <stdint.h>

void http_server_init(void);

// Call frequently from main loop (with cyw43_arch_poll)
void http_server_poll(void);

void http_get_color(uint8_t *r, uint8_t *g, uint8_t *b);

#endif

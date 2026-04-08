#include "ws2812_driver.h"

#include "config.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include "ws2812.pio.h"

static PIO pio_strip;
static uint sm_strip;

void ws2812_init(void) {
    pio_strip = pio0;
    sm_strip = pio_claim_unused_sm(pio_strip, true);
    uint offset = pio_add_program(pio_strip, &ws2812_program);
    pio_gpio_init(pio_strip, WS2812_PIN);
    pio_sm_set_consecutive_pindirs(pio_strip, sm_strip, WS2812_PIN, 1, true);
    pio_sm_config c = ws2812_program_get_default_config(offset);
    sm_config_set_sideset_pins(&c, WS2812_PIN);
    sm_config_set_out_shift(&c, false, true, 24);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    const unsigned cycles_per_bit = ws2812_T1 + ws2812_T2 + ws2812_T3;
    const float div = (float)clock_get_hz(clk_sys) / (800000.0f * (float)cycles_per_bit);
    sm_config_set_clkdiv(&c, div);
    pio_sm_init(pio_strip, sm_strip, offset, &c);
    pio_sm_set_enabled(pio_strip, sm_strip, true);
}

void ws2812_show(const uint32_t *pixels, unsigned count) {
    for (unsigned i = 0; i < count; i++) {
        pio_sm_put_blocking(pio_strip, sm_strip, pixels[i] << 8u);
    }
    sleep_us(60);
}

/*
 * ws2812_pio.c — drive WS2812 / NeoPixel LEDs using the RP2040 PIO
 *
 * Why PIO instead of “bit-bang in C”?
 *  WS2812 timing is tight (sub-microsecond). If the CPU is interrupted mid-bit, colors glitch.
 *  The Programmable I/O block runs a tiny program independently of the main cores once we feed it
 *  bytes — timing stays rock-solid.
 *
 * ws2812.pio (assembled into ws2812.pio.h by the build) defines the bit pattern for one LED color.
 * We load that program into PIO instruction memory, assign a state machine (SM), and push 24-bit
 * GRB words into its FIFO; the SM shifts them to the GPIO.
 */

#include "ws2812_driver.h"

#include "config.h"           /* WS2812_PIN */
#include "hardware/clocks.h"  /* clock_get_hz(clk_sys) — CPU frequency for baud/divider math */
#include "hardware/pio.h"     /* PIO API: pio0, state machines, FIFO */
#include "pico/stdlib.h"      /* sleep_us — reset gap after a frame */
#include "ws2812.pio.h"       /* generated: ws2812_program, ws2812_T1/T2/T3, init helper */

/* File scope: one PIO block and one state machine reserved for the strip for the whole program. */
static PIO pio_strip;
static uint sm_strip;

void ws2812_init(void) {
    /* pio0 is the first PIO block; either pio0 or pio1 would work if pins allow. */
    pio_strip = pio0;
    /* Claim a free SM so we don’t collide with other libraries using the same PIO. */
    sm_strip = pio_claim_unused_sm(pio_strip, true);
    /* Load the assembled PIO program; offset is where it landed in PIO instruction memory. */
    uint offset = pio_add_program(pio_strip, &ws2812_program);

    /* Bind the data pin to this PIO and set it as output. */
    pio_gpio_init(pio_strip, WS2812_PIN);
    pio_sm_set_consecutive_pindirs(pio_strip, sm_strip, WS2812_PIN, 1, true);

    /* Start from SDK template for this program, then tweak pins, shift width, clock. */
    pio_sm_config c = ws2812_program_get_default_config(offset);
    sm_config_set_sideset_pins(&c, WS2812_PIN); /* “side-set” toggles pin while executing insns */

    /*
     * Shift 24 bits per pixel out of the TX FIFO, MSB first (WS2812 expects green byte first on wire
     * for typical strips — your buffer should already be GRB in one word).
     * false, true = shift from OSR to pins, autopull when 24 bits shifted.
     */
    sm_config_set_out_shift(&c, false, true, 24);

    /* Merge TX FIFO halves so we have a deeper queue (fewer CPU stalls feeding pixels). */
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

    /*
     * WS2812 bit rate is 800 kHz. Each bit takes (T1+T2+T3) PIO cycles (from .pio defines).
     * clkdiv scales system clock down so one WS2812 bit time matches the datasheet.
     */
    const unsigned cycles_per_bit = ws2812_T1 + ws2812_T2 + ws2812_T3;
    const float div = (float)clock_get_hz(clk_sys) / (800000.0f * (float)cycles_per_bit);
    sm_config_set_clkdiv(&c, div);

    pio_sm_init(pio_strip, sm_strip, offset, &c);
    pio_sm_set_enabled(pio_strip, sm_strip, true);
}

/*
 * Send one frame. Blocking put: CPU waits if FIFO full — fine for modest LED counts.
 *
 * pixels[i] << 8u:
 *   The PIO program expects 24 data bits in the low end of the 32-bit FIFO word; shifting left
 *   clears the low 8 bits as padding (matches Raspberry Pi pico-examples WS2812 pattern).
 *
 * sleep_us(60):
 *   After the last bit, the line must stay low longer than ~50 µs so the strip latches the frame
 *   before the next one (datasheet “reset” code).
 */
void ws2812_show(const uint32_t *pixels, unsigned count) {
    for (unsigned i = 0; i < count; i++) {
        pio_sm_put_blocking(pio_strip, sm_strip, pixels[i] << 8u);
    }
    sleep_us(60);
}

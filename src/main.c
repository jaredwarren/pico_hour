/*
 * main.c — program entry, Wi-Fi bring-up, main loop
 *
 * Flow (why this order matters):
 *  1. USB serial works only after stdio_init_all() — we use printf for debugging.
 *  2. cyw43_arch_* initializes the Pico W’s Wi-Fi chip and lwIP (lightweight IP stack).
 *  3. We wait until DHCP gives us an IP; until then, nothing can reach our HTTP server.
 *  4. HTTP server and LED hardware are started; then we loop forever:
 *     - cyw43_arch_poll() MUST run regularly so Wi-Fi and TCP keep working (no OS thread
 *       does this for us in “poll” mode).
 *     - Read accelerometer → map tilt to LED index → draw → push pixels to WS2812.
 *
 * C note: "static" on a function (below) means visible only in this .c file — like "private".
 */

#include "accel.h"         /* accel_init(), accel_read_g() */
#include "config.h"        /* NUM_LEDS, WINDOW_SIZE, Wi-Fi macros, tilt tuning */
#include "http_server.h"
#include "ws2812_driver.h"

#include "pico/cyw43_arch.h" /* Wi-Fi + lwIP glue for Pico W */
#include "pico/stdlib.h"     /* sleep_ms, stdio_init_all, bool, uint32_t, etc. */

#include "lwip/dhcp.h"       /* dhcp_supplied_address() — “do we have an IP from DHCP yet?” */
#include "lwip/ip4_addr.h"   /* ip4addr_ntoa() — print IP as string */
#include "lwip/netif.h"      /* netif_default, netif_is_up() — “network interface” object */

#include <stdio.h>           /* printf */

/*
 * After Wi-Fi associates with the AP, lwIP still needs a few DHCP exchanges to get an address.
 * We spin here (calling cyw43_arch_poll) instead of blocking inside a single long call, because
 * the stack only progresses when polled in this SDK configuration.
 */
static void wait_for_dhcp(void) {
    while (true) {
        if (netif_default != NULL && netif_is_up(netif_default) &&
            dhcp_supplied_address(netif_default)) {
            break;
        }
        cyw43_arch_poll();
        sleep_ms(100);
    }
}

/*
 * Build one frame for the LED strip in RAM, then ws2812_show() sends it.
 *
 * pixels: pointer to array of 32-bit colors. WS2812/NeoPixel order is GRB (green, red, blue)
 *         in the low 24 bits — not RGB — so we pack that way.
 * center_start: index of the first lit LED in the window (window extends to higher indices).
 *
 * We zero the whole strip first so LEDs outside the window go dark.
 */
static void render_strip(uint32_t *pixels, int center_start) {
    uint8_t r, g, b;
    http_get_color(&r, &g, &b); /* & passes addresses so the function can write all three */

    /* Bit shifts: place G in bits 23–16, R in 15–8, B in 7–0 (common WS2812 layout). */
    uint32_t grb = ((uint32_t)g << 16) | ((uint32_t)r << 8) | (uint32_t)b;

    for (int i = 0; i < (int)NUM_LEDS; i++) {
        pixels[i] = 0;
    }

    /* If config asks for a window bigger than the strip, clamp (defensive). */
    unsigned win = WINDOW_SIZE;
    if (win > NUM_LEDS) {
        win = NUM_LEDS;
    }
    for (unsigned w = 0; w < win; w++) {
        int idx = center_start + (int)w;
        if (idx >= 0 && idx < (int)NUM_LEDS) {
            pixels[idx] = grb;
        }
    }
}

/*
 * main is the only special function name — the C runtime calls it after boot (CRT0 + SDK init).
 * int main(void) — void means “no arguments” in C (not the same as empty () in older C).
 */
int main(void) {
    stdio_init_all();
    /* Short delay so USB CDC serial is ready before we spam printf (common Pico pattern). */
    sleep_ms(800);

    /*
     * pixels[NUM_LEDS] — array on stack; size comes from config.h.
     * VLA (variable-length arrays) are avoided; NUM_LEDS must be a compile-time constant.
     */
    uint32_t pixels[NUM_LEDS];
    float tilt_filtered = 0.0f; /* exponential moving average of tilt */
    int last_start = 0;         /* if sensor fails, keep drawing at last good position */

    /* SDK convention: 0 = success, non-zero = error (here we treat any non-zero as fatal). */
    if (cyw43_arch_init()) {
        printf("cyw43_arch_init failed\n");
        return -1;
    }
    cyw43_arch_enable_sta_mode(); /* “station” = join a router, not access-point mode */

    printf("Connecting to Wi-Fi…\n");
    /* WPA2-PSK is the typical home Wi-Fi auth. Timeout in milliseconds. */
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 60000)) {
        printf("Wi-Fi connect failed\n");
        return -1;
    }
    printf("Wi-Fi connected\n");

    wait_for_dhcp();
    /* netif_ip4_addr returns struct; ip4addr_ntoa gives dotted-decimal string for printf */
    printf("IP: %s\n", ip4addr_ntoa(netif_ip4_addr(netif_default)));

    http_server_init();
    ws2812_init();

    if (!accel_init()) {
        printf("Warning: MPU-6050 not detected (check I2C wiring). LEDs will stay at last position.\n");
    }

    /* Embedded firmware rarely returns from main; reset happens on crash or watchdog if enabled. */
    while (true) {
        /*
         * Single-threaded: Wi-Fi stack runs incrementally inside cyw43_arch_poll().
         * If you skip this for too long, TCP can stall or disconnect.
         */
        cyw43_arch_poll();
        http_server_poll(); /* placeholder — real work is lwIP callbacks triggered by poll */

        float ax, ay, az;
        int center_start = last_start; /* default if read fails this frame */

        if (accel_read_g(&ax, &ay, &az)) {
            /* Pick one axis of gravity vector; which axis “tilts” depends on board mounting. */
            float v;
            switch (TILT_AXIS) {
            default:
            case 0:
                v = ax;
                break;
            case 1:
                v = ay;
                break;
            case 2:
                v = az;
                break;
            }

            /* Deadband: tiny sensor noise around 0 shouldn’t jitter the LEDs. */
            if (v > -TILT_DEADBAND_G && v < TILT_DEADBAND_G) {
                v = 0.0f;
            }

            /*
             * Exponential moving average: smooth motion without a big buffer.
             * new_avg = old_avg * (1-alpha) + sample * alpha  — lower alpha = smoother, slower.
             */
            tilt_filtered =
                tilt_filtered * (1.0f - TILT_FILTER_ALPHA) + v * TILT_FILTER_ALPHA;

            float t = tilt_filtered;
            if (t > 1.0f) {
                t = 1.0f;
            }
            if (t < -1.0f) {
                t = -1.0f;
            }
            /* Map roughly [-1, 1] (g) to [0, 1] for LED index math. */
            const float norm = (t + 1.0f) * 0.5f;

            /*
             * "center" is the ideal center of the window, but the strip is indexed 0..NUM_LEDS-1
             * and the window has width WINDOW_SIZE, so the left edge can only go 0 .. (NUM_LEDS - WINDOW).
             */
            int span = (int)NUM_LEDS - (int)WINDOW_SIZE;
            if (span < 0) {
                span = 0;
            }
            int center = (int)(norm * (float)span + 0.5f); /* +0.5f for rounding */
            if (center < 0) {
                center = 0;
            }
            if (center > span) {
                center = span;
            }

            /* Convert “center” to starting index (left edge of lit segment). */
            const int half = (int)WINDOW_SIZE / 2;
            center_start = center - half;
            if (center_start < 0) {
                center_start = 0;
            }
            if (center_start > span) {
                center_start = span;
            }
            last_start = center_start;
        }

        render_strip(pixels, center_start);
        ws2812_show(pixels, NUM_LEDS);

        /* ~100 Hz max update rate; fast enough for smooth motion, leaves CPU for Wi-Fi. */
        sleep_ms(10);
    }
}

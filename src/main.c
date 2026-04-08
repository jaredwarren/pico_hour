#include "accel.h"
#include "config.h"
#include "http_server.h"
#include "ws2812_driver.h"

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

#include "lwip/dhcp.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"

#include <stdio.h>

static void wait_for_dhcp(void) {
    while (true) {
        if (netif_default != NULL && netif_is_up(netif_default) && dhcp_supplied_address(netif_default)) {
            break;
        }
        cyw43_arch_poll();
        sleep_ms(100);
    }
}

static void render_strip(uint32_t *pixels, int center_start) {
    uint8_t r, g, b;
    http_get_color(&r, &g, &b);
    uint32_t grb = ((uint32_t)g << 16) | ((uint32_t)r << 8) | (uint32_t)b;

    for (int i = 0; i < (int)NUM_LEDS; i++) {
        pixels[i] = 0;
    }

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

int main(void) {
    stdio_init_all();
    sleep_ms(800);

    uint32_t pixels[NUM_LEDS];
    float tilt_filtered = 0.0f;
    int last_start = 0;

    if (cyw43_arch_init()) {
        printf("cyw43_arch_init failed\n");
        return -1;
    }
    cyw43_arch_enable_sta_mode();

    printf("Connecting to Wi-Fi…\n");
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 60000)) {
        printf("Wi-Fi connect failed\n");
        return -1;
    }
    printf("Wi-Fi connected\n");

    wait_for_dhcp();
    printf("IP: %s\n", ip4addr_ntoa(netif_ip4_addr(netif_default)));

    http_server_init();
    ws2812_init();

    if (!accel_init()) {
        printf("Warning: MPU-6050 not detected (check I2C wiring). LEDs will stay at last position.\n");
    }

    while (true) {
        cyw43_arch_poll();
        http_server_poll();

        float ax, ay, az;
        int center_start = last_start;

        if (accel_read_g(&ax, &ay, &az)) {
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

            if (v > -TILT_DEADBAND_G && v < TILT_DEADBAND_G) {
                v = 0.0f;
            }

            tilt_filtered =
                tilt_filtered * (1.0f - TILT_FILTER_ALPHA) + v * TILT_FILTER_ALPHA;

            float t = tilt_filtered;
            if (t > 1.0f) {
                t = 1.0f;
            }
            if (t < -1.0f) {
                t = -1.0f;
            }
            const float norm = (t + 1.0f) * 0.5f;

            int span = (int)NUM_LEDS - (int)WINDOW_SIZE;
            if (span < 0) {
                span = 0;
            }
            int center = (int)(norm * (float)span + 0.5f);
            if (center < 0) {
                center = 0;
            }
            if (center > span) {
                center = span;
            }

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

        sleep_ms(10);
    }
}

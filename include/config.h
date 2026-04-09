#ifndef PI_HOUR_CONFIG_H
#define PI_HOUR_CONFIG_H

/*
 * Wi-Fi credentials — pick one approach (do not commit secrets):
 *
 * 1) Recommended: copy include/config.local.h.example → include/config.local.h
 *    (config.local.h is gitignored) and set WIFI_SSID / WIFI_PASSWORD there.
 *
 * 2) CMake / Make: -DWIFI_SSID="..." -DWIFI_PASSWORD="..."
 *    e.g. make build WIFI_SSID=MyNet WIFI_PASSWORD=secret
 *
 * 3) Edit the defaults below only for throwaway / lab builds (easy to commit by mistake).
 */
#if defined(__has_include)
#if __has_include("config.local.h")
#include "config.local.h"
#endif
#endif

#ifndef WIFI_SSID
#define WIFI_SSID "your-ssid"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "your-password"
#endif

#define HTTP_SERVER_PORT 80

// WS2812 data pin (3.3 V; use level shifter for 5 V strips)
#define WS2812_PIN 2
#define NUM_LEDS 60
#define WINDOW_SIZE 8

// MPU-6050 on I2C0
#define I2C_INST i2c0
#define I2C_SDA_PIN 4
#define I2C_SCL_PIN 5
#define MPU6050_ADDR 0x68

// Tilt: which accel axis maps to strip position (0=X, 1=Y, 2=Z)
#define TILT_AXIS 0

// Low-pass (0..1); higher = smoother, slower
#define TILT_FILTER_ALPHA 0.12f
// Ignore small tilts (in g, applied before normalize)
#define TILT_DEADBAND_G 0.08f

#endif

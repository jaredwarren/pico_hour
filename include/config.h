#ifndef PI_HOUR_CONFIG_H
#define PI_HOUR_CONFIG_H

// WiFi — set before build or edit here
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

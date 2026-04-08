# pi_hour — Pico W: WS2812, MPU-6050 tilt, HTTP color API

Firmware for **Raspberry Pi Pico W** (C, Pico SDK): drives a **WS2812** strip with a sliding window of lit LEDs whose position follows **accelerometer tilt** (MPU-6050 over I2C), and serves a tiny **HTTP** API to change RGB color.

## Requirements

- [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) (set `PICO_SDK_PATH` or place the SDK next to this project and point CMake at it)
- Toolchain: **arm-none-eabi-gcc** and **CMake** (e.g. Raspberry Pi OS package `gcc-arm-none-eabi`, or Homebrew `arm-none-eabi-gcc` + `cmake`)

## Configuration

Edit [`include/config.h`](include/config.h):

| Setting | Meaning |
|--------|---------|
| `WIFI_SSID` / `WIFI_PASSWORD` | Your network (or pass `-DWIFI_SSID=...` at configure time) |
| `WS2812_PIN` | Data line to the strip (default `2`) |
| `NUM_LEDS` | Total LEDs on the strip |
| `WINDOW_SIZE` | How many LEDs are on at once |
| `I2C_SDA_PIN` / `I2C_SCL_PIN` | I2C for MPU-6050 (defaults `4` / `5`) |
| `MPU6050_ADDR` | `0x68` or `0x69` if AD0 is high |
| `TILT_AXIS` | `0` = X, `1` = Y, `2` = Z (pick the axis that matches how the board is mounted) |
| `TILT_FILTER_ALPHA` / `TILT_DEADBAND_G` | Smoothing and small-motion ignore |

## Wiring

- **Pico W → WS2812 data**: Use the GPIO set in `WS2812_PIN`. For **5 V** strips, use a **5 V logic level shifter** on the data line; tie grounds together; power the strip from an adequate 5 V supply (not from the Pico alone for long strips).
- **Pico W → MPU-6050**: 3.3 V, GND, SDA, SCL to the pins in `config.h`. Most breakouts include I2C pull-ups.

Avoid GPIOs reserved for the CYW43 wireless interface on Pico W; the defaults above are user GPIOs.

## Build

```bash
export PICO_SDK_PATH=/path/to/pico-sdk
cd pi_hour
mkdir -p build && cd build
cmake -DPICO_BOARD=pico_w ..
cmake --build . -j$(nproc)
```

Optional: override Wi-Fi at configure time:

```bash
cmake -DPICO_BOARD=pico_w -DWIFI_SSID="myssid" -DWIFI_PASSWORD="secret" ..
```

Flash `build/pi_hour.uf2` in BOOTSEL mode (drag onto the USB drive).

## HTTP API

After boot, serial USB (115200 baud) prints the DHCP **IP address**.

- **Set color** (query parameters; omit any channel to leave it unchanged):

  ```bash
  curl "http://<pico-ip>/color?r=255&g=64&b=0"
  ```

- **Status** (JSON):

  ```bash
  curl "http://<pico-ip>/status"
  ```

The server listens on **port 80** (`HTTP_SERVER_PORT` in `config.h`). Use this only on a trusted LAN; there is no TLS.

## Behavior

- A block of `WINDOW_SIZE` LEDs is drawn in the current RGB color; its **starting index** moves with filtered tilt on `TILT_AXIS` (gravity vector, roughly −1 g…+1 g on one axis).
- If the MPU-6050 is missing or I2C fails, a warning is printed over USB; the strip keeps the last valid position and HTTP still works.

## Layout

| Path | Role |
|------|------|
| `ws2812.pio` | PIO program for WS2812 timing |
| `src/ws2812_pio.c` | PIO init and `ws2812_show()` |
| `src/accel_mpu6050.c` | MPU-6050 I2C driver |
| `src/http_server.c` | lwIP TCP HTTP (`/color`, `/status`) |
| `src/main.c` | Wi-Fi, main loop, tilt → index, render |

## License

SPDX-License-Identifier: BSD-3-Clause for `pico_sdk_import.cmake` (Raspberry Pi template). Other project files: use as you like for your project.

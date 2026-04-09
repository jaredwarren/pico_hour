# pi_hour — Pico W: WS2812, MPU-6050 tilt, HTTP color API

Firmware for **Raspberry Pi Pico W** (C, Pico SDK): drives a **WS2812** strip with a sliding window of lit LEDs whose position follows **accelerometer tilt** (MPU-6050 over I2C), and serves a tiny **HTTP** API to change RGB color.

## Requirements

- [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) (set `PICO_SDK_PATH` or clone into `./pico-sdk`; see Build)
- **CMake** (`brew install cmake` on macOS)
- **ARM GCC for Cortex-M** — Apple’s `clang` / Xcode **cannot** build Pico firmware. You need the cross-compiler **`arm-none-eabi-gcc`**:
  - **macOS (Homebrew):** `brew install arm-none-eabi-gcc`  
    Then confirm: `arm-none-eabi-gcc --version`  
    If CMake still says `Compiler 'arm-none-eabi-gcc' not found`, ensure Homebrew’s `bin` is on your `PATH` (e.g. `/opt/homebrew/bin`), or pass `-DPICO_TOOLCHAIN_PATH=/opt/homebrew/opt/arm-none-eabi-gcc/bin` to CMake.
  - **Linux:** package `gcc-arm-none-eabi` (Debian/Ubuntu) or your distro equivalent.
- **GNU Make** (macOS: install Xcode Command Line Tools: `xcode-select --install`) — needed for `cmake -G "Unix Makefiles"`.

The follow-on errors (`CMAKE_C_COMPILER not set`, `CMAKE_MAKE_PROGRAM is not set`) usually disappear once `arm-none-eabi-gcc` is installed and CMake can finish the toolchain step.

## Configuration

Hardware and tuning live in [`include/config.h`](include/config.h). For **Wi-Fi**, use one of the options below instead of committing passwords in `config.h`.

### Wi-Fi credentials (not in git)

1. **Local header (recommended):** copy [`include/config.local.h.example`](include/config.local.h.example) to `include/config.local.h`, put your SSID and password there, and rebuild. [`include/config.local.h`](include/config.local.h) is in [`.gitignore`](.gitignore), so it is not committed. Works with **Docker** too (the file is on your machine inside the mounted repo).

2. **CMake / Make:** `make build WIFI_SSID=MyNet WIFI_PASSWORD='secret'` or `cmake .. -DWIFI_SSID=... -DWIFI_PASSWORD=...`. Values are stored under `build/` or `build-docker/` (also ignored).

3. **Defaults in `config.h`:** only for quick tests — easy to commit by accident.

Other settings in [`include/config.h`](include/config.h):

| Setting | Meaning |
|--------|---------|
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

**CMake alone is not enough** — you also need the [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) source tree. If CMake says `SDK location was not specified`, you have not pointed it at that tree yet.

**Option A — SDK next to the project (works with `make` without `export`):**

```bash
cd pi_hour
git clone --depth 1 https://github.com/raspberrypi/pico-sdk.git pico-sdk
cd pico-sdk && git submodule update --init && cd ..
make info    # should show a non-empty PICO_SDK_PATH
make build
```

**Option B — SDK anywhere + environment variable:**

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

### Build with Docker

**When it helps:** Same toolchain and SDK for everyone, no local `arm-none-eabi-gcc` or `./pico-sdk` on the host. Good for CI and for machines where you do not want a full embedded setup.

**Caveats:** Docker writes **`build-docker/`** (not `build/`) so its CMake cache never conflicts with a native build on the same machine (CMake stores absolute paths; `/project/...` inside the container ≠ `/Users/...` on the host). Copy **`build-docker/pi_hour.uf2`** to flash. **USB flashing** from inside the container is usually awkward; use the file on the host as usual. On Linux, `build-docker/` may be **root-owned** unless you use `docker compose run --user "$(id -u):$(id -g)"` (may require a matching user in the image) or `sudo chown` afterward.

```bash
cd pi_hour
docker compose build   # once: toolchain + SDK in the image
docker compose run --rm firmware
# or: make docker-build
```

`docker-compose.yml` runs **`docker/build.sh` from your repo mount**, not a copy inside the image, so you do not need to rebuild the image after editing that script. If you ever see CMake complaining about `/project/build` vs `/Users/.../build`, ensure `docker/build.sh` uses **`build-docker/`** and run again (you can delete `build-docker/` to force a clean configure).

The image pins **`PICO_SDK_REF`** (see `Dockerfile`); change the `ARG` and rebuild the image to move SDK versions.

Interactive shell with the same toolchain:

```bash
make docker-shell
# or: docker compose run --rm -it --entrypoint bash firmware
```

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

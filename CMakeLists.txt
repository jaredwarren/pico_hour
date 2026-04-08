cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico_w CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(pi_hour C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

set(WIFI_SSID "" CACHE STRING "Override WiFi SSID (otherwise use include/config.h)")
set(WIFI_PASSWORD "" CACHE STRING "Override WiFi password")

add_executable(pi_hour
    src/main.c
    src/ws2812_pio.c
    src/accel_mpu6050.c
    src/http_server.c
)

pico_generate_pio_header(pi_hour ${CMAKE_CURRENT_SOURCE_DIR}/ws2812.pio)

target_include_directories(pi_hour PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(NOT WIFI_SSID STREQUAL "")
    target_compile_definitions(pi_hour PRIVATE WIFI_SSID="${WIFI_SSID}")
endif()
if(NOT WIFI_PASSWORD STREQUAL "")
    target_compile_definitions(pi_hour PRIVATE WIFI_PASSWORD="${WIFI_PASSWORD}")
endif()

target_link_libraries(pi_hour PRIVATE
    pico_stdlib
    hardware_pio
    hardware_i2c
    pico_cyw43_arch_lwip_poll
    pico_sync
)

pico_enable_stdio_usb(pi_hour 1)
pico_enable_stdio_uart(pi_hour 0)

pico_add_extra_outputs(pi_hour)

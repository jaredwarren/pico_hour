# pi_hour — Pico W firmware (CMake wrapper)
# Requires: cmake, arm-none-eabi-gcc, and PICO_SDK_PATH (or pico-sdk next to this file via pico_sdk_import.cmake fetch).

BUILD_DIR      ?= build
NPROC          ?= $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
CMAKE          ?= cmake
FLASH_VOLUME   ?= /Volumes/RPI-RP2

# Pico SDK: CMake needs PICO_SDK_PATH (not the same as installing CMake).
# Options:
#   1) Clone SDK into ./pico-sdk (see "make help") — detected automatically.
#   2) export PICO_SDK_PATH=/path/to/pico-sdk
#   3) make build PICO_SDK_PATH=/path/to/pico-sdk
#   4) make build PICO_SDK_FETCH_FROM_GIT=1  (passes -DPICO_SDK_FETCH_FROM_GIT=ON to CMake)

CMAKE_ARGS ?=

# If ./pico-sdk exists and looks like the real SDK, use it (no export needed).
# CURDIR is the directory make was invoked from (use "cd pi_hour && make", not make -f elsewhere without -C).
LOCAL_PICO_SDK := $(CURDIR)/pico-sdk
ifeq ($(strip $(PICO_SDK_PATH)),)
  ifneq ($(wildcard $(LOCAL_PICO_SDK)/pico_sdk_init.cmake),)
    PICO_SDK_PATH := $(LOCAL_PICO_SDK)
  endif
endif

ifneq ($(strip $(PICO_SDK_PATH)),)
  CMAKE_ARGS += -DPICO_SDK_PATH=$(PICO_SDK_PATH)
endif
ifneq ($(strip $(PICO_SDK_FETCH_FROM_GIT)),)
  CMAKE_ARGS += -DPICO_SDK_FETCH_FROM_GIT=$(PICO_SDK_FETCH_FROM_GIT)
endif
ifneq ($(strip $(WIFI_SSID)),)
  CMAKE_ARGS += -DWIFI_SSID=$(WIFI_SSID)
endif
ifneq ($(strip $(WIFI_PASSWORD)),)
  CMAKE_ARGS += -DWIFI_PASSWORD=$(WIFI_PASSWORD)
endif
ifneq ($(strip $(PICO_TOOLCHAIN_PATH)),)
  CMAKE_ARGS += -DPICO_TOOLCHAIN_PATH=$(PICO_TOOLCHAIN_PATH)
endif

UF2 := $(BUILD_DIR)/pi_hour.uf2
ELF := $(BUILD_DIR)/pi_hour.elf

.PHONY: help all build configure compile clean rebuild distclean flash info compile_commands docker-build docker-shell

.DEFAULT_GOAL := build

help:
	@echo "pi_hour — useful targets (default: make = make build)"
	@echo ""
	@echo "  make build              Configure (if needed) and compile firmware"
	@echo "  make configure          Run CMake only (creates $(BUILD_DIR)/)"
	@echo "  make compile            Incremental build (CMake must have run already)"
	@echo "  make clean              Remove $(BUILD_DIR)/"
	@echo "  make rebuild            clean + build"
	@echo "  make flash              Copy $(UF2) to $(FLASH_VOLUME) (BOOTSEL USB drive)"
	@echo "  make info               Show SDK path and whether arm-none-eabi-gcc is on PATH"
	@echo "  make compile_commands   Regenerate compile_commands.json in $(BUILD_DIR)/"
	@echo "  make docker-build       Build firmware inside Docker (see README)"
	@echo "  make docker-shell       Interactive shell in the firmware build image"
	@echo ""
	@echo "Variables (optional):"
	@echo "  PICO_SDK_PATH=...   Path to Raspberry Pi Pico SDK (required unless ./pico-sdk exists)"
	@echo "  PICO_SDK_FETCH_FROM_GIT=1  Let CMake download the SDK (needs network)"
	@echo "  PICO_TOOLCHAIN_PATH=...  Directory containing arm-none-eabi-gcc (if CMake cannot find it)"
	@echo "  WIFI_SSID=... WIFI_PASSWORD=...   Override Wi-Fi (first configure only unless you reconfigure)"
	@echo "  BUILD_DIR=...       Build directory (default: build)"
	@echo "  NPROC=$(NPROC)      Parallel compile jobs"
	@echo "  FLASH_VOLUME=...  BOOTSEL mount (default: /Volumes/RPI-RP2; Linux e.g. /media/\$$USER/RPI-RP2)"

all: build

info:
	@echo "PICO_SDK_PATH for this Makefile (empty = configure will fail): $(PICO_SDK_PATH)"
	@if [ -n "$$PICO_SDK_PATH" ]; then echo "Shell env PICO_SDK_PATH=$$PICO_SDK_PATH"; fi
	@if [ -f "$(CURDIR)/pico-sdk/pico_sdk_init.cmake" ]; then echo "Found $(CURDIR)/pico-sdk (auto-used if PICO_SDK_PATH not passed to make)"; else echo "No $(CURDIR)/pico-sdk — clone Raspberry Pi pico-sdk there or set PICO_SDK_PATH"; fi
	@command -v arm-none-eabi-gcc >/dev/null 2>&1 && arm-none-eabi-gcc --version | head -1 || echo "MISSING: arm-none-eabi-gcc (install: brew install arm-none-eabi-gcc)"
	@command -v make >/dev/null 2>&1 && echo "make: $$(command -v make)" || echo "MISSING: make (macOS: xcode-select --install)"

$(BUILD_DIR)/Makefile:
	@mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && $(CMAKE) -G "Unix Makefiles" $(CMAKE_ARGS) ..

configure: $(BUILD_DIR)/Makefile

compile: $(BUILD_DIR)/Makefile
	$(CMAKE) --build $(BUILD_DIR) --parallel $(NPROC)

build: $(UF2)

$(UF2): $(BUILD_DIR)/Makefile
	$(CMAKE) --build $(BUILD_DIR) --parallel $(NPROC)

clean:
	rm -rf $(BUILD_DIR)

distclean: clean

rebuild: clean build

flash: $(UF2)
	@if [ ! -d "$(FLASH_VOLUME)" ]; then \
		echo "Error: $(FLASH_VOLUME) not found. Put Pico W in BOOTSEL mode (hold BOOTSEL, plug in USB)."; \
		exit 1; \
	fi
	cp $(UF2) $(FLASH_VOLUME)/
	@echo "Copied $(UF2) → $(FLASH_VOLUME)/"

compile_commands: $(BUILD_DIR)/Makefile
	cd $(BUILD_DIR) && $(CMAKE) -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ..
	ln -sf "$(CURDIR)/$(BUILD_DIR)/compile_commands.json" "$(CURDIR)/compile_commands.json" 2>/dev/null || true
	@echo "Wrote $(BUILD_DIR)/compile_commands.json (repo-root symlink if ln succeeded)"

docker-build:
	docker compose build
	docker compose run --rm firmware

docker-shell:
	docker compose run --rm -it --entrypoint bash firmware

# pi_hour — Pico W firmware (CMake wrapper)
# Requires: cmake, arm-none-eabi-gcc, and PICO_SDK_PATH (or pico-sdk next to this file via pico_sdk_import.cmake fetch).

BUILD_DIR      ?= build
NPROC          ?= $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
CMAKE          ?= cmake
FLASH_VOLUME   ?= /Volumes/RPI-RP2

# Optional: export PICO_SDK_PATH=/path/to/pico-sdk
# Or: make build PICO_SDK_PATH=/path/to/pico-sdk
CMAKE_ARGS ?=

ifneq ($(strip $(PICO_SDK_PATH)),)
  CMAKE_ARGS += -DPICO_SDK_PATH=$(PICO_SDK_PATH)
endif
ifneq ($(strip $(WIFI_SSID)),)
  CMAKE_ARGS += -DWIFI_SSID=$(WIFI_SSID)
endif
ifneq ($(strip $(WIFI_PASSWORD)),)
  CMAKE_ARGS += -DWIFI_PASSWORD=$(WIFI_PASSWORD)
endif

UF2 := $(BUILD_DIR)/pi_hour.uf2
ELF := $(BUILD_DIR)/pi_hour.elf

.PHONY: help all build configure compile clean rebuild distclean flash info compile_commands

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
	@echo "  make info               Show whether PICO_SDK_PATH is set"
	@echo "  make compile_commands   Regenerate compile_commands.json in $(BUILD_DIR)/"
	@echo ""
	@echo "Variables (optional):"
	@echo "  PICO_SDK_PATH=...   Path to Raspberry Pi Pico SDK"
	@echo "  WIFI_SSID=... WIFI_PASSWORD=...   Override Wi-Fi (first configure only unless you reconfigure)"
	@echo "  BUILD_DIR=...       Build directory (default: build)"
	@echo "  NPROC=$(NPROC)      Parallel compile jobs"
	@echo "  FLASH_VOLUME=...  BOOTSEL mount (default: /Volumes/RPI-RP2; Linux e.g. /media/\$$USER/RPI-RP2)"

all: build

info:
	@if [ -n "$$PICO_SDK_PATH" ]; then echo "PICO_SDK_PATH=$$PICO_SDK_PATH"; else echo "PICO_SDK_PATH is unset (set in environment or pass make PICO_SDK_PATH=...)"; fi

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

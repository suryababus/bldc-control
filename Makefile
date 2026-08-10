# Wiper BLDC (TLE9879-2QXA40) — PlatformIO + J-Link
#
#   make ENV=hold flash   Phase B: continuous one-sector hold
#   make ENV=hall flash   Phase C: Hall-locked BC (default)
#   make ENV=spin flash   Legacy open-loop
#   make clean | size | rebuild
#
#   make ENV=debug flash

ROOT   := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
FW_DIR := $(ROOT)/firmware
ENV    ?= hall

PIO   := $(shell command -v pio 2>/dev/null || echo $(HOME)/Library/Python/3.9/bin/pio)
JLINK := $(shell command -v JLinkExe 2>/dev/null || echo /usr/local/bin/JLinkExe)

BUILD_DIR    := $(FW_DIR)/.pio/build/$(ENV)
ELF          := $(BUILD_DIR)/firmware.elf
HEX          := $(BUILD_DIR)/firmware.hex
BIN          := $(BUILD_DIR)/firmware.bin
FLASH_SCRIPT := $(FW_DIR)/scripts/flash.jlink

# SWD speed: lower is more reliable on TLE987x during flash
JLINK_SPEED  ?= 400

export PATH := $(HOME)/Library/Python/3.9/bin:/usr/local/bin:$(PATH)

.PHONY: all build flash upload flash-bin clean size rebuild help check

all: build

help:
	@echo "Targets: build | flash | flash-bin | clean | size | rebuild | help"
	@echo "ENV=$(ENV)  JLINK_SPEED=$(JLINK_SPEED)"

check:
	@command -v pio >/dev/null 2>&1 || test -x "$(PIO)" || \
		(echo "pio not found — install PlatformIO or add ~/Library/Python/3.9/bin to PATH"; exit 1)

build: check
	cd $(FW_DIR) && $(PIO) run -e $(ENV)

# Default flash = hex path (proven reliable on this J-Link V9.66 + TLE9879)
flash upload: flash-hex

# Robust flash for TLE9879 — keep CPU halted after program (old FW trips 10A PSU on run)
flash-hex: build
	@command -v JLinkExe >/dev/null || (echo "JLinkExe not found on PATH"; exit 1)
	@test -f "$(HEX)" || (echo "missing $(HEX) — build first"; exit 1)
	@printf '%s\n' \
		'ExitOnError 0' \
		'halt' \
		'w4 0xE000E010, 0x00000000' \
		'w4 0xE000E180, 0xFFFFFFFF' \
		'w4 0xE000E184, 0xFFFFFFFF' \
		'w2 0x180017F0, 0xE7FE' \
		'SetPC 0x180017F1' \
		'halt' \
		'sleep 200' \
		'loadfile $(HEX)' \
		'halt' \
		'exit' > $(FLASH_SCRIPT)
	@ok=0; for i in 1 2 3 4 5 6 7 8; do \
	  echo "flash attempt $$i..."; \
	  if $(JLINK) -device TLE9879 -if SWD -speed $(JLINK_SPEED) -autoconnect 1 -NoGui 1 \
	      -CommanderScript $(FLASH_SCRIPT) 2>&1 | tee /tmp/motor-drive-flash.log | \
	      grep -q "Flash download: Bank"; then ok=1; break; fi; \
	  sleep 0.3; \
	done; \
	if [ $$ok -ne 1 ]; then echo "flash failed after retries — see /tmp/motor-drive-flash.log"; exit 1; fi; \
	echo "flash OK"; \
	bash $(FW_DIR)/scripts/start_main.sh $(ELF) $(JLINK_SPEED)

# Alternate: erase + loadbin + verify
flash-bin: build
	@command -v JLinkExe >/dev/null || (echo "JLinkExe not found on PATH"; exit 1)
	@test -f "$(BIN)" || (echo "missing $(BIN) — build first"; exit 1)
	@printf '%s\n' \
		'ExitOnError 1' \
		'halt' \
		'sleep 50' \
		'erase' \
		'sleep 100' \
		'loadbin $(BIN), 0x11000000' \
		'verifybin $(BIN), 0x11000000' \
		'r' \
		'g' \
		'exit' > $(FLASH_SCRIPT)
	$(JLINK) -device TLE9879 -if SWD -speed $(JLINK_SPEED) -autoconnect 1 -NoGui 1 \
		-CommanderScript $(FLASH_SCRIPT)

size: $(ELF)
	@SIZE_BIN=$$(command -v arm-none-eabi-size 2>/dev/null || \
		echo $(HOME)/.platformio/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-size); \
	$$SIZE_BIN -B $(ELF); \
	$$SIZE_BIN -A $(ELF) | head -16

clean: check
	cd $(FW_DIR) && $(PIO) run -e $(ENV) -t clean
	rm -f $(FLASH_SCRIPT)

rebuild: clean build

$(ELF):
	@$(MAKE) build

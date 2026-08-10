# TLE9879-2QXA40 hello world — ARM GCC + Segger J-Link
TARGET   := hello_world
BUILD    := build

PREFIX   ?= arm-none-eabi-
CC       := $(PREFIX)gcc
OBJCOPY  := $(PREFIX)objcopy
SIZE     := $(PREFIX)size

# J-Link commander (Homebrew cask / SEGGER installer)
JLINK_DEVICE ?= TLE9879-2QXA40
JLINK ?= $(shell \
  command -v JLinkExe 2>/dev/null || \
  ls /Applications/SEGGER/JLink*/JLinkExe 2>/dev/null | head -1 || \
  ls /usr/local/bin/JLinkExe 2>/dev/null | head -1)

SRCS := \
  src/main.c \
  src/system_tle9879.c \
  startup/startup_tle9879.S

OBJS := $(SRCS:%=$(BUILD)/%.o)
DEPS := $(OBJS:.o=.d)

LDSCRIPT := ld/tle9879-2qxa40.ld

CFLAGS := \
  -mcpu=cortex-m3 -mthumb \
  -std=c99 -Wall -Wextra -Werror \
  -ffreestanding -fno-builtin \
  -ffunction-sections -fdata-sections \
  -fno-common \
  -Os -g3 \
  -DTLE9879_2QXA40

LDFLAGS := \
  -T$(LDSCRIPT) \
  -Wl,--gc-sections \
  -Wl,-Map=$(BUILD)/$(TARGET).map \
  -nostartfiles -nostdlib

.PHONY: all clean flash size

all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).hex $(BUILD)/$(TARGET).bin size

$(BUILD)/$(TARGET).elf: $(OBJS) $(LDSCRIPT)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(OBJS) $(LDFLAGS) -o $@

$(BUILD)/$(TARGET).hex: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/%.S.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

size: $(BUILD)/$(TARGET).elf
	$(SIZE) --format=berkeley $<

# Flash via onboard/external Segger J-Link (SWD). Board must be powered.
flash: $(BUILD)/$(TARGET).hex
	$(JLINK) -autoconnect 1 -device $(JLINK_DEVICE) -if SWD -speed 4000 -CommanderScript scripts/flash.jlink

clean:
	rm -rf $(BUILD)

-include $(DEPS)

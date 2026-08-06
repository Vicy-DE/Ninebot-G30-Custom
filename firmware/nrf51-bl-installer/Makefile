# nRF51 bootloader installer — app slot @0x18000, installs the BL at 0x3C000.
#
#   make                                  build/nrf51_bl_installer.bin
#   make pack BL=../../bootloader/nrf51/build/nrf51_bootloader.bin
#                                         -> build/packed.bin (installer + BL + trailer)
#   make flash-packed                     program packed.bin into the app slot over SWD

CROSS   ?= arm-none-eabi-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
SIZE    := $(CROSS)size
PY      ?= python

BUILD  := build
TARGET := $(BUILD)/nrf51_bl_installer
LD     := nrf51822_installer.ld
BL     ?= ../../bootloader/nrf51/build/nrf51_bootloader.bin

ARCH    := -mcpu=cortex-m0 -mthumb
CFLAGS  := $(ARCH) -Os -g3 -Wall -Wextra -Werror -std=c11 \
           -ffunction-sections -fdata-sections -fno-builtin
LDFLAGS := $(ARCH) -T$(LD) -Wl,--gc-sections -Wl,-Map=$(TARGET).map \
           -nostdlib --specs=nosys.specs

.PHONY: all clean size pack flash-packed
all: $(TARGET).bin size

$(BUILD):
	@mkdir -p $(BUILD)

$(TARGET).elf: src/main.c src/startup.s $(LD) | $(BUILD)
	$(CC) $(CFLAGS) -c src/main.c -o $(BUILD)/main.o
	$(CC) $(ARCH) -x assembler-with-cpp -c src/startup.s -o $(BUILD)/startup.o
	$(CC) $(BUILD)/main.o $(BUILD)/startup.o $(LDFLAGS) -lgcc -o $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

size: $(TARGET).elf
	@$(SIZE) $<

pack: $(TARGET).bin
	$(PY) pack.py --installer $(TARGET).bin --bootloader $(BL) --out $(BUILD)/packed.bin

flash-packed: pack
	$(PY) ../../tools/nrf51/nrf51_swd.py flash $(BUILD)/packed.bin --address 0x18000

clean:
	rm -rf $(BUILD)

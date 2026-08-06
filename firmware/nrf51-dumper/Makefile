# nRF51 flash dumper — app slot @0x18000. Read-only; cannot brick.
#
#   make          -> build/nrf51_dumper.bin
#   make flash    -> program over SWD (overwrites the stock app — back up first!)

CROSS   ?= arm-none-eabi-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
SIZE    := $(CROSS)size

BUILD  := build
TARGET := $(BUILD)/nrf51_dumper
LD     := nrf51822_app.ld

ARCH     := -mcpu=cortex-m0 -mthumb
CFLAGS   := $(ARCH) -Os -g3 -Wall -Wextra -Werror -std=c11 \
            -ffunction-sections -fdata-sections -fno-builtin
LDFLAGS  := $(ARCH) -T$(LD) -Wl,--gc-sections -Wl,-Map=$(TARGET).map \
            -nostdlib --specs=nosys.specs

.PHONY: all clean flash size
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

flash: $(TARGET).bin
	python ../../tools/nrf51/nrf51_swd.py flash $(TARGET).bin --address 0x18000

clean:
	rm -rf $(BUILD)

# Stock-bootloader dumper — STM32F103C8, app @ 0x08001000 (flash via stock IAP)
#
#   make            # -> build/bldump.{elf,bin,hex}
#   make clean
# Reuses the dashboard firmware's startup, linker, and register header.

PREFIX  = arm-none-eabi-
CC      = $(PREFIX)gcc
CXX     = $(PREFIX)g++
OBJCOPY = $(PREFIX)objcopy
SIZE    = $(PREFIX)size

OUTPUT    = bldump
BUILD_DIR = build
DASH      = ../dashboard

INCLUDES = -I$(DASH)/include
CPU = -mcpu=cortex-m3 -mthumb

CXXFLAGS = $(CPU) -Os -ffunction-sections -fdata-sections -ffreestanding -g \
           -Wall -Wextra -std=c++17 -fno-exceptions -fno-rtti \
           -fno-threadsafe-statics -fno-use-cxa-atexit -fno-unwind-tables $(INCLUDES)
ASFLAGS  = $(CPU)

LDSCRIPT = $(DASH)/stm32f103c8_app.ld
LDFLAGS  = $(CPU) -T$(LDSCRIPT) -nostartfiles -Wl,--gc-sections \
           --specs=nano.specs --specs=nosys.specs -Wl,-Map=$(BUILD_DIR)/$(OUTPUT).map

OBJ = $(BUILD_DIR)/main.o $(BUILD_DIR)/startup_stm32f103.o

.PHONY: all clean size
all: $(BUILD_DIR)/$(OUTPUT).bin $(BUILD_DIR)/$(OUTPUT).hex size

$(BUILD_DIR)/$(OUTPUT).elf: $(OBJ)
	$(CXX) $(LDFLAGS) -o $@ $^

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf
	$(OBJCOPY) -O binary $< $@
$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD_DIR)/main.o: src/main.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<
$(BUILD_DIR)/startup_stm32f103.o: $(DASH)/src/startup_stm32f103.s | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c -o $@ $<

$(BUILD_DIR):
	mkdir -p $@

size: $(BUILD_DIR)/$(OUTPUT).elf
	@echo "-- bootloader-dumper size --"
	$(SIZE) $<

clean:
	rm -rf $(BUILD_DIR)

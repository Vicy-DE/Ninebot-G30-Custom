# Bootloader installer for the 16/32-offset test chain (STM32F103C8).
#
#   make           # -> build/installer.{elf,bin}
#   make clean
#
# The installer runs at 0x08008000 (32 offset) and writes the bootloader to
# 0x08004000 (16 offset). It embeds the relocated bootloader build
# (BL_BASE=0x08004000) and reuses the migration sources unchanged; updater.cpp is
# compiled with -DMIG_BL_BASE=0x08004000u so its write target is the 16 offset.

PREFIX  = arm-none-eabi-
CC      = $(PREFIX)gcc
CXX     = $(PREFIX)g++
OBJCOPY = $(PREFIX)objcopy
SIZE    = $(PREFIX)size

B       = build
MIG     = ../migration/src
BL16DIR = ../../bootloader/stm32/build/ble_at_0x08004000
BL16    = $(BL16DIR)/bootloader_ble_stm32_at_0x08004000.bin

CPU     = -mcpu=cortex-m3 -mthumb
INC     = -I../migration/include -I../../bootloader/stm32/include -I../../bootloader/common/include
COMMON  = $(CPU) -Os -ffunction-sections -fdata-sections -ffreestanding -g -Wall -Wextra $(INC)
CXXFLAGS = $(COMMON) -std=c++17 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit
CFLAGS   = $(COMMON) -std=c11
ASFLAGS  = $(CPU)
LDF      = $(CPU) -nostartfiles -Wl,--gc-sections --specs=nano.specs --specs=nosys.specs

# updater.cpp writes its embedded image to MIG_BL_BASE — point that at the 16 offset.
INSTALLER_DEF = -DMIG_BL_BASE=0x08004000u

.PHONY: all clean size
all: $(B)/installer.bin size

# relocated bootloader (16 offset) is the image we install
$(BL16):
	$(MAKE) -C ../../bootloader/stm32 TARGET=ble BL_BASE=0x08004000
$(B)/newbl.bin: $(BL16) | $(B)
	cp $(BL16) $@

$(B)/startup.o: $(MIG)/startup.s | $(B)
	$(CC) $(ASFLAGS) -c -o $@ $<
$(B)/flash_rt.o: $(MIG)/flash_rt.c | $(B)
	$(CC) $(CFLAGS) -c -o $@ $<
$(B)/crc32.o: ../../bootloader/common/src/crc32.c | $(B)
	$(CC) $(CFLAGS) -c -o $@ $<
$(B)/newbl.o: $(MIG)/newbl.s $(B)/newbl.bin | $(B)
	$(CC) $(ASFLAGS) -Ibuild -c -o $@ $<
$(B)/installer.o: $(MIG)/updater.cpp | $(B)
	$(CXX) $(CXXFLAGS) $(INSTALLER_DEF) -c -o $@ $<

$(B)/installer.elf: $(B)/startup.o $(B)/installer.o $(B)/flash_rt.o $(B)/crc32.o $(B)/newbl.o installer.ld
	$(CXX) $(LDF) -Tinstaller.ld -Wl,-Map=$(B)/installer.map -o $@ \
	    $(B)/startup.o $(B)/installer.o $(B)/flash_rt.o $(B)/crc32.o $(B)/newbl.o -lgcc
$(B)/installer.bin: $(B)/installer.elf
	$(OBJCOPY) -O binary $< $@

$(B):
	mkdir -p $(B)

size: $(B)/installer.elf
	@echo "-- installer (runs @0x08008000, writes BL -> 0x08004000) --"
	$(SIZE) $(B)/installer.elf

clean:
	rm -rf $(B)

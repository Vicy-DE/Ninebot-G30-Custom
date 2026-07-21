# Bootloader-migration apps (old 4 KB layout -> new 16 KB layout), STM32F103C8.
#
#   make            # -> build/{trampoline,updater,migrate_big}.bin
#   make clean
#
# Produces: trampoline.bin (@0x08001000), updater.bin (@0x08004000), and the
# packed migrate_big.bin to flash via the stock IAP at 0x08001000.

PREFIX  = arm-none-eabi-
CC      = $(PREFIX)gcc
CXX     = $(PREFIX)g++
OBJCOPY = $(PREFIX)objcopy
SIZE    = $(PREFIX)size
PY     ?= ../../.venv/Scripts/python.exe

B       = build
BL_BIN  = ../../bootloader/stm32/build/ble/bootloader_ble_stm32.bin
CPU     = -mcpu=cortex-m3 -mthumb
INC     = -Iinclude -I../../bootloader/stm32/include -I../../bootloader/common/include
COMMON  = $(CPU) -Os -ffunction-sections -fdata-sections -ffreestanding -g -Wall -Wextra $(INC)
CXXFLAGS = $(COMMON) -std=c++17 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit
CFLAGS   = $(COMMON) -std=c11
ASFLAGS  = $(CPU)
LDF      = $(CPU) -nostartfiles -Wl,--gc-sections --specs=nano.specs --specs=nosys.specs

.PHONY: all clean size
all: $(B)/migrate_big.bin size

# ── new bootloader to embed ──────────────────────────────────────────────────
$(BL_BIN):
	$(MAKE) -C ../../bootloader/stm32 TARGET=ble
$(B)/newbl.bin: $(BL_BIN) | $(B)
	cp $(BL_BIN) $@

# ── shared objects ───────────────────────────────────────────────────────────
$(B)/startup.o: src/startup.s | $(B)
	$(CC) $(ASFLAGS) -c -o $@ $<
$(B)/flash_rt.o: src/flash_rt.c | $(B)
	$(CC) $(CFLAGS) -c -o $@ $<
$(B)/crc32.o: ../../bootloader/common/src/crc32.c | $(B)
	$(CC) $(CFLAGS) -c -o $@ $<
$(B)/newbl.o: src/newbl.s $(B)/newbl.bin | $(B)
	$(CC) $(ASFLAGS) -Ibuild -c -o $@ $<     # newbl.s does .incbin "newbl.bin"

# ── trampoline (App1) ────────────────────────────────────────────────────────
$(B)/trampoline.o: src/trampoline.cpp | $(B)
	$(CXX) $(CXXFLAGS) -c -o $@ $<
$(B)/trampoline.elf: $(B)/startup.o $(B)/trampoline.o trampoline.ld
	$(CXX) $(LDF) -Ttrampoline.ld -Wl,-Map=$(B)/trampoline.map -o $@ $(B)/startup.o $(B)/trampoline.o -lgcc
$(B)/trampoline.bin: $(B)/trampoline.elf
	$(OBJCOPY) -O binary $< $@

# ── updater (App2) ───────────────────────────────────────────────────────────
$(B)/updater.o: src/updater.cpp | $(B)
	$(CXX) $(CXXFLAGS) -c -o $@ $<
$(B)/updater.elf: $(B)/startup.o $(B)/updater.o $(B)/flash_rt.o $(B)/crc32.o $(B)/newbl.o updater.ld
	$(CXX) $(LDF) -Tupdater.ld -Wl,-Map=$(B)/updater.map -o $@ \
	    $(B)/startup.o $(B)/updater.o $(B)/flash_rt.o $(B)/crc32.o $(B)/newbl.o -lgcc
$(B)/updater.bin: $(B)/updater.elf
	$(OBJCOPY) -O binary $< $@

# ── packed big app ───────────────────────────────────────────────────────────
$(B)/migrate_big.bin: $(B)/trampoline.bin $(B)/updater.bin
	$(PY) pack.py $(B)/trampoline.bin $(B)/updater.bin $@

$(B):
	mkdir -p $(B)

size: $(B)/trampoline.elf $(B)/updater.elf
	@echo "-- migration app sizes --"
	$(SIZE) $(B)/trampoline.elf $(B)/updater.elf

clean:
	rm -rf $(B)

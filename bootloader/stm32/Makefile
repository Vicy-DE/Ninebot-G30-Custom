# STM32F103 Secure Bootloader — Makefile
#
# Build for BLE Dashboard:  make TARGET=ble
# Build for BMS:            make TARGET=bms
# Clean:                    make clean
#
# Test-before-overwrite build (links the bootloader into the app slot at +16 KB so
# the installed bootloader can launch and exercise it WITHOUT erasing 0x08000000):
#                           make TARGET=ble BL_BASE=0x08004000
#   -> output build/ble/bootloader_ble_stm32_at_0x08004000.{elf,bin,hex}
#
# Requires: arm-none-eabi-gcc toolchain

# ── Toolchain ──────────────────────────────────────────────────────────────

PREFIX  = arm-none-eabi-
CC      = $(PREFIX)gcc
AS      = $(PREFIX)gcc
LD      = $(PREFIX)gcc
OBJCOPY = $(PREFIX)objcopy
SIZE    = $(PREFIX)size

# ── Target selection ───────────────────────────────────────────────────────

TARGET ?= ble

ifeq ($(TARGET),ble)
    BOARD_DEF  = -DTARGET_BOARD=1
    OUTPUT     = bootloader_ble_stm32
else ifeq ($(TARGET),bms)
    BOARD_DEF  = -DTARGET_BOARD=2
    OUTPUT     = bootloader_bms_stm32
else
    $(error Unknown TARGET=$(TARGET). Use 'ble' or 'bms')
endif

# ── Directories ────────────────────────────────────────────────────────────

COMMON_DIR  = ../common
STM32_DIR   = .
BUILD_DIR   = build/$(TARGET)

# ── Bootloader link base (relocation for test-before-overwrite) ────────────
# Default 0x08000000 is the real, final location. Override with e.g.
# `make TARGET=ble BL_BASE=0x08004000` to link the bootloader into the app slot.
BL_BASE ?= 0x08000000

ifeq ($(BL_BASE),0x08000000)
    # Default: use the static linker script (verified path; also used by CMake).
    LDSCRIPT = $(STM32_DIR)/stm32f103c8_bootloader.ld
else
    # Relocated: separate object dir (so the new -DBL_BASE_ADDR actually recompiles,
    # not reuses the default build's objects), generate a linker script from the
    # template, and tell the C code its new base so the vector table (VTOR) and the
    # app/flash bounds all follow.
    BUILD_DIR := $(BUILD_DIR)_at_$(BL_BASE)
    LDSCRIPT  = $(BUILD_DIR)/bootloader_at_$(BL_BASE).ld
    BOARD_DEF += -DBL_BASE_ADDR=$(BL_BASE)U
    OUTPUT   := $(OUTPUT)_at_$(BL_BASE)
endif

# ── Source files ───────────────────────────────────────────────────────────

C_SOURCES = \
    $(STM32_DIR)/src/bootloader_main.c \
    $(STM32_DIR)/src/stm32_flash.c \
    $(STM32_DIR)/src/stm32_uart.c \
    $(COMMON_DIR)/src/sha256.c \
    $(COMMON_DIR)/src/ecdsa.c \
    $(COMMON_DIR)/src/nbu.c \
    $(COMMON_DIR)/src/fw_header.c \
    $(COMMON_DIR)/src/crc32.c \
    $(COMMON_DIR)/src/libc_min.c

ASM_SOURCES = \
    $(STM32_DIR)/src/startup.s

# ── Include paths ──────────────────────────────────────────────────────────

INCLUDES = \
    -I$(STM32_DIR)/include \
    -I$(COMMON_DIR)/include

# ── Compiler flags ─────────────────────────────────────────────────────────

CPU    = -mcpu=cortex-m3
FPU    =
FLOAT  =

CFLAGS = $(CPU) -mthumb $(FPU) $(FLOAT)
CFLAGS += -Wall -Wextra -Werror
CFLAGS += -Os -ffunction-sections -fdata-sections
CFLAGS += -ffreestanding -nostdlib -fno-tree-loop-distribute-patterns
CFLAGS += $(BOARD_DEF)
CFLAGS += $(INCLUDES)
CFLAGS += -std=c11

ASFLAGS = $(CPU) -mthumb $(FPU) $(FLOAT)
ASFLAGS += -Wall

# ── Linker flags ───────────────────────────────────────────────────────────
# LDSCRIPT is selected above (static for the default base, generated when relocated).

LDFLAGS  = $(CPU) -mthumb $(FPU) $(FLOAT)
LDFLAGS += -T$(LDSCRIPT)
LDFLAGS += -Wl,--gc-sections
LDFLAGS += -nostdlib -nostartfiles
LDFLAGS += -Wl,-Map=$(BUILD_DIR)/$(OUTPUT).map

# Minimal libc for memcpy/memset
LDFLAGS += -lgcc

# ── Object files ───────────────────────────────────────────────────────────

C_OBJECTS   = $(addprefix $(BUILD_DIR)/, $(notdir $(C_SOURCES:.c=.o)))
ASM_OBJECTS = $(addprefix $(BUILD_DIR)/, $(notdir $(ASM_SOURCES:.s=.o)))
OBJECTS     = $(C_OBJECTS) $(ASM_OBJECTS)

vpath %.c $(sort $(dir $(C_SOURCES)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

# ── Build targets ──────────────────────────────────────────────────────────

.PHONY: all clean size

all: $(BUILD_DIR)/$(OUTPUT).bin $(BUILD_DIR)/$(OUTPUT).hex size

$(BUILD_DIR)/$(OUTPUT).elf: $(OBJECTS) $(LDSCRIPT)
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)

# Generate a relocated linker script from the template (test-before-overwrite builds).
$(BUILD_DIR)/bootloader_at_$(BL_BASE).ld: $(STM32_DIR)/stm32f103c8_bootloader.ld.in | $(BUILD_DIR)
	$(CC) -E -P -x c -DBL_ORIGIN=$(BL_BASE) $< -o $@

$(BUILD_DIR)/$(OUTPUT).bin: $(BUILD_DIR)/$(OUTPUT).elf
	$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/$(OUTPUT).hex: $(BUILD_DIR)/$(OUTPUT).elf
	$(OBJCOPY) -O ihex $< $@

# ── Compile rules ──────────────────────────────────────────────────────────

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: %.s | $(BUILD_DIR)
	$(AS) $(ASFLAGS) -c -o $@ $<

$(BUILD_DIR):
	mkdir -p $@

# ── Size report ────────────────────────────────────────────────────────────

size: $(BUILD_DIR)/$(OUTPUT).elf
	@echo "── Bootloader size report ($(TARGET)) ──"
	$(SIZE) $<
	@echo "── Max: 16384 bytes (16 KB) ──"

# ── Clean ──────────────────────────────────────────────────────────────────

clean:
	rm -rf build/

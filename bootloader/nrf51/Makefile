# ─────────────────────────────────────────────────────────────────────────────
#  Makefile — nRF51822 Secure Bootloader
# ─────────────────────────────────────────────────────────────────────────────
#
#  Usage:
#    make              Build bootloader
#    make clean        Remove build artifacts
#    make flash        Flash via J-Link / ST-Link (OpenOCD)
#    make size         Show section sizes
#
#  Prerequisites:
#    arm-none-eabi-gcc (ARM GNU Toolchain)
#
# ─────────────────────────────────────────────────────────────────────────────

# ── Toolchain ─────────────────────────────────────────────────────────────
PREFIX  ?= arm-none-eabi-
CC       = $(PREFIX)gcc
AS       = $(PREFIX)gcc
LD       = $(PREFIX)gcc
OBJCOPY  = $(PREFIX)objcopy
SIZE     = $(PREFIX)size

# ── Directories ───────────────────────────────────────────────────────────
COMMON_DIR  = ../common
NRF_DIR     = .
SRC_DIR     = $(NRF_DIR)/src
INC_DIR     = $(NRF_DIR)/include
BUILD_DIR   = build

# ── Sources ───────────────────────────────────────────────────────────────
C_SOURCES = \
    $(SRC_DIR)/bootloader_main.c \
    $(SRC_DIR)/nrf51_flash.c \
    $(SRC_DIR)/nrf51_selfupdate.c \
    $(SRC_DIR)/nrf51_uart.c \
    $(COMMON_DIR)/src/sha256.c \
    $(COMMON_DIR)/src/ecdsa.c \
    $(COMMON_DIR)/src/nbu.c \
    $(COMMON_DIR)/src/crc32.c \
    $(COMMON_DIR)/src/fw_header.c \
    $(COMMON_DIR)/src/libc_min.c

ASM_SOURCES = \
    $(SRC_DIR)/startup.s

# ── Linker script ─────────────────────────────────────────────────────────
LDSCRIPT = $(NRF_DIR)/nrf51822_bootloader.ld

# ── Build flags ───────────────────────────────────────────────────────────
CPU     = -mcpu=cortex-m0 -mthumb
OPT     = -Os

CFLAGS  = $(CPU) $(OPT)
CFLAGS += -Wall -Wextra -Werror
CFLAGS += -std=c11
CFLAGS += -ffunction-sections -fdata-sections
CFLAGS += -fno-builtin -fno-exceptions
CFLAGS += -I$(INC_DIR)
CFLAGS += -I$(COMMON_DIR)/include

ASFLAGS = $(CPU) -x assembler-with-cpp

LDFLAGS  = $(CPU)
LDFLAGS += -T$(LDSCRIPT)
LDFLAGS += -Wl,--gc-sections
LDFLAGS += -Wl,-Map=$(BUILD_DIR)/bootloader.map
LDFLAGS += -nostdlib
LDFLAGS += --specs=nosys.specs

# ── Output ────────────────────────────────────────────────────────────────
TARGET = $(BUILD_DIR)/nrf51_bootloader

# ── Object files ──────────────────────────────────────────────────────────
C_OBJECTS   = $(addprefix $(BUILD_DIR)/, $(notdir $(C_SOURCES:.c=.o)))
ASM_OBJECTS = $(addprefix $(BUILD_DIR)/, $(notdir $(ASM_SOURCES:.s=.o)))
OBJECTS     = $(C_OBJECTS) $(ASM_OBJECTS)

# vpath for source lookup
vpath %.c $(sort $(dir $(C_SOURCES)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

# ── Rules ─────────────────────────────────────────────────────────────────
.PHONY: all clean flash size

all: $(BUILD_DIR) $(TARGET).bin $(TARGET).hex
	@echo "=== nRF51822 Bootloader build complete ==="
	@$(SIZE) $(TARGET).elf

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.s
	$(AS) $(ASFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJECTS)
	$(LD) $(LDFLAGS) $(OBJECTS) -lgcc -o $@   # -lgcc after objects: Cortex-M0 64-bit helpers (__aeabi_lmul/llsr)

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

size: $(TARGET).elf
	$(SIZE) --format=berkeley $<

clean:
	rm -rf $(BUILD_DIR)

# ── Flash via OpenOCD (J-Link / ST-Link adapter) ─────────────────────────
#
#  This writes the bootloader to 0x0003C000 and sets UICR.BOOTLOADERADDR.
#  Adjust the interface config for your debug probe.
#
OPENOCD_IFACE ?= interface/jlink.cfg

flash: $(TARGET).hex
	openocd \
		-f $(OPENOCD_IFACE) \
		-f target/nrf51.cfg \
		-c "init; reset halt" \
		-c "flash write_image erase $(TARGET).hex" \
		-c "flash fillw 0x10001014 0x0003C000 1" \
		-c "reset run; exit"

# ── Flash via nrfjprog (if Nordic tools installed) ────────────────────────
flash-nrfjprog: $(TARGET).hex
	nrfjprog --program $(TARGET).hex --sectorerase
	nrfjprog --memwr 0x10001014 --val 0x0003C000
	nrfjprog --reset

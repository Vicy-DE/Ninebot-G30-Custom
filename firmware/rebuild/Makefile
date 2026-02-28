# Ninebot G30 Firmware Rebuild
# Reassembles disassembled firmware into identical binary

PYTHON = C:/Users/Layer/AppData/Local/Programs/Python/Python314/python.exe

PREFIX  = arm-none-eabi-
AS      = $(PREFIX)as
LD      = $(PREFIX)ld
OBJCOPY = $(PREFIX)objcopy
SIZE    = $(PREFIX)size

ASFLAGS  = -mcpu=cortex-m3 -mthumb
LDFLAGS  = -T stm32f103cb.ld --no-warn-rwx-segments
LDSCRIPT = stm32f103cb.ld

# Source firmware paths
DRV_ORIG = ../ESC-MotorController/firmware/DRV_1.6.13_Compat.bin
BLE_ORIG = ../BLE-Dashboard/firmware/BLE_1.1.7.bin
BMS_ORIG = ../BMS-BatteryManagement/firmware/BMS_1.7.4.5.bin

# Build outputs
BUILD    = build
DRV_ASM  = $(BUILD)/DRV_1.6.13_Compat.s
BLE_ASM  = $(BUILD)/BLE_1.1.7.s
BMS_ASM  = $(BUILD)/BMS_1.7.4.5.s

.PHONY: all clean disasm build-drv verify-drv build-ble verify-ble build-bms verify-bms

all: verify-drv

# ---- Directories ----
$(BUILD):
	mkdir -p $(BUILD)

# ---- DRV firmware ----
disasm-drv: $(BUILD) $(DRV_ORIG)
	$(PYTHON) disassemble_to_asm.py $(DRV_ORIG) $(DRV_ASM)

$(BUILD)/drv.o: $(DRV_ASM)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/drv.elf: $(BUILD)/drv.o $(LDSCRIPT)
	$(LD) $(LDFLAGS) $< -o $@

$(BUILD)/drv_rebuilt.bin: $(BUILD)/drv.elf
	$(OBJCOPY) -O binary $< $@

build-drv: $(BUILD)/drv_rebuilt.bin

verify-drv: $(BUILD)/drv_rebuilt.bin
	$(PYTHON) verify.py $(DRV_ORIG) $(BUILD)/drv_rebuilt.bin

# ---- BLE firmware ----
disasm-ble: $(BUILD) $(BLE_ORIG)
	$(PYTHON) disassemble_to_asm.py $(BLE_ORIG) $(BLE_ASM)

$(BUILD)/ble.o: $(BLE_ASM)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/ble.elf: $(BUILD)/ble.o $(LDSCRIPT)
	$(LD) $(LDFLAGS) $< -o $@

$(BUILD)/ble_rebuilt.bin: $(BUILD)/ble.elf
	$(OBJCOPY) -O binary $< $@

build-ble: $(BUILD)/ble_rebuilt.bin

verify-ble: $(BUILD)/ble_rebuilt.bin
	$(PYTHON) verify.py $(BLE_ORIG) $(BUILD)/ble_rebuilt.bin

# ---- BMS firmware ----
disasm-bms: $(BUILD) $(BMS_ORIG)
	$(PYTHON) disassemble_to_asm.py $(BMS_ORIG) $(BMS_ASM)

$(BUILD)/bms.o: $(BMS_ASM)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/bms.elf: $(BUILD)/bms.o $(LDSCRIPT)
	$(LD) $(LDFLAGS) $< -o $@

$(BUILD)/bms_rebuilt.bin: $(BUILD)/bms.elf
	$(OBJCOPY) -O binary $< $@

build-bms: $(BUILD)/bms_rebuilt.bin

verify-bms: $(BUILD)/bms_rebuilt.bin
	$(PYTHON) verify.py $(BMS_ORIG) $(BUILD)/bms_rebuilt.bin

# ---- Clean ----
clean:
	rm -rf $(BUILD)

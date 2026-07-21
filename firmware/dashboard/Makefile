# Ninebot G30 dashboard firmware — STM32F103C8T6, app @ 0x08001000
#
#   make            # build dashboard_app.{elf,bin,hex} + size
#   make DASH_DEBUG=1   # include the [BOOT] debug banner on USART2 (dev only)
#   make clean
#
# Requires the arm-none-eabi GNU toolchain (tested: 14.2.Rel1).

PREFIX  = arm-none-eabi-
CC      = $(PREFIX)gcc
CXX     = $(PREFIX)g++
OBJCOPY = $(PREFIX)objcopy
SIZE    = $(PREFIX)size

OUTPUT    = dashboard_app
BUILD_DIR = build

# Reuse the already host-tested header-only modules.
DECOMP = ../decompiled
INCLUDES = -Iinclude \
           -I$(DECOMP)/common/include \
           -I$(DECOMP)/ble/include \
           -I$(DECOMP)/nrf51822/include

CPU = -mcpu=cortex-m3 -mthumb

CPPFLAGS = $(INCLUDES) -DNINEBOT_DALY_NO_CLIENT
ifdef DASH_DEBUG
CPPFLAGS += -DDASH_DEBUG
endif

COMMON  = $(CPU) -Os -ffunction-sections -fdata-sections -ffreestanding \
          -Wall -Wextra -g
CXXFLAGS = $(COMMON) -std=c++17 -fno-exceptions -fno-rtti \
           -fno-threadsafe-statics -fno-use-cxa-atexit -fno-unwind-tables
ASFLAGS  = $(CPU)

LDSCRIPT = stm32f103c8_app.ld
LDFLAGS  = $(CPU) -T$(LDSCRIPT) -nostartfiles -Wl,--gc-sections \
           --specs=nano.specs --specs=nosys.specs \
           -Wl,-Map=$(BUILD_DIR)/$(OUTPUT).map

CXX_SRC = src/main.cpp src/dash_hal.cpp
ASM_SRC = src/startup_stm32f103.s
OBJ = $(addprefix $(BUILD_DIR)/, $(notdir $(CXX_SRC:.cpp=.o)) $(notdir $(ASM_SRC:.s=.o)))

vpath %.cpp $(sort $(dir $(CXX_SRC)))
vpath %.s   $(sort $(dir $(ASM_SRC)))

.PHONY: all clean size
all: $(BUILD_DIR)/$(OUTPUT).bin $(BUILD_DIR)/$(OUTPUT).hex size

$(BUILD_DIR)/$(OUTPUT).elf: $(OBJ)
	$(CXX) $(LDFLAGS) -o $@ $^

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD_DIR)/%.o: %.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: %.s | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c -o $@ $<

$(BUILD_DIR):
	mkdir -p $@

size: $(BUILD_DIR)/$(OUTPUT).elf
	@echo "-- dashboard firmware size (max 60 KB app region) --"
	$(SIZE) $<

clean:
	rm -rf $(BUILD_DIR)

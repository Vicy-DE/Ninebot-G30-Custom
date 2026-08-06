# Custom G30 dashboard firmware — nRF51822 (Cortex-M0), app slot above the S110 SoftDevice.
#
#   make            build build/dashboard.bin
#   make test       build + run the host tests (no hardware)
#   make flash      program over SWD (requires a verified dump — see tools/nrf51/)
#   make size       report flash/RAM usage

CROSS   ?= arm-none-eabi-
CXX     := $(CROSS)g++
OBJCOPY := $(CROSS)objcopy
SIZE    := $(CROSS)size

BUILD   := build
TARGET  := $(BUILD)/dashboard
LDSCRIPT := nrf51822_app.ld
APP_BASE := 0x18000

SRC := src/main.cpp src/startup_nrf51.cpp src/nrf51_hal.cpp src/dashboard.cpp \
       src/tm1637.cpp src/nb_protocol.cpp
OBJ := $(patsubst src/%.cpp,$(BUILD)/%.o,$(SRC))

ARCH    := -mcpu=cortex-m0 -mthumb -mfloat-abi=soft
WARN    := -Wall -Wextra -Wshadow -Wdouble-promotion
CXXFLAGS := $(ARCH) $(WARN) -Os -g3 -ffunction-sections -fdata-sections \
            -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-unwind-tables \
            -std=c++17 -Iinclude
LDFLAGS := $(ARCH) -T$(LDSCRIPT) -Wl,--gc-sections -Wl,-Map=$(TARGET).map \
           -nostartfiles -specs=nosys.specs

.PHONY: all clean test flash size

all: $(TARGET).bin size

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJ) $(LDSCRIPT)
	$(CXX) $(OBJ) $(LDFLAGS) -o $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

size: $(TARGET).elf
	@$(SIZE) $<

# Host tests — same C++, native compiler, no hardware.
test:
	@mkdir -p $(BUILD)
	g++ -std=c++17 -Wall -Wextra -O1 -Iinclude \
	    sim/test_dashboard.cpp src/tm1637.cpp src/nb_protocol.cpp src/dashboard.cpp \
	    -o $(BUILD)/test_dashboard
	@./$(BUILD)/test_dashboard
	g++ -std=c++17 -Wall -Wextra -O1 -Iinclude sim/test_persist.cpp -o $(BUILD)/test_persist
	@./$(BUILD)/test_persist

# Flash over SWD. Refuses unless tools/nrf51 finds a verified stock dump.
flash: $(TARGET).bin
	python ../../tools/nrf51/nrf51_swd.py flash $(TARGET).bin --address $(APP_BASE)

clean:
	rm -rf $(BUILD)

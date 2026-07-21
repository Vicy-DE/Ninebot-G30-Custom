# On-silicon self-test firmware for the NUCLEO-C542RC (STM32C542, Cortex-M33).
# Reuses the RC-Servo STM32CubeC5 SDK (CMSIS + startup + system) and the verified
# Ninebot cores (soft_uart.c, nbu.c, nbu_prog.c). Produces build/selftest.{elf,bin}.
#
#   make            # build
#   make flash      # flash via STM32CubeProgrammer CLI over the on-board ST-LINK
#   make verify     # reset+run, then read the .result slot (0x20000000) back over SWD

PREFIX  = arm-none-eabi-
CC      = $(PREFIX)gcc
OBJCOPY = $(PREFIX)objcopy
SIZE    = $(PREFIX)size

# RC-Servo SDK + C542 platform (override with `make SDK=... PLAT=...` if relocated)
RCS    ?= ../../../../RC-Servo
SDK    ?= $(RCS)/sdk/STM32CubeC5
PLAT   ?= $(RCS)/bootloader/platform/stm32c542
CORE    = $(SDK)/arch/cmsis/CMSIS/Core/Include
DEV     = $(SDK)/stm32c5xx_dfp/Include
SYSSRC  = $(SDK)/stm32c5xx_dfp/Source/Templates/system_stm32c5xx.c
STARTUP = $(PLAT)/startup_stm32c542xx.s

CLI    ?= $(LOCALAPPDATA)/stm32cube/bundles/programmer/2.21.0/bin/STM32_Programmer_CLI.exe

CMN     = ../../../bootloader/common
CPU     = -mcpu=cortex-m33 -mthumb -mfloat-abi=soft
DEF     = -DSTM32C542xx
INC     = -I. -I.. -I$(CMN)/include -I$(CORE) -I$(DEV)
EXTRADEF ?=
CFLAGS  = $(CPU) -Os -ffunction-sections -fdata-sections -ffreestanding -Wall -Wextra \
          -std=c11 $(DEF) $(EXTRADEF) $(INC)
LDF     = $(CPU) -nostdlib -Wl,--gc-sections -Tc542.ld -Wl,-Map=build/selftest.map

MAIN ?= selftest_main.c
# per-main output name (avoid flashing a stale binary built from another MAIN)
MB := $(MAIN:.c=)
SRC = $(MAIN) ../soft_uart.c $(CMN)/src/nbu.c ../nbu_prog.c $(SYSSRC) $(STARTUP)

.PHONY: all flash verify clean
all: build/$(MB).bin

build/$(MB).elf: $(SRC) c542.ld | build
	$(CC) $(CFLAGS) $(LDF) -o $@ $(SRC) -lgcc
	$(SIZE) $@
build/$(MB).bin: build/$(MB).elf
	$(OBJCOPY) -O binary $< $@
build:
	mkdir -p build

flash: build/$(MB).bin
	"$(CLI)" -c port=SWD mode=UR -d build/$(MB).bin 0x08000000 -v -rst

verify:
	"$(CLI)" -c port=SWD mode=HOTPLUG -r32 0x20000000 0x10

clean:
	rm -rf build

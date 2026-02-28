# ESC Motor Controller (DRV) - PCB Documentation

## Overview

The ESC (Electronic Speed Controller) is the main motor controller board of the Ninebot G30 Max. It is located inside the scooter's deck and is the central hub that communicates with both the BLE dashboard and the BMS. In the community, it is referred to as **ESC4** (the 4th generation Ninebot ESC, following the M365's ESC1/ESC2/ESC3).

The firmware for this board is designated as **DRV** (Driver).

## Microcontroller

| Property | Value |
|---|---|
| **MCU** | **STM32F103CBT6** (or GD32F103CBT6 on some revisions) |
| **Manufacturer** | STMicroelectronics (or GigaDevice for GD32 variant) |
| **Architecture** | ARM Cortex-M3 |
| **Clock Speed** | 72 MHz |
| **Flash Memory** | 128 KB |
| **SRAM** | 20 KB |
| **Package** | LQFP-48 |
| **Core Voltage** | 1.8V (internal regulator from 3.3V) |
| **Operating Voltage** | 2.0V - 3.6V |
| **ADC** | 2x 12-bit, up to 16 channels |
| **Timers** | Advanced (TIM1 for PWM), General purpose, SysTick |
| **UART** | 3x USART |
| **SPI** | 2x |
| **I2C** | 2x |
| **Debug** | SWD (Serial Wire Debug) via ST-Link |

### GD32F103CBT6 Alternative

Some later production runs or replacement controllers use the **GD32F103CBT6** from GigaDevice. This is a pin-compatible alternative with slightly different internal characteristics:
- Higher actual clock speed capability (108 MHz vs 72 MHz)
- Different flash write times
- Slightly different ADC behavior
- **Firmware is binary-compatible** with STM32F103 in most cases

## Power Stage

The ESC drives a 3-phase BLDC (Brushless DC) motor using 6 MOSFETs in a half-bridge configuration:

| Component | Description |
|---|---|
| **Gate Driver** | MT8006A - 3-phase half-bridge MOSFET gate driver |
| **MOSFETs** | NCEP85T14 (or STP15810 on older boards) - N-channel power MOSFETs |
| **Buck Converter** | SY8502FCC (v2.1+) or TPS54160 (v1.5 and below) - DC-DC step-down |
| **Main Capacitor** | 63V 1000µF electrolytic |
| **Filter Capacitor** | 63V 33µF electrolytic |

### Voltage Modification Notes

- The TPS54160 (v1.5 and below) only safely accepts up to **56V input**
- The SY8502 (v2.1+) accepts up to **85V input, 65V safely**
- For 48V (13S) mods: replace the voltage divider resistor (150k+ for 13S, 200k for 16S, size 0603)
- Replace main cap and filter cap with 80V or 100V rated equivalents

## Communication Interfaces

| Interface | Protocol | Baud Rate | Connected To |
|---|---|---|---|
| USART (half-duplex) | Ninebot protocol | 115200 8N1 | BLE Dashboard |
| USART (full-duplex) | Ninebot protocol | 115200 8N1 | BMS |
| SWD | ST-Link | - | Debug/Programming |

### Ninebot Protocol

The ESC communicates using the Ninebot protocol with packet format:
```
5A A5 [Length] [SrcAddr] [DstAddr] [Cmd] [Arg] [Payload...] [Checksum_L] [Checksum_H]
```

Address assignments:
- `0x20` - ESC
- `0x21` - BLE
- `0x22` - BMS
- `0x3E` / `0x3F` - Application (phone)

## Memory Map (estimated from firmware sizes)

| Address | Content |
|---|---|
| `0x08000000` | Bootloader |
| `0x08001000` | Application firmware (DRV) |
| `0x0800E800` | Update staging area |
| `0x0801C000` | Application configuration |
| `0x0801F800` | Update configuration |

## Key MCU I/O Functions (based on ES2 ESC, similar architecture)

| Pin | Function | Description |
|---|---|---|
| PA8/PA9/PA10 | TIM1 PWM | Motor high-side phases A/B/C |
| PB13/PB14/PB15 | TIM1 PWM | Motor low-side phases A/B/C |
| PB4/PB5/PB0 | TIM3 Input | Hall sensor A/B/C |
| PA3/PA4/PA5 | ADC | Phase current sensing A/B/C |
| PA1 | ADC | Motor supply voltage |
| PA6/PA7 | ADC | Battery voltage sensing |
| PA2 | USART2 | BLE communication (half-duplex) |
| PB10/PB11 | USART3 | BMS communication (TX/RX) |
| PA11 | GPIO | Power supply hold |
| PA12 | GPIO | Power button input |

## Firmware Files

| File | Size | Description |
|---|---|---|
| `DRV_1.2.6.bin` | ~29 KB | Oldest stock firmware, base for most CFW |
| `DRV_1.6.13_Compat.bin` | ~33 KB | Latest compatible stock firmware |

### Firmware Notes

- DRV126 is the most commonly used base for custom firmware (CFW)
- DRV145+ disabled conventional serial number changing methods
- DRV154 offers improved performance algorithm but is beta
- Firmware is encrypted for OTA updates (`.bin.enc` format), decryptable with [XiaoTea](https://tools.scooterhacking.org/xiaotea/)
- Plain `.bin` files can be flashed via ST-Link or IAP

## Flashing via ST-Link

The ESC can be programmed directly using an ST-Link V2 debugger connected to the SWD pads:
1. Connect SWDIO, SWCLK, GND, and 3.3V
2. Use STM32CubeProgrammer or OpenOCD
3. Read/write flash memory directly
4. Useful for recovery from bricked states

## Datasheets in this folder

| File | Description |
|---|---|
| `STM32F103x8xB_datasheet.pdf` | STM32F103 MCU datasheet |
| `MT8006A_gate_driver.pdf` | 3-phase MOSFET gate driver |
| `NCEP85T14_mosfet.pdf` | Power MOSFET (newer boards) |
| `STP15810_mosfet.pdf` | Power MOSFET (older boards) |
| `SY8502FCC_datasheet.pdf` | DC-DC buck converter (v2.1+) |
| `TPS54160_buck_converter.pdf` | DC-DC buck converter (v1.5 and below) |
| `ESC_v2.1_circuit_diagram.pdf` | ESC board circuit diagram (v2.1) |

## Known Issues

- TO220 MOSFET packages are poorly thermally connected to the case
- B+ power rail has poor electrical conductivity (can be improved with trace modifications)
- Phase connectors may be improperly crimped - recommend soldering directly with MT30/MT60 connectors
- Excessive braking current can burn MOSFET packages due to active brake implementation
- Full battery braking (>95% SOC) can cause regenerative overcharging damage

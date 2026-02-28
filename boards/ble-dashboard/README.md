# BLE Dashboard - PCB Documentation

## Overview

The BLE Dashboard is the front-facing control board of the Ninebot G30 Max. It is located inside the handlebar stem near the display area. This board serves a dual purpose:
1. **User Interface**: Handles the display, throttle, brake lever, and button inputs
2. **Bluetooth Communication**: Provides BLE connectivity for the Ninebot/Segway app and ScooterHacking Utility

The board contains two separate ICs: an STM32 MCU for scooter control logic and an nRF51822 SoC for Bluetooth Low Energy communication.

The firmware for this board is designated as **BLE**.

## Microcontrollers

### STM32F103C8T6 — Main Dashboard MCU

| Property | Value |
|---|---|
| **MCU** | **STM32F103C8T6** |
| **Manufacturer** | STMicroelectronics |
| **Architecture** | ARM Cortex-M3 |
| **Clock Speed** | 72 MHz |
| **Flash Memory** | 64 KB |
| **SRAM** | 20 KB |
| **Package** | LQFP-48 |
| **Operating Voltage** | 2.0V - 3.6V |
| **Debug** | SWD (Serial Wire Debug) |

This MCU handles:
- Display driving (LED segment display)
- Throttle ADC reading
- Brake lever input
- Power button
- Communication with ESC via UART (Ninebot protocol)
- Forwarding BLE commands from nRF51822

### nRF51822 — Bluetooth Low Energy SoC

| Property | Value |
|---|---|
| **SoC** | **nRF51822** (labeled as nRF51802 on some packaging) |
| **Manufacturer** | Nordic Semiconductor |
| **Architecture** | ARM Cortex-M0 |
| **Clock Speed** | 16 MHz |
| **Flash Memory** | 256 KB |
| **RAM** | 16 KB (nRF51822-QFAA) or 32 KB (nRF51822-QFAC) |
| **Package** | QFN-48 (6x6mm) |
| **Radio** | 2.4 GHz BLE 4.0 / ANT |
| **TX Power** | -20 dBm to +4 dBm |
| **Sensitivity** | -93 dBm (BLE) |
| **Operating Voltage** | 1.8V - 3.6V |

**Note on nRF51802 vs nRF51822**: The nRF51802 is a cost-reduced variant of the nRF51822 with identical silicon — it lacks ANT protocol licensing and some documentation, but is otherwise equivalent for BLE use.

### Communication Between STM32 and nRF51822

The two chips communicate via UART internally on the dashboard PCB. The nRF51822 acts as a transparent bridge between the BLE connection (phone app) and the STM32, which then forwards commands to the ESC.

```
Phone App  ←BLE→  nRF51822  ←UART→  STM32  ←UART→  ESC
```

## Communication Interfaces

| Interface | Protocol | Connected To | Direction |
|---|---|---|---|
| UART (half-duplex) | Ninebot protocol | ESC | Bidirectional |
| Internal UART | Bridge protocol | nRF51822 ↔ STM32 | Bidirectional |
| BLE 4.0 | Ninebot BLE | Phone app | Bidirectional |
| SWD | ST-Link | Debug/Programming | Programming |

### BLE Service UUIDs

| Service | UUID | Description |
|---|---|---|
| Primary Service | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` | Nordic UART Service (NUS) |
| TX Characteristic | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` | Phone → Scooter |
| RX Characteristic | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` | Scooter → Phone |

## User Interface Components

| Component | Type | Function |
|---|---|---|
| **LED Display** | 7-segment LED | Speed, battery, mode indicator |
| **Power Button** | Tactile switch | Power on/off, mode switching |
| **Throttle** | Hall sensor (analog) | Speed control input |
| **Brake Lever** | Mechanical switch + analog | Regenerative braking |
| **Headlight** | LED | Front illumination |
| **Tail Light** | LED | Rear light + brake light |

## Firmware Files

| File | Size | Description |
|---|---|---|
| `BLE_1.1.0.bin` | ~33 KB | Original stock BLE firmware |
| `BLE_1.1.7.bin` | ~33 KB | Latest BLE firmware (BLE555 variant) |

### Firmware Notes

- **BLE107** is the most commonly used version for custom firmware
- BLE117 / BLE555 variants exist for different production runs
- Downgrading BLE firmware may cause issues with newer ESC firmware
- BLE firmware handles display rendering, so display bugs are BLE firmware related
- The nRF51822 firmware is separate and is typically not modified

### Known BLE Firmware Versions

| Version | Notes |
|---|---|
| BLE100 | Original production firmware |
| BLE104 | Early update |
| BLE107 | Widely used, good CFW compatibility |
| BLE109 | Added new features |
| BLE117 | Current latest |

## Flashing

### OTA (Over-The-Air)
- Use **ScooterHacking Utility** (Android) or **XiaoFlasher** (Android)
- Connect via Bluetooth
- Flash encrypted `.bin.enc` firmware files
- Supports DRV, BLE, and BMS updates

### ST-Link (Direct Flash)
- Connect ST-Link V2 to SWD pads on the dashboard PCB
- Use STM32CubeProgrammer
- Flash plain `.bin` firmware files
- Required for recovery from bricked BLE

### IAP (In-Application Programming)
- Uses the scooter's built-in bootloader
- Triggered by sending specific protocol commands
- Used by some advanced flashing tools

## Datasheets in this folder

| File | Description |
|---|---|
| `STM32F103x8_datasheet.pdf` | STM32F103C8T6 MCU datasheet |
| `nRF51822_product_spec.pdf` | Nordic nRF51822 BLE SoC product specification |

## Known Issues

- BLE firmware downgrades can sometimes cause communication issues with newer ESC firmware
- Display flickering may occur with certain CFW combinations
- Some aftermarket dashboards use different LED segment arrangements
- Throttle calibration values are stored in BLE firmware configuration area

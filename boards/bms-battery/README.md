# BMS Battery Management System - PCB Documentation

## Overview

The BMS (Battery Management System) is the internal battery controller of the Ninebot G30 Max. It is located inside the sealed battery compartment in the scooter's deck. The BMS is responsible for cell balancing, charge/discharge protection, temperature monitoring, state-of-charge estimation, and communication with the ESC.

The firmware for this board is designated as **BMS**.

## Battery Pack Specifications

| Property | Value |
|---|---|
| **Configuration** | 10S3P (10 series, 3 parallel) |
| **Cell Type** | 18650 Lithium-ion |
| **Nominal Voltage** | 36V (3.6V × 10) |
| **Max Voltage** | 42V (4.2V × 10) |
| **Min Voltage** | 30V (3.0V × 10) |
| **Capacity** | 15.3 Ah (551 Wh) |
| **Max Discharge** | ~25A continuous, 30A peak |
| **Max Charge** | ~3A (stock charger), up to 5A+ (fast charger) |

## Microcontroller

### STM32F103C8T6 — BMS Main MCU

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

The MCU handles:
- Reading cell voltages from the analog front-end
- SoC (State of Charge) estimation
- Balancing control
- Over/under voltage protection
- Over-current protection
- Temperature monitoring
- Communication with ESC
- Charge/discharge MOSFET control

## Battery Analog Front-End (AFE)

### BQ76940 (or BQ769x0 series)

| Property | Value |
|---|---|
| **IC** | **BQ76940** (10-cell variant) |
| **Manufacturer** | Texas Instruments |
| **Supported Cells** | 9–15 series (BQ76940) |
| **Communication** | I2C (to STM32) |
| **Cell Voltage Accuracy** | ±25 mV |
| **Current Sensing** | External shunt resistor |
| **Balancing** | Internal FET-based cell balancing |
| **Protection** | OV, UV, OCD, SCD (configurable) |
| **Package** | TSSOP-44 |

**Note**: The Ninebot ES2 BMS uses the BQ7693003 (3–6 cell variant). The G30 Max with 10S configuration requires the BQ76940 (9–15 cell variant) from the same BQ769x0 family.

### BQ769x0 Register Map (relevant for reading cell data)

The AFE communicates with the STM32 over I2C. Key registers:

| Register | Address | Description |
|---|---|---|
| SYS_STAT | 0x00 | System status flags |
| CELLBAL1 | 0x01 | Cell balancing register 1 (cells 1-5) |
| CELLBAL2 | 0x02 | Cell balancing register 2 (cells 6-10) |
| SYS_CTRL1 | 0x04 | ADC enable, temp sensor |
| SYS_CTRL2 | 0x05 | CC_EN, DSG/CHG FET control |
| PROTECT1 | 0x06 | SCD threshold, delay |
| PROTECT2 | 0x07 | OCD threshold, delay |
| PROTECT3 | 0x08 | UV/OV delay |
| OV_TRIP | 0x09 | Overvoltage trip threshold |
| UV_TRIP | 0x0A | Undervoltage trip threshold |
| CC_CFG | 0x0B | Coulomb counter configuration |
| VC1_HI/LO | 0x0C-0x0D | Cell 1 voltage |
| VC2_HI/LO | 0x0E-0x0F | Cell 2 voltage |
| ... | ... | ... |
| VC10_HI/LO | 0x1E-0x1F | Cell 10 voltage |
| BAT_HI/LO | 0x2A-0x2B | Battery pack voltage |
| TS1_HI/LO | 0x2C-0x2D | Temperature sensor 1 |
| CC_HI/LO | 0x32-0x33 | Coulomb counter (current) |

## Communication Interfaces

| Interface | Protocol | Connected To | Direction |
|---|---|---|---|
| UART (full-duplex) | Ninebot protocol | ESC | Bidirectional |
| I2C | AFE protocol | BQ76940 | Master→Slave |
| SWD | ST-Link | Debug/Programming | Programming |

### BMS Ninebot Protocol Registers

The ESC can query BMS status through the Ninebot protocol. Common BMS registers accessible via protocol:

| Register | Description |
|---|---|
| 0x10-0x11 | BMS status |
| 0x17 | Temperature 1 (°C × 10) |
| 0x18 | Temperature 2 (°C × 10) |
| 0x22-0x23 | Remaining capacity (mAh) |
| 0x24-0x25 | Remaining capacity (%) |
| 0x26-0x27 | Current (mA, signed) |
| 0x30-0x31 | Cell 1 voltage (mV) |
| 0x32-0x33 | Cell 2 voltage (mV) |
| ... | ... |
| 0x3E-0x3F | Cell 10 voltage (mV) |
| 0x40-0x41 | Manufacture date |
| 0x66 | Full charge capacity (mAh) |
| 0x67 | Cycle count |
| 0x69 | Charge full voltage |
| 0x6A | Charge full current |

## Protection Features

| Protection | Description | Typical Threshold |
|---|---|---|
| **OVP** | Overvoltage per cell | 4.25V |
| **UVP** | Undervoltage per cell | 2.8V |
| **OCD** | Overcurrent discharge | 30A |
| **SCD** | Short circuit detection | 60A+, <100µs |
| **OTP** | Over-temperature protection | 60°C |
| **UTP** | Under-temperature (charge) | 0°C |

## Firmware Files

| File | Size | Description |
|---|---|---|
| `BMS_1.3.4.bin` | ~14 KB | Original stock BMS firmware |
| `BMS_1.7.4.5.bin` | ~23 KB | Latest BMS firmware |

### Firmware Notes

- BMS firmware updates are generally not required for CFW
- BMS134 is the most common version found in G30 Max scooters
- Newer BMS firmware may change cell balancing behavior
- BMS firmware communicates with the AFE and reports to ESC; it does not directly control motor behavior
- BMS firmware modifications are rare and advanced — incorrect BMS firmware can cause safety hazards

## Flashing

### OTA (via ESC passthrough)
- BMS firmware can be updated through the ScooterHacking Utility
- The ESC acts as a bridge between BLE and BMS
- Use encrypted `.bin.enc` firmware files

### ST-Link (Direct Flash)
- Requires opening the battery compartment (sealed unit)
- Connect ST-Link to SWD pads on the BMS PCB
- **Caution**: The BMS PCB is energized by the battery pack — be careful with connections
- Flash plain `.bin` files using STM32CubeProgrammer

## Datasheets in this folder

| File | Description |
|---|---|
| `STM32F103x8_datasheet.pdf` | STM32F103C8T6 MCU datasheet |

### Missing Datasheets

The following datasheets would be useful but were not found/downloaded:
- **BQ76940**: Texas Instruments battery AFE datasheet (available from [TI.com](https://www.ti.com/product/BQ76940))
- **BMS charge/discharge MOSFETs**: Specific part number not yet identified

## Safety Warnings

⚠️ **IMPORTANT**: Modifying BMS firmware or hardware can be extremely dangerous.

- The battery pack stores significant energy (551 Wh) capable of causing fires
- Short circuits can deliver hundreds of amps
- Do NOT bypass BMS protection features
- Always use appropriate tools and insulation when working with the battery
- Never puncture, crush, or overheat lithium cells
- If any cell is swollen or damaged, do not use the battery pack

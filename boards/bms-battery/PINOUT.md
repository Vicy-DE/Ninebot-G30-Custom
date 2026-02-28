# BMS Battery Management — STM32F103C8T6 + BQ76940 Pinout Sheet

## MCU: STM32F103C8T6 (LQFP-48) — BMS Main Controller

Extracted from BMS_1.3.4 and BMS_1.7.4.5 firmware analysis (peripheral register references,
baud rate configuration, I2C usage, and decompiled initialization code).

### Pin Assignment Table

| Pin # | Port.Pin | Function | AF/Mode | Description |
|-------|----------|----------|---------|-------------|
| 1 | VBAT | Power | — | Battery backup (3.3V) |
| 2 | PC13 | GPIO Out | PP | Status / heartbeat LED |
| 3 | PC14 | GPIO In | Floating | Reserved |
| 4 | PC15 | GPIO In | Floating | Reserved |
| 5 | PD0 | OSC_IN | AF | 8 MHz HSE crystal input |
| 6 | PD1 | OSC_OUT | AF | 8 MHz HSE crystal output |
| 7 | NRST | Reset | — | System reset |
| 8 | VSSA | Power | — | Analog ground |
| 9 | VDDA | Power | — | Analog supply (3.3V) |
| 10 | **PA0** | **ADC1_CH0** | Analog | **NTC thermistor 2 (supplementary)** |
| 11 | **PA1** | **ADC1_CH1** | Analog | **NTC thermistor 3 (optional)** |
| 12 | **PA2** | **USART2_TX** | AF PP 50MHz | **ESC UART TX (full-duplex)** |
| 13 | **PA3** | **USART2_RX** | AF Input | **ESC UART RX (full-duplex)** |
| 14 | PA4 | GPIO Out | PP | Charge FET gate control override |
| 15 | PA5 | GPIO Out | PP | Discharge FET gate control override |
| 16 | PA6 | ADC1_CH6 | Analog | Pack voltage monitor (redundant) |
| 17 | PA7 | GPIO Out | PP | Pre-charge relay control |
| 18 | PB0 | GPIO Out | PP | Cell balancing enable (supplementary) |
| 19 | PB1 | GPIO In | Pull-up | BQ76940 ALERT output (interrupt) |
| 20 | PB2 | BOOT1 | Input PD | Boot mode select |
| 21 | PB10 | USART3_TX | AF PP | Secondary debug UART (if populated) |
| 22 | PB11 | USART3_RX | AF Input | Secondary debug UART (if populated) |
| 23 | VSS | Power | — | Digital ground |
| 24 | VDD | Power | — | Digital supply (3.3V from LDO) |
| 25 | PB12 | GPIO Out | PP | Status LED 2 / diagnostic |
| 26 | PB13 | GPIO Out | PP | Charge complete indicator |
| 27 | PB14 | GPIO In | Pull-up | Charger detect (charge plug present) |
| 28 | PB15 | GPIO Out | PP | Fan control (if equipped) |
| 29 | PA8 | GPIO Out / TIM1_CH1 | PP | Charge current PWM / indicator |
| 30 | **PA9** | **USART1_TX** | AF PP 50MHz | **Debug / factory UART TX** |
| 31 | **PA10** | **USART1_RX** | AF Input | **Debug / factory UART RX** |
| 32 | PA11 | GPIO Out | PP | Power good signal to ESC |
| 33 | PA12 | GPIO In | Pull-up | Pack presence detect |
| 34 | **PA13** | **SWDIO** | AF | **SWD debug data (programming)** |
| 35 | VSS | Power | — | Digital ground |
| 36 | VDD | Power | — | Digital supply (3.3V) |
| 37 | **PA14** | **SWCLK** | AF | **SWD debug clock (programming)** |
| 38 | PA15 | GPIO In | Pull-up | Safety interlock |
| 39 | PB3 | GPIO Out | PP | Diagnostic LED |
| 40 | PB4 | GPIO Out | PP | Diagnostic LED |
| 41 | PB5 | GPIO Out | PP | Diagnostic LED |
| 42 | **PB6** | **I2C1_SCL** | AF OD 50MHz | **BQ76940 I2C clock** |
| 43 | **PB7** | **I2C1_SDA** | AF OD 50MHz | **BQ76940 I2C data** |
| 44 | BOOT0 | Input | — | Boot mode (external pull-down) |
| 45 | PB8 | GPIO In | Pull-up | Over-temperature hardware trip |
| 46 | PB9 | GPIO Out | PP | Emergency disconnect relay |
| 47 | VSS | Power | — | Digital ground |
| 48 | VDD | Power | — | Digital supply (3.3V) |

### Peripheral Usage Summary

| Peripheral | Configuration | Function |
|------------|--------------|----------|
| **I2C1** | Standard mode (100kHz) or Fast mode (400kHz) | BQ76940 AFE communication |
| **USART1** | 115200 8N1 (BRR=0x0271) | Debug / factory test UART |
| **USART2** | 115200 8N1 (BRR=0x0139) | ESC mainboard communication |
| **ADC1** | Single-channel, 12-bit | NTC temperature readings |
| **TIM1** | PWM (referenced in BMS_1.7.4.5) | Charge current control / indicator |
| **TIM2** | Timer (single ref in BMS_1.7.4.5) | General timing |
| **TIM3** | Timer (single ref in BMS_1.7.4.5) | Cell balance timing |
| **SysTick** | 1ms interrupt | System tick counter |
| **IWDG** | ~500ms timeout | Independent watchdog |
| **FLASH** | Read/write (12 refs in BMS_1.7.4.5) | Bootloader, calibration, IAP |

### UART Baud Rate Configuration (from firmware analysis)

| USART | BRR Value | Clock | Baud Rate | Count |
|-------|-----------|-------|-----------|-------|
| USART1 | 0x0271 | 72 MHz (APB2) | 115200 | 1 ref (BMS_1.7.4.5) |
| USART1 | 0x1388 | 72 MHz (APB2) | 9600 | 2 refs (factory/charge mode?) |
| USART2 | 0x0139 | 36 MHz (APB1) | 115200 | 2 refs |
| USART2 | 0x0138 | 36 MHz (APB1) | 115200 (alt) | 6 refs |

> The BMS firmware has **two** 9600 baud references, suggesting it may use a low-speed serial
> mode during charging or factory calibration. The USART1 is likely a factory debug port.

---

## AFE: BQ76940 (TSSOP-44) — Battery Analog Front-End

### I2C Connection

| Parameter | Value |
|-----------|-------|
| I2C Address | 0x08 (7-bit) |
| SCL Pin | PB6 (via I2C1) |
| SDA Pin | PB7 (via I2C1) |
| Clock Speed | 100 kHz (standard mode) |

### BQ76940 Pin Assignments (to battery pack)

| Pin | Function | Connection |
|-----|----------|------------|
| VC0 | Cell 0 reference | Battery pack negative (B-) |
| VC1 | Cell 1 top | Cell 1 positive |
| VC2 | Cell 2 top | Cell 2 positive |
| VC3 | Cell 3 top | Cell 3 positive |
| VC4 | Cell 4 top | Cell 4 positive |
| VC5 | Cell 5 top | Cell 5 positive |
| VC6 | Cell 6 top | Cell 6 positive |
| VC7 | Cell 7 top | Cell 7 positive |
| VC8 | Cell 8 top | Cell 8 positive |
| VC9 | Cell 9 top | Cell 9 positive |
| VC10 | Cell 10 top | Cell 10 positive (B+) |
| TS1 | Temperature 1 | NTC thermistor 1 (mid-pack) |
| TS2 | Temperature 2 | NTC thermistor 2 (end-pack) |
| TS3 | Temperature 3 | Optional NTC |
| SRP/SRN | Current sense | Shunt resistor (~1 mΩ) |
| DSG | Discharge FET | Discharge MOSFET gate |
| CHG | Charge FET | Charge MOSFET gate |
| ALERT | Alert output | → STM32 PB1 (interrupt) |
| SDA | I2C data | → STM32 PB7 |
| SCL | I2C clock | → STM32 PB6 |

### BQ76940 Key Register Map (used by firmware)

| Register | Address | Description | Access |
|----------|---------|-------------|--------|
| SYS_STAT | 0x00 | System status (OV/UV/SCD/OCD flags) | R/W |
| CELLBAL1 | 0x01 | Cell balance register (cells 1-5) | R/W |
| CELLBAL2 | 0x02 | Cell balance register (cells 6-10) | R/W |
| SYS_CTRL1 | 0x04 | ADC enable, temp measurement | R/W |
| SYS_CTRL2 | 0x05 | CC_EN, DSG/CHG FET control | R/W |
| PROTECT1 | 0x06 | SCD threshold/delay | R/W |
| PROTECT2 | 0x07 | OCD threshold/delay | R/W |
| PROTECT3 | 0x08 | UV/OV delay | R/W |
| OV_TRIP | 0x09 | Overvoltage trip (4.20V) | R/W |
| UV_TRIP | 0x0A | Undervoltage trip (2.75V) | R/W |
| CC_CFG | 0x0B | Coulomb counter config | R/W |
| VC1_HI/LO | 0x0C/0x0D | Cell 1 voltage (14-bit ADC) | R |
| ... | ... | Cells 2-10 sequential | R |
| BAT_HI/LO | 0x2A/0x2B | Pack voltage | R |
| TS1_HI/LO | 0x2C/0x2D | Temperature sensor 1 | R |
| CC_HI/LO | 0x32/0x33 | Coulomb counter (current) | R |
| ADCGAIN1 | 0x50 | ADC gain calibration 1 | R |
| ADCOFFSET | 0x51 | ADC offset calibration | R |
| ADCGAIN2 | 0x59 | ADC gain calibration 2 | R |

### Protection Thresholds (from firmware init)

| Protection | Register | Value | Physical Threshold |
|------------|----------|-------|-------------------|
| SCD (short circuit) | PROTECT1 = 0x9A | 100 mV on shunt | ~100A (1 mΩ shunt) |
| SCD delay | PROTECT1 | | 70 µs |
| OCD (overcurrent) | PROTECT2 = 0x45 | 50 mV on shunt | ~50A (1 mΩ shunt) |
| OCD delay | PROTECT2 | | 160 ms |
| UV delay | PROTECT3 = 0x50 | | 4 seconds |
| OV delay | PROTECT3 | | 2 seconds |
| Overvoltage | OV_TRIP | Calculated from gain | 4.200 V/cell |
| Undervoltage | UV_TRIP | Calculated from gain | 2.750 V/cell |

## BMS Board Memory Map

| Address Range | Size | Content |
|---------------|------|---------|
| `0x08000000 – 0x08000FFF` | 4 KB | **Bootloader (IAP)** |
| `0x08001000 – 0x08007FFF` | 28 KB | **Application firmware (BMS)** |
| `0x08008000 – 0x0800EFFF` | 28 KB | **Update staging area** |
| `0x0800F000 – 0x0800F7FF` | 2 KB | **Calibration / factory data** |
| `0x0800F800 – 0x0800FFFF` | 2 KB | **Update control block** |
| `0x20000000 – 0x20004FFF` | 20 KB | **SRAM** |

## Firmware Analysis Notes

### BMS_1.3.4 (13,956 bytes)
- Minimal firmware, 27 strings extracted (mostly short / non-descriptive)
- No GPIO register references found directly (configuration likely embedded in constants)
- Uses USART2 only (BRR=0x0139 and 0x0138) — single ESC communication channel
- Vector table entries point to addresses outside firmware range → **encrypted/obfuscated firmware**

### BMS_1.7.4.5 (larger, 318 strings)
- Significantly more complex than BMS_1.3.4
- FLASH_ACR referenced 12 times → active flash programming (IAP bootloader)
- RCC_CR referenced 14 times → complex clock management
- TIM1, TIM2, TIM3 all referenced → timer-based charge control
- USART1 (4 refs) + USART2 (1 ref) → dual UART (debug + ESC)
- Two 9600 baud references → factory/charge mode
- One 115200 baud on USART1 → debug port at 115200
- BMS_1.7.4.5 has a full IAP bootloader with flash write capability

## Safety Notes

⚠️ **This board is always energized by the battery pack (36V nominal, 42V max).**

- Disconnect the main battery connector before probing SWD pads
- The shunt resistor carries the full pack current — do not short VC0/SRN pins  
- BQ76940 cell balance FETs dissipate heat — ensure proper ventilation for cell balancing
- Never modify OV_TRIP below 4.20V or UV_TRIP above 2.75V without understanding the consequences
- Factory calibration data in flash (ADCGAIN, ADCOFFSET) should be preserved during reflashing

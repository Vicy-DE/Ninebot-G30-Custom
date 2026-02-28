# ESC Motor Controller — STM32F103CBT6 Pinout Sheet

## MCU: STM32F103CBT6 / GD32F103CBT6 (LQFP-48)

Extracted from DRV_1.2.6 and DRV_1.6.13 firmware analysis (GPIO CRL/CRH register writes,
peripheral register references, and decompiled initialization code).

## Pin Assignment Table

| Pin # | Port.Pin | Function | AF/Mode | Description |
|-------|----------|----------|---------|-------------|
| 1 | VBAT | Power | — | Battery backup (3.3V) |
| 2 | PC13 | GPIO Out | PP | Status LED / indicator |
| 3 | PC14 | GPIO In | Floating | Reserved (32kHz OSC if used) |
| 4 | PC15 | GPIO In | Floating | Reserved (32kHz OSC if used) |
| 5 | PD0 | OSC_IN | AF | 8 MHz HSE crystal input |
| 6 | PD1 | OSC_OUT | AF | 8 MHz HSE crystal output |
| 7 | NRST | Reset | — | System reset (active low) |
| 8 | VSSA | Power | — | Analog ground |
| 9 | VDDA | Power | — | Analog supply (3.3V) |
| 10 | **PA0** | **ADC1_CH0** | Analog | **Throttle input (from BLE)** |
| 11 | **PA1** | **ADC1_CH1** | Analog | **Motor supply voltage sense** |
| 12 | **PA2** | **USART2_TX** | AF PP 50MHz | **BLE dashboard UART (half-duplex)** |
| 13 | **PA3** | **ADC1_CH3** | Analog | **Phase A current sense** |
| 14 | **PA4** | **ADC1_CH4** | Analog | **Phase B current sense** |
| 15 | **PA5** | **ADC1_CH5** | Analog | **Phase C current sense** |
| 16 | **PA6** | **ADC1_CH6** | Analog | **Battery voltage sense (high)** |
| 17 | **PA7** | **ADC1_CH7** | Analog | **Battery voltage sense (low / GND ref)** |
| 18 | **PB0** | **TIM3_CH3** | AF Input | **Hall sensor C input** |
| 19 | **PB1** | GPIO Out | PP | Motor enable / gate driver enable |
| 20 | PB2 | BOOT1 | Input PD | Boot mode select (pulled low for normal) |
| 21 | **PB10** | **USART3_TX** | AF PP 50MHz | **BMS UART TX (full-duplex)** |
| 22 | **PB11** | **USART3_RX** | AF Input | **BMS UART RX (full-duplex)** |
| 23 | VSS | Power | — | Digital ground |
| 24 | VDD | Power | — | Digital supply (3.3V) |
| 25 | **PB12** | GPIO Out | PP | Power latch / system power hold |
| 26 | **PB13** | **TIM1_CH1N** | AF PP 50MHz | **Motor phase A LOW-side FET** |
| 27 | **PB14** | **TIM1_CH2N** | AF PP 50MHz | **Motor phase B LOW-side FET** |
| 28 | **PB15** | **TIM1_CH3N** | AF PP 50MHz | **Motor phase C LOW-side FET** |
| 29 | **PA8** | **TIM1_CH1** | AF PP 50MHz | **Motor phase A HIGH-side FET** |
| 30 | **PA9** | **TIM1_CH2** | AF PP 50MHz | **Motor phase B HIGH-side FET** |
| 31 | **PA10** | **TIM1_CH3** | AF PP 50MHz | **Motor phase C HIGH-side FET** |
| 32 | **PA11** | GPIO Out | PP | **Power supply hold / keep-alive** |
| 33 | **PA12** | GPIO In | Pull-up | **Power button input** |
| 34 | **PA13** | **SWDIO** | AF | **SWD debug data (programming)** |
| 35 | VSS | Power | — | Digital ground |
| 36 | VDD | Power | — | Digital supply (3.3V) |
| 37 | **PA14** | **SWCLK** | AF | **SWD debug clock (programming)** |
| 38 | **PA15** | GPIO Out | PP | Tail light control |
| 39 | **PB3** | GPIO Out | PP | Headlight control (requires AFIO remap) |
| 40 | **PB4** | **TIM3_CH1** | AF Input | **Hall sensor A input** |
| 41 | **PB5** | **TIM3_CH2** | AF Input | **Hall sensor B input** |
| 42 | **PB6** | **USART1_TX** | AF PP 50MHz | **External/debug UART TX** |
| 43 | **PB7** | **USART1_RX** | AF Input | **External/debug UART RX** |
| 44 | BOOT0 | Input | — | Boot mode select (external pull-down) |
| 45 | **PB8** | GPIO In | Pull-up | Brake lever digital input |
| 46 | **PB9** | GPIO Out | PP | Buzzer / beeper output |
| 47 | VSS | Power | — | Digital ground |
| 48 | VDD | Power | — | Digital supply (3.3V) |

## Peripheral Usage Summary

| Peripheral | Configuration | Function |
|------------|--------------|----------|
| **TIM1** | Center-aligned PWM, 16kHz (ARR=2250), dead-time ~1µs | 3-phase motor bridge (CH1-3 + CH1N-3N) |
| **TIM3** | Input capture mode | Hall sensor timing (CH1-CH3 on PB4/PB5/PB0) |
| **USART1** | 115200 8N1 (BRR=0x0271) | External/debug serial port |
| **USART2** | 115200 8N1, half-duplex (BRR=0x0271) | BLE dashboard communication |
| **USART3** | 115200 8N1, full-duplex (BRR=0x0139) | BMS battery communication |
| **ADC1** | Scan mode, 12-bit | Voltage/current/temperature sensing |
| **SysTick** | 1ms interrupt (72MHz/72000) | System tick counter |
| **IWDG** | ~500ms timeout | Independent watchdog |
| **FLASH** | Read/write (for config storage) | Bootloader + app configuration area |

## Clock Configuration

| Clock | Source | Frequency |
|-------|--------|-----------|
| HSE | 8 MHz crystal | 8 MHz |
| PLL | HSE × 9 | 72 MHz |
| SYSCLK | PLL | 72 MHz |
| AHB (HCLK) | SYSCLK / 1 | 72 MHz |
| APB1 (PCLK1) | HCLK / 2 | 36 MHz |
| APB2 (PCLK2) | HCLK / 1 | 72 MHz |
| USART1 clk | APB2 = 72 MHz | BRR=0x0271 → 115200 baud |
| USART2/3 clk | APB1 = 36 MHz | BRR=0x0139 → 115200 baud |

## Memory Map

| Address Range | Size | Content |
|---------------|------|---------|
| `0x08000000 – 0x08000FFF` | 4 KB | **Bootloader (IAP)** |
| `0x08001000 – 0x0800DFFF` | 52 KB | **Application firmware (DRV)** |
| `0x0800E000 – 0x0801BFFF` | 56 KB | **Update staging area** (for OTA) |
| `0x0801C000 – 0x0801F7FF` | 14 KB | **Configuration / calibration data** |
| `0x0801F800 – 0x0801FFFF` | 2 KB | **Update control block** |
| `0x20000000 – 0x20004FFF` | 20 KB | **SRAM** |

## AFIO Remapping

- AFIO_EVCR referenced at offset 0x0023CC (DRV_1.2.6) — partial remap for TIM3 or JTAG release
- JTAG likely disabled to free PA15/PB3/PB4 for GPIO use (SWD-only debug retained)

## Interrupt Vectors (from DRV_1.2.6 vector table)

| IRQ | Handler Address | Function |
|-----|----------------|----------|
| Reset | 0x08001100 | Entry point → SystemInit → main() |
| SysTick | 0x08005C54 | 1ms tick counter, timing flags |
| TIM1_UP | 0x08005E74 | Motor commutation at 16kHz |
| USART1 | (shared) | External/debug protocol parser |
| USART2 | (shared) | BLE protocol parser |
| USART3 | (shared) | BMS protocol parser |
| DMA1_Ch1-3 | 0x0800111A | Default handler (DMA not heavily used) |

## ADC Channel Assignments

| ADC Channel | Pin | Measurement | Scale |
|-------------|-----|-------------|-------|
| CH0 (PA0) | PA0 | Throttle position | 0-3.3V → 0-4095 |
| CH1 (PA1) | PA1 | Motor supply voltage | Resistor divider, ~1:20 |
| CH3 (PA3) | PA3 | Phase A current | Shunt + op-amp, center at VDD/2 |
| CH4 (PA4) | PA4 | Phase B current | Shunt + op-amp, center at VDD/2 |
| CH5 (PA5) | PA5 | Phase C current | Shunt + op-amp, center at VDD/2 |
| CH6 (PA6) | PA6 | Battery voltage (high) | Resistor divider input |
| CH7 (PA7) | PA7 | Battery voltage (low/ref) | Resistor divider input |
| CH16 (internal) | — | Internal temperature sensor | ~1.43V @ 25°C, 4.3mV/°C |

## Notes

- GPIO CRL/CRH configuration values observed at firmware offsets listed in analysis_output.txt
- The USART2 half-duplex mode uses a single wire (PA2) with the GPIO direction switched dynamically
- TIM1 break input (PB12) may be used for hardware overcurrent protection via the gate driver fault output
- Hall sensors use external pull-up resistors; firmware configures inputs as floating
- Some production boards use GD32F103CBT6 — same pinout, binary-compatible firmware

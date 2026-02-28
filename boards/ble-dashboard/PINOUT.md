# BLE Dashboard — STM32F103C8T6 + nRF51822 Pinout Sheet

## MCU 1: STM32F103C8T6 (LQFP-48) — Main Dashboard Controller

Extracted from BLE_1.1.0 and BLE_1.1.7 firmware analysis (peripheral register references,
baud rate configuration, decompiled initialization code, and ADC channel usage).

### Pin Assignment Table

| Pin # | Port.Pin | Function | AF/Mode | Description |
|-------|----------|----------|---------|-------------|
| 1 | VBAT | Power | — | Battery backup (3.3V) |
| 2 | PC13 | GPIO Out | PP | Status indicator (if populated) |
| 3 | PC14 | GPIO In | Floating | Reserved |
| 4 | PC15 | GPIO In | Floating | Reserved |
| 5 | PD0 | OSC_IN | AF | 8 MHz HSE crystal input |
| 6 | PD1 | OSC_OUT | AF | 8 MHz HSE crystal output |
| 7 | NRST | Reset | — | System reset (active low) |
| 8 | VSSA | Power | — | Analog ground |
| 9 | VDDA | Power | — | Analog supply (3.3V) |
| 10 | **PA0** | **ADC1_CH0** | Analog | **Throttle potentiometer input** |
| 11 | **PA1** | **ADC1_CH1** | Analog | **Brake lever pressure sensor input** |
| 12 | PA2 | GPIO / NC | — | Not used (or test point) |
| 13 | PA3 | GPIO / NC | — | Not used |
| 14 | PA4 | GPIO Out | PP | Display segment data |
| 15 | PA5 | GPIO Out | PP | Display segment clock |
| 16 | PA6 | GPIO Out | PP | Display segment latch |
| 17 | PA7 | GPIO Out | PP | Display segment control |
| 18 | **PB0** | **GPIO Out** | PP | **Power indicator LED** |
| 19 | **PB1** | **GPIO Out** | PP | **BLE connection indicator LED** |
| 20 | PB2 | BOOT1 | Input PD | Boot mode select |
| 21 | **PB10** | GPIO Out | PP | Headlight control (main beam) |
| 22 | **PB11** | GPIO Out | PP | Tail light control |
| 23 | VSS | Power | — | Digital ground |
| 24 | VDD | Power | — | Digital supply (3.3V) |
| 25 | **PB12** | **GPIO In** | Pull-up | **Power/mode button (active-low)** |
| 26 | PB13 | GPIO Out | PP | Display digit select 1 |
| 27 | PB14 | GPIO Out | PP | Display digit select 2 |
| 28 | PB15 | GPIO Out | PP | Display digit select 3 |
| 29 | **PA8** | GPIO Out | PP | Display refresh / strobe |
| 30 | **PA9** | **USART1_TX** | AF PP 50MHz | **nRF51822 UART TX** |
| 31 | **PA10** | **USART1_RX** | AF Input | **nRF51822 UART RX** |
| 32 | PA11 | GPIO Out | PP | Power supply hold |
| 33 | PA12 | GPIO In | Pull-up | Charge detect (from buck converter) |
| 34 | **PA13** | **SWDIO** | AF | **SWD debug data (programming)** |
| 35 | VSS | Power | — | Digital ground |
| 36 | VDD | Power | — | Digital supply (3.3V) |
| 37 | **PA14** | **SWCLK** | AF | **SWD debug clock (programming)** |
| 38 | PA15 | GPIO Out | PP | Misc LED / test |
| 39 | **PB3** | **GPIO Out** | PP | **Drive mode LED** |
| 40 | **PB4** | **GPIO Out** | PP | **Sport mode LED** |
| 41 | **PB5** | **GPIO Out** | PP | **Error indicator LED** |
| 42 | **PB6** | **USART2_TX** | AF PP 50MHz | **ESC mainboard UART TX (half-duplex)** |
| 43 | **PB7** | **USART2_RX** | AF Input | **ESC mainboard UART RX (half-duplex)** |
| 44 | BOOT0 | Input | — | Boot mode select (external pull-down) |
| 45 | PB8 | GPIO Out | PP | Buzzer output |
| 46 | PB9 | GPIO Out | PP | Additional LED / test |
| 47 | VSS | Power | — | Digital ground |
| 48 | VDD | Power | — | Digital supply (3.3V) |

> **Note:** PB3-PB5 require JTAG release via AFIO remap (JTAG-DP disabled, SWD retained).
> The actual LED mapping may vary between v1.x and v2.x dashboard PCB revisions.

### Peripheral Usage Summary

| Peripheral | Configuration | Function |
|------------|--------------|----------|
| **USART1** | 115200 8N1 (BRR=0x0271 @ 72MHz) | nRF51822 BLE module communication |
| **USART2** | 115200 8N1, half-duplex | ESC mainboard communication (Ninebot protocol) |
| **ADC1** | Single-channel conversion, 12-bit | Throttle (CH0/PA0) and brake (CH1/PA1) |
| **TIM2** | Timer interrupt (referenced at 0x006BAD) | Display multiplexing / LED timing |
| **SysTick** | 1ms interrupt | System tick, debounce timers |
| **IWDG** | ~500ms timeout | Independent watchdog |

### ADC Channel Assignments

| ADC Channel | Pin | Measurement | Typical Range |
|-------------|-----|-------------|---------------|
| CH0 (PA0) | PA0 | Throttle position (hall sensor / pot) | ~400 (idle) – ~3400 (full) |
| CH1 (PA1) | PA1 | Brake lever pressure sensor | ~500 (released) – ~3200 (full) |

### UART Baud Rate Configuration (from firmware analysis)

| USART | BRR Value | Clock | Baud Rate | Occurrences |
|-------|-----------|-------|-----------|-------------|
| USART1 | 0x0271 | 72 MHz (APB2) | 115200 | 5 refs |
| USART1 | 0x1388 | 72 MHz (APB2) | 9600 | 1 ref (fallback/init?) |
| USART2 | 0x0139 | 36 MHz (APB1) | 115200 | 1 ref |
| USART2 | 0x0138 | 36 MHz (APB1) | 115200 (alt) | 3 refs |

> The 9600 baud reference suggests a brief low-speed init phase or a fallback mode.

---

## MCU 2: nRF51822-QFAA (QFN-48) — Bluetooth Low Energy SoC

The nRF51822 runs Nordic SoftDevice (S110 or S130) and acts as a transparent BLE-UART bridge.
Its firmware is typically NOT modified in custom firmware projects.

### Key Pin Assignments (estimated from Nordic reference design)

| Pin | Function | Description |
|-----|----------|-------------|
| P0.08 | UART_TX | TX to STM32 USART1 RX (PA10) |
| P0.09 | UART_RX | RX from STM32 USART1 TX (PA9) |
| P0.10 | UART_CTS | Clear-to-send (may not be connected) |
| P0.11 | UART_RTS | Ready-to-send (may not be connected) |
| P0.21 | RESET | Reset input |
| XC1/XC2 | HFCLK | 16 MHz crystal (required for BLE radio) |
| XL1/XL2 | LFCLK | 32.768 kHz crystal (RTC for sleep) |
| VDD | Power | 1.8V–3.6V supply |
| SWDIO | Debug | SWD data (separate from STM32 SWD) |
| SWDCLK | Debug | SWD clock |

### BLE Configuration

| Parameter | Value |
|-----------|-------|
| BLE Service | Nordic UART Service (NUS) |
| Service UUID | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` |
| TX Characteristic | `6E400002-...` (Phone → Scooter) |
| RX Characteristic | `6E400003-...` (Scooter → Phone) |
| SoftDevice | S110 v8.0 or S130 v2.0 |
| SoftDevice size | ~96 KB (S110) or ~108 KB (S130) |
| App region | 0x00018000 – 0x0003BFFF (S110) |
| Bootloader | 0x0003C000 – 0x0003FFFF (if DFU present) |

### nRF51822 Memory Map

| Address Range | Size | Content |
|---------------|------|---------|
| `0x00000000 – 0x00017FFF` | 96 KB | SoftDevice (S110 BLE stack) |
| `0x00018000 – 0x0003BFFF` | 144 KB | Application (BLE-UART bridge) |
| `0x0003C000 – 0x0003FBFF` | 15 KB | Bootloader (Nordic DFU, if present) |
| `0x0003FC00 – 0x0003FFFF` | 1 KB | Bootloader settings |
| `0x10001000 – 0x10001FFF` | 4 KB | UICR (User Information Configuration Registers) |

### Communication Flow

```
Phone App ←── BLE 4.0 ──→ nRF51822 ←── UART 115200 ──→ STM32 ←── UART 115200 ──→ ESC
                          (NUS)          (PA9/PA10)              (PB6/PB7)
```

## BLE Board Memory Map (STM32F103C8T6)

| Address Range | Size | Content |
|---------------|------|---------|
| `0x08000000 – 0x08000FFF` | 4 KB | **Bootloader (IAP)** |
| `0x08001000 – 0x08009FFF` | 36 KB | **Application firmware (BLE)** |
| `0x0800A000 – 0x0800EFFF` | 20 KB | **Update staging area** |
| `0x0800F000 – 0x0800F7FF` | 2 KB | **Configuration / calibration** |
| `0x0800F800 – 0x0800FFFF` | 2 KB | **Update control block** |
| `0x20000000 – 0x20004FFF` | 20 KB | **SRAM** |

## Strings Found in BLE Firmware (relevant)

From BLE_1.1.0 analysis:
- `"Ninebot-Mini0001"` — BLE advertising name template
- `"FLASH_NAME"` — Flash identifier string
- `"[E]: flash operation error, opcode:%d"` — Flash write error handler
- `"[D]: update succ. len = %d"` — Firmware update success log
- `"[D]: load succ, len = %d"` — PSM data load
- `"psm callback"` — Persistent Storage Manager
- `"decrypted auth, %x"` — **Authentication decryption** (Xiaomi MiIO)
- `"Register succ, new token, encrypt sn, beaconkey."` — **MiIO BLE registration**
- `"cloud bind succ"` — Xiaomi cloud binding
- `"mi_service, bond succ"` — MiIO service bonding
- `"login cfm succ"` / `"login cfm fail"` — Login confirmation
- `"[E]: miio ble flash register fail"` — MiIO flash registration error
- `"[W]: flash write func not setting"` — Flash write function warning

> These strings reveal the BLE firmware implements the **Xiaomi MiIO BLE protocol** for device
> authentication and cloud binding, in addition to the Ninebot serial protocol.

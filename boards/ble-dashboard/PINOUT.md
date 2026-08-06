# BLE Dashboard — nRF51822 Pinout Sheet  *(no STM32 on this board)*

> ## 🚨 **VERDICT (2026-07-26): the "MCU 1: STM32F103C8T6" section below describes a chip that is NOT ON THIS BOARD.**
> The dashboard is **nRF51822-only**. The nRF51 runs BLE, speaks the Ninebot `5A A5` protocol, *and*
> drives the display through a **TM1637** it bit-bangs on **P0.04 (DIO/CLK) / P0.05**. Proven from the
> stock dumps — see **[`MCU_IDENTIFICATION.md`](MCU_IDENTIFICATION.md)**. Treat every "STM32" row on
> this page as **void**; the outward bus facts (115200 8N1, `5A A5`, SRC `0x21`) remain true, they are
> simply produced by the **nRF51**, not by an STM32.
>
> ⚠️ **Provenance correction (2026-06-02).** The `BLE_1.1.0.bin` / `BLE_1.1.7.bin` dumps in
> `firmware/` are **nRF51822 (Cortex-M0)** application images (reset vector `0x00018154`, Nordic
> UART0 only, S110 SoftDevice, Xiaomi MiIO strings) — **not** STM32 dashboard firmware. There is
> **no STM32 dashboard dump** in this repository. The STM32 pin table below is therefore
> **reference-design / community-derived and UNVERIFIED against firmware**, not "extracted from BLE
> firmware analysis" as previously stated. The nRF51822 section (MCU 2) *is* firmware-backed.
> See [`firmware/decompiled/RE_FINDINGS.md`](../../firmware/decompiled/RE_FINDINGS.md) and
> [`Documentation/VERIFICATION_REPORT.md`](../../Documentation/VERIFICATION_REPORT.md).
>
> ✅ **Bus confirmed on the live scooter (2026-06-15).** Tapping the dashboard plug with a NUCLEO-C542RC
> (software-UART logic analyzer) confirmed the STM32's outward **Ninebot bus**: 115200 8N1, `5A A5` framing
> (LEN=payload), the dashboard transmitting as **SRC 0x21 → DST 0x20 (ESC)** — captured frame
> `5A A5 05 21 20 65 00 04 28 22 02 00 04 FF` (CK ok). The STM32↔ESC link is the dashboard's **USART2**
> (the *exact* package pin remains reference-derived without a dump, but the bus, baud, framing and role
> are now hardware-fact). Tap wiring + signal flow: [`docs/C542_PROGRAMMER_SCHEMATIC.md`](../../docs/C542_PROGRAMMER_SCHEMATIC.md);
> findings: [`C542_BUS_CAPTURE.md`](C542_BUS_CAPTURE.md).

## ~~MCU 1: STM32F103C8T6 (LQFP-48) — Main Dashboard Controller~~ ❌ **NOT PRESENT — section void**

**This chip is not on the dashboard PCB** (binary verdict, [`MCU_IDENTIFICATION.md`](MCU_IDENTIFICATION.md)).
The table below was reference-design guesswork for a hypothesised STM32 and is kept only so older
cross-references resolve. **Do not wire, flash, or design against it.** The real dashboard pin facts
live in the nRF51822 section (MCU 2) — notably **P0.04/P0.05 = TM1637 display**.

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

### UART Baud Rate Configuration

> ⚠️ **Retracted.** A previous version of this section claimed BRR `0x0271`/`0x0139` "BRR refs from
> firmware analysis." Those were **coincidental 2-byte matches inside the nRF51822 image** (which
> has no STM32 USART peripheral), not STM32 USART configuration. No STM32 baud evidence exists for
> this board. Expected values (115200 8N1: `0x0271` @72 MHz APB2, `0x0139` @36 MHz APB1) are
> standard but **unverified here**.

---

## MCU 2: nRF51822-QFAA (QFN-48) — Bluetooth Low Energy SoC

**This is the board's ONLY MCU** — it runs the Nordic SoftDevice + BLE, drives the **TM1637 display**,
and speaks the Ninebot `5A A5` protocol to the ESC. It is therefore *the* target for custom dashboard
firmware (there is no STM32 app to write). See [`MCU_IDENTIFICATION.md`](MCU_IDENTIFICATION.md).

### Key Pin Assignments

| Pin | Function | Description | Evidence |
|-----|----------|-------------|----------|
| **P0.04** | **TM1637 (CLK or DIO)** | display, bit-banged 2-wire | **firmware-confirmed** — `PIN_CNF[4]`=3 @`0x18DA6`, toggled by `tm1637_start/stop` |
| **P0.05** | **TM1637 (the other line)** | display, bit-banged 2-wire | **firmware-confirmed** — `PIN_CNF[5]`=3 (written via `PIN_CNF[4]+4`) |
| P0.00 / P0.03 / P0.08 / P0.25 | GPIO (button / LEDs / UART) | other configured pins | `PIN_CNF[0]`, `[3]`, `[8]`, `[25]` referenced in the image |
| P0.08 | UART_TX *(est.)* | Ninebot bus TX | estimated |
| P0.09 | UART_RX *(est.)* | Ninebot bus RX | estimated |
| P0.10 | UART_CTS | Clear-to-send (may not be connected) | estimated |
| P0.11 | UART_RTS | Ready-to-send (may not be connected) | estimated |
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

## Bench tooling — NUCLEO-C542RC plug tap

For dumping/RE on the bench, a **NUCLEO-C542RC** (STM32C542RCT6, Cortex-M33; on-board ST-LINK
+ Arduino Uno V3 headers) can tap the two signal wires of the dashboard's internal plug — one to
the **BT** chip (nRF51822), one to the **dashboard** STM32 — and bridge them to USB:

| Nucleo Arduino pin | STM32C542 pin | Tap | Source |
|--------------------|---------------|-----|--------|
| **A0** | **PA0** | plug wire #1 (115200 8N1) | board devicetree ([Zephyr](https://docs.zephyrproject.org/latest/boards/st/nucleo_c542rc/doc/index.html)) |
| **A2** | **PA4** | plug wire #2 (115200 8N1) | board devicetree |
| GND | GND | common ground (required) | — |

The tap firmware *finds out* which wire is which (the dashboard's internal STM32↔nRF link is a
2-wire UART: STM32 **USART1** PA9/PA10 ↔ nRF **UART0**). Procedure +
verification: [`docs/DASHBOARD_DUMP_C542.md`](../../docs/DASHBOARD_DUMP_C542.md); firmware:
[`firmware/dash-tap-c542/`](../../firmware/dash-tap-c542/).

---
applyTo: "**/*.c,**/*.h,**/*.S,**/CMakeLists.txt"
---

# Hardware Guide — Ninebot G30 Max

## RULE: Read Datasheets Before Hardware Changes

**MANDATORY:** Before modifying any GPIO assignment, peripheral configuration, or hardware interaction code, read the relevant datasheets in `boards/*/datasheets/`.

### Datasheet Locations

| Board | MCU Datasheet | Reference Manual | Additional |
|-------|--------------|-----------------|------------|
| **ESC** (now VESC) | — | — | VESC hardware docs |
| **BLE Dashboard** | `boards/ble-dashboard/datasheets/` | STM32F103x8 RM0008 | nRF51822 Product Spec |
| **BMS Battery** | `boards/bms-battery/datasheets/` | STM32F103x8 RM0008 | BQ76940 datasheet |

---

## RULE: Update PINOUT.md on Pin Changes

When any pin assignment changes, update the corresponding `boards/*/PINOUT.md` file. The PINOUT files document:

1. Pin function table (pin name → function → peripheral → notes)
2. UART assignments (which USART connects to which bus)
3. I2C/SPI assignments
4. ADC channel mappings
5. GPIO assignments (buttons, LEDs, power control)

---

## Board Hardware Summary

### BLE Dashboard

| Item | Details |
|------|---------|
| **Main MCU** | STM32F103C8T6 (64 KB flash, 20 KB SRAM, 72 MHz) |
| **BLE SoC** | nRF51822 (256 KB flash, 16 KB SRAM, Cortex-M0) |
| **Display** | 7-segment or LCD dashboard (model dependent) |
| **Sensors** | Throttle ADC, brake lever, ambient light |
| **UART1** | STM32 ↔ nRF51822 (internal bridge) |
| **UART2** | STM32 ↔ VESC/ESC (external bus) |
| **Power** | 36V input → 5V/3.3V regulators |
| **SWD** | Pads on PCB back (SWDIO, SWCLK, GND, 3V3) |

### BMS Battery

| Item | Details |
|------|---------|
| **Main MCU** | STM32F103C8T6 (64 KB flash, 20 KB SRAM, 72 MHz) |
| **AFE** | BQ76940 (analog front-end for cell monitoring) |
| **Cells** | 10S3P Li-ion (36V nominal, 42V max, 30V min) |
| **Sensors** | 2× NTC temperature, cell voltages via BQ76940 I2C |
| **UART** | PA2/PA3 (USART2) → VESC/ESC bus |
| **I2C** | PB6/PB7 → BQ76940 |
| **Protection** | Hardware overcurrent, overvoltage, undervoltage via BQ76940 |

### VESC (Replaces ESC)

| Item | Details |
|------|---------|
| **Controller** | VESC 4.x / 6.x / compatible |
| **MCU** | STM32F4xx (VESC firmware) |
| **Motor** | 350W BLDC hub motor, Hall sensors |
| **Communication** | USB + UART + CAN |
| **UART** | Connects to BLE and BMS via Ninebot bus |
| **Firmware** | Standard VESC firmware (not custom) |

---

## RULE: UART Bus Wiring

The three boards communicate via UART at 115200 8N1:

```
VESC (ESC replacement)
  ├── UART TX/RX ──► BLE Dashboard STM32 (USART2: PB6/PB7 or PA2/PA3)
  └── UART TX/RX ──► BMS STM32 (USART2: PA2/PA3)

BLE Dashboard internal:
  STM32 USART1 (PA9/PA10) ◄──► nRF51822 UART0
```

**Protocol:** Ninebot protocol (5A A5 header) for runtime communication.
**Update:** XMODEM-CRC for firmware updates (via VESC passthrough).

---

## RULE: Power Considerations

- **BMS board is always energized** by the battery — disconnect battery before SWD work
- VESC is powered from the battery pack (36V)
- BLE dashboard is powered from VESC/ESC 5V output
- Never short 36V to 3.3V logic — this will destroy the MCU
- Always verify voltage levels before connecting UART adapters (must be 3.3V TTL)

---

## RULE: Component Variants

Some boards have alternate components — code must handle both:

| Component | Variant A | Variant B | Notes |
|-----------|-----------|-----------|-------|
| BLE/BMS MCU | STM32F103 | GD32F103 | Pin and binary compatible |
| nRF51 BLE | nRF51822 | nRF51802 | Same silicon, cost-reduced |
| ESC buck converter | TPS54160 (v1.5-) | SY8502FCC (v2.1+) | Irrelevant with VESC |

---
description: Gate for GPIO/peripheral/pin changes — datasheets first, then PINOUT
---

Before changing any GPIO assignment, peripheral config, or hardware-interaction code:

1. **Read the relevant datasheet(s)** in `boards/*/datasheets/` (STM32F103 RM0008, nRF51822 spec,
   BQ76940, gate driver/MOSFET as applicable). Reference: `docs/guides/HARDWARE.md`.
2. Make the change with correct AF/mode/clock-enable.
3. **Update the affected `boards/*/PINOUT.md`**: pin-function table, UART/I2C/SPI/ADC/GPIO assignments.
4. If a UART/protocol detail changes, mirror it across all three boards (ESC↔VESC, BLE, BMS).
5. Cite firmware evidence where possible (`/verify-firmware`); if a claim is design-derived/unverified,
   say so explicitly (don't repeat the old "extracted from firmware" mistake — see VERIFICATION_REPORT.md).

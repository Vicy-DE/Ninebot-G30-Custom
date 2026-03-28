---
applyTo: "**/*.c,**/*.h,**/*.S,**/CMakeLists.txt"
---

# Project Documentation — Instructions for Copilot

## When to execute

**Step 4 of the workflow — after a successful debug session, run after CHANGE_DOC.**

## Target file

`Documentation/PROJECT_DOC.md`
Update relevant sections in-place. Create the file if it does not exist.

---

## Document structure (keep this structure)

```markdown
# Project Documentation — Ninebot G30 Max Custom Firmware

**Last updated:** YYYY-MM-DD
**Toolchain:** arm-none-eabi-gcc
**Targets:** STM32F103C8T6 (BLE, BMS), nRF51822 (BLE Bluetooth)

---

## 1. Project Overview
<Custom firmware for Ninebot G30 Max with VESC motor control, stock BLE/BMS boards.>

## 2. Hardware Platform
<Three boards, VESC replaces ESC, MCU specs, UART bus.>

## 3. Software Architecture
<Bootloader, application firmware, protocol layers, signing.>

## 4. Key Modules

| Module / Component | Responsibility |
|---|---|
| `bootloader/stm32/` | STM32F103 secure bootloader (BLE + BMS) |
| `bootloader/nrf51/` | nRF51822 secure bootloader |
| `bootloader/common/` | Shared crypto (ECDSA, SHA-256, CRC, XMODEM) |
| `firmware/decompiled/ble/` | Custom BLE application firmware |
| `firmware/decompiled/bms/` | Custom BMS application firmware |
| `lib/ninebot-protocol/` | Ninebot UART protocol library |
| `tools/flasher/` | Flashing tools (IAP, XMODEM) |
| `tools/signing/` | Firmware signing (ECDSA-P256) |
| ... | ... |

## 5. Build System
<CMake cross-compilation, arm-none-eabi-gcc, per-target builds.>

## 6. Flashing & Debug Toolchain
<UART IAP, XMODEM, SWD recovery, serial monitor.>

## 7. Deployment Strategy
<Phase 0-5 incremental deployment via OTA → app area → bootloader area.>

## 8. Known Limitations / Open Issues
<Current bugs, unverified assumptions, hardware quirks.>

## 9. Revision History
| Date | Summary |
|---|---|
| YYYY-MM-DD | Initial documentation |
```

---

## Rules

- **MUST** update the "Last updated" date on every edit.
- **MUST** update section 4 (Key Modules) whenever files are added, removed, or responsibilities change.
- **MUST** update section 8 (Known Limitations) to reflect resolved issues and newly discovered ones.
- **MUST** add a one-line entry to section 9 (Revision History) for each update session.
- **MUST NOT** duplicate information already in CHANGE_LOG.md — link to it instead.

---
applyTo: "**/*.c,**/*.h,**/*.S,**/*.s,**/CMakeLists.txt,**/*.cmake,**/*.ld,**/*.bat,**/*.ps1,**/*.py,**/*.sh,**/Makefile"
---

# Ninebot G30 Max Custom — Copilot Instructions

## After Every Code Change — MANDATORY

1. **Build** → `cmake --build bootloader/build/<target>` or board-specific Makefile — fix errors before continuing. [BUILD](BUILD/BUILD_README.instructions.md)
2. **Flash** → deploy via stock IAP/OTA or XMODEM over UART. [DEBUG](DEBUG/DEBUG_README.instructions.md)
3. **Verify** → UART monitor at 115200 8N1: confirm boot messages, protocol responses, sensor data. [DEBUG §6](DEBUG/DEBUG_README.instructions.md)
4. **Document** → append `Documentation/CHANGE_LOG.md` + update `Documentation/PROJECT_DOC.md`. [CHANGE_DOC](documentation/CHANGE_DOC.instructions.md) · [PROJECT_DOC](documentation/PROJECT_DOC.instructions.md)
5. **Test** → generate tests, run on hardware, save report in `Documentation/Tests/`. [TEST_DOC](documentation/TEST_DOC.instructions.md)
6. **Commit** → stage relevant files, write Conventional Commit message — **never push**. [COMMIT](documentation/COMMIT.instructions.md)

## Before New Features

1. Update `Documentation/Requirements/requirements.md` → [REQUIREMENTS_DOC](documentation/REQUIREMENTS_DOC.instructions.md)
2. Create `Documentation/ToDo/<feature>.md` → [TODO_DOC](documentation/TODO_DOC.instructions.md)

## On Hardware / Pin Changes

1. Read relevant datasheets in `boards/*/datasheets/` before touching GPIO or peripheral config.
2. Update `boards/*/PINOUT.md` for every affected board.
3. Mirror UART protocol changes across all three boards (ESC→VESC, BLE, BMS).
4. [HARDWARE](HARDWARE/HARDWARE.instructions.md)

## Rules

- Scripts: test/debug in `tools/flasher/`, analysis in `tools/analysis/`, signing in `tools/signing/`. [SCRIPTS](CODING/SCRIPTS.instructions.md)
- Coding: [COMMENTS](CODING/COMMENTS.instructions.md)
- Hardware: [HARDWARE](HARDWARE/HARDWARE.instructions.md) — consult `boards/*/datasheets/`, update `PINOUT.md`.
- Cross-board: bootloader in `bootloader/` compiles for STM32 (BLE) and nRF51. Use `#if TARGET_BOARD == BOARD_*` for board-specific code.
- **Deployment order**: OTA first → verify → custom bootloader to app area → verify → bootloader to bootloader area. [DEPLOYMENT](DEPLOYMENT/DEPLOYMENT_STRATEGY.instructions.md)
- Toolchain: `arm-none-eabi-gcc` for STM32F103 (Cortex-M3), `arm-none-eabi-gcc` for nRF51822 (Cortex-M0).
- Flash via UART using Ninebot protocol IAP or XMODEM — SWD/ST-Link only for initial bootstrap or recovery.
- VESC replaces the ESC motor controller — no custom ESC firmware needed.
- BLE board keeps stock hardware with custom firmware. BMS stays stock (no custom BMS firmware).
- Speed cap removal and VESC app integration happen in BLE firmware.
- Daly BMS compatibility: firmware must also support Daly BMS protocol as an alternative to stock BMS.

---
description: Build a bootloader/firmware target with CMake and fix all errors
argument-hint: "[ble|bms|nrf51|firmware]"
---

Build the requested target (default `ble` if `$ARGUMENTS` is empty). Reference: `docs/guides/BUILD.md`.

1. Configure once if the build dir is missing:
   `cmake -B bootloader/build/<t> -S bootloader -G Ninja -DTARGET_BOARD=BOARD_<T>_STM32` (or `BOARD_NRF51`).
   For app firmware: `cmake -B firmware/decompiled/build -S firmware/decompiled -G Ninja`.
2. Build: `cmake --build bootloader/build/<t>` (or the firmware build dir).
3. **Fix every error and warning before continuing.** Do not proceed to flashing with a failing build.
4. Report the build output path and pass/fail. Never commit build artifacts (`bootloader/build/`, `*.elf/*.bin/*.map`).

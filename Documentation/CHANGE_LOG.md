# Change Log — Ninebot G30 Max Custom Firmware

## [2026-03-29] Bootloader Platform Abstraction & Scope Refinement

### What was changed
- `bootloader/common/include/platform.h` — Created platform abstraction interface (init, UART, flash, jump, update trigger, watchdog, board ID)
- `bootloader/common/src/bootloader.c` — Created shared bootloader logic (replaces both stm32/nrf51 bootloader_main.c)
- `bootloader/stm32/src/platform_stm32.c` — Created STM32F103 platform implementation
- `bootloader/nrf51/src/platform_nrf51.c` — Created nRF51822 platform implementation
- `bootloader/CMakeLists.txt` — Updated to use bootloader.c + platform files instead of bootloader_main.c
- `vesc-lisp/README.md` — Created VESC Lisp project overview and protocol documentation
- `vesc-lisp/g30_dash.lisp` — Created VESC Lisp script for G30 dashboard integration
- `.github/instructions/HARDWARE/USB_UART_WIRING.instructions.md` — Created USB-UART wiring guide for BLE dashboard development
- `.github/hooks/read-instructions-on-compact.json` — Created PreCompact hook for instruction persistence
- `Documentation/Requirements/requirements.md` — Struck through Req 9 (BMS custom firmware), added Req 13 (Daly BMS compatibility), added Req 14 (VESC Lisp motor control)
- `Documentation/PROJECT_DOC.md` — Updated for BMS scope removal, added vesc-lisp module, added Daly BMS mention
- `.github/copilot-instructions.md` — Updated project description (VESC Lisp, Daly BMS, BMS stock), removed BMS Phase 5
- `.github/instructions/index.instructions.md` — Removed BMS custom firmware references
- `.github/instructions/DEPLOYMENT/DEPLOYMENT_STRATEGY.instructions.md` — Struck through BMS Phase 5

### Why it was changed
1. **Bootloader restructuring**: Both STM32 and nRF51 bootloader_main.c had near-identical logic (XMODEM receive, .sfw validation, ECDSA verify, flash, reboot). The RC-Servo bootloader's platform.h pattern eliminates this duplication — shared logic in bootloader.c, platform-specific code in platform_*.c.
2. **BMS scope removal**: Custom BMS firmware adds unnecessary risk to battery safety and the stock BMS protocol works fine. The BLE firmware supports the stock Ninebot BMS protocol natively.
3. **VESC Lisp project**: The VESC needs a Lisp script to bridge the G30 dashboard protocol (frame 0x64/0x65) to motor control (app-adc-override). Based on CRZX1337/g30-vesc-dash.
4. **Daly BMS compatibility**: Add support for Daly BMS as an alternative battery management system via its UART protocol (0xA5 header, commands 0x90–0x98).

### What it does / expected behaviour
- All three bootloader targets (BLE, BMS, nRF51) build from shared bootloader.c with platform-specific implementations
- BLE bootloader: 6,336 bytes text — fits in 16 KB
- BMS bootloader: 6,264 bytes text — fits in 16 KB
- nRF51 bootloader: 6,728 bytes text — fits in 16 KB
- VESC Lisp script documents the complete Ninebot dashboard protocol interface
- USB-UART wiring guide covers three connection methods (VESC USB, direct tap, SWD)

### Verified
- Build: OK — all three bootloader targets compile without errors or warnings
- Flash: N/A (no hardware testing yet)
- UART Monitor: N/A
- Functional: N/A

## [2026-03-28] Project Conversion — Documentation to Software Development

### What was changed
- `.github/instructions/` — Created full instruction framework (index, BUILD, DEBUG, DEPLOYMENT, CODING, HARDWARE, documentation)
- `.github/copilot-instructions.md` — Updated from documentation project to software development project
- `Documentation/PROJECT_DOC.md` — Created project documentation
- `Documentation/CHANGE_LOG.md` — Created change log (this file)
- `Documentation/Requirements/requirements.md` — Created initial requirements
- `.vscode/settings.json` — Created VS Code workspace configuration
- `.vscode/tasks.json` — Created build/flash/monitor tasks

### Why it was changed
Project converted from a hardware documentation / reverse engineering knowledge base into an active software development project. The new design replaces the stock ESC with a VESC motor controller, keeps stock BLE and BMS hardware with custom firmware, and establishes a safe incremental deployment strategy using UART-based flashing.

### What it does / expected behaviour
The project now has:
- A complete workflow instruction set (adapted from Bobbycar-Steering project)
- Build → Flash → Verify → Document → Test → Commit workflow for every code change
- UART-based debug workflow using the scooter's internal bus
- 6-phase deployment strategy (backup → OTA app → bootloader-as-app → final bootloader → nRF51 → BMS)
- Requirements tracking, ToDo management, test documentation

### Verified
- Build: N/A (structural change, no code changes)
- Flash: N/A
- UART Monitor: N/A
- Functional: N/A

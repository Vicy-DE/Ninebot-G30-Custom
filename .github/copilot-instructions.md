# Copilot Instructions — Ninebot G30 Max Custom Project

## Project Overview

This is a **custom firmware development and reverse engineering project** for the **Ninebot G30 Max** electric scooter. The stock ESC motor controller is replaced with a **VESC** (Benjamin Vedder's ESC). The BLE dashboard and BMS battery boards retain their original hardware but receive custom firmware to provide the original feature set, expose the VESC App over BLE, and remove the speed cap.

The project includes hardware documentation, datasheets, stock firmware dumps, a secure custom bootloader (ECDSA-P256 signed firmware), incrementally deployable custom firmware with a UART-based debug workflow, and a VESC Lisp project for the scooter's motor control script.

## Architecture

The Ninebot G30 Max consists of three main circuit boards that communicate via UART using the Ninebot protocol (5A A5 header, 115200 8N1):

```
Phone App ←BLE→ [BLE Dashboard] ←UART→ [ESC Motor Controller] ←UART→ [BMS Battery Management]
                  (nRF51822 +            (STM32F103CBT6)              (STM32F103C8T6 +
                   STM32F103C8T6)                                      BQ76940)
```

### Board Summary

| Board | Firmware ID | Main MCU | Flash | Purpose |
|---|---|---|---|---|
| ESC | DRV | **VESC** (replaces stock) | — | Motor control (FOC) |
| BLE | BLE | STM32F103C8T6 + nRF51822 | 64 KB + 256 KB | Display, throttle, Bluetooth |
| BMS | BMS | STM32F103C8T6 + BQ76940 | 64 KB | Battery cell management (stock firmware) |

## Folder Structure

```
Ninebot-G30-Custom/
├── README.md                          # Main project overview
├── .github/
│   ├── copilot-instructions.md        # Copilot context (this file)
│   └── instructions/                  # Detailed workflow instructions
│       ├── index.instructions.md      # Master workflow (mandatory 6-step)
│       ├── BUILD/                     # Build guide (CMake, arm-none-eabi-gcc)
│       ├── DEBUG/                     # Debug guide (UART, SWD, monitoring)
│       ├── DEPLOYMENT/               # Deployment strategy (6 phases)
│       ├── CODING/                   # Coding conventions (comments, scripts)
│       ├── HARDWARE/                 # Hardware guide (datasheets, pinouts)
│       └── documentation/            # Doc workflow (changelog, commit, etc.)
├── Documentation/                     # Active development documentation
│   ├── PROJECT_DOC.md                 # Living project documentation
│   ├── CHANGE_LOG.md                  # Change history (newest first)
│   ├── Requirements/                  # Feature requirements & traceability
│   ├── ToDo/                          # Per-feature task tracking
│   └── Tests/                         # Test reports
├── boards/                            # Per-board hardware documentation
│   ├── ble-dashboard/                 # BLE dashboard board
│   ├── bms-battery/                   # Battery management board
│   └── esc-motor/                     # ESC motor controller board (stock ref)
├── bootloader/                        # Custom secure bootloader (16 KB)
│   ├── README.md                      # Architecture & concept doc
│   ├── CMakeLists.txt                 # CMake cross-compilation build
│   ├── cmake/                         # Toolchain files (Cortex-M3, Cortex-M0)
│   ├── common/                        # Shared code (SHA-256, ECDSA, XMODEM, CRC-32)
│   ├── stm32/                         # STM32F103 bootloader (BLE + BMS boards)
│   └── nrf51/                         # nRF51822 bootloader
├── docs/                              # Protocol & reverse engineering reference
│   ├── protocol.md                    # Ninebot UART protocol reference
│   ├── iap-update-protocol.md         # Stock IAP update protocol
│   ├── firmware-flashing.md           # Flashing guide
│   └── resources.md                   # Community links and resources
├── tools/                             # PC-side tools
│   ├── signing/                       # Firmware signing (ECDSA-P256)
│   ├── flasher/                       # Flash tools (IAP, XMODEM, initial flash)
│   └── analysis/                      # Firmware analysis scripts
├── firmware/                          # Firmware source & reconstruction
│   ├── decompiled/                    # Decompiled/reconstructed firmware (active dev)
│   └── rebuild/                       # Firmware rebuild from disassembly
├── vesc-lisp/                         # VESC Lisp scripts for motor control
│   ├── README.md                      # VESC Lisp project overview
│   └── g30_dash.lisp                  # G30 dashboard integration script
├── lib/                               # Libraries
│   └── ninebot-protocol/              # Ninebot protocol C++ implementation
└── Target/                            # Hardware test/debug scripts
```

## Key Conventions

### Firmware Naming
- **DRV** = ESC motor controller firmware (e.g., DRV126 = version 1.2.6)
- **BLE** = Dashboard/Bluetooth firmware (e.g., BLE107 = version 1.0.7)
- **BMS** = Battery management firmware (e.g., BMS134 = version 1.3.4)
- File format: `{TYPE}_{version}.bin` (plain) or `{TYPE}_{version}.bin.enc` (encrypted)

### Board Naming
- **ESC** = Electronic Speed Controller (motor controller board)
- **BLE** = Bluetooth Low Energy dashboard (front display board)
- **BMS** = Battery Management System (inside battery compartment)

### Protocol Addresses
- `0x20` = ESC, `0x21` = BLE, `0x22` = BMS, `0x3E` = App, `0x3F` = PC

### Component Variants
- Some boards use **GD32F103** (GigaDevice) instead of STM32F103 — pin and binary compatible
- ESC v1.5 and below use **TPS54160** buck converter; v2.1+ use **SY8502FCC**
- The nRF51822 may be labeled as **nRF51802** (cost-reduced variant, same silicon)

## Development Workflow — MANDATORY After Every Code Change

1. **Build** → `cmake --build bootloader/build/<target>` — fix errors before continuing
2. **Flash** → `python tools/flasher/ninebot_flasher.py` (stock IAP) or `python tools/flasher/xmodem_send.py` (custom bootloader)
3. **Verify** → UART monitor at 115200 8N1: check boot messages, protocol responses
4. **Document** → Update `Documentation/CHANGE_LOG.md` + `Documentation/PROJECT_DOC.md`
5. **Test** → Run tests, save report in `Documentation/Tests/`
6. **Commit** → Conventional Commits — **never push**

See `.github/instructions/index.instructions.md` for the full workflow and all linked instruction files.

## Deployment Strategy (6 Phases)

| Phase | Action | Target | Reversible |
|-------|--------|--------|------------|
| 0 | Backup + tooling + VESC install | All | Yes |
| 1 | Custom app via stock IAP (0x08001000) | BLE STM32 | Yes — reflash stock |
| 2 | Custom bootloader as app (0x08001000) | BLE STM32 | Yes — reflash stock |
| 3 | Custom bootloader at 0x08000000 (final) | BLE STM32 | SWD only |
| 4 | nRF51822 BLE firmware (VESC App) | nRF51822 | Via bootloader |

BMS custom firmware is out of scope — the stock BMS works fine. The BLE firmware supports the stock Ninebot BMS protocol and optionally a Daly BMS.

See `.github/instructions/DEPLOYMENT/DEPLOYMENT_STRATEGY.instructions.md` for full details.

## UART Debug Workflow

The primary debug interface is the scooter's internal UART bus at 115200 8N1:
- **Via VESC USB**: easiest — VESC passthrough mode to reach BLE/BMS
- **Direct tap**: USB-UART adapter (3.3V TTL) on ESC↔BLE or ESC↔BMS UART lines
- **Protocol**: Ninebot protocol (5A A5 header) for runtime communication
- **Updates**: XMODEM-CRC for firmware updates via custom bootloader
- **Debug output**: `[BOOT]` prefixed messages from bootloader, `[DBG]` from application

## When Answering Questions About This Project

1. **Hardware questions**: Refer to the respective `boards/*/README.md` files for component details, pinouts, and MCU specifications.
2. **Protocol questions**: Refer to `docs/protocol.md` for packet format, register maps, and checksum calculation.
3. **Firmware flashing**: Refer to `docs/firmware-flashing.md` for all flashing methods.
4. **Community tools**: Refer to `docs/resources.md` for external links and tools.
5. **Datasheets**: Located in `boards/*/datasheets/` folders as PDF files.
6. **Stock firmware**: Located in `boards/*/firmware/` folders as `.bin` files.
7. **Custom bootloader**: Refer to `bootloader/README.md` for the secure bootloader architecture.
8. **Signing tools**: Located in `tools/signing/` (generate_keys, sign_firmware, verify_firmware).
9. **Flash tools**: Located in `tools/flasher/` (initial_flash, update_bootloader, xmodem_send).

## Technical Context for Code Generation

If asked to write code related to this project, consider:

- **Protocol parsing**: Use the Ninebot protocol format (5A A5 header, checksum = XOR 0xFFFF of sum from length through payload)
- **Language**: Python is commonly used for protocol tools; C/C++ for embedded firmware
- **Target MCU**: ARM Cortex-M3 (STM32F103) for BLE board, compiled with arm-none-eabi-gcc; ARM Cortex-M0 (nRF51822) for BLE Bluetooth
- **Endianness**: Little-endian for all multi-byte values
- **Firmware addresses**: Custom bootloader: 16 KB at `0x08000000`, application at `0x08004000`. Stock bootloader: 4 KB at `0x08000000`, application at `0x08001000`.
- **Register access**: I2C for BQ769x0 AFE, UART for inter-board communication
- **BLE stack**: Nordic SoftDevice (S110 or S130) on nRF51822

## Safety Notes

- BMS modifications can cause battery fires — always maintain protection circuits
- Never bypass undervoltage or overcurrent protection
- The battery pack stores 551 Wh of energy — handle with care
- BMS firmware stays stock — do NOT flash custom firmware to the BMS board
- Verify firmware checksums before flashing
- Keep stock firmware backups before any modification

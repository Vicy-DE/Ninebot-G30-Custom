# Copilot Instructions — Ninebot G30 Max Custom Project

## Project Overview

This is a hardware documentation and firmware research project for the **Ninebot G30 Max** electric scooter. The project catalogs the scooter's three main PCBs (ESC, BLE, BMS), their microcontrollers, peripheral components, stock firmware binaries, datasheets, and communication protocol documentation.

**This is NOT a software development project** — it is a structured knowledge base for reverse engineering, custom firmware development, and hardware modification of the Ninebot G30 Max.

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
| ESC | DRV | STM32F103CBT6 (or GD32F103CBT6) | 128 KB | Motor control, central hub |
| BLE | BLE | STM32F103C8T6 + nRF51822 | 64 KB + 256 KB | Display, throttle, Bluetooth |
| BMS | BMS | STM32F103C8T6 + BQ76940 | 64 KB | Battery cell management |

## Folder Structure

```
Ninebot-G30-Custom/
├── README.md                          # Main project overview
├── boards/                            # Per-board hardware documentation
│   ├── ble-dashboard/                 # BLE dashboard board
│   │   ├── README.md                  # BLE hardware documentation
│   │   ├── PINOUT.md                  # Pin assignments
│   │   ├── datasheets/                # Component datasheets (PDFs)
│   │   └── firmware/                  # Stock firmware binaries (.bin)
│   ├── bms-battery/                   # Battery management board
│   │   ├── README.md                  # BMS hardware documentation
│   │   ├── PINOUT.md
│   │   ├── datasheets/
│   │   └── firmware/
│   └── esc-motor/                     # ESC motor controller board
│       ├── README.md
│       ├── PINOUT.md
│       ├── datasheets/
│       └── firmware/
├── bootloader/                        # Custom secure bootloader (16 KB)
│   ├── README.md                      # Architecture & concept doc
│   ├── CMakeLists.txt                 # CMake cross-compilation build
│   ├── cmake/                         # Toolchain files
│   ├── common/                        # Shared code (SHA-256, ECDSA, XMODEM, CRC-32)
│   ├── stm32/                         # STM32F103 bootloader (BLE + BMS boards)
│   └── nrf51/                         # nRF51822 bootloader
├── docs/                              # Cross-cutting documentation
│   ├── protocol.md                    # Ninebot UART protocol reference
│   ├── iap-update-protocol.md         # Stock IAP update protocol
│   ├── firmware-flashing.md           # Flashing guide
│   └── resources.md                   # Community links and resources
├── tools/                             # PC-side tools
│   ├── signing/                       # Firmware signing (ECDSA-P256)
│   ├── flasher/                       # Flash tools (IAP, XMODEM, initial flash)
│   └── analysis/                      # Firmware analysis scripts
├── firmware/                          # Firmware analysis & reconstruction
│   ├── decompiled/                    # Decompiled/reconstructed firmware
│   └── rebuild/                       # Firmware rebuild from disassembly
└── lib/                               # Libraries
    └── ninebot-protocol/              # Ninebot protocol C++ implementation
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
- **Target MCU**: ARM Cortex-M3 (STM32F103), compiled with arm-none-eabi-gcc
- **Endianness**: Little-endian for all multi-byte values
- **Firmware addresses**: Custom bootloader: 16 KB at `0x08000000`, application at `0x08004000`. Stock bootloader: 4 KB at `0x08000000`, application at `0x08001000`.
- **Register access**: I2C for BQ769x0 AFE, UART for inter-board communication
- **BLE stack**: Nordic SoftDevice (S110 or S130) on nRF51822

## Safety Notes

- BMS modifications can cause battery fires — always maintain protection circuits
- Never bypass undervoltage or overcurrent protection
- The battery pack stores 551 Wh of energy — handle with care
- Verify firmware checksums before flashing
- Keep stock firmware backups before any modification

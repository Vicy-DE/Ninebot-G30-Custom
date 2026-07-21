# Ninebot G30 Max - Custom Firmware & Hardware Documentation

This repository contains hardware documentation, datasheets, firmware dumps, and research for the **Segway Ninebot Max G30** electric scooter platform.

## Scooter Overview

| Spec | Value |
|---|---|
| **Model** | Ninebot Max G30 / G30P / G30D / G30LP |
| **Motor** | 350W nominal / 700W peak, rear hub BLDC |
| **Battery** | 36V 15.3Ah (551Wh), 10S6P Li-ion (60× 18650) |
| **Max Speed** | 25-30 km/h (region dependent) |
| **Range** | ~65 km |
| **Tires** | 10" pneumatic (front and rear) |
| **Brakes** | Electronic rear (regenerative) + mechanical drum front |
| **Communication** | Bluetooth Low Energy (BLE 4.0) |
| **Protocol** | Ninebot UART protocol (5A A5 header) |

## PCB Architecture

The G30 Max contains **3 main PCBs**, each with its own microcontroller and firmware:

```
Phone App
   │ (Bluetooth LE)
   ▼
┌──────────────────┐
│  BLE Dashboard   │ ◄── Throttle, Brake, Display, Headlight
│  (Head Unit)     │
│  STM32 + nRF51   │
└────────┬─────────┘
         │ Half-duplex UART 115200 8N1
         ▼
┌──────────────────┐
│  ESC Controller  │ ◄── Motor phases, Hall sensors, Power stage
│  (Motor Driver)  │
│  STM32F103CBT6   │
└────────┬─────────┘
         │ Full-duplex UART 115200 8N1
         ▼
┌──────────────────┐
│  BMS Battery     │ ◄── Cell monitoring, balancing, protection
│  Management      │
│  STM32 + BQ769x0 │
└──────────────────┘
```

## Project Structure

```
Ninebot-G30-Custom/
├── CLAUDE.md                   # Agent operating manual (Claude Code)
├── boards/                     # Per-board hardware docs, datasheets, stock dumps
│   ├── esc-motor/              #   ESC/DRV motor controller (STM32F103CBT6)
│   ├── ble-dashboard/          #   BLE dashboard (STM32F103C8T6 + nRF51822)
│   └── bms-battery/            #   Battery management (STM32F103C8T6 + BQ76940)
│       ├── datasheets/         #   MCU / AFE / power-stage datasheets (PDF)
│       ├── firmware/           #   Stock firmware dumps (.bin)
│       ├── PINOUT.md           #   Pin assignment sheet
│       └── README.md           #   Board hardware documentation
├── docs/                       # Protocol & reverse-engineering reference
│   ├── protocol.md             #   Ninebot UART protocol reference
│   ├── iap-update-protocol.md  #   Stock IAP update protocol
│   ├── firmware-flashing.md    #   Flashing guide
│   ├── resources.md            #   Community links and tools
│   ├── guides/                 #   BUILD / DEBUG / DEPLOYMENT / HARDWARE / CODING guides
│   └── VESC_INSTALL_GUIDE.md   #   VESC install + power-management docs
├── bootloader/                 # Custom secure bootloader (STM32 + nRF51)
├── firmware/                   # Decompiled/reconstructed firmware + RE_FINDINGS.md
├── lib/ninebot-protocol/       # Ninebot protocol C++ library
├── tools/                      # PC-side tools (signing / flasher / analysis / vesc)
├── vesc-lisp/                  # VESC Lisp motor-control scripts
├── Target/                     # Hardware integration/test scripts
├── Documentation/              # Living docs: PROJECT_DOC, CHANGE_LOG, Requirements, ToDo, Tests
├── .claude/                    # Claude Code config (commands, agents, settings, hooks)
└── README.md                   # This file
```

> **Firmware-dump note:** the `boards/ble-dashboard/firmware/BLE_*.bin` files are **nRF51822**
> (Cortex-M0) BLE-SoC images, *not* STM32 dashboard firmware. `BMS_1.3.4.bin` is encrypted.
> See [`Documentation/VERIFICATION_REPORT.md`](Documentation/VERIFICATION_REPORT.md) and
> [`firmware/decompiled/RE_FINDINGS.md`](firmware/decompiled/RE_FINDINGS.md).

## Key Resources

- **Custom Firmware Generator**: https://max.cfw.sh/
- **Stock Firmware Repository**: https://firmware.scooterhacking.org/max/
- **ScooterHacking Wiki**: https://wiki.scooterhacking.org/
- **Protocol Documentation**: https://github.com/etransport/ninebot-docs/wiki/protocol
- **ScooterHacking Utility (Android)**: https://scooterhack.in/utility
- **IAP (Windows Flasher)**: https://scooterhack.in/iapce
- **Firmware Encryption Tool**: https://tools.scooterhacking.org/xiaotea/
- **ScooterHacking Discord**: https://scooterhack.in/discord

## Firmware Versions Available

### DRV (Motor Controller / ESC)
| Version | Notes |
|---|---|
| 1.2.6 | Base firmware, aggressive acceleration, most CFW compatible |
| 1.4.5 | Intermediate update |
| 1.5.1 / 1.5.4 | Minor updates |
| 1.6.0 / 1.6.3 | Later updates, SN changing disabled in DRV145+ |
| 1.6.13 (Compat) | Latest compatible firmware |
| 1.8.11 (Compat) | Newest available |

### BLE (Dashboard / Bluetooth)
| Version | Notes |
|---|---|
| 1.1.0 | Original firmware |
| 1.1.3 / 1.1.4 | Updates |
| 1.1.7 | Latest, "BLE555" variant difficult to downgrade |
| 1.1.7 (Compat) | Compatibility version |

### BMS (Battery Management)
| Version | Notes |
|---|---|
| 1.3.4 | Original firmware |
| 1.5.3 / 1.5.5 / 1.5.6 / 1.5.8 | Updates |
| 1.7.4.5 | Latest firmware |

## Legal Disclaimer

This repository is for **educational and research purposes only**. Modifying your scooter's firmware may void your warranty, violate local laws, and could potentially damage your hardware. Always check local regulations before modifying your electric scooter. The authors are not responsible for any damage or legal consequences resulting from the use of this information.

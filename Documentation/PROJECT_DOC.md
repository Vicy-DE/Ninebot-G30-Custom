# Project Documentation — Ninebot G30 Max Custom Firmware

**Last updated:** 2026-06-02
**Toolchain:** arm-none-eabi-gcc (Cortex-M3 / Cortex-M0)
**Targets:** STM32F103C8T6 (BLE, BMS), nRF51822 (BLE Bluetooth)

---

## 1. Project Overview

Custom firmware project for the **Ninebot G30 Max** electric scooter. The stock ESC motor controller is replaced with a **VESC** (Benjamin Vedder's ESC). The BLE dashboard and BMS battery boards retain their stock hardware but receive custom firmware to:

- Provide the original feature set (display, throttle, brake, lights, BLE connectivity)
- Expose the VESC App over BLE (via nRF51822) for configuration with VESC Tool mobile
- Remove the speed cap
- Add VESC motor control integration (FOC, configurable parameters)
- Maintain stock BMS battery protection with enhanced monitoring

## 2. Hardware Platform

| Board | MCU | Flash | Purpose | Firmware |
|-------|-----|-------|---------|----------|
| **BLE Dashboard** | STM32F103C8T6 + nRF51822 | 64 KB + 256 KB | Display, throttle, BLE | Custom |
| **BMS Battery** | STM32F103C8T6 + BQ76940 | 64 KB | Cell monitoring, protection | Stock (no custom FW) |
| **ESC Motor** | **VESC** (replaces stock) | — | Motor control (FOC) | VESC firmware |

**Communication:** All boards connected via UART at 115200 8N1 using the Ninebot protocol (5A A5 header).

**Battery:** 36V 15.3Ah (551 Wh), 10S3P Li-ion — 350W nominal / 700W peak motor.

## 3. Software Architecture

```
┌─────────────────── Secure Bootloader (16 KB) ──────────────────┐
│  ECDSA-P256 signature verify │ SHA-256 │ XMODEM-CRC │ Flash   │
└─────────────────────────────┬──────────────────────────────────┘
                              │ Jumps to app if signature valid
┌─────────────────── Application Firmware (46 KB) ───────────────┐
│  Ninebot Protocol │ VESC UART Bridge │ Throttle/Brake │ Display│
└────────────────────────────────────────────────────────────────┘
```

- **Bootloader:** Shared across BLE STM32, BMS STM32, nRF51822 with board-specific drivers
- **Application:** Board-specific firmware using shared protocol library
- **Signing:** All firmware images signed with ECDSA-P256-SHA256 before deployment
- **Updates:** Via XMODEM-CRC over UART (through VESC passthrough)

## 4. Key Modules

| Module / Component | Responsibility |
|---|---|
| `bootloader/stm32/` | STM32F103 secure bootloader (BLE board) |
| `bootloader/nrf51/` | nRF51822 secure bootloader |
| `bootloader/common/` | Shared: ECDSA, SHA-256, CRC-32, XMODEM, .sfw header |
| `firmware/decompiled/ble/` | Custom BLE dashboard application firmware |
| `firmware/decompiled/nrf51822/` | Custom nRF51822 BLE application (VESC App) |
| `vesc-lisp/` | VESC Lisp script for G30 dashboard integration |
| `lib/ninebot-protocol/` | Ninebot UART protocol C++ library |
| `tools/flasher/` | PC-side flash tools (IAP, XMODEM, initial flash) |
| `tools/signing/` | ECDSA key generation, firmware signing, verification |
| `tools/analysis/` | Firmware analysis and disassembly scripts |

## 5. Build System

CMake cross-compilation with `arm-none-eabi-gcc`:

```powershell
# Configure
cmake -B bootloader/build/ble -S bootloader -G Ninja -DTARGET_BOARD=BOARD_BLE_STM32

# Build
cmake --build bootloader/build/ble

# Sign
python tools/signing/sign_firmware.py --input build/ble_app.bin --output build/ble_app.sfw --target ble
```

## 6. Flashing & Debug Toolchain

| Method | Tool | Use Case |
|--------|------|----------|
| Stock IAP | `tools/flasher/ninebot_flasher.py` | Phase 1 deployment (reversible) |
| XMODEM | `tools/flasher/xmodem_send.py` | Custom bootloader updates |
| SWD | ST-Link + STM32CubeProgrammer | Recovery / initial bootstrap |
| UART Monitor | `python -m serial.tools.miniterm` | Debug output at 115200 8N1 |

## 7. Deployment Strategy

See [docs/guides/DEPLOYMENT.md](../docs/guides/DEPLOYMENT.md) for full details.

| Phase | Action | Reversible |
|-------|--------|------------|
| 0 | Backup + tooling + VESC install | Yes |
| 1 | Custom app via stock IAP (0x08001000) | Yes — reflash stock |
| 2 | Custom bootloader in app area (0x08001000) | Yes — reflash stock |
| 3 | Custom bootloader at 0x08000000 (final) | SWD only |
| 4 | nRF51822 custom BLE firmware | Via bootloader |

BMS custom firmware is out of scope — stock BMS works fine. BLE firmware supports stock BMS Ninebot protocol and optionally Daly BMS.

## 8. Known Limitations / Open Issues

- Custom application firmware is in early development (reconstructed from disassembly)
- VESC UART bridge protocol integration not yet implemented
- nRF51822 VESC App BLE service not yet implemented
- Stock bootloader IAP behavior still being reverse engineered
- ✅ **Resolved (2026-06-02):** the `BLE_*.bin` dumps **are** nRF51822 (Cortex-M0) images, not STM32
  — confirmed by re-disassembly (reset `0x00018154`, Nordic UART0, S110 SoftDevice, MiIO). No STM32
  dashboard dump exists. See `Documentation/VERIFICATION_REPORT.md` and `firmware/decompiled/RE_FINDINGS.md`.
- `BMS_1.3.4.bin` is encrypted (XiaoTEA) — decryption needed before it can be analyzed.
- BMS BQ76940 link shows no hardware-I2C1 use → likely bit-banged (to confirm).
- Daly BMS protocol support not yet implemented

## 9. Revision History

| Date | Summary |
|---|---|
| 2026-03-28 | Project converted from documentation-only to software development project |
| 2026-06-02 | Migrated agent config to Claude Code; firmware re-disassembled & verified (BLE=nRF51 confirmed, BMS_1.3.4 encrypted); docs corrected. See VERIFICATION_REPORT.md |

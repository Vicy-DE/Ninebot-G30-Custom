# Project Documentation — Ninebot G30 Max Custom Firmware

**Last updated:** 2026-06-15
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

**Battery:** 36V 15.3Ah (551 Wh), **10S6P** Li-ion (60× 18650) — 350W nominal / 700W peak motor.
(Corrected 2026-06-09 from "10S3P": the genuine Segway cell `10INR19/66-6` = 10S**6**P, and 3P×≤3.5 Ah
cannot reach 15.3 Ah — see [`boards/bms-battery/DALY_BMS_SELECTION.md`](../boards/bms-battery/DALY_BMS_SELECTION.md).)

## 3. Software Architecture

```
┌─────────────────── Secure Bootloader (16 KB) ──────────────────┐
│  ECDSA-P256 signature verify │ SHA-256 │ NBU update │ Flash   │
└─────────────────────────────┬──────────────────────────────────┘
                              │ Jumps to app if signature valid
┌─────────────────── Application Firmware (46 KB) ───────────────┐
│  Ninebot Protocol │ VESC UART Bridge │ Throttle/Brake │ Display│
└────────────────────────────────────────────────────────────────┘
```

- **Bootloader:** Shared across BLE STM32, BMS STM32, nRF51822 with board-specific drivers
- **Application:** Board-specific firmware using shared protocol library
- **Signing:** All firmware images signed with ECDSA-P256-SHA256 before deployment
- **Updates:** Via **NBU** — a framed half-duplex protocol over the Ninebot UART bus (through VESC
  passthrough). The bus is a single half-duplex wire, so updates use framed request→ACK turn-taking
  (`5A A5` Ninebot frames), not a byte-stream like XMODEM.

## 4. Key Modules

| Module / Component | Responsibility |
|---|---|
| `bootloader/stm32/` | STM32F103 secure bootloader (BLE board) |
| `bootloader/nrf51/` | nRF51822 secure bootloader |
| `bootloader/common/` | Shared: ECDSA, SHA-256, CRC-32, NBU update protocol, .sfw header |
| `firmware/decompiled/ble/` | Custom BLE dashboard application firmware |
| `firmware/decompiled/nrf51822/` | Custom nRF51822 BLE application (VESC App) |
| `vesc-lisp/` | VESC Lisp script for G30 dashboard integration |
| `lib/ninebot-protocol/` | Ninebot UART protocol C++ library |
| `tools/flasher/` | PC-side flash tools (IAP, NBU update, initial flash) |
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
| NBU (framed half-duplex) | `tools/flasher/nbu_send.py` | Custom bootloader updates (one-wire bus) |
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

- Custom application firmware: core modules implemented + host-tested (2026-06-09) — `dash_bridge`
  (Ninebot⇄VESC + synthetic ESC registers), `dash_keeper` (power latch), `daly_soft_uart` (Req 13),
  `vesc_tunnel`, `haystack`, `mode_ctrl`. Remaining: on-device HAL wiring + `nb_auth`/SoftDevice glue.
- VESC UART bridge: ✅ bridge core + register synthesis implemented & tested (`dash_bridge.h`); on-device
  relay wiring into `ble_main` pending. App-compat mapping in `docs/APP_COMPATIBILITY.md`.
- nRF51822 VESC App BLE service: tunnel framing/CRC implemented & tested (`vesc_tunnel.h`); S130 2nd-NUS
  glue pending.
- Stock bootloader IAP behavior: opcodes 0x07/0x08/0x09/0x0A **confirmed** vs the official Ninebot
  protocol PDF; no-solder serial-IAP flash plan in `docs/DASHBOARD_NO_SOLDER_FLASH.md`. Whether the
  stock BL accepts a custom unsigned image (unlock gate) still needs an on-unit test.
- 🔒 **Dumping the stock dashboard bootloader is auth-blocked (2026-06-15).** The full RE rig works on the
  live scooter — software-UART tap (NUCLEO-C542RC), ESC emulator that clears the comm-fault, the runtime
  (0x64/0x65) + update protocol fully mapped — but flashing the dashboard is locked two ways by design:
  the **wired ESC bus** enter-update is **CMD 0x57**, password-gated by the STM32 **chip UID @0x1FFFF7E8**
  (`~Σ ‖ ~Π` of the UID words); the **BLE** path (`G30LD`, NUS + MiIO) gates the relay behind the **MiIO
  registration token**. Both are device secrets not on the wire. Needs one of: the MiIO token, the chip
  UID, or a captured real app-update. See `boards/ble-dashboard/C542_BUS_CAPTURE.md` +
  `docs/C542_PROGRAMMER_SCHEMATIC.md`.
- ✅ **Resolved (2026-06-02):** the `BLE_*.bin` dumps **are** nRF51822 (Cortex-M0) images, not STM32
  — confirmed by re-disassembly (reset `0x00018154`, Nordic UART0, S110 SoftDevice, MiIO). No STM32
  dashboard dump exists. See `Documentation/VERIFICATION_REPORT.md` and `firmware/decompiled/RE_FINDINGS.md`.
- `BMS_1.3.4.bin` is encrypted (XiaoTEA) — decryption needed before it can be analyzed.
- BMS BQ76940 link shows no hardware-I2C1 use → likely bit-banged (to confirm).
- Daly BMS protocol: ✅ implemented & host-tested (`daly_soft_uart.h`) — 0x90–0x98 + 0xD9/0xDA.
  Selection/AliExpress/fit: `boards/bms-battery/DALY_BMS_SELECTION.md` (note: 20S/84V needs deck mods;
  13S/48V is the deck-fit recommendation).

## 9. Revision History

| Date | Summary |
|---|---|
| 2026-03-28 | Project converted from documentation-only to software development project |
| 2026-06-02 | Migrated agent config to Claude Code; firmware re-disassembled & verified (BLE=nRF51 confirmed, BMS_1.3.4 encrypted); docs corrected. See VERIFICATION_REPORT.md |
| 2026-06-09 | Implemented + host-tested 6 BLE/dash firmware modules (150/150 tests); pinout deep-research; no-solder flash plan; Daly selection + compartment fit; app-compatibility map; corrected 10S3P→10S6P. See CHANGE_LOG 2026-06-09. |
| 2026-06-09 | Built the real STM32 dashboard firmware (`firmware/dashboard/`, ~3.7 KB) with the **5000 ms IWDG hard watchdog** (Req 16, 153/153 tests); Python tooling (`iwdg_config.py`, `build_dashboard.py`, `dashboard_watchdog_test.py`); secure-boot rollout plan (`docs/SECURE_BOOT_PLAN.md`). |
| 2026-06-09 | Added the **dashboard chip simulator** (`firmware/dashboard/sim/`) — runs the shipping `DashApp` logic against a functional STM32F103 model; verifies the watchdog fires+recovers and the firmware can't brick (no protected-flash writes, valid vector). 16/16 sim checks; caught+fixed a CLOCK false-reset bug. `tools/dashboard_sim.py`. |
| 2026-06-09 | Added **Renode** instruction-accurate emulation (`firmware/dashboard/sim/renode/`, `tools/renode_dashboard.py`) — runs the real `dashboard_app.elf` on an emulated STM32F103+IWDG. Verified on Renode 1.16.0: healthy=1 boot, ADC-fault → 5000 ms IWDG resets the CPU ~every 5 s (3 boots/13 s). |
| 2026-06-09 | Confirmed the Ninebot UART protocol on the real firmware in Renode (inject 0x64 + app READ 0x22 → exact `…04 22 50 00…` response). Added the **stock-bootloader dumper** (`firmware/bootloader-dumper/`, flashed via IAP) + `tools/dump_bootloader.py`; sim-verified 4 KB dump (CRC + byte-identical). |
| 2026-06-09 | Added the **BLE session simulator** (`firmware/decompiled/nrf51822/sim/ble_sim.cpp`, `tools/ble_sim.py`) — runs the real nRF51 BLE firmware through advertising→pair→end-to-end register read→Haystack→VESC tunnel. 14/14; full ctest 3/3. (Functional sim — Renode has no nRF51 and BLE needs the proprietary SoftDevice.) |
| 2026-06-13 | Added the **`/verify-safe` firmware safety gate** (`tools/verify_firmware_safe.py`; CLAUDE.md step 2): no-brick + update-path-preserved + regression + secure-boot, run after every firmware change. **Verified + fixed the secure-boot bootloader** — found 5 bugs (signer format, `bn_mod_mul`, dead-code, missing libc, nRF51 `-lgcc`) that made it non-functional; now both targets build and signed-accept/tamper-reject is proven (`tools/verify_secureboot.py`, `bootloader/tests/`). |
| 2026-06-14 | Adopted the **RC-Servo** bootloader layout/updater model (`docs/BOOTLOADER_V2_CONCEPT.md`): 16K BL / 40K app / 4K factory / 4K user-data. Built + **Renode-verified** the **stock→custom migration** apps (`firmware/migration/`: trampoline + bl_updater + packed image; `tools/renode_migration.py`). Added the **odometer protocol** (`docs/PROTOCOL_ODOMETER.md`) + wear-leveled persistence (`odometer_store.h`, host-tested) — dashboard saves lifetime hours/km on every power-off. Suite 157/157. |
| 2026-06-15 | Built the **C542 software-UART IAP programmer** (`soft_uart` + `nbu_prog` + `main_programmer.c`) and the **16/32 self-update chain**: relocated BL@0x08004000 flashes a signed app→0x08008000 (tamper rejected), installer@0x08008000 writes a BL→0x08004000 — `0x08000000` never touched. Sim-verified with real code (`test_c5_prog` 6/6, `test_iap_chain` 11/11, real ECDSA); installer@32 builds; one-command gate `tools/verify_c5_flash.py` → GO before flashing. See CHANGE_LOG 2026-06-15. |
| 2026-06-14 | Verified the **STM32C5 / NUCLEO-C542RC** tap claim (A0=PA0, A2=PA4, sourced) and built `firmware/dash-tap-c542/`: a Nucleo tap on the dashboard's internal plug that **finds which wire is BT(nRF) vs dashboard(STM32)** by sniffing the Ninebot framing, then bridges it to USB for the bootloader dump (`tools/dump_bootloader_c5.py`). Logic host-verified 9/9 + parser 6/6 (`tools/dash_tap_sim.py`); HAL glue built in CubeIDE. See CHANGE_LOG 2026-06-14. |
| 2026-06-14 | Added a **relocatable bootloader build** (`BL_BASE`, `stm32f103c8_bootloader.ld.in`): links the 16 KB BL into an app slot (`BL_BASE=0x08004000` / `0x08001000`) so the installed BL can launch + test it **without writing 0x08000000**. Base-derived layout + runtime `SCB->VTOR`; `tools/verify_reloc_build.py` proves correctness (6/6) and that the relocated build can't erase 0x08000000. See CHANGE_LOG 2026-06-14. |
| 2026-06-14 | Replaced the bootloader's **XMODEM** update transport with **NBU** — a framed half-duplex protocol (`bootloader/common/nbu.{h,c}`) suited to the **one-wire half-duplex** Ninebot bus: `5A A5` framed request→ACK, IAP-aligned opcodes, per-block seq retransmit. Integrated into both STM32 + nRF51 bootloaders; deleted `xmodem.{c,h}` + `xmodem_send.py`; added PC sender `tools/flasher/nbu_send.py`. Fixed a latent `uint8_t` frame-length over-read. Verified: host 6/6, Python↔C cross-check byte-identical, both targets build, `/verify-safe` SAFE + secure-boot 9/9. See CHANGE_LOG 2026-06-14. |
| 2026-06-15 | **On the live scooter** with the C542 software-UART rig: confirmed the bus (115200, `5A A5` LEN=payload, dash 0x21→ESC 0x20; captured `5A A5 05 21 20 65 00 04 28 22 02 00 04 FF`), RE'd the runtime protocol (0x65 throttle/brake, 0x64 telemetry) and **cleared the dashboard comm-fault by emulating the ESC**. Disassembled the update protocol: enter-bootloader is **CMD 0x57**, UID-password-gated (`~Σ‖~Π` of the chip UID @0x1FFFF7E8) → `0x5A5A` flash-marker → reset. BLE = `G30LD` (NUS + MiIO) gated by the MiIO token. Dump auth-blocked on a device secret; everything else (TX/RX/emulator/programmer/IAP) proven. Schematic: `docs/C542_PROGRAMMER_SCHEMATIC.md`. See CHANGE_LOG 2026-06-15. |

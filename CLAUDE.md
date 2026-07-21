# CLAUDE.md — Ninebot G30 Max Custom (Agent Operating Manual)

Custom-firmware + reverse-engineering project for the **Ninebot G30 Max** e-scooter. The stock ESC
motor controller is replaced by a **VESC**; the BLE dashboard and BMS keep their stock hardware but
get custom firmware to restore the original feature set, expose the VESC App over BLE, and remove the
speed cap. A custom ECDSA-P256-signed secure bootloader is being developed.

> This file is the single source of truth for how to work in this repo. Detailed guides live in
> [`docs/guides/`](docs/guides/); the routine workflow is encoded as `/`-commands in `.claude/commands/`.

## Architecture

Three boards on a shared Ninebot UART bus (5A A5 header, **115200 8N1**):

```
Phone App ←BLE→ [BLE Dashboard] ←UART→ [ESC → VESC] ←UART→ [BMS Battery]
                 nRF51822 + STM32F103C8     (replaces stock     STM32F103C8T6
                 (BLE_*.bin = nRF51!)        STM32F103CBT6)      + BQ76940
```

| Board | ID | Main MCU | Flash | Custom FW? |
|-------|----|----------|-------|-----------|
| ESC | DRV | **VESC** (replaces stock STM32F103CBT6) | — | No — standard VESC fw + Lisp |
| BLE | BLE | STM32F103C8T6 **+ nRF51822** | 64 KB + 256 KB | Yes (STM32 app; nRF51 BLE) |
| BMS | BMS | STM32F103C8T6 + BQ76940 | 64 KB | No — stays stock |

## Folder map

`boards/{esc-motor,bms-battery,ble-dashboard}/` (datasheets, stock dumps, `PINOUT.md`, `README.md`) ·
`bootloader/` (secure BL: `common/` crypto, `stm32/`, `nrf51/`) · `docs/` (protocol, IAP, flashing,
`guides/`) · `firmware/decompiled/` (reconstructed fw + `RE_FINDINGS.md`) · `lib/ninebot-protocol/` ·
`tools/{signing,flasher,analysis,vesc}/` · `vesc-lisp/` · `Target/` (HW test scripts) ·
`Documentation/` (PROJECT_DOC, CHANGE_LOG, Requirements, ToDo, Tests, VERIFICATION_REPORT).

## ⚠️ Verified facts & caveats (read before trusting older docs)

Confirmed by re-disassembly — see [`firmware/decompiled/RE_FINDINGS.md`](firmware/decompiled/RE_FINDINGS.md)
and [`Documentation/VERIFICATION_REPORT.md`](Documentation/VERIFICATION_REPORT.md):

- **`boards/ble-dashboard/firmware/BLE_*.bin` are nRF51822 (Cortex-M0) images, NOT STM32.** There is
  **no STM32 dashboard dump** in the repo, so the STM32 BLE pinout is reference-derived/unverified.
- **`BMS_1.3.4.bin` is encrypted** (XiaoTEA) — decrypt before analyzing. `BMS_1.7.4.5` is plain STM32.
- `DRV_*` and `BMS_1.7.4.5` are genuine STM32F103 images, app base `0x08001000`.
- The **`5A A5` framing, addresses, `sum^0xFFFF` checksum, 115200 8N1 are firmware-confirmed.**
- ESC UART RX is **polled/DMA, not interrupt-driven**; BMS BQ76940 link shows no hardware-I2C1 (likely bit-banged).

## Conventions

- **Firmware naming:** `DRV`=ESC, `BLE`=dashboard, `BMS`=battery; file `{TYPE}_{version}.bin[.enc]`.
- **Addresses:** `0x20` ESC · `0x21` BLE · `0x22` BMS · `0x3E` App · `0x3F` PC.
- **Endianness:** little-endian. **Checksum:** `XOR 0xFFFF` of the sum of bytes `len..payload`.
- **Memory:** custom BL 16 KB @ `0x08000000`, app @ `0x08004000`. Stock BL 4 KB @ `0x08000000`, app @ `0x08001000`.
- **Toolchain:** `arm-none-eabi-gcc` (Cortex-M3 STM32F103 / Cortex-M0 nRF51822); CMake + Ninja; Python 3.9+.
- **Variants:** GD32F103 ≡ STM32F103 (pin/binary compatible); nRF51802 ≡ nRF51822.
- **C code:** Doxygen comment per function; side-effecting functions tagged `@sideeffects` (or
  `*_init`/`*_Handler`/`flash_*`/`uart_*`/`gpio_*`/`i2c_*` well-known patterns). See `docs/guides/CODING.md`.
- **Scripts:** flashing→`tools/flasher/`, signing→`tools/signing/`, analysis→`tools/analysis/`, HW
  tests→`Target/`. Use script-relative paths, never `cwd`. No temp scripts in repo root.

## Workflow — after every code change (use the slash commands)

1. `/build` — `cmake --build bootloader/build/<target>`; fix errors first.
2. `/flash` — stock IAP (`tools/flasher/ninebot_flasher.py`) or XMODEM (`tools/flasher/xmodem_send.py`).
3. `/verify-hw` — UART monitor @115200 8N1: boot banner, protocol responses, sensors.
4. `/document` — append `Documentation/CHANGE_LOG.md` + update `Documentation/PROJECT_DOC.md`.
5. `/test` — generate/run tests in `Target/`, save report in `Documentation/Tests/`.
6. `/commit` — Conventional Commits. **Never push** (enforced via settings deny-rule).

**Before a new feature:** `/new-feature` (updates `Documentation/Requirements/requirements.md` +
creates `Documentation/ToDo/<feature>.md`). **On pin/hardware changes:** `/hardware-change` (read
datasheets, update the board `PINOUT.md`). **To re-verify dumps:** `/verify-firmware`.

## Subagents (parallelizable work)

`firmware-analyst` (disassembly/RE) · `protocol-verifier` (claims↔binary) · `hardware-reviewer`
(datasheets↔pinout) · `doc-keeper` (changelog/project-doc hygiene). Defined in `.claude/agents/`.

## Deployment (incremental, reversible — full detail in `docs/guides/DEPLOYMENT.md`)

| Phase | Action | Target | Reversible |
|------|--------|--------|------------|
| 0 | Backup + tooling + VESC install | All | Yes |
| 1 | Custom app via stock IAP (`0x08001000`) | BLE STM32 | Yes (reflash stock) |
| 2 | Custom bootloader as "app" (`0x08001000`) | BLE STM32 | Yes (reflash stock) |
| 3 | Custom bootloader @ `0x08000000` (final) | BLE STM32 | SWD only |
| 4 | nRF51822 BLE firmware (VESC App) | nRF51822 | Via bootloader |

BMS stays stock (out of scope). BLE firmware must support stock Ninebot BMS protocol and optionally Daly BMS.

## Safety

- Battery pack = 551 Wh. Never bypass BMS over/under-voltage or overcurrent protection.
- **Do not flash custom firmware to the BMS.** BMS board is always energized — disconnect before SWD.
- 3.3V TTL only on UART. Verify firmware checksums and keep stock backups before any flash.

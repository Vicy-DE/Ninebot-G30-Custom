# Repository Verification Report — Ninebot G30 Max Custom

**Date:** 2026-06-02
**Scope:** Cross-check every checkable factual claim in the repo against (a) the actual file tree,
(b) the firmware binaries, and (c) internal consistency. Firmware evidence is detailed in
[`firmware/decompiled/RE_FINDINGS.md`](../firmware/decompiled/RE_FINDINGS.md).

**Legend:** ✅ PASS · ❌ FAIL · 🛠 CORRECTED (was wrong, fixed in this pass) · ⚠️ UNVERIFIED/PARTIAL

---

## 1. Structure & inventory

| # | Claim | Source | Result | Evidence / Action |
|---|-------|--------|--------|-------------------|
| S1 | Top-level dirs are `ESC-MotorController/`, `BLE-Dashboard/`, `BMS-BatteryManagement/` | `README.md` | 🛠 | Real layout is `boards/{esc-motor,bms-battery,ble-dashboard}/`. **README tree rewritten.** |
| S2 | Folder tree in Copilot instructions (`boards/`, `bootloader/`, `docs/`, `tools/`, `firmware/`, `vesc-lisp/`, `lib/`) | `.github/copilot-instructions.md` | ✅ | Matches disk (migrated into `CLAUDE.md`). |
| S3 | `Target/` directory holds hardware test scripts | `SCRIPTS`/`TEST_DOC`/`TODO_DOC` instructions | 🛠 | Directory **did not exist**. Created `Target/README.md`. |
| S4 | `tools/` inventory (flasher/signing/analysis scripts) | `SCRIPTS.instructions.md` | ✅ | All listed scripts present except `Target/` ones (S3). |
| S5 | Firmware version tables | `README.md` | ⚠️ | Dumps present: DRV 1.2.6 & 1.6.13; BLE 1.1.0 & 1.1.7; BMS 1.3.4 & 1.7.4.5. Other listed versions are catalog-only (not in repo) — acceptable, clarified. |

## 2. Firmware identity & architecture

| # | Claim | Source | Result | Evidence |
|---|-------|--------|--------|----------|
| FW1 | BLE board firmware dumps are STM32F103 dashboard firmware | `boards/ble-dashboard/PINOUT.md`, `copilot-instructions.md` | 🛠 (**F1**) | `BLE_1.1.x.bin` reset vector `0x00018154`, Nordic UART0 only, S110 SoftDevice, MiIO strings → **nRF51822 (Cortex-M0)**. No STM32 dashboard dump exists. |
| FW2 | `DRV_*` are STM32F103 ESC firmware @ app base `0x08001000` | `boards/esc-motor/PINOUT.md` | ✅ | Valid M3 vector tables; reset `0x080011xx`. |
| FW3 | `BMS_1.7.4.5` is STM32F103 BMS firmware | `boards/bms-battery/PINOUT.md` | ✅ | Valid M3 vector table; reset `0x080010D8`. |
| FW4 | `BMS_1.3.4` is plain STM32 firmware | implied | 🛠 (**F3**) | Invalid vector table, entropy 6.75 → **encrypted/obfuscated**. Labeled as such. |
| FW5 | ESC MCU is STM32F103C**B**T6 (128 KB) | `README.md`, `esc-motor/README.md` | ✅ | Consistent across ESC docs; BLE/BMS are C8 (64 KB). |

## 3. Protocol (`docs/protocol.md`)

| # | Claim | Result | Evidence |
|---|-------|--------|----------|
| P1 | Header `0x5A 0xA5` | ✅ | Identical header state machine in `DRV` (×3), `BMS_1.7.4.5` (×1), `BLE` nRF51 (×1). Disasm in RE_FINDINGS §2. |
| P2 | Checksum = `sum(len..payload) ^ 0xFFFF`, little-endian | ✅ | Self-consistent on doc example (`…→ 0xFF7C → 7C FF`). |
| P3 | Addresses `0x20`/`0x21`/`0x22`/`0x3E`/`0x3F` | ✅ | Address immediates present; each board recognizes self+peers. |
| P4 | UART 115200 8N1 | ✅ | BRR `0x0271` (72 MHz) in all STM32 images; `0x0139` (36 MHz) in BMS. |
| P5 | ESC/BMS/BLE register maps | 🛠 | **Resolved.** Register-file *mechanism* firmware-confirmed (ARG-indexed 16-bit files: ESC `@0x200007D6`, BMS `@0x20000400`; CMD 1=R/2=W/4=read-resp). *Semantics* sourced from etransport/ninebot-docs → new authoritative `docs/REGISTER_MAP.md`. The legacy `docs/protocol.md` tables had G30 errors (BMS cells `0x40-0x49` not `0x30-0x39`; ESC `0x48`=voltage not `0x3A`; `0x7B`=KERS not speed-limit) — flagged inline. |
| P7 | LEN field = "bytes from SrcAddr to end of Payload" (`docs/protocol.md`) | 🛠 | **Decompiled `buildPacket`/`parseProtocolByte` prove `LEN = payload count`** (frame = LEN+9). Corrected `protocol.md`; flagged `firmware/decompiled/common/include/protocol.h` (used payload+6). Verified by `tests/test_decompiled_protocol.cpp` (26/26). See `firmware/decompiled/DECOMPILATION.md`. |
| P6 | Half-duplex ESC↔BLE, full-duplex ESC↔BMS | ⚠️ | ESC uses 3 USARTs (consistent); duplex mode not separately provable from static image. |

## 4. Pinouts (`boards/*/PINOUT.md`)

| # | Claim | Result | Evidence |
|---|-------|--------|----------|
| PIN1 | ESC: TIM1 3-phase motor PWM + TIM3 Hall capture | ✅ | `TIM1_CR1`×13-15, `TIM3_CR1`×6-8. |
| PIN2 | ESC: three USART buses (debug / BLE / BMS) | ✅ | USART1/2/3 SR+DR all referenced. |
| PIN3 | ESC: USART1/2/3 "shared interrupt parser" | 🛠 | USART vectors → default handler `0x0800111A`; RX is **polled/DMA, not ISR**. Wording corrected. |
| PIN4 | BMS: USART1 (debug) + USART2 (ESC) | ✅ | USART1×4, USART2×1. |
| PIN5 | BMS: **hardware I2C1 (PB6/PB7)** for BQ76940 | ⚠️ | **No I2C1/I2C2 literal in `BMS_1.7.4.5`** → likely **bit-banged I2C on GPIO**. Provenance corrected to "design-derived / likely software I2C". |
| PIN6 | BLE STM32 pin table "extracted from BLE_1.1.x firmware analysis" | 🛠 (**F1**) | False provenance — the only BLE dump is the nRF51 image. Reattributed to reference-design/community knowledge; STM32 dashboard pinout marked unverified. |
| PIN7 | BLE STM32 BRR/peripheral evidence "from firmware" | 🛠 | The cited `0x0271`/USART refs were coincidental byte matches in an nRF51 image, not STM32 USART config. Removed/relabeled. |

## 5. Tooling

| # | Claim | Result | Evidence / Action |
|---|-------|--------|-------------------|
| T1 | `tools/analysis/disassemble_firmware.py` analyzes the dumps | 🛠 (**F5**) | Hardcoded old paths (`ESC-MotorController/…`), assumed STM32 base for all, didn't mask Thumb bit. **Rewritten** as an arch-aware harness. |
| T2 | `analyze_nrf51822.py` treats BLE as nRF51 | ✅ | Already correct — directly contradicted the PINOUT/Copilot STM32 claim (now reconciled repo-wide). |
| T3 | `analyze_bootloader.py` has XiaoTEA key | ✅ | Present; relevant to decrypting `BMS_1.3.4` (future work). |

---

## 6. Fixes applied in this pass

- `README.md` — folder tree rewritten to the real `boards/*` layout + current dirs.
- `tools/analysis/disassemble_firmware.py` — rewritten (arch/base auto-detect, Thumb-bit mask, encrypted-image detection, correct paths).
- `boards/ble-dashboard/PINOUT.md` — provenance corrected (nRF51 vs STM32); STM32 table marked reference-derived/unverified.
- `boards/esc-motor/PINOUT.md` — USART IRQ wording corrected (polled/DMA).
- `boards/bms-battery/PINOUT.md` — I2C1 provenance corrected (likely bit-banged); `BMS_1.3.4` marked encrypted.
- `docs/protocol.md` — added a "verified against firmware" note for header/checksum/baud/addresses.
- `Documentation/PROJECT_DOC.md` — "open issue: BLE may be nRF51" marked **resolved/confirmed**.
- `firmware/decompiled/RE_FINDINGS.md` — new authoritative re-disassembly report.

## 7. Open items (not fixed — flagged)

- `BMS_1.3.4.bin` decryption (XiaoTEA) to enable its disassembly.
- Exhaustive register-dispatch reconstruction (P5) from `DRV`/`BMS` for a fully firmware-derived register map.
- Confirm BMS BQ76940 I2C is bit-banged (PIN5) by locating the GPIO toggle routine.
- STM32 dashboard pinout cannot be verified until a real STM32 BLE-board dump is obtained.

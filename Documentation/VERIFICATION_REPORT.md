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

---

## 8. Live-hardware confirmation (2026-06-15)

Bus and protocol confirmed on the **actual G30 scooter** using the NUCLEO-C542RC software-UART rig
(bit-banged UART, no hardware USART; capture read back over SWD). Full method and traces:
[`../boards/ble-dashboard/C542_BUS_CAPTURE.md`](../boards/ble-dashboard/C542_BUS_CAPTURE.md).

| # | Claim | Result | Evidence |
|---|-------|--------|----------|
| HW1 | Bus is 115200 8N1, `5A A5` framing, `sum(LEN..payload)^0xFFFF` checksum | ✅ | Captured + checksum-valid on the live bus (measured bit ≈ 416 cyc @ 48 MHz → 115385 baud). |
| HW2 | **LEN = payload byte count** (not `4+payload`, not `SrcAddr..Payload`) | ✅ | Live frame `5A A5 05 21 20 65 00 04 28 22 02 00 04 FF`: LEN `05` = the 5 payload bytes `04 28 22 02 00`, checksum `04 FF` valid. |
| HW3 | Addresses `0x20` ESC / `0x21` BLE-dashboard / `0x22` BMS / `0x3E` App / `0x3F` PC | ✅ | Captured frame is dashboard (SRC `0x21`) → ESC (DST `0x20`); dashboard is bus master on this wire. |
| HW4 | Runtime `0x65` (dash→ESC throttle/brake) + `0x64` (ESC→dash telemetry) frames | ✅ | RE'd from `vesc-lisp/g30_dash.lisp`, confirmed live. `0x64` payload = mode/batt/light/beep/speed/error; **error `0` = no fault**. |
| HW5 | Replying with `0x64` (error=0) clears the dashboard "ESC missing" comm-fault | ✅ | With the C542 emulating the ESC, the dashboard stopped retrying and began emitting its own `0x64` frames — verified on hardware. |
| HW6 | Enter-update is **CMD `0x57`/`0x59`, UID-password-gated** (not "write reg 0x78") | ✅ | Disassembled `DRV_1.2.6`/`BMS_1.7.4.5` (`App_to_ESC_handler @0x08005624`, tbb @`0x08005650`). CMD `0x57` password = `~(UID0+UID1+UID2) ‖ ~(UID0·UID1·UID2)` (two 32-bit LE words) from the STM32 96-bit UID @`0x1FFFF7E8` (referenced @vma `0x08005478`). Valid password → `0x5A5A` IAP marker (DRV `0x0801C000`, BMS `0x0800F000`) → `NVIC_SystemReset`. CMD `0x18` is **calibration** (sub-cmd `0x12` + `"N4G"`), not reset. Zero-password `0x57/0x58/0x59/0x5C` were all ignored on hardware → the gate is real. |
| HW7 | BLE identity = **`G30LD` (D8:68:BA:16:A0:33)** = Nordic UART Service + Xiaomi MiIO `0xfe95` | ✅ | Read live over PC Bluetooth (`tools/ble_ninebot.py`): NUS `6e400001`; MiIO chars 0x0001 control, 0x0004 beaconkey, 0x0010 auth, 0x0013 token, 0x0014 device-id; product id `0x035C`. |
| HW8 | The nRF51 relays NUS↔STM32 **only after the MiIO secure handshake** | ✅ | Live: connected and read MiIO info chars, but raw `5A A5` / MiIO-control / dummy-auth writes got **zero notifications** unauthenticated — the relay is keyed by the device's MiIO registration token. |
| HW9 | **Two-channel authentication wall** — flashing the dashboard is locked both ways by design | ✅ | Wired ESC bus needs the chip-UID password (CMD `0x57`, HW6); BLE needs the MiIO registration token (HW8). Both are per-device secrets, **not derivable** from the bus or an unauthenticated BLE read. |

**Net result:** the bus electrical + protocol layer is now measured fact on the live scooter (not
reference-derived). The dashboard read/write/telemetry path, comm-fault recovery, and enter-update
mechanism are fully reverse-engineered. The remaining blocker to flashing the stock dashboard is a
single per-device secret on each channel (chip UID for wired, MiIO token for BLE) — see
[`../docs/BLE_PROTOCOL_VERIFIED.md`](../docs/BLE_PROTOCOL_VERIFIED.md) and
[`../docs/protocol.md`](../docs/protocol.md).

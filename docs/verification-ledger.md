# Verification Ledger — Ninebot G30 Max Custom

Every factual claim in [`PROJECT_DOCUMENTATION.md`](../PROJECT_DOCUMENTATION.md) and in the repo's
pre-existing documentation, checked by an independent agent against the firmware binaries, the source
tree, or a primary external source.

**Run date:** 2026-08-06 · **Fleet:** Sonnet 5 fact-checkers (10 parallel) + Opus 5 adjudicators
(max 2 parallel, 5 slots each) · **Method:** `verify-docu`

**Legend:** ✅ VERIFIED · ❌ REFUTED · ⚠️ PARTIAL (conclusion holds, details wrong) · ❓ UNVERIFIABLE

---

## Summary

**88 claims checked.** 12 Sonnet fact-checkers across the binaries, the tree and external primary
sources; 2 Opus adjudicators on the escalated contradictions.

| Verdict | Count | Meaning |
|---|---|---|
| ✅ VERIFIED | 48 | Checked against evidence; keep as written |
| ⚠️ PARTIAL | 19 | Conclusion holds, stated details wrong or method-dependent |
| ❌ REFUTED | 23 | Wrong as stated; corrected text given |
| ❓ UNVERIFIABLE | 3 | Genuinely open — must not be asserted as fact |

**Escalated to Opus:** 2 — the BLE authentication contradiction (§G) and the `/verify-safe` safety
gate (§H). Both returned REFUTED against the documented claim.

**The three unverifiable claims**, which this documentation marks as open rather than asserting:

- **C-039** — the power button's GPIO number. Passed through a runtime config struct, so it is not in
  the image. Needs an SWD read of `PIN_CNF[0..31]` on a live board or PCB tracing.
- **C-075** — the stock `UICR.BOOTLOADERADDR` value. No SWD dump exists, so UICR has never been read.
- **C-148** — "IAP opcodes confirmed against the official Ninebot PDF". The PDF is not in the repo and
  its only cited URL returns HTTP 403.

**Biggest single lesson:** the substantive reverse-engineering is largely sound — checksum core,
TM1637 driver, UART pin-swap, UID password formula and battery pack config all verified byte-exact or
against primary sources. What failed was *bookkeeping about* that work: addresses attributed to the
wrong firmware image, a stale checksum copied between files, counts that only reproduce under an
undisclosed method, and ~35 files still describing a chip that was disproved in July.

---

## A. Firmware identity & architecture

| id | class | claim | verdict | model | evidence |
|---|---|---|---|---|---|
| C-001 | INTERNAL | The BLE dashboard has no STM32; its only MCU is the nRF51822 | ✅ | sonnet | Reset vectors `0x00018155` (both BLE dumps) vs `0x08001101`/`0x080010D9` (control STM32 dumps). nRF51 and STM32F103 flash ranges are disjoint, so a valid vector in one is impossible for the other. Zero unambiguous STM32-only peripheral literals in either dashboard image vs 42–70 in the controls. |
| C-002 | INTERNAL | Peripheral-constant census: dashboard = 7 nRF51 / 4–5 STM32; `DRV_1.2.6` = 62 STM32; `BMS_1.7.4.5` = 37 STM32 | ⚠️ | sonnet | Numbers are **method-dependent and not reproducible as printed**. Re-measured word-aligned with a fuller address list: dashboard 11–15 nRF51 / 0–3 STM32; `DRV_1.2.6` 74 STM32; `BMS` 42. The doc's figures reproduce only under an unaligned scan with a narrower address list. Conclusion unaffected — the aligned numbers are *more* favourable. Needs a method footnote. |
| C-003 | INTERNAL | Image table: sizes, entropies, SP values (6 images) | ✅ | sonnet | All 18 cells byte-exact. Sizes 30076/33388/33612/34252/13956/23596; entropies 6.99/7.00/6.95/6.96/6.75/6.83 at 2 dp. |
| C-004 | INTERNAL | Reset-vector values in `RE_FINDINGS.md` §1 | ⚠️ | sonnet | `RE_FINDINGS.md` prints reset vectors with the Thumb bit **cleared** (`…154`, `…100`, `…D8`); `MCU_IDENTIFICATION.md` prints the **raw** odd words (`…155`, `…101`, `…D9`). Both describe the same handler; the two documents disagree on convention for identical measured quantities, with no footnote. |
| C-005 | INTERNAL | `BMS_1.3.4.bin` is XiaoTEA-encrypted | ❌ | sonnet | **Refuted.** Entropy 6.75 is *lower* than the known-plaintext `BMS_1.7.4.5` (6.83) and `DRV_1.2.6` (6.99); windowed entropy 5.0–6.0 vs 7.9–8.0 for real ciphertext; byte `0xCD` = 6.96 % of the file; a **25-byte constant run** of `0x80`; an 8-byte block recurring 18×; a templated 128-byte header. Size 13956 ≡ 4 (mod 8) fails the TEA block precondition. Actual decryption with the repo's own working TEA code produced no vector table, no strings, and entropy *rose* to 7.99 — the signature of a wrong key. |
| C-006 | INTERNAL | The XiaoTEA key schedule lives in `tools/analysis/analyze_bootloader.py` | ❌ | sonnet | **Refuted.** That file contains no key schedule and no decrypt function — only the 16 ASCII bytes `"Ninebot Scooter "` used in two substring searches. It also retains pre-reorganisation hardcoded paths, so it silently skips every firmware file, and its printed "KEY FINDINGS" block is static text, not a computed result. |
| C-007 | INTERNAL | Identity strings `Scooter_G30_SAT` / `NBScooter0001` / `G30_HD_HDPRO_VXX` | ✅ | sonnet | Found byte-exact: `Scooter_G30_SAT` @ `BLE_1.1.7` file `0x3300` and `DRV_1.6.13` file `0x400` (absent from `DRV_1.2.6`); `G30_HD_HDPRO_VXX` @ `BMS_1.7.4.5` file `0x100`. |

## B. Ninebot bus protocol

| id | class | claim | verdict | model | evidence |
|---|---|---|---|---|---|
| C-010 | INTERNAL | Checksum = `sum(LEN..payload) XOR 0xFFFF`, little-endian | ✅ | sonnet | Hand-verified and Python-verified on the live frame; matches `calculateChecksum()`/`buildPacket()` in all three C++ implementations. |
| C-011 | INTERNAL | `LEN` = payload byte count; frame total = `LEN + 9` | ✅ | sonnet | Live frame is 14 bytes with LEN=5. The 9 = 2 preamble + LEN + SRC + DST + CMD + ARG + 2 checksum. |
| C-012 | INTERNAL | `ARG` is a distinct header byte not counted in LEN | ✅ | sonnet | Forced by the arithmetic: the alternative 13-byte layout without ARG contradicts the measured 14-byte frame. |
| C-013 | INTERNAL | Live frame `5A A5 05 21 20 65 00 04 28 22 02 00 04 FF` is checksum-valid | ✅ | sonnet | Σ(LEN..payload) = `0xFB`; `0x00FB ^ 0xFFFF = 0xFF04`; LE = `04 FF`. Every other plausible byte range was tested and fails — this is the unique validating range. |
| C-014 | INTERNAL | `docs/protocol.md` worked example checksum = `7C FF` (`0xFF7C`) | ❌ | sonnet | **Refuted — arithmetic error.** For the current bytes `02 3E 20 01 10 0E 00`, Σ = `0x007F` and the checksum is `0xFF80` → `80 FF`. `0xFF7C` is the correct answer for the *old* `LEN=06` example; when LEN was corrected 06→02 the checksum was never recomputed. Propagated into `Documentation/VERIFICATION_REPORT.md` row P2, which additionally calls it "self-consistent". |
| C-015 | INTERNAL | Checksum core visible at VMA `0x000184B0` (`mvns`/`uxth`/…/`bne 0x18532`) | ✅ | sonnet | Byte-for-byte exact, including the branch target. Two intervening instructions are elided from the doc's quotation but every cited address is right. |
| C-016 | INTERNAL | Frame builder `movs #0x5A` @ `0x184CC` / `movs #0xA5` @ `0x184D0`, 3 preamble pairs | ✅ | sonnet | Exact. Three pairs at `0x184CC`, `0x1898C`, `0x18CA0`. "Immediately followed by" is loose — a `strb` sits between the two `movs` — but the pattern is as described. |
| C-017 | INTERNAL | Address immediates in the dashboard image: `0x20`×35, `0x21`×13, `0x3E`×6 | ⚠️ | sonnet | Reproducible **only as a `movs`-only count**. Including the genuine `cmp` instructions the totals are 42/14/6. `RE_FINDINGS.md` hedges its counts as approximate; `RE_NRF51_DASHBOARD.md` and `MCU_IDENTIFICATION.md` state them without the hedge. |
| C-018 | INTERNAL | `5A A5` parser instance counts (DRV 3, BMS 1, BLE 1) | ⚠️ | sonnet | DRV and BMS confirmed. **BLE is 2, not 1** — a full duplicate of the state machine exists at `0x0001A030` alongside `0x0001854E`. Also: the doc's stated "immediately followed by" method yields **zero** hits in all four binaries; real instances have 1–3 intervening instructions. |
| C-019 | INTERNAL | Parsers are "byte-for-byte identical" across images | ⚠️ | sonnet | Logic is equivalent; **opcodes are not** — `DRV_1.2.6` and `DRV_1.6.13` differ in branch selection. Overstated. |
| C-020 | INTERNAL | Body-length caps: ESC `LEN+7 ≤ 0xF3`, BMS `0xC2`, nRF51 `LEN+8`/`LEN+0x0D` cap `0x8F` | ⚠️ | sonnet | Constants all correct. BMS compares **raw LEN after subtracting 7**, not `LEN+7` — different comparison semantics, same constant. nRF51's two paths confirmed falling through to one shared `0x8F` cap. |
| C-021 | INTERNAL | Runtime `0x65` (throttle/brake) and `0x64` (telemetry, error=0 clears fault) | ✅ | sonnet | Confirmed live on the scooter: with a correct `0x64` reply the dashboard stopped its dead-ESC retry and began emitting `0x64` frames. |

## C. Dashboard reverse-engineering

| id | class | claim | verdict | model | evidence |
|---|---|---|---|---|---|
| C-030 | INTERNAL | TM1637 font table at file `0x81FF` (1.1.0) / `0x846F` (1.1.7), absent from ESC/BMS | ✅ | sonnet | Byte-exact at both offsets; zero occurrences in `DRV_1.2.6`, `DRV_1.6.13`, `BMS_1.7.4.5`. Minor nit: the single VMA `0x2046F` applies only to the 1.1.7 offset (1.1.0's would be `0x201FF`). |
| C-031 | INTERNAL | P0.04 / P0.05 are the TM1637 pair, via `PIN_CNF[4]`=`0x50000710` | ✅ | sonnet | Literal pool at `0x18DD4` = `0x50000710` = `0x50000700 + 4×4` = PIN_CNF[4]; `adds r1,#4` then writes PIN_CNF[5]. Exact. |
| C-032 | INTERNAL | TM1637 symbol addresses (start/stop/write_byte/read_ack/update/gpio/delay) | ✅ | sonnet | All confirmed byte-exact, including the full `0x40`/`0xC0`/6×seg/`0x88\|brightness` sequence with `cmp r4,#6`. |
| C-033 | INTERNAL | `tm1637_display_off()` @ `0x00019D96` | ❌ | sonnet | **Refuted — off by 2.** `0x19D90`–`0x19D97` is a literal pool (constants `0x50000500`, `0x2000275C`), proven by two `ldr rX,[pc,…]` at `0x19D60`/`0x19D64` targeting them. `0x19D96` is the tail of `0x2000275C`, which a linear sweep misread as a `movs r0,#0` prologue. Real entry is **`0x00019D98`**, confirmed by the only call site, `bl 0x19d98` at `0x193C0`. |
| C-034 | INTERNAL | `tm1637_write_byte` is LSB-first | ✅ | sonnet | Verified from code, not assumed: `lsls r0,r4,#31` isolates bit 0, then `lsrs r4,r4,#1`. |
| C-035 | INTERNAL | `uart_init` @ `0x0001FDB4` with PSELTXD/PSELRXD/BAUDRATE/ENABLE registers as documented | ✅ | sonnet | Every literal confirmed by two independent methods (objdump + a from-scratch Thumb BL decoder). `EVENTS_RXDRDY` clear resolves to `0x40002108`, matching the Nordic spec. Minor omission: `PIN_CNF=4` also enables a pulldown, not mentioned. |
| C-036 | EXTERNAL | `0x01D7E000` is the nRF51 BAUDRATE constant for 115200 | ✅ | sonnet | Matches Nordic's `UART_BAUDRATE_BAUDRATE_Baud115200`. |
| C-037 | INTERNAL | P0.15/P0.20 swap for half-duplex; exactly two callers of `uart_init` | ✅ | sonnet | `uart_mode_tx20` @`0x1A128` (tx=20, rx=15, flag 1) and `uart_mode_tx15` @`0x1A140` (tx=15, rx=20, flag 0); both write the same RAM byte `0x20002160+5`. An exhaustive BL scan found exactly these two callers. Full PSEL reprogram each call — no tri-state step. |
| C-038 | INTERNAL | P0.08 is a board-variant strap, not the power button | ✅ | sonnet | `PIN_CNF[8]=0x0C` (input + pull-up) read once at init `0x1A158`; result decides whether `PIN_CNF[25]` is driven at `0x186A0`. |
| C-039 | INTERNAL | The power button's GPIO number is not recoverable from the image | ❓ | sonnet | Genuinely unresolved — handled via GPIOTE `PORT` + `PIN_CNF.SENSE` with the pin passed through a runtime config struct. Requires an SWD read of `PIN_CNF[0..31]` on a live board or PCB tracing. Correctly documented as open. |

## D. Hardware specification

| id | class | claim | verdict | model | evidence |
|---|---|---|---|---|---|
| C-050 | EXTERNAL | Pack is 10S6P, 60 × 18650, 2.55 Ah/cell — not 10S3P | ✅ | sonnet | OEM MPN `10INR19/66-6` decoded under IEC 61960; sibling Segway part `10INR19/66-2` rated 5100 mAh → 2550 mAh/cell, identical to 15300/6P. Two independent retailers label the exact part "10S6P". 10S3P would need 5.1 Ah in an 18650 (~3.6 Ah ceiling). The 2026-06-09 correction was right. |
| C-051 | EXTERNAL | 36 V 15.3 Ah = 551 Wh | ✅ | sonnet | Segway official spec + OEM listing. |
| C-052 | EXTERNAL | 350 W nominal / 700 W peak rear hub motor | ✅ | sonnet | 350 W nominal on Segway's official page; 700 W peak well-corroborated across sources but not verbatim on the primary page — medium-high confidence on the peak figure only. |
| C-053 | EXTERNAL | Front mechanical drum + rear electronic/regenerative brake | ✅ | sonnet | Segway official text. Also physically necessary — regen requires the driven wheel, which is the rear hub. (A reviewer hypothesis that the repo had this backwards was itself wrong.) |
| C-054 | EXTERNAL | 25–30 km/h region-dependent, ~65 km range, 10″ pneumatic tyres | ✅ | sonnet | Segway EU page "up to 25 km/h" and "up to approx. 65 km"; 30 km/h in other-region listings. |

## E. Repository state & build

| id | class | claim | verdict | model | evidence |
|---|---|---|---|---|---|
| C-070 | INTERNAL | `bootloader/CMakeLists.txt` accepts only `TARGET_BOARD=nrf51` | ✅ | sonnet | `ble`/`bms` hit an explicit `FATAL_ERROR` with a migration message; everything the `nrf51` path references exists on disk. |
| C-071 | INTERNAL | The documented build command works | ❌ | sonnet | **Refuted by reproduction.** `PROJECT_DOC.md:73` and `docs/guides/BUILD.md:54` omit `-DCMAKE_TOOLCHAIN_FILE`; CMake then silently picks the host mingw64 gcc, reports "Configuring done", and fails 11/12 objects at `-mthumb`. `PROJECT_DOC.md` additionally passes `BOARD_BLE_STM32`, which was never a valid value. With `-DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m0.cmake` the build succeeds: 12/12 objects, 6956 B text. |
| C-072 | INTERNAL | `firmware-decompiled/` is a project directory (`HANDSOFF.md:53`) | ❌ | sonnet | **Refuted.** It is an empty, untracked, gitignored stray containing only a leftover `build/`. The real location is `firmware/decompiled/`. |
| C-073 | INTERNAL | ~35 files still assert an STM32 dashboard | ✅ | sonnet | Confirmed with file:line refs across `docs/`, `Documentation/`, `boards/`, `firmware/`, `bootloader/README.md`. 13 doc-vs-doc contradictions, several intra-file. |
| C-074 | INTERNAL | A stock nRF51 SWD dump exists | ❌ | main | **Refuted.** `boards/ble-dashboard/firmware/` contains only `BLE_1.1.0.bin` and `BLE_1.1.7.bin`. No SWD dump exists anywhere in the tree. |
| C-075 | INTERNAL | Custom BL sits at `0x0003C000` "= stock `UICR.BOOTLOADERADDR`" | ❓ | main | The address arithmetic is sound (`0x3C000 + 0x4000 = 0x40000`), but the "= stock UICR" half **cannot be evidenced** — UICR has never been read, because no SWD dump exists (C-074). Must be stated as the intended value, not a measured one. |

## F. nRF51 SoftDevice & memory map

| id | class | claim | verdict | model | evidence |
|---|---|---|---|---|---|
| C-080 | EXTERNAL | S110 SoftDevice occupies `0x0`–`0x17FFF` (96 KB) | ✅ | sonnet | Nordic docs: S110 **v8.0.0** ends at `0x18000` (96 KB); v7.0.0 ends at `0x16000` (88 KB). Three repo files independently label it v8.0. |
| C-081 | EXTERNAL | `UICR.BOOTLOADERADDR` is at `0x10001014`; `0x3C000 + 0x4000 = 0x40000` | ✅ | sonnet | Register offset correct per Nordic's UICR layout; arithmetic exact for a 256 KB part. |
| C-082 | INTERNAL | `0x0003C000` "= stock `UICR.BOOTLOADERADDR`" | ❌ | sonnet | **Refuted as a statement about the stock chip.** No SWD dump exists. The repo's own `analyze_nrf51822.py` output (`nrf51822_analysis_BLE_1.1.7.txt`) explicitly reports *"No bootloader address patterns found in binary"* — the byte pattern `14 10 00 10` is absent. All 7 UICR hits are CLENR0. `0x3C000` is **Nordic's conventional DFU offset adopted by design**, not a measured value. |
| C-083 | EXTERNAL | S110 SVC numbers listed in `RE_NRF51_DASHBOARD.md` §7 | ⚠️ | sonnet | 8 of 13 correct, **5 wrong**. All four GAP mappings are off by exactly −4 because the doc used `BLE_GAP_SVC_BASE=0x6C` (which is Nordic's *reserved* padding range) instead of the real `0x70`: `conn_param_update` 113→**117**, `ppcp_set` 118→**122**, `authenticate` 122→**126**, `auth_key_reply` 124→**128**. The flash SVCs are wrong too: claimed "~39–41", real values are **32/33** — and 40/41 in the binary are actually `SD_NVIC_GET/SETPENDINGIRQ`, unrelated to flash. SVCs 126, 128, 32 and 33 **never appear** in the image. Correct entries: `sd_ble_enable`=96, `evt_get`=97, `uuid_vs_add`=99, `gatts_service_add`=160, `characteristic_add`=162, `value_set`=164, `hvx`=166, `sys_attr_set`=169. |
| C-084 | INTERNAL | The image contains 58 SVC call sites | ✅ | sonnet | Independently reproduced: 58 call sites, 37 distinct numbers — matches `nrf51_hal.h`'s own note. |
| C-085 | DERIVED | "2 services + 3 characteristics matches the observed GATT" | ❌ | sonnet | **Refuted by the repo's own live capture.** `docs/BLE_PROTOCOL_VERIFIED.md` §4b records a real connection to the dashboard showing **7 characteristics** (2 NUS + 5 MiIO: control, beaconkey, auth, token, device-id), not 3. The claim conflated 3 static `characteristic_add` **call sites** with the runtime object count — one call site plausibly sits in a loop over a table. |
| C-086 | INTERNAL | nRF51 peripheral base addresses (§6 table, 9 entries) | ✅ | sonnet | All 9 correct per the nRF51 APB peripheral-ID layout, cross-consistent across three repo files. |
| C-087 | INTERNAL | Peripheral reference counts (§6 table) | ⚠️ | sonnet | 5 of 9 exact (UART0 11, ADC 6, WDT 7, GPIOTE 2, POWER/CLOCK 4). TIMER1 5 vs 6 claimed, GPIO_P0 18 vs 20. **RTC1 11 vs 16 and UICR 4 vs 7** are moderate mismatches. Which peripherals are present is unaffected. |
| C-088 | INTERNAL | Three repo files give mutually contradictory SVC tables | ✅ | sonnet | `RE_NRF51_DASHBOARD.md` §7, `nrf51_hal.h`'s `svc` namespace, and `analyze_nrf51822.py`'s `SOFTDEVICE_SVC_RANGES` disagree with each other and with Nordic. |
| C-089 | DERIVED | SP `0x20003DE8` proves a 16 KB QFAA part | ⚠️ | sonnet | Suggestive, not proof — software may place SP anywhere. It sits 536 B below the QFAA RAM top, the natural spot; under QFAC (32 KB) it would sit mid-RAM, atypical but legal. `MCU_IDENTIFICATION.md` already hedges this correctly; `RE_NRF51_DASHBOARD.md`'s table states it flatly and should adopt the hedge. |

## J. Register map, VESC Lisp & IAP docs

| id | class | claim | verdict | model | evidence |
|---|---|---|---|---|---|
| C-140 | INTERNAL | BMS register file at RAM `0x20000400` | ✅ | sonnet | Bytes `00 04 00 20` at file offset `0x474C`, loaded by `ldr r2,[pc,#224]` @ VMA `0x08005668`, followed by `lsls r0,r0,#1 / adds r0,r0,r2` (= `&regfile[ARG]`) inside the real dispatcher @ `0x08005610`. |
| C-141 | INTERNAL | ESC register file at RAM `0x200007D6`, sourced from `DRV_1.2.6` | ⚠️ | sonnet | The **address is real but the cited image is wrong.** `D6 07 00 20` occurs **zero times** in `DRV_1.2.6.bin`; the cited instructions there are unrelated math. It occurs **33×** in `DRV_1.6.13_Compat.bin`, where VMA `0x08005680` is exactly `add.w r0,r7,fp,lsl #1` as described. Same `DRV_1.2.6`→`DRV_1.6.13` mis-citation as C-121 — a **systematic** attribution error across `REGISTER_MAP.md:12-14`, `protocol.md:133`, `VERIFICATION_REPORT.md:99`. |
| C-142 | INTERNAL | `REGISTER_MAP.md` marks provenance per row | ⚠️ | sonnet | One blanket statement (mechanism hardware-verified; semantics community-sourced) plus a few call-outs. ~40 rows carry no per-row citation. Honest overall, but not per-row as implied. |
| C-143 | INTERNAL | Legacy `protocol.md` register errors are flagged inline | ⚠️ | sonnet | The wrong values persist verbatim (`0x3A`=voltage :193, `0x7B`=speed-limit :206, `0x30-0x39`=cells :224), flagged by a **note above the table**, not per row. `REGISTER_MAP.md` does carry the corrected values (`0x48`, KERS, `0x40-0x49`). |
| C-144 | INTERNAL | `g30_dash.lisp`: throttle byte 5 / brake byte 6; `0x64` order mode·batt·light·beep·speed·error; checksum `sum^0xFFFF` LE; `pin-mode-in-pu` for the button | ✅ | sonnet | All confirmed from source: `adc-input` lines 81-82; `update-dash` lines 152-195; checksum builder lines 197-204; `pin-mode-in-pu` line 44 read via `gpio-read 'pin-rx` line 379. |
| C-145 | INTERNAL | The `0x64` frame has LEN=6 vs an observed LEN=7 | ✅ | sonnet | Not a contradiction — **two different frames**. The Lisp emits ESC→dash with LEN=6 (line 57). The `5A A5 07 21 20 64 00` seen after the fault clears is dash→ESC. The docs never state this, and the LEN=7 payload layout is not reverse-engineered anywhere in the repo. |
| C-146 | INTERNAL | `docs/iap-update-protocol.md` uses the correct LEN convention | ❌ | sonnet | **Refuted — still wrong today.** Despite a correction note at line 110, two worked examples in the same file still compute `LEN = 4 + payload`: line 150 (`LEN = 4 + 4 = 8`, should be `04`) and line 187 (`LEN = 4 + 2 + 64 = 0x46`, should be `0x42`). |
| C-147 | INTERNAL | `PROTOCOL_V2.md`: "NB+ is implemented in `lib/ninebot-protocol/`" | ❌ | sonnet | **Refuted.** No `nbx.*` file exists; repo-wide grep for `nbx_crc16` / `NBX_` / `0x5AA6` returns zero hits. The rest of the document is properly hedged as design-stage; this one closing line asserts present-tense fact. |
| C-148 | EXTERNAL | IAP opcodes "confirmed vs the official Ninebot protocol PDF" | ❓ | sonnet | The PDF is **not in the repo**; its only citation is a URL that returns **HTTP 403**. Unverifiable in either direction. Note `iap-update-protocol.md` itself disclaims the walkthrough (lines 117-125) as "not what the stock G30 app uses", and the live bus ignored those commands — so "confirmed" means "matches a written spec", never "works on this hardware". |

## I. Enter-update / CMD 0x57 authentication gate

| id | class | claim | verdict | model | evidence |
|---|---|---|---|---|---|
| C-120 | INTERNAL | Password = `~(UID0+UID1+UID2) ‖ ~(UID0·UID1·UID2)`, two LE words | ✅ | sonnet | **Byte-exact confirmed** in `DRV_1.2.6` at a dedicated compare routine @ `0x08002078`: `adds/add` → `mvns r4,r4` compared against payload word 0; `muls r2,r1; muls r3,r2` → `mvns r1,r3` compared against payload word 1; returns 1 only if both match. |
| C-121 | INTERNAL | `App_to_ESC_handler @0x08005624`, `tbb @0x08005650`, from `DRV_1.2.6` / `BMS_1.7.4.5` | ❌ | sonnet | **Refuted for both cited images.** In `DRV_1.2.6`, `0x08005624` is `sub.w r3,r6,sl` inside unrelated motor-duty code. `BMS_1.7.4.5` contains **no `tbb`/`tbh` instruction anywhere**. Real locations: `DRV_1.2.6` handler `0x08004E20`, tbb `0x08004E48`; `BMS_1.7.4.5` uses a linear cmp-chain at `0x08005610`. The claimed pair matches a **third, uncited image** — `DRV_1.6.13_Compat` (handler exact at `0x08005624`, tbb 4 bytes off at `0x0800564C`) — so the addresses were sourced from 1.6.13 and mis-attributed. Anyone opening `DRV_1.2.6` at `0x08005624` to patch the dispatcher lands in unrelated code. |
| C-122 | INTERNAL | CMD table includes `0x18/0x50/0x57/0x58/0x59/0x5C`; `0x57` and `0x59` share a handler | ✅ | sonnet | Confirmed at the *real* dispatcher `DRV_1.2.6` `0x08004E48`. |
| C-123 | INTERNAL | UID `0x1FFFF7E8` referenced at VMA `0x08005478` | ⚠️ | sonnet | Exact **for `DRV_1.2.6` only** (loaded 3× for UID0/1/2). In `DRV_1.6.13_Compat` the literal sits at `0x08005E58`. In **`BMS_1.7.4.5` the literal is absent entirely** — the BMS never reads the STM32 UID this way, contradicting the docs' implied joint DRV+BMS sourcing. |
| C-124 | INTERNAL | Valid password → `0x5A5A` marker → `NVIC_SystemReset` | ⚠️ | sonnet | **Fully traced for BMS**: literal `0x00005A5A` @`0x080031F0` → `strh` to `0x20000400` → flash commit to `0x0800F000` → `AIRCR = 0x05FA0004`. **For DRV the `0x5A5A` value is absent** from both images (exhaustive raw-byte + movw-immediate scan). The mechanism is real (`0x0801C000` erase/program at `0x08005BBC` and `0x08007DA4`, plus a textbook reset at `0x080051EA`) but the specific magic constant is unconfirmed for DRV. |
| C-125 | INTERNAL | CMD `0x18` is calibration (sub-cmd `0x12` + `"N4G"`), not reset | ✅ | sonnet | Byte-exact: `DRV_1.2.6` @`0x08004FC8` compares packet offsets 5/6/7 against `'N'`,`'4'`,`'G'`; backing string `N4GEA1601C0001` @`0x08007F80`. |

## H. Tests, gates & tooling (all re-run live, not read from docs)

| id | class | claim | verdict | model | evidence |
|---|---|---|---|---|---|
| C-100 | INTERNAL | `/verify-safe` ends "VERDICT: SAFE to flash, UPDATE path preserved, SECURE BOOT sound." | ✅ | sonnet | Re-run from a clean rebuild; verbatim match, exit 0. |
| C-101 | DERIVED | `/verify-safe` proves the **no-brick** guarantee | ❌ | **opus** | **Refuted, and worse than first reported.** Confirmed via a `sys.addaudithook` tracer on `subprocess.Popen` across a full gate run: 14 processes launched, **no `ctest`, no `dashboard_sim`, no `dashboard_persist_sim`, no `ble_sim`**. `build_dashboard.py:105-108` hardcodes `firmware_tests.exe`. But the Sonnet framing ("compiled but not exercised") was **too generous** — the no-brick assertions **no longer exist at all**. They lived in `firmware/dashboard/sim/sim_main.cpp:155-175` (`flash_writes == 0`, `!bricked`, valid-vector), deleted with the STM32 target in `b60cb86` and never re-created for nRF51. The surviving `FlashSim` models only a bare 4 KB odometer region with region-relative offsets and **no bounds check**, so it structurally cannot detect a protected-region write. Two independent defects: ctest is never invoked (3 of 4 test binaries silently skipped), *and* even wiring in ctest would not restore promises (a) and (c). |
| C-101a | DERIVED | Promise: "no bootloader/option-byte writes" | ❌ | opus | NOT PROVEN — no executed test asserts it. Mitigating: the shipped 5584-byte image has **zero NVMC references**, so it currently has no flash-write path at all. That protection is by construction, not by test, and evaporates the moment `nrf51_persist.cpp` (written, currently compiled by nothing) is added to the Makefile. |
| C-101b | DERIVED | Promise: "valid vector table" | ✅ | opus | PROVEN — `build_dashboard.py:50-63` `validate_bin()` runs unconditionally; observed `SP=0x20004000 reset=0x00018121 -> OK`. The one no-brick promise the gate genuinely keeps. |
| C-101c | DERIVED | Promise: "watchdog recovers" | ❌ | opus | NOT PROVEN — the fire-and-recover scenarios were in the deleted sim. What remains is CRV arithmetic and `shouldFeed()` policy, neither executed by the gate, neither exercising a reset. Separately, the shipped image writes only WDT `RR[0]` and never `CRV`/`RREN`/`CONFIG`/`TASKS_START` — **the watchdog is never started on target today**. |
| C-101d | DERIVED | Promise: "update path preserved" | ⚠️ | opus | PARTIALLY PROVEN — "bootloader-loadable" is proven by the same vector check; "never touches the bootloader region" is not. The docstring at `verify_firmware_safe.py:11` still cites the obsolete STM32 address `0x08001000`; the real check uses `0x00018000`. |
| C-101e | DERIVED | Promise: "secure boot verifies, both targets build" | ⚠️ | opus | Signed-accept + tamper-reject **PROVEN** (9/9: valid P-256 point, plus five distinct rejections — flipped firmware byte, flipped signature byte, wrong key, corrupted magic, wrong target board). **"Both targets build" is obsolete and misleading** — `bootloader/stm32/` is stripped to an empty stale `build/`; the script correctly builds only nRF51. `.claude/commands/verify-safe.md` is the worst offender, still telling the operator the gate checks `0x08000000` and option-byte/RDP flash on a board with no STM32. |
| C-102 | INTERNAL | Secure boot 9/9 + signed-accept / tamper-reject | ✅ | sonnet | `tools/verify_secureboot.py` re-run: 9 passed, 0 failed; nRF51 bootloader built (`text=8100`); verdict line reproduced. |
| C-103 | INTERNAL | Regression suite is 157/157 | ✅ | sonnet | Fresh build + run: 157 passed, 0 failed, 482 assertions. (`firmware/decompiled/README.md:84` still says "134 tests" — stale.) |
| C-104 | INTERNAL | `test_decompiled_protocol.cpp` is 26/26 | ❌ | sonnet | **Refuted.** Actual: **45/45**. The repo states three different numbers for one file — `RE_FINDINGS.md` 26/26, `firmware/decompiled/README.md` 35/35, `DECOMPILATION.md` 45/45. Only the last is right. |
| C-105 | INTERNAL | "full ctest 3/3" | ⚠️ | sonnet | ctest now registers **4** tests (a `dashboard_persist_sim` was added); all 4 pass. |
| C-106 | INTERNAL | BLE session simulator 14/14 | ✅ | sonnet | Reproduced exactly. |
| C-107 | INTERNAL | `test_c5_prog` 6/6 | ✅ | sonnet | Reproduced exactly. |
| C-108 | INTERNAL | `dash_tap_sim` "9/9 + parser 6/6" | ⚠️ | sonnet | Wire-finder half reproduces (9/9). **Parser half is broken:** `tools/dump_bootloader_c5.py:30` imports the deleted `tools/dump_bootloader.py` → `ModuleNotFoundError`. `tools/dash_tap_sim.py` ends **"VERDICT: FAILED"** and `tools/verify_c5_flash.py` ends **"VERDICT: NO-GO"**, both exit non-zero, today. |
| C-109 | INTERNAL | "16/16 sim checks", "6/6 reloc build", "11/11 IAP chain", "both targets build" | ❌ | sonnet | **No artifact left to run.** `firmware/dashboard/sim/`, `tools/verify_reloc_build.py`, `firmware/dash-tap-c542/sim/test_iap_chain.cpp` and all of `bootloader/stm32/**` are deleted. `-DTARGET_BOARD=ble` now hard-errors, so "both targets build" is structurally impossible — exactly one target exists. |
| C-110 | INTERNAL | `tools/dashboard_sim.py` is current | ⚠️ | sonnet | Line 27 still points at the deleted STM32 `firmware/dashboard`; fails a `make` step every run, degrades to a warning, then runs the nRF51 sim successfully. Dead code path. |

## K. Concrete corrections to apply

Ordered by consequence. Each is a specific, mechanical edit.

**Wrong facts — a reader acting on these gets a wrong result**

1. `docs/protocol.md:86` — checksum `7C FF` / `0xFF7C` → **`80 FF` / `0xFF80`** (stale from the old `LEN=06` example).
2. `Documentation/VERIFICATION_REPORT.md:37` — same wrong checksum, additionally described as "self-consistent". Fix both.
3. `firmware/decompiled/nrf51822/RE_NRF51_DASHBOARD.md:41,76` and `boards/ble-dashboard/MCU_IDENTIFICATION.md` — `tm1637_display_off()` `0x00019D96` → **`0x00019D98`**.
4. `RE_NRF51_DASHBOARD.md` §7 — GAP SVC numbers are each −4 (wrong base `0x6C`, real `0x70`): `conn_param_update` 113→**117**, `ppcp_set` 118→**122**, `authenticate` 122→**126**, `auth_key_reply` 124→**128**. Flash SVCs "~39–41" → **32/33**. Note that 126/128/32/33 never appear in the image, so the GAP-authenticate and flash-SVC claims should be dropped, not just renumbered.
5. `docs/iap-update-protocol.md:150,187` — worked examples still compute `LEN = 4 + payload`; should be `04` and `0x42`. Contradicts the correction note at line 110 of the same file.
6. `docs/REGISTER_MAP.md:12-14`, `docs/protocol.md:133`, `Documentation/VERIFICATION_REPORT.md:99` — ESC register-file evidence cited to `DRV_1.2.6`; the literal is absent there. Re-cite **`DRV_1.6.13_Compat`**.
7. `Documentation/VERIFICATION_REPORT.md` HW6 + `docs/protocol.md` + `docs/REGISTER_MAP.md` + `firmware/decompiled/DECOMPILATION.md` — `App_to_ESC_handler @0x08005624` / `tbb @0x08005650` are wrong for both cited images. Use **`DRV_1.2.6`: `0x08004E20` / `0x08004E48`**; **`BMS_1.7.4.5`: `0x08005610`, no `tbb`**. Drop the implication that BMS references `0x1FFFF7E8` — it does not.
8. `docs/PROTOCOL_V2.md:144` — "NB+ is implemented in `lib/ninebot-protocol/`" → it is not; no `nbx.*` exists. Re-mark as design-stage.
9. `CLAUDE.md` — `0x0003C000` "= stock `UICR.BOOTLOADERADDR`" → state as the intended/conventional value; UICR has never been read because no SWD dump exists.
10. `firmware/decompiled/RE_FINDINGS.md` — `BMS_1.3.4.bin` "encrypted (XiaoTEA)" → **format unidentified**; remove the `analyze_bootloader.py` "key schedule" citation, which does not exist.

**BLE authentication — replace the MiIO-token account wherever it appears**

Affected: `docs/BLE_PROTOCOL_VERIFIED.md` (§4b, §4 table, §5, the "two-channel wall" callout),
`Documentation/VERIFICATION_REPORT.md:101,102,107`, `Documentation/PROJECT_DOC.md:122,152`,
`Documentation/CHANGE_LOG.md:12,17-18`, `boards/ble-dashboard/C542_BUS_CAPTURE.md:144,148-149`,
`docs/protocol.md:295-297`, `docs/C542_PROGRAMMER_SCHEMATIC.md:85,90`,
`docs/iap-update-protocol.md:320`, `firmware/decompiled/RE_FINDINGS.md:32-34,98-99`,
`firmware/decompiled/nrf51822/nrf51822-reprogramming.md:302,435`.

10a. Replace "the relay is keyed by the MiIO registration token" with the Encryption2 account
     everywhere. Keep the verified live-device facts (name, MAC, NUS present, `0xfe95` present with
     its five characteristics, product `0x035C`) — only the *conclusion* was wrong.
10b. `RE_FINDINGS.md:98-99` — `LEN + 0x0D` is **not** "the Xiaomi MiIO path"; it is the Encryption2
     encrypted-frame size.
10c. `CLAUDE.md` — the Encryption2 line is right, but add: preamble `5A A5` (never `5A B5`), **Gen2**
     so key = `SHA1(btName ‖ fw_data)`, and board address `0x21` (not `0x04`).
10d. Explain the zero-notification result correctly: plaintext `5A A5` frames are `LEN + 9` while the
     firmware expects `LEN + 13` and decrypts. Remove the "supply the MiIO token / python-miio"
     remediation — it is a dead end.
10e. **Code fix** — `tools/ble/pre_comm_probe.py`, `pre_comm_wait.py`, `pre_comm_grab.py`: use DST
     `0x21`, drop the `5A B5` combos, and pass `key2 = fw_data`. Or simply drive them from
     `tools/ble/ninebot_crypto.py`, which already matches the firmware exactly.

**Broken commands and code**

11. `Documentation/PROJECT_DOC.md:73` and `docs/guides/BUILD.md:54` — build command omits `-DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m0.cmake` and `PROJECT_DOC` passes the never-valid `BOARD_BLE_STM32`. As written the build silently uses the host compiler and fails.
12. `tools/dump_bootloader_c5.py:30` — `import dump_bootloader` targets a deleted module. This makes `tools/dash_tap_sim.py` exit **FAILED** and `tools/verify_c5_flash.py` exit **NO-GO** today.
13. `tools/dashboard_sim.py:27` — still points at the deleted STM32 `firmware/dashboard`; fails a `make` every run.

**Stale counts**

14. `test_decompiled_protocol.cpp` is **45/45** — `RE_FINDINGS.md:108` says 26/26, `firmware/decompiled/README.md:84` says 35/35. Also that same line says "134 tests" for `firmware_tests`; actual is **157**.
15. "full ctest 3/3" → **4/4** (a `dashboard_persist_sim` test was added).
16. "16/16 sim", "6/6 reloc build", "11/11 IAP chain", "both targets build" — the artifacts are deleted; mark these CHANGE_LOG entries as superseded rather than leaving them as live claims.

**Method footnotes (numbers are method-dependent, not wrong)**

17. `MCU_IDENTIFICATION.md` peripheral-count table — footnote the counting method (unaligned, narrow address list), or restate with the aligned numbers.
18. Address-immediate counts 35/13/6 — mark as **`movs`-only**; `movs`+`cmp` totals are 42/14/6.
19. `RE_FINDINGS.md` §1 reset-vector column — footnote "Thumb bit cleared", or switch to raw values to match `MCU_IDENTIFICATION.md`.
20. `RE_FINDINGS.md` §2 — the dashboard has **2** parser instances, not 1 (second at `0x0001A030`); and the scan is windowed, not "immediately followed by" (which finds zero).

**Systemic**

21. ~35 files still assert an STM32 dashboard, including `docs/guides/DEPLOYMENT.md`, `docs/guides/BUILD.md`, `docs/guides/HARDWARE.md`, `bootloader/README.md` (body), the whole power-management family, and `Documentation/Requirements/requirements.md`. `CLAUDE.md` cites `DEPLOYMENT.md` for "full detail" two lines before disowning its content.
22. Intra-file self-contradictions in `README.md`, `boards/ble-dashboard/PINOUT.md`, `boards/ble-dashboard/README.md`, and `DASHBOARD_PINOUT_RESEARCH.md` §B3 (an "Internal STM32 ↔ nRF51 UART" still tagged **[CONFIRMED]**).
23. `HANDSOFF.md:53` lists the stray empty `firmware-decompiled/` as a project directory; the real one is `firmware/decompiled/`.

## G. BLE authentication — escalated

| id | class | claim | verdict | model | evidence |
|---|---|---|---|---|---|
| C-090 | DERIVED | The BLE relay is gated by the **Xiaomi MiIO registration token** | ❌ | **opus** | **Refuted at the firmware level.** The NUS write handler (`ble_nus_on_ble_evt` @`0x1B3B4`) routes writes straight to `data_handler` @`0x1F2AC`, which feeds every byte to the Ninebot parser @`0x18450`. **No MiIO/bond check exists on that path**, and the MiIO code never references the Ninebot state struct `0x2000275C` or the crypto buffers. MiIO (`0xfe95`) is genuinely present but gates Mi Home pairing only. A MiIO token is neither necessary nor sufficient. |
| C-091 | INTERNAL | The relay is gated by Ninebot **Encryption2** over NUS | ✅ | **opus** | Confirmed: command dispatcher @`0x19990` with `cmp r1,#0x5B / #0x5C / #0x5D`, each gated on the encryption-enabled flag; the handlers' log-stripped debug strings are the vendor's own phase names (前置信息 / 设置密码 / 认证通过). AES via `SVC 77` (`sd_ecb_block_encrypt`). |
| C-092 | INTERNAL | Encryption2 params: **Gen3**, preamble `5A B5`, key `SHA1(btName ‖ 16 zeros)`, board `0x04` | ❌ | **opus** | **All three parameters refuted.** (i) The `fw_data` constant `97 CF B8 02 84 41 43 DE 56 00 2B 3B 34 78 0A 5D` sits at VMA `0x204F4` with three code xrefs, and `set_key` @`0x1B9F0` substitutes it — **not zeros** — for a NULL `key2` ⇒ **Gen2**, key = `SHA1(btName ‖ fw_data)`. (ii) The parser accepts only `5A A5` (`0x1854E`/`0x18554`); **`5A B5` appears nowhere** in the image (it is WiFi-v2-only). (iii) The dashboard answers only DST `0x21` or `0xFF`; **`0x04` is silently discarded**. |
| C-093 | DERIVED | The 2026-06-17 silence was caused by the dashboard STM32 control-plane being asleep | ❌ | **opus** | **Refuted — impossible mechanism** (no STM32 exists), and the supporting premise "a wrong key would ECHO" is false for this firmware. Three real causes, all hit simultaneously: wrong DST (`0x04`), wrong `key2` (zeros instead of `fw_data`), and a genuine bus dependency — the `0x5B` handler @`0x199B8` does not answer the phone but queues a register-`0x10` read onto the **wired** bus toward `0x20`, replying only once a peer answers. With the ESC absent, a byte-perfect PRE_COMM still gets nothing. |
| C-094 | INTERNAL | `LEN + 0x0D` is "the Xiaomi MiIO path" | ❌ | **opus** | **Misattribution.** `LEN + 13` is the **Encryption2 encrypted-frame size**, not a MiIO wrapper. Affects `RE_FINDINGS.md:98-99`. |
| C-095 | EXTERNAL | The BLE secret is retrievable from the Ninebot/Xiaomi cloud | ❌ | **opus** | The Encryption2 session password is generated locally at pairing and stored on the scooter and the paired phone (`{SerialNumber}_decrypt` in `com.ninebot.segway`). Cloud routes are a dead end. |
| C-096 | INTERNAL | `tools/ble/ninebot_crypto.py` matches the firmware | ✅ | **opus** | The repo's own port is **correct**. The failing probes (`pre_comm_probe.py` etc.) called the vendored reference CLI instead, which uses the wrong Gen3 parameters. |

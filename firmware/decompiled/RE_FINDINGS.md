# Firmware Re-Disassembly Findings — Ninebot G30 Max Stock Dumps

**Generated:** 2026-06-02
**Tool:** [`tools/analysis/disassemble_firmware.py`](../../tools/analysis/disassemble_firmware.py) (arch-aware harness, capstone 5.0.7)
**Method:** Static disassembly (ARM Thumb), vector-table arch detection, literal-pool peripheral
reference scan, byte-pattern scan for Thumb `cmp Rn,#imm` header encodings, string extraction.

> This report supersedes the older, partly-incorrect notes in
> `firmware/decompiled/nrf51822/nrf51822_analysis_*.txt` (whose reset-handler disassembly was
> garbled because it disassembled from the odd Thumb-bit address) and the broken
> path assumptions in the previous version of `disassemble_firmware.py`.

---

## 1. Image classification (ground truth)

| Image | Size | Entropy | Arch (from vector table) | Load base | SP / Reset | Verdict |
|-------|------|---------|--------------------------|-----------|-----------|---------|
| `DRV_1.2.6` | 30076 B | 6.99 | **Cortex-M3 (STM32F103)** | `0x08001000` | `0x20002B18` / `0x08001100` | OK |
| `DRV_1.6.13_Compat` | 33388 B | 7.00 | **Cortex-M3 (STM32F103)** | `0x08001000` | `0x20002950` / `0x08001188` | OK |
| `BLE_1.1.0` | 33612 B | 6.95 | **Cortex-M0 (nRF51822)** | `0x00018000` | `0x20003D10` / `0x00018154` | OK — **not STM32** |
| `BLE_1.1.7` | 34252 B | 6.96 | **Cortex-M0 (nRF51822)** | `0x00018000` | `0x20003DE8` / `0x00018154` | OK — **not STM32** |
| `BMS_1.3.4` | 13956 B | 6.75 | unrecognized | — | `0xF9C10082` / `0x5BC60082` | **ENCRYPTED** |
| `BMS_1.7.4.5` | 23596 B | 6.83 | **Cortex-M3 (STM32F103)** | `0x08001000` | `0x200017C8` / `0x080010D8` | OK |

### Key correction (finding F1)
`BLE_1.1.0.bin` / `BLE_1.1.7.bin` are **nRF51822 (Cortex-M0) application images** loaded at
`0x00018000` (immediately after the Nordic **S110 SoftDevice**). Evidence:
- Reset vector `0x00018154` and all handlers in `0x18xxx`–`0x20xxx` (post-SoftDevice app region).
- Only **Nordic UART0** peripheral registers referenced (`0x40002000`/`…2100`/`…2300`/`…2500`) —
  **no** STM32 USART/GPIO/TIM register literals.
- Strings reveal the **Xiaomi MiIO BLE** auth/cloud-bind framework + Nordic PSM
  (`"decrypted auth"`, `"cloud bind succ"`, `"Register succ, new token, encrypt sn, beaconkey"`,
  `"psm callback"`).

There is **no STM32-dashboard firmware dump in this repository.** Any STM32 BLE-board pinout/peripheral
claim cannot be sourced from these binaries (see §4 and the verification report).

### BMS_1.3.4 (finding F3)
No valid Cortex vector table at offset 0 (`SP=0xF9C10082`, `reset=0x5BC60082`); whole-image entropy 6.75.
This image is **encrypted/obfuscated** (consistent with Ninebot's XiaoTEA-encrypted distribution
format — see the key schedule in `tools/analysis/analyze_bootloader.py`). Static disassembly is not
meaningful until decrypted; the harness now reports this instead of emitting noise.

---

## 2. Protocol verification — Ninebot `5A A5` framing  ✅ CONFIRMED

The documented header state machine in [`docs/protocol.md`](../../docs/protocol.md) is present and
**byte-for-byte identical** across the genuine images. Each parser: compare RX byte to `0x5A`
(set "saw-header-1" flag); else compare to `0xA5` and require the flag; otherwise reset.

**ESC — `DRV_1.2.6` @ `0x08006936`** (one of three identical copies, one per USART):
```asm
0x08006936: cmp   r0, #0x5a      ; rx == 0x5A ?
0x08006938: ldrb  r1, [r1, #7]   ; load "saw 0x5A" state flag
0x0800693A: beq   #0x800694a     ;   yes -> set flag (0x694e: strb r2,[r4,#7])
0x0800693C: cmp   r0, #0xa5      ; rx == 0xA5 ?
0x0800693E: bne   #0x80068ec     ;   no  -> reset parser
0x08006940: cmp   r1, #0         ; previous byte was 0x5A ?
0x08006942: beq   #0x80068ec     ;   no  -> reset parser
0x08006944: strb  r2, [r4, #8]   ;   yes -> header complete, begin packet
```

**nRF51 BLE — `BLE_1.1.7` @ `0x0001854E`** (identical logic):
```asm
0x0001854E: cmp   r0, #0x5a
0x00018552: beq   #0x18566
0x00018554: cmp   r0, #0xa5
0x00018556: bne   #0x1847c
0x00018558: cmp   r1, #0          ; state flag
```

Header-parser instances found (robust byte-pattern scan of `cmp Rn,#0x5A` immediately followed by
`cmp Rn,#0xA5`):

| Image | `5A A5` parser instances | Interpretation |
|-------|--------------------------|----------------|
| `DRV_1.2.6` | 3 (offsets 0x5936/0x5C56/0x5F7C) | one per USART1/2/3 (debug / BLE / BMS buses) |
| `DRV_1.6.13` | 3 | same |
| `BMS_1.7.4.5` | 1 (parser @0x08002E4C) | single ESC UART link |
| `BLE_1.1.7` | 1 (offset 0x054E → addr 0x1854E) | phone/ESC bridge |

> Note: a naïve linear disassembly sweep *misses* these (literal pools shift Thumb alignment).
> The harness therefore also does a byte-pattern scan of the `cmp Rn,#imm8` encoding.

**Device addresses** (`0x20`=ESC, `0x21`=BLE, `0x22`=BMS, `0x3E`=App) appear as `cmp`/`movs`
immediates consistent with each board recognizing its own and peer addresses (DRV references
`0x20`+`0x22`; BLE references `0x20`+`0x21`). Directionally confirms the address map; counts are
approximate (linear-sweep).

**Checksum** (`sum(len..payload) ^ 0xFFFF`, little-endian) is self-consistent: the `docs/protocol.md`
worked example bytes `02 3E 20 01 10 0E 00` → sum `0x007F`… (the checksum is computed over LEN..payload).

**Cross-board protocol-core identity:** the ESC (`DRV_1.6.13`, parser @`0x08007128`), BMS
(`BMS_1.7.4.5`, parser @`0x08002E4C`) and nRF51 BLE (`BLE_1.1.7`, parser @`0x00018450`) all implement
the **byte-identical** `5A A5` header detect + `~(Σ − ckLo)` checksum core. The STM32 boards use
`expected = LEN + 7` (rx caps: ESC `0xF3`, BMS `0xC2`); the nRF51 bridge uses `LEN + 8` (normal) /
`LEN + 0x0D` (Xiaomi MiIO path), cap `0x8F`, reflecting its extra BLE/MiIO wrapper bytes.
Both expose an **ARG-indexed 16-bit register file** (ESC @`0x200007D6`, BMS @`0x20000400`) with
**CMD 1 = READ / CMD 2 = WRITE**. So `docs/protocol.md`'s ESC/BMS register maps are ARG indices into
those files. Full detail in [DECOMPILATION.md](DECOMPILATION.md) §4b/§4d.

**LEN convention (decompiled & verified — see [DECOMPILATION.md](DECOMPILATION.md)):** `buildPacket`
@`0x080036AC` stores `LEN = payload byte count` and `parseProtocolByte` @`0x08007128` computes
`expected body = LEN + 7` (frame total = `LEN + 9`). This corrects both `docs/protocol.md` (whose
example wrongly used `LEN=06` for a 2-byte payload) and `firmware/decompiled/common/include/protocol.h`
(which used `LEN = payload + 6`). A standalone test (`tests/test_decompiled_protocol.cpp`, 26/26) proves
the byte-faithful C++ reproduction in `common/include/ninebot_protocol_verified.hpp`.

**Baud / 8N1**: STM32 images carry the canonical 115200 BRR words — `0x0271` (72 MHz APB2 / USART1)
in all three; `0x0139` (36 MHz APB1 / USART2-3) additionally in `BMS_1.7.4.5`. Matches 115200 8N1.

---

## 3. ESC (DRV) peripheral / pinout verification  ✅ STRONG SUPPORT

Literal-pool references in `DRV_1.2.6` / `DRV_1.6.13`:

| Peripheral | refs (1.2.6 / 1.6.13) | Supports pinout claim |
|------------|----------------------|------------------------|
| `TIM1_CR1` | 13 / 15 | 3-phase motor PWM (CH1-3 + CH1N-3N) ✅ |
| `TIM3_CR1` | 6 / 8 | Hall-sensor input capture ✅ |
| `USART1_SR/DR` | 4+1 / 4+1 | external/debug UART ✅ |
| `USART2_SR/DR` | 3+1 / 3+1 | BLE-dashboard UART ✅ |
| `USART3_SR/DR` | 3+1 / 3+1 | BMS UART ✅ |
| `GPIOA_CRL`, `GPIOB_CRL` | 8/7 , 8/8 | extensive GPIO config ✅ |

This corroborates `boards/esc-motor/PINOUT.md` (TIM1 motor bridge, TIM3 Hall, three USART buses).
**USART RX is not interrupt-driven**: the USART1/2/3 vectors all point to the common
default handler (`0x0800111A`) — RX is handled by polling/DMA in the main loop, not per-byte IRQ.
(The PINOUT's "USART1/2/3 shared interrupt parser" wording should be read as polled, not ISR.)

String `NBScooter0001` and (1.6.13) `Scooter_G30_SAT` confirm the G30 platform.

---

## 4. BMS peripheral / pinout verification  ⚠️ PARTIAL

`BMS_1.7.4.5` literal refs: `USART1_SR`×4, `USART2_SR`×1, `TIM1`×6, `TIM2`×1, `TIM3`×1.
- Dual UART (USART1 debug/factory + USART2 ESC) ✅ matches pinout.
- **No `I2C1`/`I2C2` register literal found** (neither `0x40005400` nor `0x40005410`/`0x40005800`).
  The pinout claims a **hardware I2C1 (PB6/PB7)** link to the BQ76940. Hardware I2C1 is **not
  evidenced** in this image — BQ76940 access is most likely **software/bit-banged I2C on GPIO**
  (common in these BMS designs), or the AFE-comms code lives outside the analyzed region. Flag the
  PINOUT "I2C1 (hardware)" attribution as **unverified** pending confirmation.
- No `GPIOx_CRL/CRH` literal refs either → GPIO config likely via computed addresses; not disprovable.

String `G30_HD_HDPRO_VXX` confirms the G30 HD/HD-Pro platform.

`BMS_1.3.4` cannot be analyzed (encrypted, §1).

---

## 5. nRF51 BLE firmware  ✅ (corrected scope)

`BLE_1.1.0` / `BLE_1.1.7`: nRF51822 Cortex-M0, S110 SoftDevice, app @ `0x00018000`.
- **Nordic UART0** at `0x40002xxx` (ENABLE / INTEN / EVENTS_CTS / TASKS_STARTRX) — the STM32↔nRF
  bridge, 115200, from the nRF side.
- **Ninebot `5A A5` parser** present (§2) — the BLE module frames Ninebot packets toward the phone.
- **Xiaomi MiIO** auth/bind/login flow + Nordic **PSM** persistent storage; firmware update path is
  MiIO-over-BLE (SoftDevice `sd_flash_*` SVCs), not the STM32 IAP path.

The STM32 dashboard MCU's own pinout/peripherals are **not derivable** from these images.

---

## 6. Summary of corrections to existing docs

1. BLE `.bin` dumps are nRF51822, not STM32 — fix every doc that treats them as STM32 dashboard FW.
2. `boards/ble-dashboard/PINOUT.md` STM32 table provenance ("extracted from BLE_1.1.x analysis") is
   invalid — reattribute to reference-design/community knowledge.
3. `BMS_1.3.4.bin` is encrypted — label it as such; analysis requires decryption first.
4. BMS hardware-I2C1 claim is unverified — likely bit-banged.
5. `docs/protocol.md` `5A A5` framing, addresses, checksum, and 115200 8N1 are **confirmed**.

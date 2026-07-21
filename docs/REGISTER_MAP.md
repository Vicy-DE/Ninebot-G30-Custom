# Ninebot G30 Max — Register Map (firmware-grounded + authoritative)

**Date:** 2026-06-03
**Mechanism (firmware-verified):** every board exposes a flat **16-bit register file indexed by the
ARG byte**; `CMD 0x01`=READ (response cmd `0x04`), `CMD 0x02`=WRITE+response, `0x03`=WRITE no-response,
`0x07–0x09`=firmware update, `0x64`=head I/O. Register-file bases:
ESC `@0x200007D6`, BMS `@0x20000400` (see `firmware/decompiled/DECOMPILATION.md` §4b/§4d).

> ✅ **Hardware-verified (2026-06-15).** The ARG-indexed 16-bit register-file mechanism, the address
> scheme, and the live `0x65`/`0x64` head-I/O frames were confirmed on the **live G30 scooter** via the
> NUCLEO-C542RC software-UART rig, and the application command dispatch
> (`App_to_ESC_handler @0x08005624`, `tbb` table @`0x08005650`) was disassembled from `DRV_1.2.6` /
> `BMS_1.7.4.5`. The full command-opcode table (incl. the UID-gated update CMD `0x57`) and the `0x64`
> telemetry field layout are below. Source:
> [`../boards/ble-dashboard/C542_BUS_CAPTURE.md`](../boards/ble-dashboard/C542_BUS_CAPTURE.md).
**Semantics source:** [etransport/ninebot-docs](https://github.com/etransport/ninebot-docs/wiki)
(ES2 family — the G30 Max shares this protocol). Treat per-register *meanings* as
**documented/authoritative**; the *access mechanism, CMD codes, and LEN convention are
firmware-confirmed*. Some G30 registers may differ from ES2 — flagged where known.

> **LEN convention — triple-confirmed.** `bLen = length of payload only` (frame = LEN+9). Confirmed by
> (1) the ESC/BMS/nRF firmware decompilation, (2) the ninebot-docs protocol page
> ("bLen - length of bPayload"), and (3) the cross-board parsers. The repo's earlier
> `LEN = payload+4/+6` text was wrong and has been corrected.

---

## ESC (DRV) — address `0x20`, register file `@0x200007D6`

### Read-only
| ARG | Name | Size | Unit / meaning |
|-----|------|------|----------------|
| 0x00 | Magic | U16 | constant `0x515C` |
| 0x0D/0x0E/0x0F | Motor phase A/B/C current | U16 | |
| 0x1A | ESC firmware version | U16 | BCD |
| 0x1B | Error code | U16 | bitmask |
| 0x1C | Alarm/warning code | U16 | bitmask |
| 0x1D | ESC status | U16 | flags |
| 0x1F | Operation mode | U16 | 0=Normal,1=Eco,2=Sport |
| 0x20 / 0x21 | Battery 1 / 2 capacity | U16/S16 | |
| 0x22 | Battery level | U16 | 0–100 % |
| 0x24 / 0x25 | Remaining / predicted range | S16 | km×100 |
| 0x26 | Current speed | S16 | 0.1 km/h |
| 0x29 | Total distance | S32 | m |
| 0x2F | Session distance | S16 | 10 m |
| 0x32 / 0x34 | Total operation / riding time | S32 | s |
| 0x3A / 0x3B | Session operation / riding time | S16 | s |
| 0x3E | Frame temperature | S16 | 0.1 °C |
| 0x3F / 0x40 | Battery 1 / 2 temperature | S16 | 0.1 °C |
| 0x41 | MOSFET temperature | S16 | 0.1 °C |
| 0x47 | Supply voltage | S16 | (firmware special-cases this ARG) |
| 0x48 | Battery voltage | S16 | |
| 0x49 | Battery current | S16 | signed |
| 0x53 | Motor phase current | S16 | 0.01 A |
| 0x65 | Average speed | S16 | 0.1 km/h |
| 0x66 / 0x67 | External / internal BMS fw version | U16 | |
| 0x68 | BLE firmware version | U16 | |
| 0xB0–0xBF | Bulk telemetry block (error/alarm/status/levels/speed/distance/temp/speed-limit/power/range) | S16/S32 | dashboard polls this block |

### Read-write
| ARG | Name | Notes |
|-----|------|-------|
| 0x17 | Scooter PIN (6×U8) | **firmware-confirmed**: writing here sets a refresh flag (@0x080056C4) |
| 0x70 / 0x71 | Lock / Unlock | |
| 0x72 | Speed-limit control | |
| 0x73 / 0x74 | Normal / Eco speed limit | 0.1 km/h |
| 0x75 | Operating mode | 0=Normal,1=Eco,2=Sport |
| 0x77 / 0x78 / 0x79 | Motor start-stop / Reboot / Powerdown | |
| 0x7B | KERS (regen) level | ⚠️ repo's old protocol.md mislabeled this "speed limit" |
| 0x7C / 0x7D | Cruise control / Tail light | |
| 0xC6, 0xC8–0xCE | Lamp-strip mode / colors | |

### Command opcodes (not registers) — firmware-disassembled

These are **`CMD`-byte opcodes** dispatched by `App_to_ESC_handler @0x08005624` (`tbb` table
@`0x08005650`), distinct from the ARG-indexed register file above:

| CMD | Action |
|-----|--------|
| `0x01` | READ `regfile[ARG..]` (response cmd `0x04`) |
| `0x02` | WRITE `regfile[ARG..]` + response |
| `0x03` | WRITE `regfile[ARG..]`, no response |
| `0x07`–`0x0A` | Subscribe / stream register updates |
| `0x18` | **Calibration** (needs sub-cmd `0x12` + `"N4G"` magic) — **NOT a reset** |
| `0x50` | Firmware data-block write (IAP) |
| `0x57` / `0x59` | **Enter firmware update** — UID-password-gated (see below) |
| `0x58` | Erase / begin-flash (application area) |
| `0x5C` | Parameter / seed write |
| `0x64` | Head I/O telemetry (ESC→dashboard; see telemetry layout below) |
| `0x65` | Head I/O control (dashboard→ESC; throttle/brake) |

> **Enter-update is UID-authenticated.** CMD `0x57` payload =
> `~(UID0 + UID1 + UID2) ‖ ~(UID0 · UID1 · UID2)` (two 32-bit LE words), computed from the STM32 96-bit
> chip UID @`0x1FFFF7E8` (referenced at vma `0x08005478`). A valid password sets a RAM flag → the main
> loop writes a `0x5A5A` "stay in IAP" magic to a flash marker page (DRV `0x0801C000`, BMS `0x0800F000`)
> → `NVIC_SystemReset` (`AIRCR=0x05FA0004`). Writing "reg `0x78`" does **NOT** reset (unverified
> convention). See [`protocol.md`](protocol.md) and
> [`../boards/ble-dashboard/C542_BUS_CAPTURE.md`](../boards/ble-dashboard/C542_BUS_CAPTURE.md).

### 0x64 head-I/O telemetry payload (ESC→dashboard, 6 bytes)

Frame `5A A5 06 20 21 64 00 | mode batt light beep speed error | CK` — hardware-confirmed; sending it
with `error = 0` clears the dashboard's "ESC missing" comm-fault:

| Offset | Field | Meaning |
|--------|-------|---------|
| 0 | `mode` | riding mode (eco/drive/sport) |
| 1 | `batt` | battery % |
| 2 | `light` | headlight state |
| 3 | `beep` | beeper request |
| 4 | `speed` | km/h while riding (battery % when idle) |
| 5 | `error` | fault code — **`0` = no fault** |

The reverse `0x65` frame (dashboard→ESC) carries **throttle** (payload byte at frame offset 5) and
**brake** (byte 6) hall levels.

---

## BMS — address `0x22`, register file `@0x20000400`

| ARG | Name | Size | Unit / meaning |
|-----|------|------|----------------|
| 0x00 | Magic | U16 | `5A 5A` |
| 0x10 | Serial number | 14 B | ASCII |
| 0x17 | Firmware version | U16 | |
| 0x18 | Factory (rated) capacity | U16 | mAh |
| 0x19 | Actual capacity | U16 | mAh |
| 0x1B | Full charge cycles | U16 | |
| 0x1C | Charge count | U16 | |
| 0x20 | Manufacture date | U16 | packed |
| 0x30 | Status register | U16 | 16 flags (config valid, charge protection, …) |
| 0x31 | Remaining capacity | U16 | mAh |
| 0x32 | Remaining capacity | U16 | % (SoC) |
| 0x33 | Current | S16 | ×10 mA (+discharge / −charge) |
| 0x34 | Pack voltage | U16 | ×10 mV |
| 0x35 | Temperature (2 sensors) | U16 | °C, 0 = −20 |
| 0x36 | Balancing bitmap | U16 | per-cell balance status |
| 0x3B | Health (SoH) | U16 | % |
| **0x40–0x49** | **Cell 1–10 voltages** | U16 each | **mV per cell** |
| 0x51 | Config straps | U16 | |
| 0x70 | Activation data | 12 B | MCU UID copy |

> ⚠️ The repo's earlier `docs/protocol.md` placed the cell block at `0x30–0x39` and voltage/current at
> `0x26/0x25`; the authoritative ES2 BMS map (cells `0x40–0x49`, voltage `0x34`, current `0x33`,
> SoC `0x32`) supersedes that. Firmware special-cases ARG `0x29` on writes and CMD `0x18` (IAP/version).
> **Do not write BMS registers in custom firmware** (project rule).

---

## BLE dashboard — address `0x21`

| ARG | Name | Size |
|-----|------|------|
| 0x10 | BLE serial number | 14 B |
| 0x17 | BLE firmware version | U16 |
| 0x68 | BLE MAC address | 6 B |
| 0x69 | Scooter model string | 16 B |
| 0x79 | BLE pairing password/PIN | — |

The nRF51 also runs the Xiaomi **MiIO** auth/bind layer over BLE (separate framing,
`expected = LEN + 0x0D`) — see `firmware/decompiled/DECOMPILATION.md` §4e.

---

## How the BLE/VESC bridge should poll these (custom-firmware use)

Read ESC battery voltage (App/BLE → ESC):
```
5A A5 02 21 20 01 48 02 00  CKlo CKhi      # LEN=2 (payload "02 00" = request 2 bytes), CMD READ, ARG 0x48
```
Read BMS cell 1 voltage (BLE → BMS):
```
5A A5 02 21 22 01 40 02 00  CKlo CKhi      # ARG 0x40 = cell 1 (mV)
```
Response comes back with `CMD 0x04` (Ninebot read-response) carrying the little-endian value.

Sources: [ninebot-docs ES2ESC](https://github.com/etransport/ninebot-docs/wiki/ES2ESC) ·
[ES2BMS](https://github.com/etransport/ninebot-docs/wiki/ES2BMS) ·
[protocol](https://github.com/etransport/ninebot-docs/wiki/protocol) · firmware: `DECOMPILATION.md`.

# Ninebot G30 Max — Custom Firmware & Reverse-Engineering Project

**Document status:** every factual claim below carries a claim id in the verification ledger
(`docs/verification-ledger.md`) and was checked by an independent agent against the firmware
binaries, the source tree, or a primary external source. Claims that could not be verified are
marked inline.

**Last verified:** 2026-08-06 · **Branch:** `claude/firmware-re-claude-migration-wiring`

---

## 1. What this project is

The stock **ESC motor controller is replaced by a VESC**. The **BLE dashboard** keeps its stock
hardware but receives **custom firmware**; the **BMS keeps stock firmware entirely** (out of scope,
and never to be flashed — the board is always energised). Goals: restore the original dashboard
feature set on top of a VESC, expose the VESC App over BLE, and remove the speed cap. A custom
ECDSA-P256-signed secure bootloader backs the update path.

### 1.1 The single most important correction

> **The BLE dashboard has no STM32. Its only MCU is the nRF51822.**

For most of this project's life the dashboard was documented as `STM32F103C8T6 + nRF51822`. That was
an **inference, never evidence** — the reasoning was "the ScooterHacking flashing tutorial uses an
ST-Link, therefore the chip is an STM32", and an ST-Link is a generic SWD probe that is routinely
used on nRF51 parts. Binary analysis (2026-07-26) settled it:

| Evidence | Result |
|---|---|
| Reset vector, both dashboard dumps | `0x00018155` — inside nRF51 flash, an address that **cannot exist** on an STM32F103 (flash `0x08000000`+) |
| Reset vector, control STM32 dumps | `DRV_1.2.6` → `0x08001101`, `BMS_1.7.4.5` → `0x080010D9` |
| Unambiguous STM32-only peripheral literals in the dashboard images | **zero** |
| Unambiguous nRF51 peripheral literals in the dashboard images | 11–12 |
| Same counts for the control STM32 images | 42–70 STM32 literals, single-digit nRF51 noise |
| TM1637 7-segment font table | present in **both** dashboard images, **absent** from the ESC and BMS images |

The nRF51 does everything: BLE, the Ninebot `5A A5` bus protocol, and it bit-bangs the **TM1637**
6-digit display itself. The address-range argument alone is decisive — nRF51 flash (`0x0`–`0x3FFFF`)
and STM32F103 flash (`0x08000000`–`0x0800FFFF`) are disjoint, so a valid reset vector in one is
categorically impossible for the other family.

**Consequences.** The STM32 dashboard bootloader had no target and was deleted. Deployment phases
that flashed "the dashboard STM32" at `0x08001000` / `0x08000000` describe a chip that is not on the
board. Custom dashboard firmware is a **Cortex-M0 nRF51 project**, installed over **SWD**.

---

## 2. Hardware architecture

```
Phone App ──BLE──▶ ┌──────────────────────┐
                   │  BLE Dashboard       │  nRF51822-QFAA  (the ONLY MCU)
                   │  + TM1637 display    │  256 KB flash / 16 KB RAM
                   └──────────┬───────────┘
                              │ Ninebot bus, 115200 8N1, half-duplex one wire
                   ┌──────────▼───────────┐
                   │  ESC  →  VESC        │  stock STM32F103CBT6 removed
                   └──────────┬───────────┘
                              │ Ninebot bus, 115200 8N1
                   ┌──────────▼───────────┐
                   │  BMS Battery         │  STM32F103C8T6 + BQ76940 — STAYS STOCK
                   └──────────────────────┘
```

| Board | ID | MCU | Flash | Custom firmware? |
|---|---|---|---|---|
| ESC | `DRV` | **VESC** (replaces stock STM32F103C**B**T6, 128 KB) | — | No — stock VESC fw + Lisp |
| Dashboard | `BLE` | **nRF51822-QFAA alone** + TM1637 display driver | 256 KB | **Yes** — Cortex-M0 |
| BMS | `BMS` | STM32F103C8T6 + BQ76940 | 64 KB | **No — never flash** |

### 2.1 Scooter specification (verified against Segway's published spec)

| Spec | Value |
|---|---|
| Model | Ninebot KickScooter MAX G30 / G30P / G30D / G30LP |
| Motor | 350 W nominal / 700 W peak, **rear** hub motor |
| Battery | 36 V 15.3 Ah = **551 Wh** |
| Pack | **10S6P**, 60 cells, 18650 format, 2.55 Ah/cell |
| Cell part | `10INR19/66-6` (genuine Segway MPN) |
| Max speed | 25 km/h (EU) – 30 km/h (other regions) |
| Range | ~65 km |
| Tyres | 10″ tubeless pneumatic, front and rear |
| Brakes | **Front** mechanical drum + **rear** electronic/regenerative |
| BLE | Bluetooth 4.0 LE (nRF51822 + S110 SoftDevice) |

The pack configuration is `10S6P`, not `10S3P`. The decisive evidence is the OEM part number decoded
under IEC 61960: `10` = 10 cells in series · `I` = lithium-ion · `N` = nickel-based cathode · `R` =
round/cylindrical · `19/66` = 19 mm × 66 mm envelope, i.e. the IEC code for the **18650** form factor
· `-6` = **6 cells in parallel**. A sibling genuine Segway part `10INR19/66-2` is rated 5100 mAh;
5100 ÷ 2P = 2550 mAh/cell, identical to 15300 ÷ 6P = 2550 mAh/cell. 10S3P would require 5.1 Ah in an
18650, which no commercial cell reaches (~3.6 Ah ceiling).

**Note on brake direction:** front = drum, rear = electronic. Regeneration can only occur at the
driven wheel, and the motor is a rear hub — so the regenerative brake must be at the rear.

---

## 3. The Ninebot bus protocol

Confirmed three ways: in the firmware binaries, in the C++ implementations, and **measured on the
live scooter** with a NUCLEO-C542RC software-UART tap.

### 3.1 Frame layout

```
 0    1    2     3     4     5     6      7 .. 7+LEN-1     +2
┌────┬────┬─────┬─────┬─────┬─────┬─────┬──────────────┬────────┐
│ 5A │ A5 │ LEN │ SRC │ DST │ CMD │ ARG │  PAYLOAD     │  CKSUM │
└────┴────┴─────┴─────┴─────┴─────┴─────┴──────────────┴────────┘
```

- **`LEN` = payload byte count.** Not `4 + payload`, not `SrcAddr..Payload`. Total frame = `LEN + 9`,
  where 9 = 2 preamble + LEN + SRC + DST + CMD + ARG + 2 checksum.
- **`ARG` is a distinct header byte** and is *not* counted in `LEN`.
- **Checksum** = `sum(bytes from LEN through end of payload) XOR 0xFFFF`, stored **little-endian**.
  Equivalently `(~Σ) & 0xFFFF` — which is literally what the firmware computes.
- **115200 8N1**, one-wire **half-duplex**.

### 3.2 Worked example — the live-captured frame

```
5A A5 05 21 20 65 00 04 28 22 02 00 04 FF
```

| Field | Bytes | Meaning |
|---|---|---|
| preamble | `5A A5` | — |
| LEN | `05` | 5 payload bytes |
| SRC | `21` | dashboard |
| DST | `20` | ESC |
| CMD | `65` | throttle/brake |
| ARG | `00` | — |
| payload | `04 28 22 02 00` | 5 bytes |
| checksum | `04 FF` | little-endian `0xFF04` |

```
Σ(LEN..payload) = 05+21+20+65+00+04+28+22+02+00 = 0xFB
checksum        = 0x00FB XOR 0xFFFF = 0xFF04
stored LE       = 04 FF                             ✓ matches
```
Frame is 14 bytes = LEN(5) + 9. ✓ This is the **only** byte range that validates — every other
plausible range was tested and fails.

### 3.3 Addresses

`0x20` ESC · `0x21` BLE dashboard · `0x22` BMS · `0x3E` phone app · `0x3F` PC.

On the live bus the **dashboard is the master**: it transmits SRC `0x21` → DST `0x20`.

### 3.4 Register files

Both STM32 boards expose an **ARG-indexed array of 16-bit registers**, with `CMD 1` = READ,
`CMD 2` = WRITE, `CMD 4` = read-response. The documented register numbers are indices into that array.

| Board | Register file | Evidence |
|---|---|---|
| BMS | RAM `0x20000400` | literal at file `0x474C`, loaded @ VMA `0x08005668`, followed by `lsls r0,r0,#1 / adds r0,r0,r2` (= `&regfile[ARG]`) |
| ESC | RAM `0x200007D6` | 33 occurrences in **`DRV_1.6.13_Compat`**; `add.w r0,r7,fp,lsl #1` @ VMA `0x08005680` |

> **Citation correction.** Existing docs attribute the ESC register-file finding to `DRV_1.2.6`. The
> literal `0x200007D6` occurs **zero times** in that image — the evidence is in
> `DRV_1.6.13_Compat`. This is the same mis-attribution as the dispatcher addresses in §10, and it
> appears in `docs/REGISTER_MAP.md`, `docs/protocol.md` and `Documentation/VERIFICATION_REPORT.md`.

Register *semantics* (what each index means) are community-sourced from `etransport/ninebot-docs`,
not firmware-derived — only the *mechanism* is firmware-confirmed. `docs/REGISTER_MAP.md` carries the
corrected G30 values (`0x48` = voltage, `0x7B` = KERS, `0x40`–`0x49` = cells); the legacy tables in
`docs/protocol.md` still contain the superseded values (`0x3A`, speed-limit, `0x30`–`0x39`) with a
warning note above the table rather than per row.

### 3.5 Runtime commands

| CMD | Direction | Payload |
|---|---|---|
| `0x65` | dash → ESC | throttle (byte 5) + brake (byte 6) hall levels |
| `0x64` | ESC → dash | `mode · batt · light · beep · speed · error` — **error `0` = no fault** |

Replying with a correct `0x64` (error = 0) **clears the dashboard's "ESC missing" comm-fault** — this
was verified on the real scooter with the C542 emulating the ESC: the dashboard stopped its dead-ESC
retry pattern and began emitting `0x64` frames of its own.

This is confirmed in `vesc-lisp/g30_dash.lisp`, which is the working implementation: throttle is read
from buffer byte 5 and brake from byte 6 (`adc-input`); the `0x64` payload is written in the order
mode · batt · light · beep · speed · error; and the checksum is built as `sum(LEN..payload) XOR
0xFFFF` transmitted little-endian.

> **Two different `0x64` frames exist.** The Lisp emits ESC→dash with **LEN = 6**. The frame observed
> after the fault clears, `5A A5 07 21 20 64 00 …`, is **dash→ESC with LEN = 7** — a different
> direction and a different payload. They are not contradictory, but no document says so, and the
> LEN = 7 payload layout has **not** been reverse-engineered anywhere in this repo.

### 3.6 Checksum core in the firmware

The algorithm is not inferred from captures — it is visible in the dashboard binary at VMA
`0x000184B0`:

```asm
184b0: mvns r3,r3        ; r3 = ~sum        <- ones' complement == XOR 0xFFFF
184b2: uxth r3,r3        ; truncate to 16 bits
184b6: ldrb r0,[r0,#31]  ; received checksum high byte
184b8: lsls r0,r0,#8
184ba: adds r0,r1,r0     ; lo | (hi<<8)     <- little-endian
184be: cmp  r3,r0
184c0: bne  0x18532      ; mismatch -> reject frame
```

Immediately after it, the frame builder writes the preamble:
`184cc: movs r0,#0x5A / strb r0,[r1,#0]` · `184d0: movs r0,#0xA5 / strb r0,[r1,#1]`.

### 3.7 Header parsers across the three boards

The same `5A A5` header state machine appears in every genuine image. Instance counts, from a windowed
byte-pattern scan (`cmp Rn,#0x5A` followed within a few instructions by `cmp Rn,#0xA5` on the same
register):

| Image | Instances | Locations (VMA) |
|---|---|---|
| `DRV_1.2.6` | 3 | `0x08006936`, `0x08006C56`, `0x08006F7C` — one per USART1/2/3 |
| `DRV_1.6.13` | 3 | `0x08007194`, `0x080074D4`, `0x0800781A` |
| `BMS_1.7.4.5` | 1 | `0x08002EBA` (enclosing function entry `0x08002E4C`) |
| `BLE_1.1.7` | **2** | `0x0001854E` and `0x0001A030` |

Two details that older documents get wrong and are corrected here:

- The dashboard image contains **two** copies of the state machine, not one. The second, at
  `0x0001A030`, is a full duplicate of the same logic.
- The scan is **windowed, not adjacent**. A strict "immediately followed by" search finds **zero**
  matches in all four binaries — every real instance has 1–3 intervening instructions (4–8 byte gaps).

Body-length checks differ per board: the ESC computes `LEN + 7` and compares `<= 0xF3`; the BMS uses
the same `0xC2` constant but compares raw `LEN` after subtracting 7; the dashboard uses `LEN + 8`
normally and `LEN + 0x0D` on the Xiaomi MiIO path, both falling through to a shared `<= 0x8F` cap.
The parser *logic* is equivalent across builds, but the **opcodes are not identical** — `DRV_1.2.6`
and `DRV_1.6.13` differ in branch selection, so "byte-for-byte identical across images" overstates it.

---

## 4. Stock firmware dumps

| Image | Size (B) | Entropy | Arch | Load base | SP | Reset (raw) |
|---|---|---|---|---|---|---|
| `DRV_1.2.6` | 30 076 | 6.99 | Cortex-M3 STM32F103 | `0x08001000` | `0x20002B18` | `0x08001101` |
| `DRV_1.6.13_Compat` | 33 388 | 7.00 | Cortex-M3 STM32F103 | `0x08001000` | `0x20002950` | `0x08001189` |
| `BLE_1.1.0` | 33 612 | 6.95 | **Cortex-M0 nRF51822** | `0x00018000` | `0x20003D10` | `0x00018155` |
| `BLE_1.1.7` | 34 252 | 6.96 | **Cortex-M0 nRF51822** | `0x00018000` | `0x20003DE8` | `0x00018155` |
| `BMS_1.3.4` | 13 956 | 6.75 | no valid vector table | — | `0xF9C10082` | `0x5BC60082` |
| `BMS_1.7.4.5` | 23 596 | 6.83 | Cortex-M3 STM32F103 | `0x08001000` | `0x200017C8` | `0x080010D9` |

Reset vectors are shown **raw**, i.e. with the Thumb bit set, exactly as stored. Some older documents
print them with the Thumb bit cleared (`…154`, `…100`, `…D8`); both conventions describe the same
handler, but mixing them across documents caused a long-standing apparent disagreement.

Identity strings: `Scooter_G30_SAT` (dashboard @ file `0x3300` + `DRV_1.6.13` @ file `0x400`),
`NBScooter0001` (ESC), `G30_HD_HDPRO_VXX` (BMS @ file `0x100`) — all confirm the G30 platform.

### 4.1 `BMS_1.3.4.bin` — format unidentified (previously mislabelled "XiaoTEA-encrypted")

This image has no valid Cortex vector table and cannot be disassembled. It was long documented as
"encrypted (XiaoTEA)". **That label is not supported by the evidence and has been withdrawn.**

| Test | Result |
|---|---|
| Whole-file entropy | 6.75 — *lower* than the known-plaintext `BMS_1.7.4.5` (6.83) and `DRV_1.2.6` (6.99) |
| Windowed entropy (256 B) | flat 5.0–6.0; real block-cipher output sits at 7.9–8.0 |
| Byte histogram | wildly non-uniform — `0xCD` is 6.96 % of all bytes vs 0.39 % expected |
| Longest constant run | **25 consecutive `0x80` bytes** — essentially impossible in genuine ciphertext |
| Repeated blocks | one 8-byte sequence recurs 18× |
| First 128 bytes | a clean templated table (`82 00 C1|C6 xx` per word), not noise |
| Size | 13 956 B = 4 mod 8 — fails the 8-byte block-size precondition of the TEA tooling |
| Actual decryption attempt | run with the repo's own working TEA code and key → no valid vector table anywhere, no strings, and entropy *rose* to 7.99 (the signature of a wrong key, not a decryption) |

The citation was also wrong: `tools/analysis/analyze_bootloader.py` contains **no key schedule and no
decrypt function** — only the 16 ASCII bytes `"Ninebot Scooter "` used in two substring searches. That
script additionally still has pre-reorganisation hardcoded paths, so it silently skips every firmware
file, and its printed "KEY FINDINGS" section is static text rather than a computed result.

Correct statement: **`BMS_1.3.4.bin` is not a plain STM32 image and its container format is
unidentified.** Whether it is encrypted, packed, or simply a different format is an open question. It
is out of the project's critical path either way — the BMS stays stock.

---

## 5. Dashboard reverse-engineering (`BLE_1.1.7`, app base `0x00018000`)

Everything in this section is recovered from the stock binary. Reproduce with:

```bash
arm-none-eabi-objdump -D -b binary -m arm -M force-thumb --adjust-vma=0x18000 BLE_1.1.7.bin
```

### 5.1 Recovered pin map

| Pin | Role | Confidence |
|---|---|---|
| **P0.04** | TM1637 **DIO** | certain — driven per data bit in `tm1637_write_byte` |
| **P0.05** | TM1637 **CLK** | certain — toggled low→high once per bit |
| **P0.15** | Ninebot bus (TX or RX — swaps) | certain — `uart_init` argument |
| **P0.20** | Ninebot bus (RX or TX — swaps) | certain — `uart_init` argument |
| P0.25 | variant-dependent output | high |
| P0.08 | **board-variant strap**, input + pull-up, read once at boot | high — *not* the button |
| P0.00, P0.03 | GPIO (LED / aux) | medium |

The **power button's GPIO number is still unknown.** It is handled the Nordic way — GPIOTE `PORT`
event + `PIN_CNF.SENSE`, which is also the wake-from-System-OFF mechanism — and the pin arrives
through a runtime config struct rather than as a literal, so it cannot be read out of the image.
Resolve it by reading `PIN_CNF[0..31]` over SWD on a live board (the sensed pin has `SENSE` set) or
by continuity from the dash cable's green wire.

### 5.2 Display — TM1637, 6 grids, fully recovered

| Symbol | VMA |
|---|---|
| `tm1637_start()` | `0x00018DA0` |
| `tm1637_stop()` | `0x00018DD8` |
| `tm1637_write_byte()` | `0x00018E20` |
| `tm1637_read_ack()` | `0x00018E68` |
| `tm1637_display_off()` | `0x00019D98` |
| `tm1637_update()` | `0x00019DAA` |
| `gpio_set(pin)` / `gpio_clear(pin)` | `0x0001F29C` / `0x0001F27C` |
| `delay()` | `0x000180F4` |
| 7-segment font table | VMA `0x0002046F` (file `0x846F` in `BLE_1.1.7`; file `0x81FF` in `BLE_1.1.0`) |

Font (common-cathode, `0`–`9` then `A`–`F`):
`3F 06 5B 4F 66 6D 7D 07 7F 6F | 77 7C 39 5E 79 71`

`tm1637_write_byte(b)` is bit-banged **LSB-first** — verified from the code, not assumed: the loop
does `CLK↓`, sets `DIO` from bit 0, `lsrs r4,r4,#1`, `CLK↑`, then reads an ACK.

`tm1637_update(uint8_t seg[6], uint8_t brightness)` is the exact TM1637 command sequence:
1. `START`, `0x40` (data command, auto-increment address), `STOP`
2. `START`, `0xC0` (address, digit 0), **6 segment bytes**, `STOP`
3. `START`, `0x88 | brightness` (display ON), `STOP`

`tm1637_display_off()` writes `0x80`.

> **Corrected here:** earlier documents put `tm1637_display_off()` at `0x00019D96`. That address is
> inside a literal pool (it is the tail of the constant `0x2000275C`) which a linear disassembly
> sweep misread as a `movs r0,#0` prologue. The real entry is **`0x00019D98`**, confirmed by the only
> call site in the image, `bl 0x19d98` at `0x193C0`.

### 5.3 Ninebot bus UART — half-duplex by pin swap

`uart_init(tx_pin /*r0*/, rx_pin /*r1*/)` @ **`0x0001FDB4`**:

```
PIN_CNF[tx] = 3   (output)              PIN_CNF[rx] = 4  (input, buffer connected)
UART0.PSELTXD  (0x4000250C) = tx        UART0.PSELRXD  (0x40002514) = rx
UART0.BAUDRATE (0x40002524) = 0x01D7E000   -> 115200   (Nordic's Baud115200 constant)
UART0.ENABLE   (0x40002500) = 4
TASKS_STARTTX / TASKS_STARTRX = 1 ; clear EVENTS_RXDRDY (0x40002108)
```

Exactly **two** call sites exist in the whole image, and they swap the same pin pair:

| Function | VMA | Direction | mode flag |
|---|---|---|---|
| `uart_mode_tx20()` | `0x0001A128` | TX = P0.20, RX = P0.15 | `1` |
| `uart_mode_tx15()` | `0x0001A140` | TX = P0.15, RX = P0.20 | `0` |

Both write the same RAM byte (`0x20002160 + 5`), confirming a genuine shared-state turnaround. **The
stock firmware reverses TX/RX rather than tri-stating** — replicate this when driving the bus.

### 5.4 Peripherals in use

| Base | Peripheral | Purpose |
|---|---|---|
| `0x40002000` | UART0 | Ninebot bus |
| `0x40007000` | ADC | analog sense |
| `0x40009000` | TIMER1 | timing |
| `0x40010000` | WDT | watchdog |
| `0x40011000` | RTC1 | tick / scheduling |
| `0x40006000` | GPIOTE | button edge/PORT event |
| `0x40000000` | POWER/CLOCK | clock, DCDC, RAM retention |
| `0x10001000` | UICR | user config |
| `0x50000000` | GPIO P0 | pins above |

---

### 5.5 SoftDevice (S110) usage

The image contains **58 SVC call sites across 37 distinct SVC numbers**. Confirmed BLE calls:

| Call | SVC |
|---|---|
| `sd_ble_enable` | 96 |
| `sd_ble_evt_get` | 97 |
| `sd_ble_uuid_vs_add` (vendor UUID → NUS) | 99 |
| `sd_ble_gatts_service_add` | 160 (×2) |
| `sd_ble_gatts_characteristic_add` | 162 (×3) |
| `sd_ble_gatts_value_set` | 164 |
| `sd_ble_gatts_hvx` (notify) | 166 |
| `sd_ble_gatts_sys_attr_set` | 169 |

> **Corrections to earlier SVC tables.** Four GAP mappings were each off by exactly −4, from using
> `BLE_GAP_SVC_BASE = 0x6C` (which is Nordic's *reserved* padding range) instead of the real `0x70`.
> The correct numbers are `conn_param_update` = **117**, `ppcp_set` = **122**, `authenticate` = **126**,
> `auth_key_reply` = **128**. The flash SVCs were given as "~39–41"; the real values are **32/33**.
>
> More importantly: **SVCs 126, 128, 32 and 33 never appear in the image.** So this firmware shows
> *no* evidence of calling SoftDevice-level GAP authenticate / auth-key-reply, and no evidence of
> `sd_flash_write` / `sd_flash_page_erase` at all. The 40/41 that *are* present are
> `SD_NVIC_GET/SETPENDINGIRQ` — interrupt control, unrelated to flash. Any claim that odometer or
> config persistence goes through SoftDevice flash SVCs is unsupported by this binary.
>
> Three files in the repo (`RE_NRF51_DASHBOARD.md` §7, `nrf51_hal.h`'s `svc` namespace, and
> `analyze_nrf51822.py`'s `SOFTDEVICE_SVC_RANGES`) give **mutually contradictory** SVC tables. Trust
> the Nordic S110 headers.

The claim that "2 services + 3 characteristics matches the observed GATT" is **wrong**: it counted
3 static `characteristic_add` *call sites*, but the repo's own live capture of the real dashboard
enumerated **7 characteristics** — 2 NUS plus 5 Xiaomi MiIO (control, beaconkey, auth, token,
device-id). One call site plausibly sits in a loop over a table.

---

### 5.6 BLE authentication — two independent gates

The dashboard exposes **two separate BLE surfaces**, and it is the Ninebot one, not the Xiaomi one,
that gates the command channel. Getting this backwards has cost this project months, so it is stated
carefully.

| Surface | UUID | What it gates |
|---|---|---|
| **Nordic UART Service** | `6e400001-b5a3-f393-e0a9-e50e24dcca9e` | **The Ninebot command relay** — `5A A5` frames, register read/write, vehicle actions, IAP opcodes. Gated by **Encryption2**. |
| **Xiaomi MiIO** | `0xfe95` | Mi Home pairing / registration / cloud-bind (and, per earlier unre-verified RE, the stock OTA path). |

Every byte written to the NUS RX characteristic is fed **directly** into the Ninebot frame parser at
`0x18450`. There is **no MiIO precondition anywhere on that path**, and the MiIO code never touches
the Ninebot protocol state at `0x2000275C` or the crypto buffers. A MiIO registration token is
neither necessary nor sufficient to talk to this dashboard.

**Encryption2 as this firmware actually implements it** (recovered from `BLE_1.1.7`):

- AES-128 via the SoftDevice ECB peripheral (`SVC 77` = `sd_ecb_block_encrypt`); key at `0x200035B4`.
- `set_key(key1, key2)` @ `0x1B9A8` computes `aes_key = SHA-1(key1_pad16 ‖ key2_pad16)[0:16]`.
- **When `key2` is NULL the firmware substitutes the 16-byte `fw_data` constant**
  `97 CF B8 02 84 41 43 DE 56 00 2B 3B 34 78 0A 5D` (at VMA `0x204F4`, three code xrefs) — **not**
  zero bytes. This makes the G30 a **Gen2** device.
- Three-phase key evolution: PRE_COMM `set_key(bt_name, fw_data)` → SET_PWD
  `set_key(bt_name, auth_param)` → AUTH `set_key(session_password, auth_param)`.
- Commands at `0x19990`: **`0x5B` PRE_COMM · `0x5C` SET_PWD · `0x5D` AUTH**. The firmware's own
  log-stripped debug strings name them 前置信息 / 设置密码 / 认证通过.
- Encrypted frames are `LEN + 13` bytes; the parser forces this path whenever encryption is enabled
  (the default).
- SN mode is AES-CCM-like: nonce = `counter_BE32 ‖ auth[0:8] ‖ 0x00`, 4-byte encrypted CBC-MAC tail,
  2-byte big-endian counter with replay checking.

> **Three corrections to the Encryption2 notes themselves.** The newer account got the mechanism
> right but three parameters wrong:
>
> | Claimed | Actual |
> |---|---|
> | Gen3, preamble `5A B5` | **Gen2, preamble `5A A5`** — `5A B5` appears **nowhere** in the image; it is WiFi-v2-only |
> | Key = `SHA1(btName ‖ 16 zeros)` | **`SHA1(btName ‖ fw_data)`** |
> | BLE board addressed `0x04` | **`0x21`** — frames to `0x04` are silently discarded |

**Why every handshake attempt got silence.** Three independent causes, all hit at once:

1. **Wrong destination.** The probes sent DST `0x04`; the dispatcher accepts only `0x21` or the `0xFF`
   broadcast, and `0x04` matches no relay target either — dropped with no reply and no error.
2. **Wrong key.** The probes used a reference implementation that substitutes zeros for a NULL
   `key2`; this firmware substitutes `fw_data`. Decryption fails the checksum and the frame is
   dropped. Silence, not echo — the "a wrong key would echo" assumption is an iOS-bonded-peripheral
   artifact, not a wrong-key signature. The repo's **own** `tools/ble/ninebot_crypto.py` is correct;
   it simply was not the code the probes called.
3. **A real bus dependency.** The `0x5B` handler does **not** answer the phone directly. It sets a
   "waiting SN" flag and queues a request onto the **wired** bus toward the controller at `0x20` for
   register `0x10`. Only when a bus peer answers does the nRF51 assemble `auth[16] ‖ serial[14]` and
   emit the `0x5B` reply. **With the ESC absent (VESC swap in progress), a byte-perfect PRE_COMM still
   gets no reply.** The ESC emulator is the right remedy — provided it also answers a read of register
   `0x10` with the 14-byte serial number.

The earlier "the dashboard STM32's control-plane isn't running" diagnosis had the right instinct
(device state) but an impossible mechanism — there is no STM32. The nRF51's silence is fully
explained without one.

**The Encryption2 session password is generated locally at pairing and lives on the scooter and the
paired phone** (`{SerialNumber}_decrypt` in `com.ninebot.segway`). It is **never** in any cloud, so
cloud-token routes are a dead end.

---

## 6. Custom dashboard firmware (`firmware/dashboard-nrf51/`)

A Cortex-M0 project that links into the stock app slot at `0x00018000`, above the S110 SoftDevice.

| File | Contents |
|---|---|
| `include/dash_hal.h` | chip-independent HAL + the recovered pin map |
| `src/tm1637.cpp` | TM1637 driver — the stock sequence, byte for byte |
| `src/nb_protocol.cpp` | Ninebot codec (LEN = payload, `sum ^ 0xFFFF` LE) |
| `src/dashboard.cpp` | state machine: telemetry → display, button → mode/light/power |
| `src/nrf51_hal.cpp` | nRF51 registers (GPIO, UART0, RTC1, WDT) |
| `src/startup_nrf51.cpp` | vector table, `.data`/`.bss` init, freestanding C++ stubs |
| `nrf51822_app.ld` | app slot `0x18000`, RAM `0x20002000` |
| `sim/` | host tests — no hardware needed |

The host tests check against **ground truth recovered from the stock firmware and the live scooter**,
not against themselves: the decoder must accept the real captured frame and re-encode it
byte-for-byte; the TM1637 driver's waveform is sniffed by a simulated TM1637 and must produce exactly
the stock 9-byte sequence; the font must equal the table extracted from the stock image.

**Not done yet:** the SoftDevice is not driven (no advertising / NUS yet); the button's GPIO number
is a placeholder; odometer persistence via `sd_flash_write` is unimplemented.

---

## 7. Bootloader

The only remaining target is **nRF51**. `bootloader/stm32/` was deleted — it had no board to run on.
`bootloader/CMakeLists.txt` now accepts `-DTARGET_BOARD=nrf51` only, and explicitly errors with a
migration message if given the old `ble` or `bms` values.

**The build command requires the toolchain file.** Omitting it does not fail loudly — CMake silently
selects the host compiler, reports "Configuring done", and then dies at `-mthumb` on 11 of 12 objects.
The correct, verified-working invocation is:

```bash
cmake -B bootloader/build/nrf51 -S bootloader -G Ninja -DTARGET_BOARD=nrf51 -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m0.cmake
```

```bash
cmake --build bootloader/build/nrf51
```

This was run end to end during verification: 12/12 objects compile and `nrf51_bootloader` links at
**6 956 B** of text, comfortably inside the 16 KB budget.

### 7.1 Memory map (nRF51822, 256 KB flash)

| Range | Size | Contents |
|---|---|---|
| `0x00000000`–`0x00017FFF` | 96 KB | S110 SoftDevice (**v8.0.0** — v7.0.0 would end at `0x16000`) |
| `0x00018000`–`0x0003BFFF` | 144 KB | Application |
| `0x0003C000`–`0x0003FFFF` | 16 KB | Custom secure bootloader |

> **`0x0003C000` is a design choice, not a measured value.** It is Nordic's conventional DFU-bootloader
> offset for a 256 KB part, and the arithmetic closes exactly (`0x3C000 + 0x4000 = 0x40000`). Documents
> that describe it as "= the stock `UICR.BOOTLOADERADDR`" overstate it: **no SWD dump of the stock
> dashboard exists**, so UICR has never been read. The repo's own analysis output for `BLE_1.1.7`
> reports *"No bootloader address patterns found in binary"* — the `UICR.BOOTLOADERADDR` address
> (`0x10001014`) does not appear in the app image at all. Confirm the real value when Phase 0's dump is
> taken.

### 7.2 Flash / self-update rules (nRF51 NVMC)

Flash-resident code **can** program *other* pages: the NVMC halts the CPU during a write or erase and
resumes afterwards, so the bootloader at `0x3C000` can erase and rewrite the app at `0x18000` with no
RAM copy. It **cannot** program the page it is executing from — so a **bootloader self-update must
run from SRAM**. `bootloader/nrf51/src/nrf51_selfupdate.c` places exactly one function,
`ram_install()` (124 B), in a `.ramfunc` section; it takes source pointer and length as parameters,
polls `NVMC.READY` inline rather than calling a flash-resident helper, and ends in
`AIRCR = VECTKEY|SYSRESETREQ` without returning. The caller does `cpsid i` first — an interrupt
vectoring into erased flash would be fatal.

Additional NVMC rules the driver obeys: word-aligned writes only (a byte or half-word write hard
faults); write only into erased (all-ones) bits; `NVMC.CONFIG` is always assigned, never OR-ed. A
hardware safety net exists on top of the software guards: `ERASEPCR0` is MPU-restricted to code in
region 0, and the bootloader lives in region 1, so it **physically cannot erase the SoftDevice**.

**Residual risk:** the window between "erase bootloader" and "program complete" inside `ram_install()`
is not power-fail-safe. A power loss there leaves no bootloader and recovery needs SWD. Application
updates do not have this problem — the bootloader survives, so a failed app update simply re-enters
update mode.

### 7.3 Update transport — NBU, not XMODEM

The Ninebot bus is a **single half-duplex wire**, so a byte-stream protocol like XMODEM is the wrong
shape. The update transport is **NBU**: `5A A5`-framed request→ACK turn-taking with IAP-aligned
opcodes and per-block sequence retransmit. XMODEM was removed.

---

## 8. Deployment

| Phase | Action | Target | Reversible |
|---|---|---|---|
| 0 | **SWD dump** of the stock nRF51 + VESC install | dashboard | — (read-only) |
| 1 | Custom **app** @ `0x00018000` over SWD; SoftDevice untouched | nRF51822 | Yes — restore the dump |
| 2 | Custom **bootloader** @ `0x0003C000` over SWD | nRF51822 | Yes — restore the dump |
| 3 | Updates via the bootloader (NBU over the bus) or stock OTA | nRF51822 | Via bootloader |

The BMS stays stock and is out of scope. Custom dashboard firmware must support the stock Ninebot BMS
protocol and optionally a Daly BMS.

---

## 9. Reverse-engineering rigs

### 9.1 NUCLEO-C542RC bus tap

An STM32C542 (Cortex-M33, 256 KB, on-board ST-LINK/V3) taps the dashboard's internal plug. It has
**no usable hardware USART for this job** and instead bit-bangs a software UART: 4×-oversampled RX
with per-byte start-edge re-sync, push-pull TX, and explicit half-duplex turnaround.

Measured on the live scooter:

| Property | Value |
|---|---|
| Live wire | **A2 = PA4** (A0/PA0 was dead: 1 edge vs 7012 in 2 s) |
| Core clock | 48 MHz (HSI), read off-chip |
| Bit period | ≈ 416 cycles → 48e6/416 = **115 385 baud** ⇒ 115200 |
| Captured frame | `5A A5 05 21 20 65 00 04 28 22 02 00 04 FF`, checksum valid, reproduced across 3 captures |

1× real-time sampling drifted and mis-framed (it decoded `AE C8 35 …` for the real
`5A A5 05 21 20 …`); 4× oversampling fixed it. TX needed **push-pull** — the internal pull-up is too
weak for the bus capacitance and open-drain rises too slowly at 115200.

`STM32_Programmer_CLI` is used for flashing; pyOCD and OpenOCD 0.12 do not support STM32C5.

### 9.2 A build bug that looked like hardware damage

The C542 board Makefile hard-coded its output name regardless of which `MAIN` was selected, so
rebuilding a different source produced no new binary and the flasher kept writing a stale image. This
presented as "the C542 stopped running / SRAM is garbage". **When a hardware rig appears to fail,
suspect the build first.**

---

## 10. The stock update path is authentication-gated

Reflashing the **stock** dashboard over the wired bus is blocked by design. This matters only for
dumping/patching the stock image — custom firmware is installed over **SWD**, which bypasses it
entirely.

- `CMD 0x18` is **calibration**, not reset. It requires sub-command `0x12` plus the magic `"N4G"`
  (backed by the string `N4GEA1601C0001`). Byte-exact at `DRV_1.2.6` `0x08004FC8`.
- The real enter-update is **`CMD 0x57` / `0x59`** (both share one handler), and it is
  **password-gated**. The password is two little-endian 32-bit words:
  `~(UID0 + UID1 + UID2)` ‖ `~(UID0 · UID1 · UID2)`.
  This formula is **byte-exact confirmed** in a dedicated compare routine at `DRV_1.2.6`
  `0x08002078` — `adds/add` → `mvns` for word 0, `muls/muls` → `mvns` for word 1, returning success
  only if both match.
- On hardware, `0x57`/`0x58`/`0x59`/`0x5C` sent with a zero password were all ignored — the gate is real.

**Address citations corrected.** Existing docs cite `App_to_ESC_handler @0x08005624` with a
`tbb @0x08005650` "from `DRV_1.2.6` / `BMS_1.7.4.5`". Both attributions are wrong:

| Image | Real dispatcher | Real jump table |
|---|---|---|
| `DRV_1.2.6` | `0x08004E20` | `tbb` @ `0x08004E48` |
| `DRV_1.6.13_Compat` | `0x08005624` | `tbb` @ `0x0800564C` |
| `BMS_1.7.4.5` | `0x08005610` | **none — linear `cmp` chain, no `tbb` anywhere in the image** |

The cited pair belongs to `DRV_1.6.13_Compat`, an image the claim does not mention. In `DRV_1.2.6`,
`0x08005624` is `sub.w r3,r6,sl` inside unrelated motor-duty code.

Two further corrections: the UID literal `0x1FFFF7E8` is at `0x08005478` **in `DRV_1.2.6` only**
(it is at `0x08005E58` in 1.6.13 and **absent entirely from `BMS_1.7.4.5`**, which never reads the
STM32 UID this way); and the `0x5A5A` IAP marker is fully traced **only in the BMS**
(`0x080031F0` → `0x20000400` → flash `0x0800F000` → `AIRCR = 0x05FA0004`) — the constant `0x5A5A`
**does not appear in either DRV image**, so DRV's marker value is still unresolved even though its
commit-and-reset mechanism is confirmed.

> **On the nRF51 this whole derivation needs redoing.** The formula above was recovered from **STM32**
> images using the STM32 UID at `0x1FFFF7E8`. The dashboard is an nRF51, whose device ID is
> `FICR.DEVICEID` at `0x10000060`. Any UID-derived password for the dashboard must be re-derived
> against the nRF51 image — it has not been.

---

## 11. Repository layout

```
boards/{esc-motor,ble-dashboard,bms-battery}/   datasheets, stock dumps, PINOUT.md, README.md
bootloader/{common,nrf51}/                      secure bootloader (common crypto + nRF51 target)
docs/                                           protocol, IAP, flashing, wiring, guides/
firmware/dashboard-nrf51/                       custom dashboard firmware (Cortex-M0)
firmware/nrf51-dumper/                          stock-image dumper
firmware/nrf51-bl-installer/                    bootloader installer
firmware/dash-tap-c542/                         NUCLEO-C542RC bus-tap / programmer rig
firmware/decompiled/                            reconstructed firmware + RE reports
lib/ninebot-protocol/                           Ninebot protocol C++ library
tools/{signing,flasher,analysis,vesc,ble,nrf51}/  PC-side tooling
vesc-lisp/                                      VESC Lisp dashboard bridge
Target/                                          hardware test scripts
Documentation/                                   PROJECT_DOC, CHANGE_LOG, Requirements, ToDo, Tests
```

> There is a stray empty top-level `firmware-decompiled/` directory. It is untracked, gitignored, and
> contains only a leftover `build/`. **The real location is `firmware/decompiled/`.** `HANDSOFF.md`'s
> auto-generated directory table lists the stray as if it were a project directory — it is not.

---

## 12. What `/verify-safe` actually proves

`CLAUDE.md` makes `python tools/verify_firmware_safe.py` mandatory before any flash and credits it
with five guarantees. It passes today — but it proves less than it says. Traced with a subprocess
audit hook across a full run:

| Promise | Status |
|---|---|
| Valid vector table | ✅ **Proven.** `validate_bin()` runs unconditionally: SP in SRAM, reset inside the app slot. Observed `SP=0x20004000 reset=0x00018121 → OK`. |
| Regression suite passes | ✅ **Proven.** 157 tests / 482 assertions. |
| Secure boot: signed accept + tamper reject | ✅ **Proven.** 9/9, including five distinct rejections — flipped firmware byte, flipped signature byte, wrong key, corrupted magic, wrong target board. |
| Update path preserved | ⚠️ **Partial.** "Bootloader-loadable" is proven by the vector check; "never touches the bootloader region" is not. |
| No bootloader / option-byte writes | ❌ **Not proven.** |
| Watchdog fires and recovers | ❌ **Not proven.** |
| "Both targets build" | ❌ **Obsolete wording.** Only one target exists. |

Two separate defects produce this:

1. **`ctest` is never invoked.** `tools/build_dashboard.py` hardcodes `firmware_tests.exe`, so
   `dashboard_sim`, `dashboard_persist_sim` and `ble_sim` are compiled on every run and then never
   executed — three of four registered test binaries silently skipped. Regressions in the live-bus
   protocol decoder, the TM1637 sequence, the odometer flash semantics, the power-keeper state
   machine or the BLE session would pass the gate unnoticed.
2. **The no-brick assertions no longer exist.** They lived in the STM32 dashboard simulator
   (`flash_writes == 0`, `!bricked`, valid-vector) and were deleted with that target; they were never
   re-created for nRF51. The surviving flash model covers only a 4 KB odometer region with
   region-relative offsets and no bounds check, so it *structurally cannot* detect a protected-region
   write. Wiring in `ctest` alone would not restore these two promises.

**Calibrated severity: moderate — a coverage hole and stale wording, not an unsafe gate.** The
realistic failure modes are currently covered *by construction* rather than by test: the shipped
image contains **zero NVMC references**, so the firmware physically cannot write flash and cannot
self-brick; and deployment is SWD-based with a full stock dump taken first. No false "SAFE" verdict
would have flashed a bricking image today.

**But that protection is incidental, and it expires.** `src/nrf51_persist.cpp` is written and
currently compiled by nothing. The moment it is added to the Makefile to enable odometer
persistence, the firmware gains a real flash-write path with **zero gate coverage of it** — and
that is exactly when the no-brick guarantee becomes load-bearing. Restore the assertions before
enabling persistence, not after.

Related: the watchdog is not actually started on target — the image writes WDT `RR[0]` but never
`CRV`, `RREN`, `CONFIG` or `TASKS_START`.

---

## 13. Documentation health — read this before trusting an older file

The 2026-07-26 nRF51-only verdict invalidated a large amount of previously-written material, and the
correction was applied to `CLAUDE.md`, `boards/ble-dashboard/MCU_IDENTIFICATION.md` and
`bootloader/CMakeLists.txt` but **not** propagated to the rest of the tree. A full audit found stale
STM32-dashboard assertions in roughly 35 files, plus 13 doc-vs-doc contradictions — several of them
*inside a single file*:

- `README.md` shows `STM32 + nRF51` in its architecture diagram and project tree, then states seven
  lines later that the dumps are nRF51 and *not* STM32 dashboard firmware.
- `boards/ble-dashboard/PINOUT.md` opens with a correction banner declaring every STM32 row void, then
  leaves an unmarked `BLE Board Memory Map (STM32F103C8T6)` table and an
  `nRF51822 ↔ STM32 ↔ ESC` flow diagram live further down.
- `CLAUDE.md` points readers to `docs/guides/DEPLOYMENT.md` for "full detail" two lines before
  declaring that document's phases void — and `DEPLOYMENT.md` was never rewritten.
- `boards/ble-dashboard/DASHBOARD_PINOUT_RESEARCH.md` §B3 still tags an "Internal STM32 ↔ nRF51 UART"
  as **[CONFIRMED]** — a confirmed link to a chip that does not exist.

Treat `CLAUDE.md`, `MCU_IDENTIFICATION.md`, `RE_NRF51_DASHBOARD.md`, `Requirements/dashboard-nrf51.md`
and this document as current. Treat the power-management document family, the guides, and
`DASHBOARD_FIRMWARE.md` / `NRF51_BLE_FIRMWARE.md` / `APP_COMPATIBILITY.md` /
`DASHBOARD_NO_SOLDER_FLASH.md` as **pre-verdict** — their architecture premises are void even where
their protocol content is still good.

---

## 14. Safety rules

- Pack is **551 Wh**. Never bypass BMS over-voltage, under-voltage or over-current protection.
- **Never flash custom firmware to the BMS.** The BMS board is always energised — disconnect before
  any SWD work.
- 3.3 V TTL only on the UART.
- Verify firmware checksums and keep stock backups before any flash.
- Run the `/verify-safe` gate after **every** firmware change and before any flash.

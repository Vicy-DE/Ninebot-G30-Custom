# ESC Protocol Core — Verified Decompilation (DRV_1.6.13)

**Date:** 2026-06-03
**Binary:** `boards/esc-motor/firmware/DRV_1.6.13_Compat.bin` (STM32F103, app base `0x08001000`)
**Output:** [`common/include/ninebot_protocol_verified.hpp`](common/include/ninebot_protocol_verified.hpp)
**Test:** [`tests/test_decompiled_protocol.cpp`](tests/test_decompiled_protocol.cpp) — **45/45 pass** (g++ 15.2, `-std=c++17`)

This is a *genuine, byte-level* decompilation of the four functions that implement the Ninebot wire
protocol on the ESC, reconstructed from the disassembly and re-implemented in dependency-free C++ that
compiles and runs on a host. It supersedes the un-verified parts of
`common/include/protocol.h` (see the LEN correction in §5).

```
g++ -std=c++17 -Wall -Wextra -I../common/include tests/test_decompiled_protocol.cpp -o t && ./t
```

---

## 1. `calculateChecksum` @ `0x08002720`

```asm
0x08002720: push {r4,lr}
0x08002722: movs r3,#0          ; sum = 0
0x08002724: movs r2,#0          ; i = 0
0x08002728: ldrb r4,[r0,r2]     ; r4 = data[i]
0x0800272A: add  r3,r4          ; sum += data[i]
0x0800272C: uxth r3,r3          ; sum &= 0xFFFF
0x0800272E: adds r2,r2,#1       ; i++
0x08002730: cmp  r2,r1
0x08002732: blo  0x08002728     ; while (i < len)
0x08002734: mvns r0,r3          ; r0 = ~sum
0x08002736: uxth r0,r0          ; & 0xFFFF
0x08002738: pop  {r4,pc}
```
→ `checksum = (~Σ data[i]) & 0xFFFF` (identical to `Σ ^ 0xFFFF`). **Confirms `docs/protocol.md`.**

## 2. `buildPacket` @ `0x080036AC`

Writes `5A A5`, then `LEN SRC DST CMD ARG`, copies `LEN` payload bytes, appends the little-endian
checksum over `LEN..payload`. Argument mapping (AAPCS): `r0=src, r1=dst, r2=len, r3=cmd,
[sp+0x14]=arg, [sp+0x18]=payload, [sp+0x1c]=out`.

```asm
0x080036B6: movs r5,#0x5a ; out[0]=0x5A
0x080036BA: movs r5,#0xa5 ; out[1]=0xA5
0x080036BE: strb r2,[r4,#2] ; out[2]=LEN  (== payload count r2)
0x080036C0: strb r0,[r4,#3] ; out[3]=SRC
0x080036C2: strb r1,[r4,#4] ; out[4]=DST
0x080036C4: strb r3,[r4,#5] ; out[5]=CMD
0x080036C8: strb ip,[r4,#6] ; out[6]=ARG
...        copy r2 payload bytes from r7 to out[7..]
0x080036E4: bl  0x08002720  ; chk = calculateChecksum(out+2, (r5-2))  ; r5-2 = LEN+5
0x080036EC: strb r0,[r4,r5] ; out[..]=chk_lo
0x080036F4: strb r2,[r4,r1] ; out[..]=chk_hi
```
**Key fact:** `r2` is both the stored `LEN` byte *and* the payload copy count → **`LEN` = payload length**.

## 3. `parseProtocolByte` @ `0x08007128`

Per-UART state machine; state struct @ `0x200003AC`, rx body buffer @ `0x200010F8`.

| Field | Offset | Meaning |
|-------|--------|---------|
| `sawHdr1` | `+0x07` | saw `0x5A` |
| `inPacket` | `+0x08` | header complete, collecting body |
| `idx` | `+0x09` | body byte index |
| `expected` | `+0x0A` | expected body length = **`LEN + 7`** (`adds r2,r0,#7` @`0x0800713C`) |
| `runSum` | `+0x0C` | running checksum accumulator |

Header: `5A` sets `sawHdr1`; subsequent `A5` sets `inPacket` (`@0x08007190`+). Body: first byte is
`LEN`; `expected = LEN+7`, rejected if `> 0xF3` (`@0x08007142`). Each non-final byte is added to
`runSum`; on the final byte the firmware subtracts the accumulated `ck_lo`, inverts, and compares to
the received `ck_lo | ck_hi<<8`; on match it calls `dispatchReceivedPacket(1, buf)` (`@0x0800717C`).
Frame total = `LEN + 9`.

## 4. `dispatchReceivedPacket` @ `0x08005468`

Routes on `DST = body[2]`:

| DST | Branch | Route |
|-----|--------|-------|
| `0x20` ESC | `@0x08005516` | handle locally (register read/write) |
| `0x21` BLE | `@0x0800548E` | forward / build response on BLE bus |
| `0x22/0x23` BMS | `@0x08005564` | forward to BMS bus |
| `0x3D/0x3E/0x3F` | `@0x080054BE` | App / PC |

The register read/write response builders (`@0x08006550`, `@0x08005af8`) are detailed in §4b.
The C++ exposes routing as `routeByDst()` + a `Packet`.

## 4b. ESC register access — `App_to_ESC_handler` @ `0x08005624`

A packet addressed to the ESC is routed by SRC (`0x08005516`) to a source-specific handler; the
App/PC handler `@0x08005624` switches on **CMD = body[3]** via a `tbb` jump table `@0x08005650`:

| CMD | tbb target | Meaning |
|-----|-----------|---------|
| `0x01` | `0x0800567E` | **READ** — send `regfile[ARG..]` to source |
| `0x02` | `0x0800569C` | **WRITE** — copy payload → `regfile[ARG..]`, then ACK |
| `0x03` | `0x0800569C` | WRITE (variant) |
| `0x00,0x04..0x06` | `0x08005698` | ignored (return) |
| `0x07..0x0A` | `0x08005744` | extended (subscribe/stream) |
| `0x18,0x50,0x57-0x59,0x5C` | special | IAP / update / calibration |

This **confirms `docs/protocol.md`'s READ=0x01 / WRITE=0x02** command codes.

**Register file:** a flat array of **16-bit words in SRAM @ `0x200007D6`**, indexed by the **ARG byte**:
```asm
0x08005680: add.w r0, r7, fp, lsl #1   ; r7 = 0x200007D6 (regfile), fp = ARG → &reg[ARG]
```
READ returns ARG-indexed words; WRITE copies the payload there (`bl 0x80011AC` memcpy) and, for
`ARG == 0x17` or `0xE0`, sets a refresh flag (`@0x080056C4`). The response UART channel is chosen by
`buildAndSendResponse` `@0x08005AF8` (r6: 0→`0x08007534`, 1→`enqueuePacket 0x080071F4`, 2→`0x08007878`).

So the ESC/BMS/BLE "register maps" in `docs/protocol.md` are **ARG indices into this 16-bit register
file** — now structurally firmware-confirmed (the per-register *semantics*, e.g. `0x3A`=voltage, remain
documentation-sourced). Modeled in C++ as `RegisterFile` + `handleEscPacket()`.

---

## 4c. ESC motor control (DRV_1.2.6) — architecture finding

`TIM1_UP` is live only in **DRV_1.2.6** (`@0x08005E74`); in DRV_1.6.13 its vector points at the
default handler. Decoded:

- **`TIM1_UP_IRQHandler` @ `0x08005E74`** — handles the TIM1 **break flag** (BIF, bit 7 of `TIM1_SR`
  @`0x40012C10`; increments a fault counter) and the **update flag** (UIF); on update it reads input
  captures (`0x20000704`, `0x200006A4`), computes a timing-based speed as `(capture_diff * 4028) /
  1000` (`movw #0xFBC; sdiv #0x3E8`), and runs a guarded filter (`@0x080047B4`). State struct @`0x200004B8`.
- **`TIM3` handler @ `0x08005F18` → `0x080039FC`** — the Hall/commutation entry. It is **not** a simple
  6-step ROM lookup: it is a **runtime PWM/PI duty controller** (state @`0x20000540`, an 8-byte-per-entry
  RAM scheduling table @`0x2000208C` indexed by a 0..3 step counter, `udiv` speed math, and a duty
  output clamped to `0xC8` = 200). No static Hall→phase commutation table exists in the image.

> Finding: contrary to a naive "6-step table" assumption, the stock ESC uses a computed PWM/PI scheme.
> The ESC is replaced by a VESC in this project, so this is documented for reference only and **not**
> reprogrammed (doing so faithfully would require modelling the full PI/PWM control loop).

## 4d. BMS_1.7.4.5 — same protocol core, BMS register file

The BMS runs the **byte-identical** Ninebot protocol core, independently confirming the shared design
(and the `LEN = payload` convention):

- **Parser @ `0x08002E4C`** (state @`0x20000248`, rx buf @`0x20000ED0`): identical state machine,
  `expected = LEN + 7` (`adds r3,r0,#7` @`0x08002E62`), identical checksum + header detection. Only
  difference: rx-length cap is **`0xC2`** (vs ESC `0xF3`) — the BMS has less SRAM. Valid packets call
  the dispatcher `@0x08005610`.
- **Dispatcher @ `0x08005610`**: switches on DST=`body[2]` (`0x22` self / `0xFF` broadcast), then on
  CMD=`body[3]`: **CMD 1 = READ**, **CMD 2 = WRITE**, CMD 7/8/9/0xA extended, CMD 0x18 = IAP/version.
- **BMS register file @ `0x20000400`**, 16-bit words indexed by ARG (`r0=ARG; lsls r0,#1; adds r0,r0,regfile`
  @`0x08005668`), same model as the ESC. READ returns `regfile[ARG..]`; WRITE stores there (ARG `0x29` special).

So the `docs/protocol.md` **BMS register map (cells `0x30-0x39`, etc.) are ARG indices into the
16-bit register file @`0x20000400`** — structurally firmware-confirmed. To poll the BMS, the BLE
firmware sends `5A A5 LEN 21 22 01 <reg> <count> CKlo CKhi` and reads the response. Covered by test #9.

## 4e. nRF51 BLE bridge (BLE_1.1.7, Cortex-M0) — shared core, dual framing

The nRF51 BLE SoC (app @`0x00018000`, post-S110) is the phone↔ESC bridge. Its Ninebot parser
@`0x00018450` uses the **byte-identical header + checksum core** as the STM32 boards:

- Header detect @`0x0001854C`: `cmp #0x5A` → set `sawHdr1`; `cmp #0xA5` (+ flag) → enter packet.
- Checksum @`0x000184A8`: `calc = ~(runSum − ckLo) & 0xFFFF`, compare to `ckLo | ckHi<<8` — identical.
- State struct @`0x200021B4` (`+3` sawHdr1, `+4` inPacket, `+5` idx, `+6` expected, `+8` runSum,
  `+7` header sub-state), rx buffer @`0x200030E8`, mode flag @`0x2000275C[0x16]`.

**Bridge-specific framing (genuine nRF difference):** because it wraps/relays, the nRF length
accounting is **`expected = LEN + 8`** on the normal Ninebot path (`adds r2,#8` @`0x00018472`) and
**`expected = LEN + 0x0D`** on the **Xiaomi MiIO** path (`adds r2,#0xD` @`0x0001848A`) — one/​several
more than the ESC/BMS `LEN + 7`, reflecting the extra BLE/MiIO wrapper bytes. rx cap `0x8F`. Valid
packets dispatch to `0x00019804` (toward ESC/UART) or `0x0001B9A8` (toward the phone/BLE); the MiIO
path re-emits the `5A A5` header into the buffer (@`0x000184CC`) before relaying.

> So all three boards share the same `5A A5`/checksum core; the nRF adds a dual Ninebot/MiIO framing
> layer on top. The verified C++ module models the **STM32 (ESC/BMS) wire core**; the nRF MiIO wrapper
> (SoftDevice + Xiaomi crypto) is documented here but not reprogrammed (separate, larger effort).

## 4f. nRF51 Xiaomi MiIO / SoftDevice layer (BLE_1.1.7) — researched + firmware-grounded

The nRF51 runs **Nordic SoftDevice S110 v8.0** (app @`0x00018000`) and implements Xiaomi's **MiIO**
BLE binding/auth on top of the Ninebot bridge. Firmware evidence (string addrs in `BLE_1.1.7`, +
SoftDevice SVCs from the disassembly):

| Stage | Firmware string (addr) | MiIO meaning (researched) |
|-------|------------------------|---------------------------|
| service init | `"Mi Serivce Init fail"` `@0x1B297` | register the Mi BLE GATT service |
| auth write | `"On Auth Written"` / `"decrypted auth, %x"` `@0x1E55F` | app writes auth blob; device decrypts |
| token | `"On Token Written"`, `"retrive token, %02x %02x"` `@0x1E2DB` | session token exchange |
| register | `"Register succ,new token, encrypt sn, beaconkey."` `@0x1E5D7` | first-bind: derive **beaconkey** (16-byte bindkey) + encrypt SN |
| cloud bind | `"cloud bind succ/fail"` `@0x1E57C` | MiHome cloud binding |
| bond | `"mi_service, bond succ"` `@0x1E597` | BLE bond stored |
| login | `"login cfm handler"`, `"login cfm succ/fail"` `@0x1E9E3` | subsequent-connect login confirm |
| persist | `"miio ble flash register succ/fail"`, `"psm callback"` | store token/beaconkey via Nordic **PSM** |

**SoftDevice usage** (SVC numbers): `sd_ble_gap_*` (18 calls), `sd_ble_l2cap_*` (14), `sd_ble_gattc_*`,
`sd_power_*`, `sd_ppi_*`, `sd_softdevice_*`; flash writes go through SoftDevice `sd_flash_*` SVCs
(no direct NVMC), and the beaconkey/token are persisted with the SDK **Persistent Storage Manager**.
The MiIO crypto itself (token/beaconkey derivation, per-session encryption) is Xiaomi-proprietary
ECC/AES — **not reproduced here** (out of scope; it is the *stock* binding the custom firmware will
replace with its own VESC-App BLE service + NUS).

> Implication for the project: the custom nRF51 firmware (Deployment Phase 4) replaces this MiIO/NUS
> stack with a VESC-Tool-compatible BLE service; only the **Ninebot bridge core** (§4e) must be kept to
> talk to the STM32/ESC. The MiIO layer is documented so it can be cleanly removed, not re-implemented.

Sources: [matterxiaomi: MiBeacon encryption key](https://blog.matterxiaomi.com/blog/how-get-encryption-key-part1/) ·
[PiotrMachowski/Xiaomi-cloud-tokens-extractor](https://github.com/PiotrMachowski/Xiaomi-cloud-tokens-extractor) ·
[Home Assistant Xiaomi BLE](https://www.home-assistant.io/integrations/xiaomi_ble/) (beaconkey = 16-byte bindkey).

## 5. ⚠️ Correction: the `LEN` field convention

The earlier reconstruction `common/include/protocol.h` uses **`LEN = payload + 6`** with a parser
`expected = LEN + 1`. That is *self-consistent inside its own simulator* (its 86 tests pass), but the
**on-wire `LEN` byte is wrong** — a real scooter (and this binary) use **`LEN = payload count`** with
`expected = LEN + 7`. A packet built by the old `protocol.h` would be mis-framed by a real ESC.

- Verified module (`ninebot_protocol_verified.hpp`): **`LEN = payload`, frame = `LEN + 9`.** ✅ matches binary.
- `docs/protocol.md`: corrected (LEN definition + worked example).
- `common/include/protocol.h` **and** `nrf51822/src/nrf51_main.cpp`: **reconciled** to `LEN = payload`
  (`buildPacket` stores `payloadLen`; parser `expected = LEN + 7`; `payloadLength = LEN`). The full
  reconstruction test suite was updated and **re-run green: 134/134** (g++ 15.2). The earlier
  `LEN = payload + 6` convention is gone repo-wide.

## 6. Verification summary

`tests/test_decompiled_protocol.cpp` (**45 checks, all pass**):
checksum golden vector `0xFF7C` + edge cases · `buildPacket` exact bytes incl. `LEN==payload` ·
build→parse round-trip · corrupt-checksum rejection · header resync after junk/lone `0x5A` ·
oversized-`LEN` rejection at the `0xF3` boundary · `routeByDst` for all addresses ·
register WRITE→regfile→READ round-trip (reg `0x7B`) · ARG `0x17` refresh flag.

Also update the header/intro counts when changing the test.

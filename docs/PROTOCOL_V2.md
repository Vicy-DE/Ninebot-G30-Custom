# NB+ — Improved Ninebot-compatible Protocol (v2)

**Date:** 2026-06-03
**Status:** design. Supersedes nothing — it **coexists** with stock Ninebot on the same bus.

## Why a v2, and what stays stock

The stock Ninebot protocol (`5A A5 | LEN | SRC | DST | CMD | ARG | payload | ~sum`) is
firmware-verified (see `docs/REGISTER_MAP.md`, `firmware/decompiled/DECOMPILATION.md`) but has real
weaknesses for a VESC build:

| Stock weakness | NB+ fix |
|----------------|---------|
| `~sum` checksum (1's-complement sum) misses bit-swaps/reordering | **CRC-16/CCITT-FALSE** (poly `0x1021`, init `0xFFFF`) |
| 8-bit length → ≤255 B payload | **16-bit length** (telemetry blocks, key blobs) |
| No version / capability negotiation | `ver` byte + **HELLO** negotiation |
| No sequence/ACK → silent loss | optional **seq + ACK/NACK** (reliable mode) |
| Polled register reads only | **STREAM** push frames for VESC telemetry at a fixed rate |
| One header → can't tell dialects apart | distinct header **`5A A6`** so NB+ and stock share a wire safely |

**App compatibility is NOT broken:** the phone app speaks **stock `5A A5`** to the BLE module, and the
BLE firmware bridges stock⇄NB+ internally. NB+ is used dashboard⇄VESC and on a custom/VESC-Tool path.
A device that doesn't understand a header simply drops the frame (different magic, CRC fails) — both
dialects coexist on the half-duplex bus.

## Frame format

```
┌──────┬──────┬─────┬───────┬─────┬─────┬──────┬───────────┬──────────────┬──────────┐
│ 0x5A │ 0xA6 │ VER │ FLAGS │ SRC │ DST │ TYPE │ REG/CMD   │ LEN (u16 LE) │ PAYLOAD  │  … CRC16 (LE)
│  hdr │  hdr │  1B │  1B   │ 1B  │ 1B  │  1B  │    1B     │     2B       │ LEN bytes│
└──────┴──────┴─────┴───────┴─────┴─────┴──────┴───────────┴──────────────┴──────────┘
 CRC-16/CCITT-FALSE over VER..last-payload byte (everything after the 2 header bytes,
 before the 2 CRC bytes), transmitted little-endian.
```

- **VER** — protocol version, currently `0x02`.
- **FLAGS** — bit0 `ACK_REQ` (sender wants an ACK), bit1 `IS_ACK`, bit2 `IS_NACK`,
  bit3 `ENCRYPTED` (payload is MiIO/AES-wrapped, BLE path only), bits4-7 `SEQ` (4-bit sequence).
- **SRC/DST** — addresses (below).
- **TYPE** — frame type (below).
- **REG/CMD** — register index (READ/WRITE) or sub-command (control).
- **LEN** — payload length, little-endian u16 (0…65535; practical cap per buffer).
- **CRC16** — CCITT-FALSE, little-endian.

### Addresses
| Addr | Device |
|------|--------|
| `0x20` | ESC / VESC bridge |
| `0x21` | BLE dashboard |
| `0x22` | BMS (stock Ninebot) |
| `0x2A` | **VESC telemetry source** (NB+ STREAM) |
| `0x2B` | **Daly BMS** (bridged) |
| `0x3E` | phone app |
| `0x3F` | PC / VESC-Tool |

### Frame types (`TYPE`)
| Type | Name | Meaning |
|------|------|---------|
| `0x01` | READ | read `REG` (payload: u8 count or u16 count) |
| `0x02` | WRITE | write `REG` (payload = data); ACK unless FLAGS.ACK_REQ=0 |
| `0x04` | READ_RESP | response to READ (payload = data) — mirrors stock cmd `0x04` |
| `0x05` | WRITE_ACK | write acknowledged |
| `0x06` | STREAM | unsolicited telemetry push (no ACK) |
| `0x07` | ACK | positive ack of `SEQ` |
| `0x08` | NACK | negative ack (payload[0]=reason) |
| `0x09` | HELLO | capability/version exchange (payload = capability struct) |
| `0x0A` | TUNNEL | opaque pass-through (e.g. VESC binary packet or .sfw chunk) |

## CRC-16/CCITT-FALSE (reference)
```c
uint16_t nbx_crc16(const uint8_t *d, size_t n) {   // poly 0x1021, init 0xFFFF, no reflect, xorout 0
    uint16_t c = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        c ^= (uint16_t)d[i] << 8;
        for (int b = 0; b < 8; b++)
            c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
    }
    return c;                                        // tx little-endian: lo, hi
}
```

## STREAM telemetry frame (TYPE=0x06, SRC=0x2A, REG=0x01)

One packet carries the full VESC live state (replaces many stock polled reads). All little-endian:

```c
typedef struct __attribute__((packed)) {
    uint16_t voltage_cv;     // 0.01 V        (pack voltage)
    int16_t  current_ca;     // 0.01 A        (battery current, signed)
    int16_t  motor_current_ca;// 0.01 A       (q-axis / motor current)
    int16_t  duty_pm;        // 0.1 %         (-1000..1000)
    int32_t  erpm;           // electrical rpm
    uint16_t speed_mkmh;     // 0.001 km/h
    int16_t  temp_fet_dc;    // 0.1 °C
    int16_t  temp_mot_dc;    // 0.1 °C
    uint32_t wh_used;        // mWh consumed
    uint32_t wh_charged;     // mWh regen
    uint8_t  soc_pct;        // 0..100 %
    uint8_t  mode;           // 1=drive 2=eco 4=sport 16=off 32=lock
    uint8_t  fault;          // VESC fault code
    uint8_t  flags;          // bit0 light, bit1 lock, bit2 temp-warn, bit3 brake
} nbx_telemetry_t;           // 30 bytes
```

The BLE firmware decomposes this into the stock register reads the **phone app** expects
(`docs/REGISTER_MAP.md`): e.g. `voltage_cv→ESC 0x48`, `current_ca→0x49`, `speed→0x26`, `soc→0x22`,
`fault→0x1B`, `mode→0x75/0x1F`.

## Reliable mode (optional)
- Sender sets `FLAGS.ACK_REQ` and a 4-bit `SEQ`. Receiver replies `ACK` (same SEQ) or `NACK`.
- Used for WRITEs that must not be lost (mode change, lock, firmware-update chunks via `TUNNEL`).
- STREAM and display refreshes are fire-and-forget (no ACK) to keep latency low.

## HELLO / capability negotiation (TYPE=0x09)
On link-up each end sends HELLO with: `{ver, role, fw_ver(u16), caps(u32 bitmap)}`. Caps bits:
`CRC16`, `STREAM`, `ENCRYPT`, `TUNNEL`, `DALY`, `HAYSTACK`. If a peer doesn't answer HELLO, the sender
**falls back to stock `5A A5`** for that peer (e.g. talking to an un-upgraded VESC lisp or the phone app).

## Bridging rules (who speaks what)

```
 Phone app ──stock 5A A5 (BLE/NUS)──► nRF51 ──stock──► STM32 dashboard
                                                         │  bridges ⇅
 STM32 dashboard ──NB+ (5A A6)──► VESC   (if VESC lisp upgraded; else stock 5A A5)
 STM32 dashboard ──Daly 0xA5 (9600)──► Daly BMS   (separate dialect, see WIRING_PLAN §)
 VESC ──NB+ STREAM (5A A6)──► dashboard   (telemetry push)
```

- The **BLE↔app path is always stock** → the original app keeps working (verified against the APK,
  see `docs/BLE_PROTOCOL_VERIFIED.md`).
- The **dashboard↔VESC path** negotiates NB+ via HELLO; falls back to stock so the existing
  `vesc-lisp/g30_dash.lisp` keeps working unchanged until upgraded.
- Coexistence: stock (`5A A5`) and NB+ (`5A A6`) frames interleave on the half-duplex bus; each side
  ignores the other magic, and CRC/`~sum` mismatches drop foreign frames safely.

## Migration path
1. Keep everything stock (today).
2. Upgrade the VESC lisp + dashboard to add NB+ STREAM telemetry (richer dash, fewer polls) — both
   still answer stock for the app.
3. Optionally expose NB+ over BLE to a **custom companion app / VESC-Tool** (TYPE=TUNNEL) alongside the
   stock app service.

> NB+ is implemented in `lib/ninebot-protocol/` (add `nbx.*`) and consumed by the dashboard + nRF51
> firmware. The verified byte-faithful **stock** core remains in
> `firmware/decompiled/common/include/ninebot_protocol_verified.hpp`.
